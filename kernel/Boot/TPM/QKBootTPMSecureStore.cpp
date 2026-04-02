#include "QKBootTPMSecureStore.h"

#include "QCBuiltins.h"
#include "QCString.h"
#include "QFSFile.h"
#include "QFSVFS.h"
#include "QKEntropy.h"
#include "QKMemVMM.h"
#include "QKSecureStore.h"

#include "Debug/Serial/QKDebugSerial.h"

extern "C" QC::VirtAddr physToVirt(QC::PhysAddr phys);

namespace QK::Boot::Tpm
{
    namespace
    {
        static void LogStr(FLogFn Log, const char *Msg)
        {
            if (Log)
                Log(Msg);
        }

        static void LogHex32Fixed(FLogFn Log, QC::u32 Value)
        {
            char HexBuf[9];
            for (int i = 7; i >= 0; --i)
            {
                int nibble = static_cast<int>((Value >> (i * 4)) & 0xF);
                HexBuf[7 - i] = nibble < 10 ? static_cast<char>('0' + nibble) : static_cast<char>('a' + nibble - 10);
            }
            HexBuf[8] = 0;
            LogStr(Log, HexBuf);
        }

        static void LogHex64(FLogFn Log, const char *Label, QC::u64 Value)
        {
            LogStr(Log, Label);
            LogStr(Log, "0x");

            char HexBuf[17];
            for (int i = 15; i >= 0; --i)
            {
                int nibble = static_cast<int>((Value >> (i * 4)) & 0xF);
                HexBuf[15 - i] = nibble < 10 ? static_cast<char>('0' + nibble) : static_cast<char>('a' + nibble - 10);
            }
            HexBuf[16] = 0;
            LogStr(Log, HexBuf);
            LogStr(Log, "\r\n");
        }

        static void LogDecU32(FLogFn Log, const char *Label, QC::u32 Value)
        {
            LogStr(Log, Label);

            char Buf[11];
            QC::usize Pos = 0;

            if (Value == 0)
            {
                Buf[Pos++] = '0';
            }
            else
            {
                char Tmp[10];
                QC::usize TmpPos = 0;
                while (Value > 0 && TmpPos < sizeof(Tmp))
                {
                    Tmp[TmpPos++] = static_cast<char>('0' + (Value % 10));
                    Value /= 10;
                }
                while (TmpPos > 0)
                {
                    Buf[Pos++] = Tmp[--TmpPos];
                }
            }

            Buf[Pos] = 0;
            LogStr(Log, Buf);
            LogStr(Log, "\r\n");
        }

        static QC::u16 ReadBe16(const QC::u8 *p)
        {
            return static_cast<QC::u16>((static_cast<QC::u16>(p[0]) << 8) | static_cast<QC::u16>(p[1]));
        }

        static QC::u32 ReadBe32(const QC::u8 *p)
        {
            return (static_cast<QC::u32>(p[0]) << 24) |
                   (static_cast<QC::u32>(p[1]) << 16) |
                   (static_cast<QC::u32>(p[2]) << 8) |
                   static_cast<QC::u32>(p[3]);
        }

        static void WriteBe16(QC::u8 *p, QC::u16 v)
        {
            p[0] = static_cast<QC::u8>((v >> 8) & 0xFF);
            p[1] = static_cast<QC::u8>(v & 0xFF);
        }

        static void WriteBe32(QC::u8 *p, QC::u32 v)
        {
            p[0] = static_cast<QC::u8>((v >> 24) & 0xFF);
            p[1] = static_cast<QC::u8>((v >> 16) & 0xFF);
            p[2] = static_cast<QC::u8>((v >> 8) & 0xFF);
            p[3] = static_cast<QC::u8>(v & 0xFF);
        }

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

        static bool EnsureHhdmMappedWithFlags(FLogFn Log, QC::PhysAddr Phys, QC::usize Size, QK::Memory::PageFlags Flags)
        {
            if (Phys == 0 || Size == 0)
                return false;

            constexpr QC::usize kPageSize = 4096;
            QC::PhysAddr Start = Phys & ~(static_cast<QC::PhysAddr>(kPageSize - 1));
            QC::PhysAddr End = (Phys + Size + (kPageSize - 1)) & ~(static_cast<QC::PhysAddr>(kPageSize - 1));

            for (QC::PhysAddr P = Start; P < End; P += kPageSize)
            {
                QC::VirtAddr V = physToVirt(P);
                if (!QK::Memory::VMM::instance().isMapped(V))
                {
                    QC::Status Status = QK::Memory::VMM::instance().map(V, P, Flags);
                    if (Status != QC::Status::Success)
                    {
                        LogStr(Log, "TPM2: failed to map physical page\r\n");
                        return false;
                    }
                }
            }

            return true;
        }

        static bool EnsureHhdmMapped(FLogFn Log, QC::PhysAddr Phys, QC::usize Size)
        {
            auto Flags = QK::Memory::PageFlags::Present | QK::Memory::PageFlags::Writable | QK::Memory::PageFlags::NoExecute;
            return EnsureHhdmMappedWithFlags(Log, Phys, Size, Flags);
        }

        static bool EnsureHhdmMappedMmio(FLogFn Log, QC::PhysAddr Phys, QC::usize Size)
        {
            auto Flags = QK::Memory::PageFlags::Present | QK::Memory::PageFlags::Writable |
                         QK::Memory::PageFlags::NoExecute | QK::Memory::PageFlags::NoCache |
                         QK::Memory::PageFlags::WriteThrough;
            return EnsureHhdmMappedWithFlags(Log, Phys, Size, Flags);
        }

        struct FCrbCtx
        {
            QC::VirtAddr Base;
            QC::usize Off;

            QC::VirtAddr Reg(QC::usize R) const
            {
                return Base + Off + R;
            }
        };

        struct FTpmSecureStoreCtx
        {
            bool bReady = false;
            FCrbCtx Ctx{};
        };

        static FTpmSecureStoreCtx g_TpmSecureStore;
        static QC::u32 g_TpmLastRspCode = 0;

        static void SerialStr(const char *Msg)
        {
            QK::Debug::Serial::Write(Msg);
        }

        static void SerialHex32Fixed(QC::u32 Value)
        {
            char HexBuf[9];
            for (int i = 7; i >= 0; --i)
            {
                int nibble = static_cast<int>((Value >> (i * 4)) & 0xF);
                HexBuf[7 - i] = nibble < 10 ? static_cast<char>('0' + nibble) : static_cast<char>('a' + nibble - 10);
            }
            HexBuf[8] = 0;
            SerialStr(HexBuf);
        }

        struct FTpmBufWriter
        {
            QC::u8 *Buf;
            QC::usize Cap;
            QC::usize Len;

            bool Push(const void *Data, QC::usize N)
            {
                if (!Data || Len + N > Cap)
                    return false;
                const QC::u8 *P = static_cast<const QC::u8 *>(Data);
                for (QC::usize i = 0; i < N; ++i)
                    Buf[Len + i] = P[i];
                Len += N;
                return true;
            }

            bool U8(QC::u8 V)
            {
                return Push(&V, 1);
            }

            bool Be16(QC::u16 V)
            {
                QC::u8 Tmp[2];
                WriteBe16(Tmp, V);
                return Push(Tmp, 2);
            }

            bool Be32(QC::u32 V)
            {
                QC::u8 Tmp[4];
                WriteBe32(Tmp, V);
                return Push(Tmp, 4);
            }
        };

        static bool TpmRspParams(const QC::u8 *Rsp, QC::u32 RspLen, const QC::u8 *&OutParams, QC::u32 &OutParamsLen)
        {
            OutParams = nullptr;
            OutParamsLen = 0;
            if (!Rsp || RspLen < 10)
                return false;

            const QC::u16 Tag = ReadBe16(Rsp);
            if (Tag == 0x8001)
            {
                OutParams = Rsp + 10;
                OutParamsLen = (RspLen >= 10) ? (RspLen - 10) : 0;
                return true;
            }

            if (Tag != 0x8002)
                return false;

            if (RspLen >= 14)
            {
                const QC::u32 ParameterSize = ReadBe32(Rsp + 10);
                if (14u + ParameterSize <= RspLen)
                {
                    OutParams = Rsp + 14;
                    OutParamsLen = ParameterSize;
                    return true;
                }
            }

            OutParams = Rsp + 10;
            OutParamsLen = RspLen - 10;
            return true;
        }

        static bool TpmAppendSessionAuthHandle(FTpmBufWriter &W, QC::u32 SessionHandle);

        static bool TpmAppendPwSessionAuth(FTpmBufWriter &W)
        {
            return TpmAppendSessionAuthHandle(W, 0x40000009u);
        }

        static bool TpmAppendSessionAuthHandle(FTpmBufWriter &W, QC::u32 SessionHandle)
        {
            if (!W.Be32(SessionHandle))
                return false;
            if (!W.Be16(0))
                return false;
            if (!W.U8(0))
                return false;
            if (!W.Be16(0))
                return false;
            return true;
        }

        static QC::u32 CrbSubmitQuiet(FLogFn Log, const FCrbCtx &Ctx, const QC::u8 *Cmd, QC::usize CmdLen, QC::u32 &RspLenOut, QC::PhysAddr &RspPhysOut)
        {
            constexpr QC::usize CTRL_REQ = 0x00;
            constexpr QC::usize CTRL_CANCEL = 0x08;
            constexpr QC::usize CTRL_START = 0x0C;
            constexpr QC::usize CMD_SIZE = 0x18;
            constexpr QC::usize CMD_PA_LOW = 0x1C;
            constexpr QC::usize CMD_PA_HIGH = 0x20;
            constexpr QC::usize RSP_SIZE = 0x24;
            constexpr QC::usize RSP_PA = 0x28;

            RspLenOut = 0;
            RspPhysOut = 0;

            QC::mmio_write32(Ctx.Reg(CTRL_REQ), QC::mmio_read32(Ctx.Reg(CTRL_REQ)) | 1u);
            if (!SpinWaitClears32(Ctx.Reg(CTRL_REQ), 1u, 5'000'000))
            {
                return 0xFFFFFFFFu;
            }

            QC::u32 CmdSize = QC::mmio_read32(Ctx.Reg(CMD_SIZE));
            QC::u32 CmdLow = QC::mmio_read32(Ctx.Reg(CMD_PA_LOW));
            QC::u32 CmdHigh = QC::mmio_read32(Ctx.Reg(CMD_PA_HIGH));
            QC::u64 CmdPhys64 = (static_cast<QC::u64>(CmdHigh) << 32) | CmdLow;

            QC::u32 RspSize = QC::mmio_read32(Ctx.Reg(RSP_SIZE));
            QC::u64 RspPhys64 = QC::mmio_read64(Ctx.Reg(RSP_PA));

            if (CmdPhys64 == 0 || RspPhys64 == 0)
            {
                return 0xFFFFFFFFu;
            }

            if (CmdLen > CmdSize || CmdSize < 12 || RspSize < 10)
            {
                return 0xFFFFFFFFu;
            }

            QC::PhysAddr CmdPhys = static_cast<QC::PhysAddr>(CmdPhys64);
            QC::PhysAddr RspPhys = static_cast<QC::PhysAddr>(RspPhys64);

            if (!EnsureHhdmMapped(Log, CmdPhys, CmdSize) || !EnsureHhdmMapped(Log, RspPhys, RspSize))
            {
                return 0xFFFFFFFFu;
            }

            auto *CmdBuf = reinterpret_cast<volatile QC::u8 *>(physToVirt(CmdPhys));
            for (QC::usize i = 0; i < CmdLen; ++i)
            {
                CmdBuf[i] = Cmd[i];
            }

            QC::write_barrier();
            QC::memory_barrier();

            QC::mmio_write32(Ctx.Reg(CTRL_START), 1u);
            if (!SpinWaitClears32(Ctx.Reg(CTRL_START), 1u, 50'000'000))
            {
                QC::mmio_write32(Ctx.Reg(CTRL_CANCEL), 1u);
                (void)SpinWaitClears32(Ctx.Reg(CTRL_START), 1u, 5'000'000);
                return 0xFFFFFFFFu;
            }

            QC::read_barrier();
            QC::memory_barrier();

            auto *RspBuf = reinterpret_cast<const QC::u8 *>(physToVirt(RspPhys));
            QC::u32 RspLen = ReadBe32(RspBuf + 2);
            QC::u32 RspCode = ReadBe32(RspBuf + 6);

            RspLenOut = RspLen;
            RspPhysOut = RspPhys;

            QC::mmio_write32(Ctx.Reg(CTRL_REQ), QC::mmio_read32(Ctx.Reg(CTRL_REQ)) | 2u);
            (void)SpinWaitClears32(Ctx.Reg(CTRL_REQ), 2u, 5'000'000);

            return RspCode;
        }

        static QC::u32 CrbSubmit(FLogFn Log, const FCrbCtx &Ctx, const QC::u8 *Cmd, QC::usize CmdLen, QC::u32 &RspLenOut, QC::PhysAddr &RspPhysOut)
        {
            constexpr QC::usize CTRL_REQ = 0x00;
            constexpr QC::usize CTRL_STS = 0x04;
            constexpr QC::usize CTRL_CANCEL = 0x08;
            constexpr QC::usize CTRL_START = 0x0C;
            constexpr QC::usize CMD_SIZE = 0x18;
            constexpr QC::usize CMD_PA_LOW = 0x1C;
            constexpr QC::usize CMD_PA_HIGH = 0x20;
            constexpr QC::usize RSP_SIZE = 0x24;
            constexpr QC::usize RSP_PA = 0x28;

            RspLenOut = 0;
            RspPhysOut = 0;

            QC::mmio_write32(Ctx.Reg(CTRL_REQ), QC::mmio_read32(Ctx.Reg(CTRL_REQ)) | 1u);
            if (!SpinWaitClears32(Ctx.Reg(CTRL_REQ), 1u, 5'000'000))
            {
                LogStr(Log, "TPM2: CMD_READY timeout\r\n");
                return 0xFFFFFFFFu;
            }

            QC::u32 CmdSize = QC::mmio_read32(Ctx.Reg(CMD_SIZE));
            QC::u32 CmdLow = QC::mmio_read32(Ctx.Reg(CMD_PA_LOW));
            QC::u32 CmdHigh = QC::mmio_read32(Ctx.Reg(CMD_PA_HIGH));
            QC::u64 CmdPhys64 = (static_cast<QC::u64>(CmdHigh) << 32) | CmdLow;

            QC::u32 RspSize = QC::mmio_read32(Ctx.Reg(RSP_SIZE));
            QC::u64 RspPhys64 = QC::mmio_read64(Ctx.Reg(RSP_PA));

            LogHex64(Log, "TPM2: cmdBuf phys ", CmdPhys64);
            LogDecU32(Log, "TPM2: cmdBuf size ", CmdSize);
            LogHex64(Log, "TPM2: rspBuf phys ", RspPhys64);
            LogDecU32(Log, "TPM2: rspBuf size ", RspSize);

            if (CmdPhys64 == 0 || RspPhys64 == 0)
            {
                LogStr(Log, "TPM2: invalid CRB buffer address\r\n");
                return 0xFFFFFFFFu;
            }

            if (CmdLen > CmdSize || CmdSize < 12 || RspSize < 10)
            {
                LogStr(Log, "TPM2: invalid CRB buffer sizes\r\n");
                return 0xFFFFFFFFu;
            }

            QC::PhysAddr CmdPhys = static_cast<QC::PhysAddr>(CmdPhys64);
            QC::PhysAddr RspPhys = static_cast<QC::PhysAddr>(RspPhys64);

            if (!EnsureHhdmMapped(Log, CmdPhys, CmdSize) || !EnsureHhdmMapped(Log, RspPhys, RspSize))
            {
                LogStr(Log, "TPM2: failed to map cmd/rsp buffers\r\n");
                return 0xFFFFFFFFu;
            }

            auto *CmdBuf = reinterpret_cast<volatile QC::u8 *>(physToVirt(CmdPhys));
            for (QC::usize i = 0; i < CmdLen; ++i)
            {
                CmdBuf[i] = Cmd[i];
            }

            QC::write_barrier();
            QC::memory_barrier();

            QC::mmio_write32(Ctx.Reg(CTRL_START), 1u);
            if (!SpinWaitClears32(Ctx.Reg(CTRL_START), 1u, 50'000'000))
            {
                LogStr(Log, "TPM2: START timeout; issuing CANCEL\r\n");
                QC::mmio_write32(Ctx.Reg(CTRL_CANCEL), 1u);
                (void)SpinWaitClears32(Ctx.Reg(CTRL_START), 1u, 5'000'000);
                return 0xFFFFFFFFu;
            }

            QC::u32 Sts = QC::mmio_read32(Ctx.Reg(CTRL_STS));
            LogStr(Log, "TPM2: CTRL_STS 0x");
            LogHex32Fixed(Log, Sts);
            LogStr(Log, "\r\n");

            QC::read_barrier();
            QC::memory_barrier();

            auto *RspBuf = reinterpret_cast<const QC::u8 *>(physToVirt(RspPhys));
            QC::u32 RspLen = ReadBe32(RspBuf + 2);
            QC::u32 RspCode = ReadBe32(RspBuf + 6);

            RspLenOut = RspLen;
            RspPhysOut = RspPhys;

            LogDecU32(Log, "TPM2: rspLen ", RspLen);
            LogStr(Log, "TPM2: rspCode 0x");
            LogHex32Fixed(Log, RspCode);
            LogStr(Log, "\r\n");

            QC::mmio_write32(Ctx.Reg(CTRL_REQ), QC::mmio_read32(Ctx.Reg(CTRL_REQ)) | 2u);
            (void)SpinWaitClears32(Ctx.Reg(CTRL_REQ), 2u, 5'000'000);

            return RspCode;
        }

        static QC::Status TpmStartPolicySession(FLogFn Log, const FCrbCtx &Ctx, bool bTrial, QC::u32 &OutSession)
        {
            QC::u8 Cmd[128];
            FTpmBufWriter W{Cmd, sizeof(Cmd), 0};

            constexpr QC::u16 TPM_ST_NO_SESSIONS = 0x8001;
            constexpr QC::u32 TPM_CC_StartAuthSession = 0x00000176u;
            constexpr QC::u32 TPM_RH_NULL = 0x40000007u;
            constexpr QC::u16 TPM_ALG_NULL = 0x0010;
            constexpr QC::u16 TPM_ALG_SHA256 = 0x000B;

            if (!W.Be16(TPM_ST_NO_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_StartAuthSession))
                return QC::Status::Error;

            if (!W.Be32(TPM_RH_NULL) || !W.Be32(TPM_RH_NULL))
                return QC::Status::Error;

            QC::u8 NonceCaller[16];
            for (QC::usize i = 0; i < sizeof(NonceCaller); ++i)
                NonceCaller[i] = static_cast<QC::u8>(0xA5u ^ static_cast<QC::u8>(i));
            if (!W.Be16(static_cast<QC::u16>(sizeof(NonceCaller))) || !W.Push(NonceCaller, sizeof(NonceCaller)))
                return QC::Status::Error;

            if (!W.Be16(0))
                return QC::Status::Error;
            if (!W.U8(bTrial ? static_cast<QC::u8>(0x03) : static_cast<QC::u8>(0x01)))
                return QC::Status::Error;
            if (!W.Be16(TPM_ALG_NULL))
                return QC::Status::Error;
            if (!W.Be16(TPM_ALG_SHA256))
                return QC::Status::Error;

            WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

            QC::u32 RspLen = 0;
            QC::PhysAddr RspPhys = 0;
            QC::u32 RspCode = CrbSubmitQuiet(Log, Ctx, Cmd, W.Len, RspLen, RspPhys);
            g_TpmLastRspCode = RspCode;
            if (RspCode != 0)
                return QC::Status::Error;
            if (!EnsureHhdmMapped(Log, RspPhys, RspLen))
                return QC::Status::Error;

            const QC::u8 *Rsp = reinterpret_cast<const QC::u8 *>(physToVirt(RspPhys));
            const QC::u8 *Params = nullptr;
            QC::u32 ParamsLen = 0;
            if (!TpmRspParams(Rsp, RspLen, Params, ParamsLen))
                return QC::Status::Error;
            if (!Params || ParamsLen < 4)
                return QC::Status::Error;

            OutSession = ReadBe32(Params);
            g_TpmLastRspCode = 0;
            return QC::Status::Success;
        }

        static QC::Status TpmPolicyPCR(FLogFn Log, const FCrbCtx &Ctx, QC::u32 PolicySession)
        {
            QC::u8 Cmd[96];
            FTpmBufWriter W{Cmd, sizeof(Cmd), 0};

            constexpr QC::u16 TPM_ST_NO_SESSIONS = 0x8001;
            constexpr QC::u32 TPM_CC_PolicyPCR = 0x0000017Fu;
            constexpr QC::u16 TPM_ALG_SHA256 = 0x000B;

            if (!W.Be16(TPM_ST_NO_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_PolicyPCR))
                return QC::Status::Error;
            if (!W.Be32(PolicySession))
                return QC::Status::Error;
            if (!W.Be16(0))
                return QC::Status::Error;
            if (!W.Be32(1))
                return QC::Status::Error;
            if (!W.Be16(TPM_ALG_SHA256))
                return QC::Status::Error;
            if (!W.U8(3))
                return QC::Status::Error;
            if (!W.U8(0x80) || !W.U8(0x00) || !W.U8(0x00))
                return QC::Status::Error;

            WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

            QC::u32 RspLen = 0;
            QC::PhysAddr RspPhys = 0;
            QC::u32 RspCode = CrbSubmitQuiet(Log, Ctx, Cmd, W.Len, RspLen, RspPhys);
            g_TpmLastRspCode = RspCode;
            if (RspCode != 0)
                return QC::Status::Error;
            g_TpmLastRspCode = 0;
            return QC::Status::Success;
        }

        static QC::Status TpmPcrExtendSha256(FLogFn Log, const FCrbCtx &Ctx, QC::u32 PcrIndex, const QC::u8 Digest[32])
        {
            QC::u8 Cmd[128];
            FTpmBufWriter W{Cmd, sizeof(Cmd), 0};

            constexpr QC::u16 TPM_ST_SESSIONS = 0x8002;
            constexpr QC::u32 TPM_CC_PCR_Extend = 0x00000182u;
            constexpr QC::u16 TPM_ALG_SHA256 = 0x000B;

            const QC::u32 PcrHandle = PcrIndex;

            if (!W.Be16(TPM_ST_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_PCR_Extend))
                return QC::Status::Error;
            if (!W.Be32(PcrHandle))
                return QC::Status::Error;
            if (!W.Be32(9))
                return QC::Status::Error;
            if (!TpmAppendPwSessionAuth(W))
                return QC::Status::Error;
            if (!W.Be32(1))
                return QC::Status::Error;
            if (!W.Be16(TPM_ALG_SHA256))
                return QC::Status::Error;
            if (!W.Push(Digest, 32))
                return QC::Status::Error;

            WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

            QC::u32 RspLen = 0;
            QC::PhysAddr RspPhys = 0;
            QC::u32 RspCode = CrbSubmitQuiet(Log, Ctx, Cmd, W.Len, RspLen, RspPhys);
            g_TpmLastRspCode = RspCode;
            if (RspCode != 0)
                return QC::Status::Error;
            g_TpmLastRspCode = 0;
            return QC::Status::Success;
        }

        static QC::Status TpmPolicyGetDigest(FLogFn Log, const FCrbCtx &Ctx, QC::u32 PolicySession, QC::u8 OutDigest[32])
        {
            QC::u8 Cmd[64];
            FTpmBufWriter W{Cmd, sizeof(Cmd), 0};

            constexpr QC::u16 TPM_ST_NO_SESSIONS = 0x8001;
            constexpr QC::u32 TPM_CC_PolicyGetDigest = 0x00000189u;

            if (!W.Be16(TPM_ST_NO_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_PolicyGetDigest))
                return QC::Status::Error;
            if (!W.Be32(PolicySession))
                return QC::Status::Error;

            WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

            QC::u32 RspLen = 0;
            QC::PhysAddr RspPhys = 0;
            QC::u32 RspCode = CrbSubmitQuiet(Log, Ctx, Cmd, W.Len, RspLen, RspPhys);
            g_TpmLastRspCode = RspCode;
            if (RspCode != 0)
                return QC::Status::Error;
            if (!EnsureHhdmMapped(Log, RspPhys, RspLen))
                return QC::Status::Error;

            const QC::u8 *Rsp = reinterpret_cast<const QC::u8 *>(physToVirt(RspPhys));
            const QC::u8 *Params = nullptr;
            QC::u32 ParamsLen = 0;
            if (!TpmRspParams(Rsp, RspLen, Params, ParamsLen))
                return QC::Status::Error;
            if (!Params || ParamsLen < 2)
                return QC::Status::Error;

            const QC::u16 Sz = ReadBe16(Params);
            if (Sz != 32)
                return QC::Status::Error;
            if (2u + Sz > ParamsLen)
                return QC::Status::Error;
            for (int i = 0; i < 32; ++i)
                OutDigest[i] = Params[2 + i];

            g_TpmLastRspCode = 0;
            return QC::Status::Success;
        }

        static QC::Status TpmCreatePrimaryStorageKey(FLogFn Log, const FCrbCtx &Ctx, QC::u32 &OutHandle)
        {
            QC::u8 Cmd[512];
            FTpmBufWriter W{Cmd, sizeof(Cmd), 0};

            constexpr QC::u16 TPM_ST_SESSIONS = 0x8002;
            constexpr QC::u32 TPM_CC_CreatePrimary = 0x00000131u;
            constexpr QC::u32 TPM_RH_OWNER = 0x40000001u;

            if (!W.Be16(TPM_ST_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_CreatePrimary))
                return QC::Status::Error;
            if (!W.Be32(TPM_RH_OWNER))
                return QC::Status::Error;
            if (!W.Be32(9))
                return QC::Status::Error;
            if (!TpmAppendPwSessionAuth(W))
                return QC::Status::Error;
            if (!W.Be16(4) || !W.Be16(0) || !W.Be16(0))
                return QC::Status::Error;

            const QC::usize InPublicSizeOffset = W.Len;
            if (!W.Be16(0))
                return QC::Status::Error;
            const QC::usize InPublicStart = W.Len;

            constexpr QC::u16 TPM_ALG_RSA = 0x0001;
            constexpr QC::u16 TPM_ALG_SHA256 = 0x000B;
            constexpr QC::u16 TPM_ALG_AES = 0x0006;
            constexpr QC::u16 TPM_ALG_CFB = 0x0043;
            constexpr QC::u16 TPM_ALG_NULL = 0x0010;

            constexpr QC::u32 TPMA_FIXEDTPM = 0x00000002u;
            constexpr QC::u32 TPMA_FIXEDPARENT = 0x00000010u;
            constexpr QC::u32 TPMA_SENSITIVEDATAORIGIN = 0x00000020u;
            constexpr QC::u32 TPMA_USERWITHAUTH = 0x00000040u;
            constexpr QC::u32 TPMA_NODA = 0x00000400u;
            constexpr QC::u32 TPMA_RESTRICTED = 0x00010000u;
            constexpr QC::u32 TPMA_DECRYPT = 0x00020000u;

            const QC::u32 ObjectAttributes = TPMA_FIXEDTPM | TPMA_FIXEDPARENT | TPMA_SENSITIVEDATAORIGIN |
                                             TPMA_USERWITHAUTH | TPMA_NODA | TPMA_RESTRICTED | TPMA_DECRYPT;

            if (!W.Be16(TPM_ALG_RSA) || !W.Be16(TPM_ALG_SHA256) || !W.Be32(ObjectAttributes))
                return QC::Status::Error;
            if (!W.Be16(0))
                return QC::Status::Error;
            if (!W.Be16(TPM_ALG_AES) || !W.Be16(128) || !W.Be16(TPM_ALG_CFB))
                return QC::Status::Error;
            if (!W.Be16(TPM_ALG_NULL))
                return QC::Status::Error;
            if (!W.Be16(2048) || !W.Be32(0))
                return QC::Status::Error;
            if (!W.Be16(0))
                return QC::Status::Error;

            const QC::u16 InPublicSize = static_cast<QC::u16>(W.Len - InPublicStart);
            WriteBe16(Cmd + InPublicSizeOffset, InPublicSize);

            if (!W.Be16(0))
                return QC::Status::Error;
            if (!W.Be32(0))
                return QC::Status::Error;

            WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

            QC::u32 RspLen = 0;
            QC::PhysAddr RspPhys = 0;
            QC::u32 RspCode = CrbSubmitQuiet(Log, Ctx, Cmd, W.Len, RspLen, RspPhys);
            g_TpmLastRspCode = RspCode;
            if (RspCode != 0)
                return QC::Status::Error;
            if (RspLen < 14)
                return QC::Status::Error;
            if (!EnsureHhdmMapped(Log, RspPhys, RspLen))
                return QC::Status::Error;

            const QC::u8 *Rsp = reinterpret_cast<const QC::u8 *>(physToVirt(RspPhys));
            const QC::u8 *Params = nullptr;
            QC::u32 ParamsLen = 0;
            if (!TpmRspParams(Rsp, RspLen, Params, ParamsLen))
                return QC::Status::Error;
            if (!Params || ParamsLen < 4)
                return QC::Status::Error;

            OutHandle = ReadBe32(Params);
            g_TpmLastRspCode = 0;
            return QC::Status::Success;
        }

        static QC::Status TpmFlushContext(FLogFn Log, const FCrbCtx &Ctx, QC::u32 Handle)
        {
            (void)Log;
            QC::u8 Cmd[64];
            FTpmBufWriter W{Cmd, sizeof(Cmd), 0};
            constexpr QC::u16 TPM_ST_NO_SESSIONS = 0x8001;
            constexpr QC::u32 TPM_CC_FlushContext = 0x00000165u;

            if (!W.Be16(TPM_ST_NO_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_FlushContext))
                return QC::Status::Error;
            if (!W.Be32(Handle))
                return QC::Status::Error;

            WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

            QC::u32 RspLen = 0;
            QC::PhysAddr RspPhys = 0;
            QC::u32 RspCode = CrbSubmitQuiet(Log, Ctx, Cmd, W.Len, RspLen, RspPhys);
            g_TpmLastRspCode = RspCode;
            if (RspCode != 0)
                return QC::Status::Error;
            g_TpmLastRspCode = 0;
            return QC::Status::Success;
        }

        static QC::Status TpmLoadExternalRsa2048RsassaSha256PublicKey(FLogFn Log, const FCrbCtx &Ctx, const QC::u8 Modulus[256], QC::u32 &OutHandle)
        {
            OutHandle = 0;
            if (!Modulus)
                return QC::Status::InvalidParam;

            QC::u8 Cmd[1024];
            FTpmBufWriter W{Cmd, sizeof(Cmd), 0};

            constexpr QC::u16 TPM_ST_NO_SESSIONS = 0x8001;
            constexpr QC::u32 TPM_CC_LoadExternal = 0x00000167u;
            constexpr QC::u32 TPM_RH_NULL = 0x40000007u;

            constexpr QC::u16 TPM_ALG_RSA = 0x0001;
            constexpr QC::u16 TPM_ALG_SHA256 = 0x000B;
            constexpr QC::u16 TPM_ALG_NULL = 0x0010;
            constexpr QC::u16 TPM_ALG_RSASSA = 0x0014;

            constexpr QC::u32 TPMA_USERWITHAUTH = 0x00000040u;
            constexpr QC::u32 TPMA_SIGN_ENCRYPT = 0x00040000u;
            constexpr QC::u32 TPMA_NODA = 0x00000400u;
            const QC::u32 ObjectAttributes = TPMA_USERWITHAUTH | TPMA_SIGN_ENCRYPT | TPMA_NODA;

            if (!W.Be16(TPM_ST_NO_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_LoadExternal))
                return QC::Status::Error;

            // TPM2_LoadExternal has no handle area; hierarchy is a parameter.
            // inPrivate: TPM2B_SENSITIVE (empty)
            if (!W.Be16(0))
                return QC::Status::Error;

            // inPublic: TPM2B_PUBLIC
            const QC::usize InPublicSizeOffset = W.Len;
            if (!W.Be16(0))
                return QC::Status::Error;
            const QC::usize InPublicStart = W.Len;

            // TPMT_PUBLIC
            if (!W.Be16(TPM_ALG_RSA) || !W.Be16(TPM_ALG_SHA256) || !W.Be32(ObjectAttributes))
                return QC::Status::Error;

            // authPolicy: TPM2B_DIGEST (empty)
            if (!W.Be16(0))
                return QC::Status::Error;

            // parameters: TPMU_PUBLIC_PARMS -> TPMS_RSA_PARMS
            // symmetric: TPMT_SYM_DEF_OBJECT (NULL)
            if (!W.Be16(TPM_ALG_NULL))
                return QC::Status::Error;
            // scheme: TPMT_RSA_SCHEME (RSASSA + SHA256)
            if (!W.Be16(TPM_ALG_RSASSA) || !W.Be16(TPM_ALG_SHA256))
                return QC::Status::Error;
            // keyBits, exponent
            if (!W.Be16(2048) || !W.Be32(0))
                return QC::Status::Error;

            // unique: TPM2B_PUBLIC_KEY_RSA
            if (!W.Be16(256))
                return QC::Status::Error;
            if (!W.Push(Modulus, 256))
                return QC::Status::Error;

            const QC::u16 InPublicSize = static_cast<QC::u16>(W.Len - InPublicStart);
            WriteBe16(Cmd + InPublicSizeOffset, InPublicSize);

            // hierarchy: TPMI_RH_HIERARCHY
            if (!W.Be32(TPM_RH_NULL))
                return QC::Status::Error;

            WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

            QC::u32 RspLen = 0;
            QC::PhysAddr RspPhys = 0;
            QC::u32 RspCode = CrbSubmitQuiet(Log, Ctx, Cmd, W.Len, RspLen, RspPhys);
            g_TpmLastRspCode = RspCode;
            if (RspCode != 0)
                return QC::Status::Error;
            if (!EnsureHhdmMapped(Log, RspPhys, RspLen))
                return QC::Status::Error;

            const QC::u8 *Rsp = reinterpret_cast<const QC::u8 *>(physToVirt(RspPhys));
            const QC::u8 *Params = nullptr;
            QC::u32 ParamsLen = 0;
            if (!TpmRspParams(Rsp, RspLen, Params, ParamsLen))
                return QC::Status::Error;
            if (!Params || ParamsLen < 4)
                return QC::Status::Error;

            OutHandle = ReadBe32(Params);
            g_TpmLastRspCode = 0;
            return QC::Status::Success;
        }

        static QC::Status TpmVerifyRsa2048RsassaSha256Digest(FLogFn Log, const FCrbCtx &Ctx, QC::u32 KeyHandle, const QC::u8 Digest[32], const QC::u8 Signature[256])
        {
            if (!Digest || !Signature)
                return QC::Status::InvalidParam;

            QC::u8 Cmd[768];
            FTpmBufWriter W{Cmd, sizeof(Cmd), 0};

            constexpr QC::u16 TPM_ST_NO_SESSIONS = 0x8001;
            constexpr QC::u32 TPM_CC_VerifySignature = 0x00000177u;
            constexpr QC::u16 TPM_ALG_RSASSA = 0x0014;
            constexpr QC::u16 TPM_ALG_SHA256 = 0x000B;

            if (!W.Be16(TPM_ST_NO_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_VerifySignature))
                return QC::Status::Error;
            if (!W.Be32(KeyHandle))
                return QC::Status::Error;

            // digest: TPM2B_DIGEST
            if (!W.Be16(32) || !W.Push(Digest, 32))
                return QC::Status::Error;

            // signature: TPMT_SIGNATURE -> RSASSA(SHA256) + TPM2B_PUBLIC_KEY_RSA
            if (!W.Be16(TPM_ALG_RSASSA) || !W.Be16(TPM_ALG_SHA256))
                return QC::Status::Error;
            if (!W.Be16(256) || !W.Push(Signature, 256))
                return QC::Status::Error;

            WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

            QC::u32 RspLen = 0;
            QC::PhysAddr RspPhys = 0;
            QC::u32 RspCode = CrbSubmitQuiet(Log, Ctx, Cmd, W.Len, RspLen, RspPhys);
            g_TpmLastRspCode = RspCode;
            if (RspCode != 0)
                return QC::Status::Error;

            g_TpmLastRspCode = 0;
            return QC::Status::Success;
        }

        static QC::Status TpmCreateSealedObject(FLogFn Log,
                                                const FCrbCtx &Ctx,
                                                QC::u32 ParentHandle,
                                                const QC::u8 *Secret,
                                                QC::usize SecretLen,
                                                const QC::u8 PolicyDigest[32],
                                                QC::Vector<QC::u8> &OutPrivate2b,
                                                QC::Vector<QC::u8> &OutPublic2b)
        {
            if (!Secret || SecretLen == 0 || SecretLen > 64)
                return QC::Status::InvalidParam;

            QC::u8 Cmd[768];
            FTpmBufWriter W{Cmd, sizeof(Cmd), 0};
            constexpr QC::u16 TPM_ST_SESSIONS = 0x8002;
            constexpr QC::u32 TPM_CC_Create = 0x00000153u;
            constexpr QC::u16 TPM_ALG_KEYEDHASH = 0x0008;
            constexpr QC::u16 TPM_ALG_SHA256 = 0x000B;
            constexpr QC::u16 TPM_ALG_NULL = 0x0010;

            constexpr QC::u32 TPMA_FIXEDTPM = 0x00000002u;
            constexpr QC::u32 TPMA_FIXEDPARENT = 0x00000010u;
            constexpr QC::u32 TPMA_ADMINWITHPOLICY = 0x00000080u;
            constexpr QC::u32 TPMA_NODA = 0x00000400u;
            const QC::u32 ObjectAttributes = TPMA_FIXEDTPM | TPMA_FIXEDPARENT | TPMA_ADMINWITHPOLICY | TPMA_NODA;

            if (!W.Be16(TPM_ST_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_Create))
                return QC::Status::Error;
            if (!W.Be32(ParentHandle))
                return QC::Status::Error;
            if (!W.Be32(9))
                return QC::Status::Error;
            if (!TpmAppendPwSessionAuth(W))
                return QC::Status::Error;

            const QC::u16 SensInnerSize = static_cast<QC::u16>(2 + 0 + 2 + SecretLen);
            if (!W.Be16(SensInnerSize))
                return QC::Status::Error;
            if (!W.Be16(0))
                return QC::Status::Error;
            if (!W.Be16(static_cast<QC::u16>(SecretLen)))
                return QC::Status::Error;
            if (!W.Push(Secret, SecretLen))
                return QC::Status::Error;

            const QC::usize InPublicSizeOffset = W.Len;
            if (!W.Be16(0))
                return QC::Status::Error;
            const QC::usize InPublicStart = W.Len;

            if (!W.Be16(TPM_ALG_KEYEDHASH) || !W.Be16(TPM_ALG_SHA256) || !W.Be32(ObjectAttributes))
                return QC::Status::Error;
            if (!W.Be16(32))
                return QC::Status::Error;
            if (!W.Push(PolicyDigest, 32))
                return QC::Status::Error;
            if (!W.Be16(TPM_ALG_NULL))
                return QC::Status::Error;
            if (!W.Be16(0))
                return QC::Status::Error;

            const QC::u16 InPublicSize = static_cast<QC::u16>(W.Len - InPublicStart);
            WriteBe16(Cmd + InPublicSizeOffset, InPublicSize);

            if (!W.Be16(0))
                return QC::Status::Error;
            if (!W.Be32(0))
                return QC::Status::Error;

            WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

            QC::u32 RspLen = 0;
            QC::PhysAddr RspPhys = 0;
            QC::u32 RspCode = CrbSubmitQuiet(Log, Ctx, Cmd, W.Len, RspLen, RspPhys);
            g_TpmLastRspCode = RspCode;
            if (RspCode != 0)
                return QC::Status::Error;
            if (!EnsureHhdmMapped(Log, RspPhys, RspLen))
                return QC::Status::Error;

            const QC::u8 *Rsp = reinterpret_cast<const QC::u8 *>(physToVirt(RspPhys));

            const QC::u8 *Params = nullptr;
            QC::u32 ParamsLen = 0;
            if (!TpmRspParams(Rsp, RspLen, Params, ParamsLen))
                return QC::Status::Error;
            if (!Params || ParamsLen < 4)
                return QC::Status::Error;

            QC::usize Off = 0;
            const QC::u16 PrivSize = ReadBe16(Params + Off);
            Off += 2;
            if (Off + PrivSize > ParamsLen)
                return QC::Status::Error;
            OutPrivate2b.clear();
            OutPrivate2b.resize(static_cast<QC::usize>(2 + PrivSize));
            OutPrivate2b[0] = Params[0];
            OutPrivate2b[1] = Params[1];
            for (QC::usize i = 0; i < PrivSize; ++i)
                OutPrivate2b[2 + i] = Params[Off + i];
            Off += PrivSize;

            if (Off + 2 > ParamsLen)
                return QC::Status::Error;
            const QC::u16 PubSize = ReadBe16(Params + Off);
            Off += 2;
            if (Off + PubSize > ParamsLen)
                return QC::Status::Error;
            OutPublic2b.clear();
            OutPublic2b.resize(static_cast<QC::usize>(2 + PubSize));
            OutPublic2b[0] = Params[Off - 2];
            OutPublic2b[1] = Params[Off - 1];
            for (QC::usize i = 0; i < PubSize; ++i)
                OutPublic2b[2 + i] = Params[Off + i];

            g_TpmLastRspCode = 0;
            return QC::Status::Success;
        }

        static QC::Status TpmLoadSealedObject(FLogFn Log,
                                              const FCrbCtx &Ctx,
                                              QC::u32 ParentHandle,
                                              const QC::Vector<QC::u8> &InPrivate2b,
                                              const QC::Vector<QC::u8> &InPublic2b,
                                              QC::u32 &OutHandle)
        {
            if (InPrivate2b.size() < 2 || InPublic2b.size() < 2)
                return QC::Status::InvalidParam;

            QC::u8 Cmd[1024];
            FTpmBufWriter W{Cmd, sizeof(Cmd), 0};
            constexpr QC::u16 TPM_ST_SESSIONS = 0x8002;
            constexpr QC::u32 TPM_CC_Load = 0x00000157u;

            if (!W.Be16(TPM_ST_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_Load))
                return QC::Status::Error;
            if (!W.Be32(ParentHandle))
                return QC::Status::Error;
            if (!W.Be32(9))
                return QC::Status::Error;
            if (!TpmAppendPwSessionAuth(W))
                return QC::Status::Error;
            if (!W.Push(InPrivate2b.data(), InPrivate2b.size()))
                return QC::Status::Error;
            if (!W.Push(InPublic2b.data(), InPublic2b.size()))
                return QC::Status::Error;

            WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

            QC::u32 RspLen = 0;
            QC::PhysAddr RspPhys = 0;
            QC::u32 RspCode = CrbSubmitQuiet(Log, Ctx, Cmd, W.Len, RspLen, RspPhys);
            g_TpmLastRspCode = RspCode;
            if (RspCode != 0)
                return QC::Status::Error;
            if (!EnsureHhdmMapped(Log, RspPhys, RspLen))
                return QC::Status::Error;

            const QC::u8 *Rsp = reinterpret_cast<const QC::u8 *>(physToVirt(RspPhys));
            const QC::u8 *Params = nullptr;
            QC::u32 ParamsLen = 0;
            if (!TpmRspParams(Rsp, RspLen, Params, ParamsLen))
                return QC::Status::Error;
            if (!Params || ParamsLen < 4)
                return QC::Status::Error;
            OutHandle = ReadBe32(Params);
            g_TpmLastRspCode = 0;
            return QC::Status::Success;
        }

        static QC::Status TpmUnsealWithAuthSession(FLogFn Log,
                                                   const FCrbCtx &Ctx,
                                                   QC::u32 ObjectHandle,
                                                   QC::u32 AuthSessionHandle,
                                                   QC::u8 *Out,
                                                   QC::usize OutLen)
        {
            if (!Out || OutLen == 0)
                return QC::Status::InvalidParam;

            QC::u8 Cmd[128];
            FTpmBufWriter W{Cmd, sizeof(Cmd), 0};
            constexpr QC::u16 TPM_ST_SESSIONS = 0x8002;
            constexpr QC::u32 TPM_CC_Unseal = 0x0000015Eu;

            if (!W.Be16(TPM_ST_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_Unseal))
                return QC::Status::Error;
            if (!W.Be32(ObjectHandle))
                return QC::Status::Error;
            if (!W.Be32(9))
                return QC::Status::Error;
            if (!TpmAppendSessionAuthHandle(W, AuthSessionHandle))
                return QC::Status::Error;

            WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

            QC::u32 RspLen = 0;
            QC::PhysAddr RspPhys = 0;
            QC::u32 RspCode = CrbSubmitQuiet(Log, Ctx, Cmd, W.Len, RspLen, RspPhys);
            g_TpmLastRspCode = RspCode;
            if (RspCode != 0)
                return QC::Status::Error;
            if (!EnsureHhdmMapped(Log, RspPhys, RspLen))
                return QC::Status::Error;

            const QC::u8 *Rsp = reinterpret_cast<const QC::u8 *>(physToVirt(RspPhys));
            const QC::u8 *Params = nullptr;
            QC::u32 ParamsLen = 0;
            if (!TpmRspParams(Rsp, RspLen, Params, ParamsLen))
                return QC::Status::Error;
            if (!Params || ParamsLen < 2)
                return QC::Status::Error;

            const QC::u16 Sz = ReadBe16(Params);
            if (static_cast<QC::usize>(Sz) != OutLen)
                return QC::Status::Error;
            if (2u + Sz > ParamsLen)
                return QC::Status::Error;
            for (QC::usize i = 0; i < OutLen; ++i)
                Out[i] = Params[2 + i];

            g_TpmLastRspCode = 0;
            return QC::Status::Success;
        }

        static QC::Status SecureStoreTpmSealWrapKey(void *User, const QC::u8 *WrapKey, QC::usize WrapKeyLen, QC::Vector<QC::u8> &OutBlob)
        {
            if (!User || !WrapKey || WrapKeyLen != 32)
                return QC::Status::InvalidParam;

            auto *Dev = static_cast<FTpmSecureStoreCtx *>(User);
            if (!Dev->bReady)
                return QC::Status::Busy;

            QC::u32 PolicyDigestSession = 0;
            QC::Status St = TpmStartPolicySession(nullptr, Dev->Ctx, false, PolicyDigestSession);
            if (St != QC::Status::Success)
            {
                SerialStr("SecureStoreTPM: StartAuthSession(POLICY-DIGEST) failed (rsp=0x");
                SerialHex32Fixed(g_TpmLastRspCode);
                SerialStr(")\r\n");
                return St;
            }

            St = TpmPolicyPCR(nullptr, Dev->Ctx, PolicyDigestSession);
            if (St != QC::Status::Success)
            {
                SerialStr("SecureStoreTPM: PolicyPCR(POLICY-DIGEST) failed (rsp=0x");
                SerialHex32Fixed(g_TpmLastRspCode);
                SerialStr(")\r\n");
                (void)TpmFlushContext(nullptr, Dev->Ctx, PolicyDigestSession);
                return St;
            }

            QC::u8 PolicyDigest[32];
            St = TpmPolicyGetDigest(nullptr, Dev->Ctx, PolicyDigestSession, PolicyDigest);
            (void)TpmFlushContext(nullptr, Dev->Ctx, PolicyDigestSession);
            if (St != QC::Status::Success)
            {
                SerialStr("SecureStoreTPM: PolicyGetDigest failed (rsp=0x");
                SerialHex32Fixed(g_TpmLastRspCode);
                SerialStr(")\r\n");
                return St;
            }

            QC::u32 Primary = 0;
            St = TpmCreatePrimaryStorageKey(nullptr, Dev->Ctx, Primary);
            if (St != QC::Status::Success)
            {
                SerialStr("SecureStoreTPM: CreatePrimary failed (rsp=0x");
                SerialHex32Fixed(g_TpmLastRspCode);
                SerialStr(")\r\n");
                return St;
            }

            QC::Vector<QC::u8> Priv2b;
            QC::Vector<QC::u8> Pub2b;
            St = TpmCreateSealedObject(nullptr, Dev->Ctx, Primary, WrapKey, WrapKeyLen, PolicyDigest, Priv2b, Pub2b);
            (void)TpmFlushContext(nullptr, Dev->Ctx, Primary);

            if (St != QC::Status::Success)
            {
                SerialStr("SecureStoreTPM: Create failed (rsp=0x");
                SerialHex32Fixed(g_TpmLastRspCode);
                SerialStr(")\r\n");
                if (Priv2b.size() > 0)
                    QC::String::memset(Priv2b.data(), 0, Priv2b.size());
                if (Pub2b.size() > 0)
                    QC::String::memset(Pub2b.data(), 0, Pub2b.size());
                Priv2b.clear();
                Pub2b.clear();
                return St;
            }

            const QC::u32 Ver = 1;
            const QC::u32 PrivLen = static_cast<QC::u32>(Priv2b.size());
            const QC::u32 PubLen = static_cast<QC::u32>(Pub2b.size());
            OutBlob.clear();
            OutBlob.resize(4 + 4 + 4 + 4 + Priv2b.size() + Pub2b.size());

            auto PutLe32 = [](QC::u8 *P, QC::u32 V)
            {
                P[0] = static_cast<QC::u8>(V & 0xFF);
                P[1] = static_cast<QC::u8>((V >> 8) & 0xFF);
                P[2] = static_cast<QC::u8>((V >> 16) & 0xFF);
                P[3] = static_cast<QC::u8>((V >> 24) & 0xFF);
            };

            OutBlob[0] = 'W';
            OutBlob[1] = 'K';
            OutBlob[2] = 'T';
            OutBlob[3] = '1';
            PutLe32(OutBlob.data() + 4, Ver);
            PutLe32(OutBlob.data() + 8, PrivLen);
            PutLe32(OutBlob.data() + 12, PubLen);

            QC::usize O = 16;
            for (QC::usize i = 0; i < Priv2b.size(); ++i)
                OutBlob[O++] = Priv2b[i];
            for (QC::usize i = 0; i < Pub2b.size(); ++i)
                OutBlob[O++] = Pub2b[i];

            QC::String::memset(Priv2b.data(), 0, Priv2b.size());
            QC::String::memset(Pub2b.data(), 0, Pub2b.size());
            Priv2b.clear();
            Pub2b.clear();
            return QC::Status::Success;
        }

        static QC::Status SecureStoreTpmUnsealWrapKey(void *User, const QC::Vector<QC::u8> &Blob, QC::u8 *OutWrapKey, QC::usize OutWrapKeyLen)
        {
            if (!User || !OutWrapKey || OutWrapKeyLen != 32)
                return QC::Status::InvalidParam;

            auto *Dev = static_cast<FTpmSecureStoreCtx *>(User);
            if (!Dev->bReady)
                return QC::Status::Busy;

            QC::u32 PolicySession = 0;
            QC::Status St = TpmStartPolicySession(nullptr, Dev->Ctx, false, PolicySession);
            if (St != QC::Status::Success)
            {
                SerialStr("SecureStoreTPM: StartAuthSession(POLICY) failed (rsp=0x");
                SerialHex32Fixed(g_TpmLastRspCode);
                SerialStr(")\r\n");
                return St;
            }

            St = TpmPolicyPCR(nullptr, Dev->Ctx, PolicySession);
            if (St != QC::Status::Success)
            {
                SerialStr("SecureStoreTPM: PolicyPCR failed (rsp=0x");
                SerialHex32Fixed(g_TpmLastRspCode);
                SerialStr(")\r\n");
                (void)TpmFlushContext(nullptr, Dev->Ctx, PolicySession);
                return St;
            }

            if (Blob.size() < 16)
                return QC::Status::Error;
            if (!(Blob[0] == 'W' && Blob[1] == 'K' && Blob[2] == 'T' && Blob[3] == '1'))
                return QC::Status::Error;

            auto GetLe32 = [](const QC::u8 *P) -> QC::u32
            {
                return static_cast<QC::u32>(P[0]) |
                       (static_cast<QC::u32>(P[1]) << 8) |
                       (static_cast<QC::u32>(P[2]) << 16) |
                       (static_cast<QC::u32>(P[3]) << 24);
            };

            const QC::u32 Ver = GetLe32(Blob.data() + 4);
            if (Ver != 1)
                return QC::Status::Error;
            const QC::u32 PrivLen = GetLe32(Blob.data() + 8);
            const QC::u32 PubLen = GetLe32(Blob.data() + 12);
            const QC::usize Total = 16u + static_cast<QC::usize>(PrivLen) + static_cast<QC::usize>(PubLen);
            if (Total != Blob.size())
                return QC::Status::Error;

            QC::Vector<QC::u8> Priv2b;
            QC::Vector<QC::u8> Pub2b;
            Priv2b.resize(PrivLen);
            Pub2b.resize(PubLen);

            QC::usize O = 16;
            for (QC::usize i = 0; i < Priv2b.size(); ++i)
                Priv2b[i] = Blob[O++];
            for (QC::usize i = 0; i < Pub2b.size(); ++i)
                Pub2b[i] = Blob[O++];

            QC::u32 Primary = 0;
            St = TpmCreatePrimaryStorageKey(nullptr, Dev->Ctx, Primary);
            if (St != QC::Status::Success)
            {
                SerialStr("SecureStoreTPM: CreatePrimary failed (rsp=0x");
                SerialHex32Fixed(g_TpmLastRspCode);
                SerialStr(")\r\n");
                QC::String::memset(Priv2b.data(), 0, Priv2b.size());
                QC::String::memset(Pub2b.data(), 0, Pub2b.size());
                Priv2b.clear();
                Pub2b.clear();
                return St;
            }

            QC::u32 Obj = 0;
            St = TpmLoadSealedObject(nullptr, Dev->Ctx, Primary, Priv2b, Pub2b, Obj);

            QC::String::memset(Priv2b.data(), 0, Priv2b.size());
            QC::String::memset(Pub2b.data(), 0, Pub2b.size());
            Priv2b.clear();
            Pub2b.clear();

            if (St != QC::Status::Success)
            {
                SerialStr("SecureStoreTPM: Load failed (rsp=0x");
                SerialHex32Fixed(g_TpmLastRspCode);
                SerialStr(")\r\n");
                (void)TpmFlushContext(nullptr, Dev->Ctx, Primary);
                (void)TpmFlushContext(nullptr, Dev->Ctx, PolicySession);
                return St;
            }

            St = TpmUnsealWithAuthSession(nullptr, Dev->Ctx, Obj, PolicySession, OutWrapKey, OutWrapKeyLen);
            if (St != QC::Status::Success)
            {
                SerialStr("SecureStoreTPM: Unseal failed (rsp=0x");
                SerialHex32Fixed(g_TpmLastRspCode);
                SerialStr(")\r\n");
            }

            (void)TpmFlushContext(nullptr, Dev->Ctx, Obj);
            (void)TpmFlushContext(nullptr, Dev->Ctx, Primary);
            (void)TpmFlushContext(nullptr, Dev->Ctx, PolicySession);
            return St;
        }

        static QC::Status SecureStoreTpmSealSecret(void *User, const QC::u8 *Secret, QC::usize SecretLen, QC::Vector<QC::u8> &OutBlob)
        {
            if (!User || !Secret || SecretLen == 0)
                return QC::Status::InvalidParam;

            // Current sealed-object template only supports small secrets.
            if (SecretLen > 64)
                return QC::Status::NotSupported;

            auto *Dev = static_cast<FTpmSecureStoreCtx *>(User);
            if (!Dev->bReady)
                return QC::Status::Busy;

            QC::u32 PolicyDigestSession = 0;
            QC::Status St = TpmStartPolicySession(nullptr, Dev->Ctx, false, PolicyDigestSession);
            if (St != QC::Status::Success)
                return St;

            St = TpmPolicyPCR(nullptr, Dev->Ctx, PolicyDigestSession);
            if (St != QC::Status::Success)
            {
                (void)TpmFlushContext(nullptr, Dev->Ctx, PolicyDigestSession);
                return St;
            }

            QC::u8 PolicyDigest[32];
            St = TpmPolicyGetDigest(nullptr, Dev->Ctx, PolicyDigestSession, PolicyDigest);
            (void)TpmFlushContext(nullptr, Dev->Ctx, PolicyDigestSession);
            if (St != QC::Status::Success)
                return St;

            QC::u32 Primary = 0;
            St = TpmCreatePrimaryStorageKey(nullptr, Dev->Ctx, Primary);
            if (St != QC::Status::Success)
                return St;

            QC::Vector<QC::u8> Priv2b;
            QC::Vector<QC::u8> Pub2b;
            St = TpmCreateSealedObject(nullptr, Dev->Ctx, Primary, Secret, SecretLen, PolicyDigest, Priv2b, Pub2b);
            (void)TpmFlushContext(nullptr, Dev->Ctx, Primary);

            if (St != QC::Status::Success)
            {
                if (Priv2b.size() > 0)
                    QC::String::memset(Priv2b.data(), 0, Priv2b.size());
                if (Pub2b.size() > 0)
                    QC::String::memset(Pub2b.data(), 0, Pub2b.size());
                Priv2b.clear();
                Pub2b.clear();
                return St;
            }

            const QC::u32 Ver = 1;
            const QC::u32 SecretLen32 = static_cast<QC::u32>(SecretLen);
            const QC::u32 PrivLen = static_cast<QC::u32>(Priv2b.size());
            const QC::u32 PubLen = static_cast<QC::u32>(Pub2b.size());
            OutBlob.clear();
            OutBlob.resize(4 + 4 + 4 + 4 + 4 + Priv2b.size() + Pub2b.size());

            auto PutLe32 = [](QC::u8 *P, QC::u32 V)
            {
                P[0] = static_cast<QC::u8>(V & 0xFF);
                P[1] = static_cast<QC::u8>((V >> 8) & 0xFF);
                P[2] = static_cast<QC::u8>((V >> 16) & 0xFF);
                P[3] = static_cast<QC::u8>((V >> 24) & 0xFF);
            };

            OutBlob[0] = 'S';
            OutBlob[1] = 'S';
            OutBlob[2] = 'T';
            OutBlob[3] = '1';
            PutLe32(OutBlob.data() + 4, Ver);
            PutLe32(OutBlob.data() + 8, SecretLen32);
            PutLe32(OutBlob.data() + 12, PrivLen);
            PutLe32(OutBlob.data() + 16, PubLen);

            QC::usize O = 20;
            for (QC::usize i = 0; i < Priv2b.size(); ++i)
                OutBlob[O++] = Priv2b[i];
            for (QC::usize i = 0; i < Pub2b.size(); ++i)
                OutBlob[O++] = Pub2b[i];

            QC::String::memset(Priv2b.data(), 0, Priv2b.size());
            QC::String::memset(Pub2b.data(), 0, Pub2b.size());
            Priv2b.clear();
            Pub2b.clear();
            return QC::Status::Success;
        }

        static QC::Status SecureStoreTpmUnsealSecret(void *User,
                                                     const QC::Vector<QC::u8> &Blob,
                                                     QC::u8 *OutSecret,
                                                     QC::usize OutSecretCap,
                                                     QC::usize *OutSecretLen)
        {
            if (!User || !OutSecret || !OutSecretLen)
                return QC::Status::InvalidParam;

            auto *Dev = static_cast<FTpmSecureStoreCtx *>(User);
            if (!Dev->bReady)
                return QC::Status::Busy;

            if (Blob.size() < 20)
                return QC::Status::Error;
            if (!(Blob[0] == 'S' && Blob[1] == 'S' && Blob[2] == 'T' && Blob[3] == '1'))
                return QC::Status::Error;

            auto GetLe32 = [](const QC::u8 *P) -> QC::u32
            {
                return static_cast<QC::u32>(P[0]) |
                       (static_cast<QC::u32>(P[1]) << 8) |
                       (static_cast<QC::u32>(P[2]) << 16) |
                       (static_cast<QC::u32>(P[3]) << 24);
            };

            const QC::u32 Ver = GetLe32(Blob.data() + 4);
            if (Ver != 1)
                return QC::Status::Error;

            const QC::u32 SecretLen32 = GetLe32(Blob.data() + 8);
            const QC::u32 PrivLen = GetLe32(Blob.data() + 12);
            const QC::u32 PubLen = GetLe32(Blob.data() + 16);

            if (SecretLen32 == 0 || SecretLen32 > 64)
                return QC::Status::Error;

            const QC::usize Total = 20u + static_cast<QC::usize>(PrivLen) + static_cast<QC::usize>(PubLen);
            if (Total != Blob.size())
                return QC::Status::Error;

            const QC::usize SecretLen = static_cast<QC::usize>(SecretLen32);
            if (OutSecretCap < SecretLen)
                return QC::Status::OutOfMemory;

            QC::u32 PolicySession = 0;
            QC::Status St = TpmStartPolicySession(nullptr, Dev->Ctx, false, PolicySession);
            if (St != QC::Status::Success)
                return St;

            St = TpmPolicyPCR(nullptr, Dev->Ctx, PolicySession);
            if (St != QC::Status::Success)
            {
                (void)TpmFlushContext(nullptr, Dev->Ctx, PolicySession);
                return St;
            }

            QC::Vector<QC::u8> Priv2b;
            QC::Vector<QC::u8> Pub2b;
            Priv2b.resize(PrivLen);
            Pub2b.resize(PubLen);

            QC::usize O = 20;
            for (QC::usize i = 0; i < Priv2b.size(); ++i)
                Priv2b[i] = Blob[O++];
            for (QC::usize i = 0; i < Pub2b.size(); ++i)
                Pub2b[i] = Blob[O++];

            QC::u32 Primary = 0;
            St = TpmCreatePrimaryStorageKey(nullptr, Dev->Ctx, Primary);
            if (St != QC::Status::Success)
            {
                QC::String::memset(Priv2b.data(), 0, Priv2b.size());
                QC::String::memset(Pub2b.data(), 0, Pub2b.size());
                Priv2b.clear();
                Pub2b.clear();
                (void)TpmFlushContext(nullptr, Dev->Ctx, PolicySession);
                return St;
            }

            QC::u32 Obj = 0;
            St = TpmLoadSealedObject(nullptr, Dev->Ctx, Primary, Priv2b, Pub2b, Obj);

            QC::String::memset(Priv2b.data(), 0, Priv2b.size());
            QC::String::memset(Pub2b.data(), 0, Pub2b.size());
            Priv2b.clear();
            Pub2b.clear();

            if (St != QC::Status::Success)
            {
                (void)TpmFlushContext(nullptr, Dev->Ctx, Primary);
                (void)TpmFlushContext(nullptr, Dev->Ctx, PolicySession);
                return St;
            }

            St = TpmUnsealWithAuthSession(nullptr, Dev->Ctx, Obj, PolicySession, OutSecret, SecretLen);
            if (St == QC::Status::Success)
                *OutSecretLen = SecretLen;
            else
                *OutSecretLen = 0;

            (void)TpmFlushContext(nullptr, Dev->Ctx, Obj);
            (void)TpmFlushContext(nullptr, Dev->Ctx, Primary);
            (void)TpmFlushContext(nullptr, Dev->Ctx, PolicySession);
            return St;
        }

        static void SecureStoreSelfTest(FLogFn Log)
        {
            LogStr(Log, "SecureStore: self-test...\r\n");

            QC::Status St = QK::SecureStore::ensureBaseDir();
            if (St != QC::Status::Success)
            {
                LogStr(Log, "SecureStore: FAIL (ensureBaseDir)\r\n");
                return;
            }

            QC::u8 Plain[96];
            (void)QK::Entropy::fillRandom(Plain, sizeof(Plain));

            St = QK::SecureStore::writeSealedBlob("SSTEST.BIN", Plain, sizeof(Plain));
            if (St != QC::Status::Success)
            {
                LogStr(Log, "SecureStore: FAIL (writeSealedBlob)\r\n");
                return;
            }

            QC::Vector<QC::u8> Out;
            St = QK::SecureStore::readSealedBlob("SSTEST.BIN", Out);
            if (St != QC::Status::Success)
            {
                LogStr(Log, "SecureStore: FAIL (readSealedBlob)\r\n");
                (void)QK::SecureStore::removeBlob("SSTEST.BIN");
                return;
            }

            bool bOk = (Out.size() == sizeof(Plain));
            if (bOk)
            {
                bOk = (QC::String::memcmp(Out.data(), Plain, sizeof(Plain)) == 0);
            }

            (void)QK::SecureStore::removeBlob("SSTEST.BIN");
            QC::String::memset(Plain, 0, sizeof(Plain));
            if (Out.size() > 0)
                QC::String::memset(Out.data(), 0, Out.size());
            Out.clear();

            LogStr(Log, bOk ? "SecureStore: PASS\r\n" : "SecureStore: FAIL (mismatch)\r\n");
        }

        static bool ShouldRunPcrMismatchTest(QFS::VFS *Vfs)
        {
            if (!Vfs)
                return false;
            QFS::File *F = Vfs->open("/PCRTEST.FLG", QFS::OpenMode::Read);
            if (!F)
                return false;
            delete F;
            return true;
        }

        static void SecureStorePcrMismatchTest(QFS::VFS *Vfs, FLogFn Log)
        {
            if (!ShouldRunPcrMismatchTest(Vfs))
                return;

            if (!g_TpmSecureStore.bReady)
            {
                LogStr(Log, "SecureStore: PCR mismatch test SKIP (no TPM)\r\n");
                return;
            }

            LogStr(Log, "SecureStore: PCR mismatch test...\r\n");

            QC::u8 Plain[64];
            (void)QK::Entropy::fillRandom(Plain, sizeof(Plain));
            QC::Status St = QK::SecureStore::writeSealedBlob("PCRNEG.BIN", Plain, sizeof(Plain));
            if (St != QC::Status::Success)
            {
                LogStr(Log, "SecureStore: PCR mismatch test FAIL (write)\r\n");
                return;
            }

            QC::u8 ExtendDigest[32];
            for (QC::usize i = 0; i < sizeof(ExtendDigest); ++i)
                ExtendDigest[i] = static_cast<QC::u8>(0x42u ^ static_cast<QC::u8>(i));

            St = TpmPcrExtendSha256(Log, g_TpmSecureStore.Ctx, 7, ExtendDigest);
            if (St != QC::Status::Success)
            {
                LogStr(Log, "SecureStoreTPM: PCR_Extend failed (rsp=0x");
                LogHex32Fixed(Log, g_TpmLastRspCode);
                LogStr(Log, ")\r\n");
                LogStr(Log, "SecureStore: PCR mismatch test FAIL (extend)\r\n");
                (void)QK::SecureStore::removeBlob("PCRNEG.BIN");
                return;
            }

            QC::Vector<QC::u8> Out;
            St = QK::SecureStore::readSealedBlob("PCRNEG.BIN", Out);

            const bool bExpectedFail = (St != QC::Status::Success);
            LogStr(Log, bExpectedFail ? "SecureStore: PCR mismatch test PASS (unseal blocked)\r\n"
                                      : "SecureStore: PCR mismatch test FAIL (unexpected unseal)\r\n");

            (void)QK::SecureStore::removeBlob("PCRNEG.BIN");
            (void)QK::SecureStore::removeBlob("WRAPKEY.TPM");
            (void)QK::SecureStore::removeBlob("WRAPKEY.BIN");
            QC::String::memset(Plain, 0, sizeof(Plain));
            if (Out.size() > 0)
                QC::String::memset(Out.data(), 0, Out.size());
            Out.clear();
        }
    }

    void TryTpm2CrbStartup(QC::u32 StartMethod, QC::PhysAddr ControlAreaPhys, FLogFn Log)
    {
        if (ControlAreaPhys == 0)
        {
            LogStr(Log, "TPM2: no control area\r\n");
            return;
        }

        if (!(StartMethod == 6 || StartMethod == 7))
        {
            LogStr(Log, "TPM2: start method not CRB-style; skipping TPM commands\r\n");
            return;
        }

        QC::PhysAddr PagePhys = ControlAreaPhys & ~static_cast<QC::PhysAddr>(0xFFF);
        if (!EnsureHhdmMappedMmio(Log, PagePhys, 0x1000))
        {
            LogStr(Log, "TPM2: failed to map control area\r\n");
            return;
        }

        FCrbCtx Ctx{.Base = physToVirt(PagePhys), .Off = static_cast<QC::usize>(ControlAreaPhys & 0xFFF)};

        LogStr(Log, "TPM2: attempting TPM2_Startup via CRB\r\n");
        const QC::u8 StartupCmd[12] = {0x80, 0x01, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x01, 0x44, 0x00, 0x00};
        QC::u32 StartupRspLen = 0;
        QC::PhysAddr StartupRspPhys = 0;
        QC::u32 StartupRspCode = CrbSubmit(Log, Ctx, StartupCmd, sizeof(StartupCmd), StartupRspLen, StartupRspPhys);
        if (StartupRspCode == 0xFFFFFFFFu)
        {
            LogStr(Log, "TPM2: Startup transport failed\r\n");
            return;
        }

        if (StartupRspCode == 0x00000100)
        {
            LogStr(Log, "TPM2: TPM_RC_INITIALIZE (already started)\r\n");
        }
        else if (StartupRspCode != 0)
        {
            LogStr(Log, "TPM2: Startup failed\r\n");
            return;
        }
        else
        {
            LogStr(Log, "TPM2: Startup OK\r\n");
        }

        LogStr(Log, "TPM2: attempting TPM2_GetRandom(16)\r\n");
        const QC::u8 GetRandomCmd[12] = {0x80, 0x01, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x01, 0x7B, 0x00, 0x10};
        QC::u32 RandRspLen = 0;
        QC::PhysAddr RandRspPhys = 0;
        QC::u32 RandRspCode = CrbSubmit(Log, Ctx, GetRandomCmd, sizeof(GetRandomCmd), RandRspLen, RandRspPhys);
        if (RandRspCode != 0)
        {
            LogStr(Log, "TPM2: GetRandom failed\r\n");
            return;
        }

        if (RandRspLen < 12)
        {
            LogStr(Log, "TPM2: GetRandom response too short\r\n");
            return;
        }

        if (!EnsureHhdmMapped(Log, RandRspPhys, RandRspLen))
        {
            LogStr(Log, "TPM2: failed to map GetRandom response\r\n");
            return;
        }

        auto *RspBuf = reinterpret_cast<const QC::u8 *>(physToVirt(RandRspPhys));
        const QC::u16 BytesSize = ReadBe16(RspBuf + 10);
        LogDecU32(Log, "TPM2: GetRandom bytes ", static_cast<QC::u32>(BytesSize));

        const QC::usize Avail = (RandRspLen > 12) ? (RandRspLen - 12) : 0;
        const QC::usize ToDump = (BytesSize < Avail) ? BytesSize : Avail;

        LogStr(Log, "TPM2: RAND ");
        for (QC::usize i = 0; i < ToDump; ++i)
        {
            const QC::u8 b = RspBuf[12 + i];
            const QC::u8 hi = (b >> 4) & 0xF;
            const QC::u8 lo = b & 0xF;
            char Buf[3];
            Buf[0] = hi < 10 ? static_cast<char>('0' + hi) : static_cast<char>('a' + (hi - 10));
            Buf[1] = lo < 10 ? static_cast<char>('0' + lo) : static_cast<char>('a' + (lo - 10));
            Buf[2] = 0;
            LogStr(Log, Buf);
        }
        LogStr(Log, "\r\n");

        if (ToDump > 0)
        {
            QK::Entropy::addEntropy(RspBuf + 12, ToDump);

            g_TpmSecureStore.Ctx = Ctx;
            g_TpmSecureStore.bReady = true;

            auto ScCfg = QK::SecureStore::defaultConfig();
            ScCfg.tpmUser = &g_TpmSecureStore;
            ScCfg.tpmSealWrapKey = &SecureStoreTpmSealWrapKey;
            ScCfg.tpmUnsealWrapKey = &SecureStoreTpmUnsealWrapKey;
            ScCfg.tpmSealSecret = &SecureStoreTpmSealSecret;
            ScCfg.tpmUnsealSecret = &SecureStoreTpmUnsealSecret;
            QK::SecureStore::setDefaultConfig(ScCfg);
            LogStr(Log, "SecureStore: TPM wrap-key enabled\r\n");
        }
    }

    bool IsReady()
    {
        return g_TpmSecureStore.bReady;
    }

    bool ExtendPcrSha256Digest(QC::u32 PcrIndex, const QC::u8 Digest[32], FLogFn Log)
    {
        if (!g_TpmSecureStore.bReady)
            return false;
        if (!Digest)
            return false;

        const QC::Status st = TpmPcrExtendSha256(Log, g_TpmSecureStore.Ctx, PcrIndex, Digest);
        if (st != QC::Status::Success)
        {
            LogStr(Log, "TPM2: PCR_Extend failed\r\n");
            return false;
        }

        return true;
    }

    bool VerifyRsa2048RsassaSha256Digest(const QC::u8 Modulus[256], const QC::u8 Digest[32], const QC::u8 Signature[256], FLogFn Log)
    {
        if (!g_TpmSecureStore.bReady)
        {
            LogStr(Log, "TPM2: not ready; cannot verify BOOT.JSN signature\r\n");
            return false;
        }
        if (!Modulus || !Digest || !Signature)
            return false;

        QC::u32 keyHandle = 0;
        const QC::Status loadSt = TpmLoadExternalRsa2048RsassaSha256PublicKey(Log, g_TpmSecureStore.Ctx, Modulus, keyHandle);
        if (loadSt != QC::Status::Success)
        {
            LogStr(Log, "TPM2: LoadExternal failed\r\n");
            LogStr(Log, "TPM2: rspCode 0x");
            LogHex32Fixed(Log, g_TpmLastRspCode);
            LogStr(Log, "\r\n");
            return false;
        }

        const QC::Status verSt = TpmVerifyRsa2048RsassaSha256Digest(Log, g_TpmSecureStore.Ctx, keyHandle, Digest, Signature);
        (void)TpmFlushContext(Log, g_TpmSecureStore.Ctx, keyHandle);

        if (verSt != QC::Status::Success)
        {
            LogStr(Log, "TPM2: VerifySignature failed\r\n");
            LogStr(Log, "TPM2: rspCode 0x");
            LogHex32Fixed(Log, g_TpmLastRspCode);
            LogStr(Log, "\r\n");
            return false;
        }

        return true;
    }

    void RunSecureStoreSelfTests(QFS::VFS *Vfs, FLogFn Log)
    {
        SecureStoreSelfTest(Log);
        SecureStorePcrMismatchTest(Vfs, Log);
    }
}
