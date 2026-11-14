#include "QKBootTPMTransport.h"

#include "QCBuiltins.h"

extern "C" QC::VirtAddr physToVirt(QC::PhysAddr phys);

namespace QK::Boot::Tpm::Transport
{
    using namespace QK::Boot::Tpm::Internal;

    namespace
    {
        constexpr QC::PhysAddr kDefaultTisBasePhys = 0xFED40000u;
        constexpr QC::usize kLocalityWindowSize = 0x1000;

        // Locality register space shared by CRB and FIFO/TIS.
        constexpr QC::usize REG_INTERFACE_ID = 0x30;

        // CRB locality registers (present when the control area sits at base + 0x40).
        constexpr QC::usize CRB_LOC_STATE = 0x00;
        constexpr QC::usize CRB_LOC_CTRL = 0x08;
        constexpr QC::usize CRB_CONTROL_AREA_OFFSET = 0x40;
        constexpr QC::u32 CRB_LOC_STATE_ASSIGNED = 1u << 1;
        constexpr QC::u32 CRB_LOC_STATE_REG_VALID = 1u << 7;

        // CRB control area registers (relative to the control area).
        constexpr QC::usize CRB_CTRL_REQ = 0x00;
        constexpr QC::usize CRB_CTRL_STS = 0x04;
        constexpr QC::usize CRB_CTRL_CANCEL = 0x08;
        constexpr QC::usize CRB_CTRL_START = 0x0C;
        constexpr QC::usize CRB_CMD_SIZE = 0x18;
        constexpr QC::usize CRB_CMD_PA_LOW = 0x1C;
        constexpr QC::usize CRB_CMD_PA_HIGH = 0x20;
        constexpr QC::usize CRB_RSP_SIZE = 0x24;
        constexpr QC::usize CRB_RSP_PA = 0x28;
        constexpr QC::u32 CRB_CTRL_REQ_CMD_READY = 1u << 0;
        constexpr QC::u32 CRB_CTRL_REQ_GO_IDLE = 1u << 1;
        constexpr QC::u32 CRB_CTRL_STS_ERROR = 1u << 0;

        // FIFO/TIS registers (locality 0).
        constexpr QC::usize TIS_ACCESS = 0x00;
        constexpr QC::usize TIS_STS = 0x18;
        constexpr QC::usize TIS_DATA_FIFO = 0x24;
        constexpr QC::usize TIS_DID_VID = 0xF00;
        constexpr QC::usize TIS_RID = 0xF04;
        constexpr QC::u8 TIS_ACCESS_VALID = 0x80;
        constexpr QC::u8 TIS_ACCESS_ACTIVE_LOCALITY = 0x20;
        constexpr QC::u8 TIS_ACCESS_REQUEST_USE = 0x02;
        constexpr QC::u32 TIS_STS_VALID = 0x80;
        constexpr QC::u32 TIS_STS_COMMAND_READY = 0x40;
        constexpr QC::u32 TIS_STS_GO = 0x20;
        constexpr QC::u32 TIS_STS_DATA_AVAIL = 0x10;
        constexpr QC::u32 TIS_STS_EXPECT = 0x08;

        struct FState
        {
            bool bInitialized = false;
            FInfo Info{};
            QC::VirtAddr LocalityBase = 0;
            QC::VirtAddr ControlArea = 0;
        };

        static FState g_State;

        static bool SpinWaitClears32(QC::VirtAddr Addr, QC::u32 Mask, QC::usize Iterations)
        {
            for (QC::usize i = 0; i < Iterations; ++i)
            {
                if ((QC::mmio_read32(Addr) & Mask) == 0)
                    return true;
                QC::pause();
            }
            return false;
        }

        static bool SpinWaitSet32(QC::VirtAddr Addr, QC::u32 Mask, QC::usize Iterations)
        {
            for (QC::usize i = 0; i < Iterations; ++i)
            {
                if ((QC::mmio_read32(Addr) & Mask) == Mask)
                    return true;
                QC::pause();
            }
            return false;
        }

        static bool SpinWaitSet8(QC::VirtAddr Addr, QC::u8 Mask, QC::usize Iterations)
        {
            for (QC::usize i = 0; i < Iterations; ++i)
            {
                if ((QC::mmio_read8(Addr) & Mask) == Mask)
                    return true;
                QC::pause();
            }
            return false;
        }

        static bool CrbUsesIdleHandshake()
        {
            // Firmware-started CRB (start methods 2/8) manages idle state itself.
            return !StartMethodRequestsAcpiStart(g_State.Info.StartMethod);
        }

        static bool CrbRequestLocality0(FLogFn Log)
        {
            if (!g_State.Info.bCrbLocalityRegs)
                return true;

            const QC::VirtAddr Base = g_State.LocalityBase;
            constexpr QC::u32 Want = CRB_LOC_STATE_ASSIGNED | CRB_LOC_STATE_REG_VALID;
            if ((QC::mmio_read32(Base + CRB_LOC_STATE) & Want) == Want)
                return true;

            QC::mmio_write32(Base + CRB_LOC_CTRL, 1u);
            if (!SpinWaitSet32(Base + CRB_LOC_STATE, Want, kTpmCmdReadySpinIterations))
            {
                LogStr(Log, "TPM2: CRB locality 0 request timed out; continuing\r\n");
                return false;
            }
            return true;
        }

        static QC::u32 CrbSubmit(const QC::u8 *Cmd, QC::usize CmdLen, QC::u8 *Rsp, QC::usize RspCap, QC::u32 &RspLenOut, FLogFn Log)
        {
            const QC::VirtAddr Ctrl = g_State.ControlArea;
            RspLenOut = 0;

            if (CrbUsesIdleHandshake())
            {
                QC::mmio_write32(Ctrl + CRB_CTRL_REQ, CRB_CTRL_REQ_CMD_READY);
                if (!SpinWaitClears32(Ctrl + CRB_CTRL_REQ, CRB_CTRL_REQ_CMD_READY, kTpmCmdReadySpinIterations))
                {
                    LogStr(Log, "TPM2: CMD_READY timeout\r\n");
                    return kTpmTransportFailure;
                }
            }

            const QC::u32 CmdSize = QC::mmio_read32(Ctrl + CRB_CMD_SIZE);
            const QC::u64 CmdPhys64 = (static_cast<QC::u64>(QC::mmio_read32(Ctrl + CRB_CMD_PA_HIGH)) << 32) |
                                      QC::mmio_read32(Ctrl + CRB_CMD_PA_LOW);
            const QC::u32 RspSize = QC::mmio_read32(Ctrl + CRB_RSP_SIZE);
            const QC::u64 RspPhys64 = QC::mmio_read64(Ctrl + CRB_RSP_PA);

            if (Log)
            {
                LogHex64(Log, "TPM2: cmdBuf phys ", CmdPhys64);
                LogDecU32(Log, "TPM2: cmdBuf size ", CmdSize);
                LogHex64(Log, "TPM2: rspBuf phys ", RspPhys64);
                LogDecU32(Log, "TPM2: rspBuf size ", RspSize);
            }

            if (CmdPhys64 == 0 || RspPhys64 == 0)
            {
                LogStr(Log, "TPM2: invalid CRB buffer address\r\n");
                return kTpmTransportFailure;
            }
            if (CmdLen > CmdSize || CmdSize < 12 || RspSize < 10)
            {
                LogStr(Log, "TPM2: invalid CRB buffer sizes\r\n");
                return kTpmTransportFailure;
            }

            const QC::PhysAddr CmdPhys = static_cast<QC::PhysAddr>(CmdPhys64);
            const QC::PhysAddr RspPhys = static_cast<QC::PhysAddr>(RspPhys64);
            if (!EnsureHhdmMapped(Log, CmdPhys, CmdSize) || !EnsureHhdmMapped(Log, RspPhys, RspSize))
            {
                LogStr(Log, "TPM2: failed to map cmd/rsp buffers\r\n");
                return kTpmTransportFailure;
            }

            auto *CmdBuf = reinterpret_cast<volatile QC::u8 *>(physToVirt(CmdPhys));
            for (QC::usize i = 0; i < CmdLen; ++i)
                CmdBuf[i] = Cmd[i];

            QC::write_barrier();
            QC::memory_barrier();

            QC::mmio_write32(Ctrl + CRB_CTRL_START, 1u);
            if (!SpinWaitClears32(Ctrl + CRB_CTRL_START, 1u, kTpmCommandCompleteSpinIterations))
            {
                if (StartMethodRequestsAcpiStart(g_State.Info.StartMethod))
                    LogStr(Log, "TPM2: START timeout; this TPM likely needs an ACPI _DSM start (unsupported)\r\n");
                else
                    LogStr(Log, "TPM2: START timeout; issuing CANCEL\r\n");
                QC::mmio_write32(Ctrl + CRB_CTRL_CANCEL, 1u);
                (void)SpinWaitClears32(Ctrl + CRB_CTRL_START, 1u, kTpmCmdReadySpinIterations);
                QC::mmio_write32(Ctrl + CRB_CTRL_CANCEL, 0u);
                return kTpmTransportFailure;
            }

            const QC::u32 Sts = QC::mmio_read32(Ctrl + CRB_CTRL_STS);
            if (Log)
            {
                LogStr(Log, "TPM2: CTRL_STS 0x");
                LogHex32Fixed(Log, Sts);
                LogStr(Log, "\r\n");
            }
            if (Sts & CRB_CTRL_STS_ERROR)
            {
                LogStr(Log, "TPM2: CRB reports fatal error status\r\n");
                return kTpmTransportFailure;
            }

            QC::read_barrier();
            QC::memory_barrier();

            auto *RspBuf = reinterpret_cast<const volatile QC::u8 *>(physToVirt(RspPhys));
            QC::u8 Hdr[10];
            for (QC::usize i = 0; i < sizeof(Hdr); ++i)
                Hdr[i] = RspBuf[i];
            QC::u32 RspLen = ReadBe32(Hdr + 2);
            const QC::u32 RspCode = ReadBe32(Hdr + 6);

            if (RspLen < 10 || RspLen > RspSize || RspLen > RspCap)
            {
                LogStr(Log, "TPM2: CRB response length invalid\r\n");
                RspLen = 0;
            }
            for (QC::usize i = 0; i < RspLen; ++i)
                Rsp[i] = RspBuf[i];
            RspLenOut = RspLen;

            if (Log)
            {
                LogDecU32(Log, "TPM2: rspLen ", RspLen);
                LogStr(Log, "TPM2: rspCode 0x");
                LogHex32Fixed(Log, RspCode);
                LogStr(Log, "\r\n");
            }

            if (CrbUsesIdleHandshake())
            {
                QC::mmio_write32(Ctrl + CRB_CTRL_REQ, CRB_CTRL_REQ_GO_IDLE);
                (void)SpinWaitClears32(Ctrl + CRB_CTRL_REQ, CRB_CTRL_REQ_GO_IDLE, kTpmCmdReadySpinIterations);
            }

            return RspLen == 0 ? kTpmTransportFailure : RspCode;
        }

        static bool TisRequestLocality0(FLogFn Log)
        {
            const QC::VirtAddr Access = g_State.LocalityBase + TIS_ACCESS;
            constexpr QC::u8 Want = TIS_ACCESS_VALID | TIS_ACCESS_ACTIVE_LOCALITY;
            if ((QC::mmio_read8(Access) & Want) == Want)
                return true;

            QC::mmio_write8(Access, TIS_ACCESS_REQUEST_USE);
            if (!SpinWaitSet8(Access, Want, kTpmCmdReadySpinIterations))
            {
                LogStr(Log, "TPM2: TIS locality 0 request timed out\r\n");
                return false;
            }
            return true;
        }

        static void TisRelinquishLocality0()
        {
            QC::mmio_write8(g_State.LocalityBase + TIS_ACCESS, TIS_ACCESS_ACTIVE_LOCALITY);
        }

        static QC::u32 TisBurstCount(QC::usize Iterations)
        {
            const QC::VirtAddr Sts = g_State.LocalityBase + TIS_STS;
            for (QC::usize i = 0; i < Iterations; ++i)
            {
                const QC::u32 Burst = (QC::mmio_read32(Sts) >> 8) & 0xFFFFu;
                if (Burst != 0)
                    return Burst;
                QC::pause();
            }
            return 0;
        }

        static bool TisWaitStsValid(QC::u32 &OutSts, QC::usize Iterations)
        {
            const QC::VirtAddr Sts = g_State.LocalityBase + TIS_STS;
            for (QC::usize i = 0; i < Iterations; ++i)
            {
                const QC::u32 V = QC::mmio_read32(Sts);
                if (V & TIS_STS_VALID)
                {
                    OutSts = V;
                    return true;
                }
                QC::pause();
            }
            return false;
        }

        static bool TisReadBytes(QC::u8 *Out, QC::usize Count)
        {
            const QC::VirtAddr Fifo = g_State.LocalityBase + TIS_DATA_FIFO;
            QC::usize Done = 0;
            while (Done < Count)
            {
                QC::u32 Burst = TisBurstCount(kTpmCmdReadySpinIterations);
                if (Burst == 0)
                    return false;
                while (Burst > 0 && Done < Count)
                {
                    const QC::u8 B = QC::mmio_read8(Fifo);
                    if (Out)
                        Out[Done] = B;
                    ++Done;
                    --Burst;
                }
            }
            return true;
        }

        static QC::u32 TisSubmitLocked(const QC::u8 *Cmd, QC::usize CmdLen, QC::u8 *Rsp, QC::usize RspCap, QC::u32 &RspLenOut, FLogFn Log)
        {
            const QC::VirtAddr StsReg = g_State.LocalityBase + TIS_STS;
            const QC::VirtAddr Fifo = g_State.LocalityBase + TIS_DATA_FIFO;
            RspLenOut = 0;

            QC::mmio_write32(StsReg, TIS_STS_COMMAND_READY);
            if (!SpinWaitSet32(StsReg, TIS_STS_COMMAND_READY, kTpmCmdReadySpinIterations))
            {
                LogStr(Log, "TPM2: TIS commandReady timeout\r\n");
                return kTpmTransportFailure;
            }

            // Write all but the last byte in bursts; the TPM must still expect data.
            QC::usize Sent = 0;
            while (Sent + 1 < CmdLen)
            {
                QC::u32 Burst = TisBurstCount(kTpmCmdReadySpinIterations);
                if (Burst == 0)
                {
                    LogStr(Log, "TPM2: TIS burstCount timeout (write)\r\n");
                    return kTpmTransportFailure;
                }
                while (Burst > 0 && Sent + 1 < CmdLen)
                {
                    QC::mmio_write8(Fifo, Cmd[Sent++]);
                    --Burst;
                }

                QC::u32 Sts = 0;
                if (!TisWaitStsValid(Sts, kTpmCmdReadySpinIterations) || !(Sts & TIS_STS_EXPECT))
                {
                    LogStr(Log, "TPM2: TIS stopped expecting command data\r\n");
                    return kTpmTransportFailure;
                }
            }

            if (TisBurstCount(kTpmCmdReadySpinIterations) == 0)
            {
                LogStr(Log, "TPM2: TIS burstCount timeout (last byte)\r\n");
                return kTpmTransportFailure;
            }
            QC::mmio_write8(Fifo, Cmd[Sent++]);

            QC::u32 Sts = 0;
            if (!TisWaitStsValid(Sts, kTpmCmdReadySpinIterations) || (Sts & TIS_STS_EXPECT))
            {
                LogStr(Log, "TPM2: TIS still expects data after full command\r\n");
                return kTpmTransportFailure;
            }

            QC::mmio_write32(StsReg, TIS_STS_GO);
            if (!SpinWaitSet32(StsReg, TIS_STS_VALID | TIS_STS_DATA_AVAIL, kTpmCommandCompleteSpinIterations))
            {
                LogStr(Log, "TPM2: TIS response timeout\r\n");
                return kTpmTransportFailure;
            }

            QC::u8 Hdr[10];
            if (!TisReadBytes(Hdr, sizeof(Hdr)))
            {
                LogStr(Log, "TPM2: TIS response header read failed\r\n");
                return kTpmTransportFailure;
            }

            const QC::u32 RspLen = ReadBe32(Hdr + 2);
            const QC::u32 RspCode = ReadBe32(Hdr + 6);
            if (RspLen < sizeof(Hdr) || RspLen > RspCap)
            {
                LogStr(Log, "TPM2: TIS response length invalid\r\n");
                return kTpmTransportFailure;
            }

            for (QC::usize i = 0; i < sizeof(Hdr); ++i)
                Rsp[i] = Hdr[i];
            if (!TisReadBytes(Rsp + sizeof(Hdr), RspLen - sizeof(Hdr)))
            {
                LogStr(Log, "TPM2: TIS response body read failed\r\n");
                return kTpmTransportFailure;
            }

            if (TisWaitStsValid(Sts, kTpmCmdReadySpinIterations) && (Sts & TIS_STS_DATA_AVAIL))
                LogStr(Log, "TPM2: TIS has unexpected trailing response data\r\n");

            RspLenOut = RspLen;
            if (Log)
            {
                LogDecU32(Log, "TPM2: rspLen ", RspLen);
                LogStr(Log, "TPM2: rspCode 0x");
                LogHex32Fixed(Log, RspCode);
                LogStr(Log, "\r\n");
            }
            return RspCode;
        }

        static QC::u32 TisSubmit(const QC::u8 *Cmd, QC::usize CmdLen, QC::u8 *Rsp, QC::usize RspCap, QC::u32 &RspLenOut, FLogFn Log)
        {
            if (!TisRequestLocality0(Log))
                return kTpmTransportFailure;

            const QC::u32 Rc = TisSubmitLocked(Cmd, CmdLen, Rsp, RspCap, RspLenOut, Log);
            QC::mmio_write32(g_State.LocalityBase + TIS_STS, TIS_STS_COMMAND_READY);
            TisRelinquishLocality0();
            return Rc;
        }
    }

    const char *TypeName(EType Type)
    {
        switch (Type)
        {
        case EType::Crb:
            return "crb";
        case EType::Tis:
            return "tis-fifo";
        default:
            return "none";
        }
    }

    const char *StartMethodName(QC::u32 StartMethod)
    {
        switch (StartMethod)
        {
        case kTpmStartMethodAcpi:
            return "acpi-start";
        case kTpmStartMethodMmio:
            return "mmio";
        case kTpmStartMethodCrb:
            return "crb";
        case kTpmStartMethodCrbAcpi:
            return "crb+acpi-start";
        default:
            return "unknown";
        }
    }

    bool Init(QC::u32 StartMethod, QC::PhysAddr ControlAreaPhys, FLogFn Log)
    {
        g_State = FState{};
        FInfo &Info = g_State.Info;
        Info.StartMethod = StartMethod;
        Info.ControlAreaPhys = ControlAreaPhys;
        Info.bAcpiStartRequested = StartMethodRequestsAcpiStart(StartMethod);

        const QC::PhysAddr LocalityBasePhys = ControlAreaPhys ? (ControlAreaPhys & ~static_cast<QC::PhysAddr>(0xFFF))
                                                              : kDefaultTisBasePhys;
        if (!EnsureHhdmMappedMmio(Log, LocalityBasePhys, kLocalityWindowSize))
        {
            LogStr(Log, "TPM2: failed to map TPM register window\r\n");
            return false;
        }
        Info.LocalityBasePhys = LocalityBasePhys;
        g_State.LocalityBase = physToVirt(LocalityBasePhys);

        // The interface ID lives in the locality register space; a CRB control
        // area elsewhere in the page means there is no such space to probe.
        const bool bHasLocalityRegs = (ControlAreaPhys == 0) ||
                                      ((ControlAreaPhys & 0xFFF) == CRB_CONTROL_AREA_OFFSET);
        QC::u32 InterfaceType = 0;
        if (bHasLocalityRegs)
        {
            const QC::u32 Lo = QC::mmio_read32(g_State.LocalityBase + REG_INTERFACE_ID);
            const QC::u32 Hi = QC::mmio_read32(g_State.LocalityBase + REG_INTERFACE_ID + 4);
            Info.InterfaceId = (static_cast<QC::u64>(Hi) << 32) | Lo;
            Info.bInterfaceIdValid = true;
            InterfaceType = Lo & 0xFu;
        }

        Info.Type = ClassifyTransport(StartMethod, ControlAreaPhys != 0, Info.bInterfaceIdValid, InterfaceType);

        LogStr(Log, "TPM2: start method ");
        LogStr(Log, StartMethodName(StartMethod));
        if (Info.bInterfaceIdValid)
        {
            LogStr(Log, ", interface id 0x");
            LogHex32Fixed(Log, static_cast<QC::u32>(Info.InterfaceId >> 32));
            LogHex32Fixed(Log, static_cast<QC::u32>(Info.InterfaceId));
        }
        LogStr(Log, ", transport ");
        LogStr(Log, TypeName(Info.Type));
        LogStr(Log, "\r\n");

        if (Info.Type == EType::Crb)
        {
            if (!EnsureHhdmMappedMmio(Log, ControlAreaPhys, 0x40))
            {
                LogStr(Log, "TPM2: failed to map CRB control area\r\n");
                return false;
            }
            g_State.ControlArea = physToVirt(ControlAreaPhys);
            Info.bCrbLocalityRegs = bHasLocalityRegs;
            if (Info.bInterfaceIdValid && (InterfaceType == kTpmInterfaceTypeCrb))
            {
                Info.VendorId = static_cast<QC::u16>(Info.InterfaceId >> 48);
                Info.DeviceId = static_cast<QC::u16>(Info.InterfaceId >> 32);
                Info.RevisionId = static_cast<QC::u8>(Info.InterfaceId >> 24);
            }
            if (Info.bAcpiStartRequested)
                LogStr(Log, "TPM2: firmware requests ACPI _DSM start; using START register only\r\n");
            (void)CrbRequestLocality0(Log);
        }
        else if (Info.Type == EType::Tis)
        {
            const QC::u32 DidVid = QC::mmio_read32(g_State.LocalityBase + TIS_DID_VID);
            if (DidVid == 0 || DidVid == 0xFFFFFFFFu)
            {
                LogStr(Log, "TPM2: no TIS device responds at register window\r\n");
                Info.Type = EType::None;
                return false;
            }
            Info.VendorId = static_cast<QC::u16>(DidVid & 0xFFFF);
            Info.DeviceId = static_cast<QC::u16>(DidVid >> 16);
            Info.RevisionId = QC::mmio_read8(g_State.LocalityBase + TIS_RID);
        }
        else
        {
            LogStr(Log, "TPM2: unsupported start method/interface; skipping TPM commands\r\n");
            return false;
        }

        g_State.bInitialized = true;
        return true;
    }

    bool IsInitialized()
    {
        return g_State.bInitialized;
    }

    const FInfo &Info()
    {
        return g_State.Info;
    }

    QC::u32 Submit(const QC::u8 *Cmd, QC::usize CmdLen, QC::u8 *Rsp, QC::usize RspCap, QC::u32 &RspLen, FLogFn VerboseLog)
    {
        RspLen = 0;
        if (!g_State.bInitialized || !Cmd || CmdLen < 10 || !Rsp || RspCap < 10)
            return kTpmTransportFailure;

        ++g_State.Info.CommandsSubmitted;
        QC::u32 Rc = kTpmTransportFailure;
        if (g_State.Info.Type == EType::Crb)
            Rc = CrbSubmit(Cmd, CmdLen, Rsp, RspCap, RspLen, VerboseLog);
        else if (g_State.Info.Type == EType::Tis)
            Rc = TisSubmit(Cmd, CmdLen, Rsp, RspCap, RspLen, VerboseLog);

        if (Rc == kTpmTransportFailure)
            ++g_State.Info.TransportFailures;
        return Rc;
    }
}
