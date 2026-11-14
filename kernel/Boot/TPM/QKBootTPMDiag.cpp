#include "QKBootTPMDiag.h"
#include "QKBootTPMCommands.h"
#include "QKBootTPMEventLog.h"
#include "QKBootTPMLock.h"
#include "QKBootTPMProbePolicy.h"
#include "QKBootTPMSecureBoot.h"
#include "QKBootTPMSecureStore.h"
#include "QKBootTPMTransport.h"

namespace QK::Boot::Tpm
{
    using namespace QK::Boot::Tpm::Internal;

    namespace
    {
        // Diagnostics may run in IRQ context; keep lock waits short.
        constexpr QC::usize kDiagLockSpinIterations = kTpmStartSpinIterations;

        struct FStartupInfo
        {
            bool bPropertiesValid = false;
            char Manufacturer[5]{};
            char VendorString[17]{};
            QC::u32 FirmwareVersion1 = 0;
            QC::u32 FirmwareVersion2 = 0;
            bool bTestResultValid = false;
            QC::u32 TestResult = 0;
        };

        static FStartupInfo g_Info;

        static void AppendAscii32(char *Dst, QC::usize &Pos, QC::usize Cap, QC::u32 Value)
        {
            for (int Shift = 24; Shift >= 0; Shift -= 8)
            {
                const char C = static_cast<char>((Value >> Shift) & 0xFF);
                if (C >= 0x20 && C < 0x7F && Pos + 1 < Cap)
                    Dst[Pos++] = C;
            }
            Dst[Pos] = 0;
        }

        static void RefreshTestResultLocked()
        {
            QC::u32 Result = 0;
            g_Info.bTestResultValid = (Cmd::TpmGetTestResult(Result) == QC::Status::Success);
            g_Info.TestResult = Result;
        }

        struct FLine
        {
            char Buf[192];
            QC::usize Len = 0;
            FLineFn Out;
            void *User;

            FLine(FLineFn InOut, void *InUser) : Out(InOut), User(InUser) { Buf[0] = 0; }

            FLine &Str(const char *S)
            {
                while (S && *S && Len + 1 < sizeof(Buf))
                    Buf[Len++] = *S++;
                Buf[Len] = 0;
                return *this;
            }

            FLine &Dec(QC::u64 V)
            {
                char Tmp[21];
                QC::usize N = 0;
                do
                {
                    Tmp[N++] = static_cast<char>('0' + (V % 10));
                    V /= 10;
                } while (V && N < sizeof(Tmp));
                while (N && Len + 1 < sizeof(Buf))
                    Buf[Len++] = Tmp[--N];
                Buf[Len] = 0;
                return *this;
            }

            FLine &Hex(QC::u64 V, int Digits)
            {
                Str("0x");
                for (int i = Digits - 1; i >= 0 && Len + 1 < sizeof(Buf); --i)
                {
                    const int Nibble = static_cast<int>((V >> (i * 4)) & 0xF);
                    Buf[Len++] = Nibble < 10 ? static_cast<char>('0' + Nibble) : static_cast<char>('a' + Nibble - 10);
                }
                Buf[Len] = 0;
                return *this;
            }

            FLine &Bytes(const QC::u8 *P, QC::usize N)
            {
                for (QC::usize i = 0; i < N && Len + 2 < sizeof(Buf); ++i)
                {
                    const QC::u8 Hi = P[i] >> 4;
                    const QC::u8 Lo = P[i] & 0xF;
                    Buf[Len++] = Hi < 10 ? static_cast<char>('0' + Hi) : static_cast<char>('a' + Hi - 10);
                    Buf[Len++] = Lo < 10 ? static_cast<char>('0' + Lo) : static_cast<char>('a' + Lo - 10);
                }
                Buf[Len] = 0;
                return *this;
            }

            void Emit()
            {
                if (Out)
                    Out(User, Buf);
                Len = 0;
                Buf[0] = 0;
            }
        };

        static void Emit(FLineFn Out, void *User, const char *Line)
        {
            if (Out)
                Out(User, Line);
        }

        static const char *SkipSpaces(const char *P)
        {
            while (P && (*P == ' ' || *P == '\t'))
                ++P;
            return P;
        }

        static bool TokenIs(const char *P, const char *Word, const char *&Rest)
        {
            QC::usize i = 0;
            for (; Word[i]; ++i)
            {
                char C = P[i];
                if (C >= 'A' && C <= 'Z')
                    C = static_cast<char>(C - 'A' + 'a');
                if (C != Word[i])
                    return false;
            }
            if (P[i] != 0 && P[i] != ' ' && P[i] != '\t')
                return false;
            Rest = SkipSpaces(P + i);
            return true;
        }

        static void CmdStatus(FLineFn Out, void *User)
        {
            const Transport::FInfo &Ti = Transport::Info();
            FLine L(Out, User);

            L.Str("tpm_ready: ").Str(IsReady() ? "yes" : "no").Emit();
            L.Str("tpm_transport: ").Str(Transport::TypeName(Ti.Type)).Emit();
            L.Str("tpm_start_method: ").Dec(Ti.StartMethod).Str(" (").Str(Transport::StartMethodName(Ti.StartMethod)).Str(")").Emit();
            if (Ti.bAcpiStartRequested)
                L.Str("tpm_acpi_start: requested by firmware (unsupported; using START register)").Emit();
            L.Str("tpm_register_base: ").Hex(Ti.LocalityBasePhys, 16).Emit();
            if (Ti.ControlAreaPhys)
                L.Str("tpm_control_area: ").Hex(Ti.ControlAreaPhys, 16).Emit();
            if (Ti.bInterfaceIdValid)
                L.Str("tpm_interface_id: ").Hex(Ti.InterfaceId, 16).Emit();
            if (Ti.VendorId || Ti.DeviceId)
                L.Str("tpm_vendor_id: ").Hex(Ti.VendorId, 4).Str(" device_id: ").Hex(Ti.DeviceId, 4).Str(" revision: ").Hex(Ti.RevisionId, 2).Emit();

            if (g_Info.bPropertiesValid)
            {
                L.Str("tpm_manufacturer: ").Str(g_Info.Manufacturer[0] ? g_Info.Manufacturer : "?");
                if (g_Info.VendorString[0])
                    L.Str(" (").Str(g_Info.VendorString).Str(")");
                L.Emit();
                L.Str("tpm_firmware: ")
                    .Dec(g_Info.FirmwareVersion1 >> 16)
                    .Str(".")
                    .Dec(g_Info.FirmwareVersion1 & 0xFFFF)
                    .Str(" (fw1=")
                    .Hex(g_Info.FirmwareVersion1, 8)
                    .Str(" fw2=")
                    .Hex(g_Info.FirmwareVersion2, 8)
                    .Str(")")
                    .Emit();
            }
            else
            {
                L.Str("tpm_manufacturer: unknown").Emit();
            }

            if (g_Info.bTestResultValid)
                L.Str("tpm_selftest: ").Str(g_Info.TestResult == 0 ? "pass" : "fail").Str(" (result=").Hex(g_Info.TestResult, 8).Str(")").Emit();
            else
                L.Str("tpm_selftest: unknown").Emit();

            L.Str("tpm_commands: ").Dec(Ti.CommandsSubmitted).Str(" transport_failures: ").Dec(Ti.TransportFailures)
                .Str(" last_rc: ").Hex(Cmd::LastResponseCode(), 8).Emit();

            FLockStats Ls{};
            GetLockStats(Ls);
            L.Str("tpm_lock: acquisitions=").Dec(Ls.Acquisitions).Str(" contended=").Dec(Ls.ContendedAcquisitions)
                .Str(" same_cpu_busy=").Dec(Ls.SameCpuRejections).Str(" timeouts=").Dec(Ls.TimeoutRejections)
                .Str(" owner=").Dec(Ls.OwnerTag).Emit();

            const EventLog::FFirmwareLog Fw = EventLog::FirmwareLog();
            if (Fw.Phys)
                L.Str("tpm_firmware_eventlog: ").Hex(Fw.Phys, 16).Str(" len=").Dec(Fw.Length).Emit();
            else
                L.Str("tpm_firmware_eventlog: none").Emit();

            const SecureBoot::FStatus Sb = SecureBoot::Status();
            L.Str("secure_boot: ").Str(SecureBoot::Summary());
            if (Sb.State == SecureBoot::EState::On || Sb.State == SecureBoot::EState::Off)
                L.Str(Sb.bPkEnrolled ? " pk=enrolled" : " pk=none").Str(" pcr7_events=").Dec(Sb.Pcr7Events);
            L.Str(" (").Str(Sb.Detail).Str(")").Emit();
        }

        static bool ParsePcrIndex(const char *P, QC::u32 &Out)
        {
            if (!P || *P < '0' || *P > '9')
                return false;
            QC::u32 V = 0;
            while (*P >= '0' && *P <= '9')
            {
                V = V * 10 + static_cast<QC::u32>(*P - '0');
                if (V > 23)
                    return false;
                ++P;
            }
            P = SkipSpaces(P);
            if (*P)
                return false;
            Out = V;
            return true;
        }

        static void CmdPcrs(const char *Rest, FLineFn Out, void *User)
        {
            QC::u32 First = 0;
            QC::u32 Last = 23;
            if (Rest && *Rest)
            {
                if (!ParsePcrIndex(Rest, First))
                {
                    Emit(Out, User, "usage: tpm pcrs [0-23]");
                    return;
                }
                Last = First;
            }

            FTpmOpGuard Guard(kDiagLockSpinIterations);
            if (!Guard.Acquired())
            {
                Emit(Out, User, "tpm: busy (TPM operation in progress); try again");
                return;
            }

            FLine L(Out, User);
            for (QC::u32 Pcr = First; Pcr <= Last; ++Pcr)
            {
                QC::u8 Digest[32];
                QC::u32 UpdateCounter = 0;
                L.Str("pcr").Str(Pcr < 10 ? "0" : "").Dec(Pcr).Str(" sha256: ");
                if (Cmd::TpmPcrReadSha256(Pcr, Digest, UpdateCounter) == QC::Status::Success)
                    L.Bytes(Digest, sizeof(Digest));
                else
                    L.Str("read failed (rc=").Hex(Cmd::LastResponseCode(), 8).Str(")");
                L.Emit();
            }
        }

        static void CmdSelfTest(FLineFn Out, void *User)
        {
            FTpmOpGuard Guard(kDiagLockSpinIterations);
            if (!Guard.Acquired())
            {
                Emit(Out, User, "tpm: busy (TPM operation in progress); try again");
                return;
            }

            FLine L(Out, User);
            const QC::Status St = Cmd::TpmSelfTest(false);
            L.Str("tpm_selftest_rc: ").Hex(Cmd::LastResponseCode(), 8).Emit();
            RefreshTestResultLocked();
            if (g_Info.bTestResultValid)
                L.Str("tpm_test_result: ").Hex(g_Info.TestResult, 8).Emit();
            const bool bPass = St == QC::Status::Success && g_Info.bTestResultValid && g_Info.TestResult == 0;
            Emit(Out, User, bPass ? "tpm selftest: pass" : "tpm selftest: fail");
        }

        static void CmdEventLog(FLineFn Out, void *User)
        {
            FTpmOpGuard Guard(kDiagLockSpinIterations);
            if (!Guard.Acquired())
            {
                Emit(Out, User, "tpm: busy (TPM operation in progress); try again");
                return;
            }

            static EventLog::FEntry Entries[EventLog::kMaxEntries];
            QC::u64 Total = 0;
            const QC::usize Count = EventLog::Snapshot(Entries, EventLog::kMaxEntries, Total);

            FLine L(Out, User);
            L.Str("tpm_kernel_measurements: ").Dec(Total).Str(" (showing ").Dec(Count).Str(")").Emit();
            for (QC::usize i = 0; i < Count; ++i)
            {
                const EventLog::FEntry &E = Entries[i];
                L.Str("#").Dec(E.Sequence).Str(" pcr").Dec(E.Pcr).Str(E.bExtended ? " ok " : " FAILED ").Str(E.Label).Emit();
                L.Str("    sha256: ").Bytes(E.Digest, sizeof(E.Digest)).Emit();
            }

            const EventLog::FFirmwareLog Fw = EventLog::FirmwareLog();
            if (Fw.Phys)
                L.Str("tpm_firmware_eventlog: ").Hex(Fw.Phys, 16).Str(" len=").Dec(Fw.Length).Str(" (not parsed)").Emit();
            else
                L.Str("tpm_firmware_eventlog: none").Emit();
        }
    }

    namespace Diag
    {
        void CaptureStartupInfo(FLogFn Log)
        {
            g_Info = FStartupInfo{};

            Cmd::FTaggedProperty Props[8];
            const QC::usize N = Cmd::TpmGetFixedProperties(Cmd::TPM_PT_MANUFACTURER, Props, 8);
            QC::usize ManPos = 0;
            QC::usize VsPos = 0;
            for (QC::usize i = 0; i < N; ++i)
            {
                const QC::u32 P = Props[i].Property;
                const QC::u32 V = Props[i].Value;
                if (P == Cmd::TPM_PT_MANUFACTURER)
                    AppendAscii32(g_Info.Manufacturer, ManPos, sizeof(g_Info.Manufacturer), V);
                else if (P >= Cmd::TPM_PT_VENDOR_STRING_1 && P < Cmd::TPM_PT_VENDOR_STRING_1 + 4)
                    AppendAscii32(g_Info.VendorString, VsPos, sizeof(g_Info.VendorString), V);
                else if (P == Cmd::TPM_PT_FIRMWARE_VERSION_1)
                    g_Info.FirmwareVersion1 = V;
                else if (P == Cmd::TPM_PT_FIRMWARE_VERSION_2)
                    g_Info.FirmwareVersion2 = V;
            }
            g_Info.bPropertiesValid = N > 0;

            RefreshTestResultLocked();

            if (g_Info.bPropertiesValid)
            {
                LogStr(Log, "TPM2: manufacturer ");
                LogStr(Log, g_Info.Manufacturer[0] ? g_Info.Manufacturer : "?");
                LogStr(Log, " fw1 0x");
                LogHex32Fixed(Log, g_Info.FirmwareVersion1);
                LogStr(Log, " fw2 0x");
                LogHex32Fixed(Log, g_Info.FirmwareVersion2);
                LogStr(Log, "\r\n");
            }
            else
            {
                LogStr(Log, "TPM2: GetCapability(properties) failed\r\n");
            }

            if (g_Info.bTestResultValid)
            {
                LogStr(Log, "TPM2: self-test result 0x");
                LogHex32Fixed(Log, g_Info.TestResult);
                LogStr(Log, "\r\n");
            }
        }
    }

    bool RunDiagnosticCommand(const char *Args, FLineFn Out, void *User)
    {
        const char *P = SkipSpaces(Args ? Args : "");
        const char *Rest = nullptr;

        if (!Transport::IsInitialized() && !(*P == 0 || TokenIs(P, "status", Rest)))
        {
            Emit(Out, User, "tpm: no TPM detected");
            return false;
        }

        if (*P == 0 || TokenIs(P, "status", Rest))
        {
            CmdStatus(Out, User);
            return true;
        }
        if (TokenIs(P, "pcrs", Rest))
        {
            CmdPcrs(Rest, Out, User);
            return true;
        }
        if (TokenIs(P, "selftest", Rest) && *Rest == 0)
        {
            CmdSelfTest(Out, User);
            return true;
        }
        if (TokenIs(P, "eventlog", Rest) && *Rest == 0)
        {
            CmdEventLog(Out, User);
            return true;
        }

        Emit(Out, User, "usage: tpm [status|pcrs [0-23]|selftest|eventlog]");
        return false;
    }
}
