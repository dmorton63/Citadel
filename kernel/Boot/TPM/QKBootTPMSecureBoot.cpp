#include "QKBootTPMSecureBoot.h"
#include "QKBootTPMCommands.h"
#include "QCString.h"
#include "QKMemVMM.h"
#include "../Security/QKBootSha256.h"

extern "C" QC::VirtAddr physToVirt(QC::PhysAddr phys);

namespace QK::Boot::Tpm::SecureBoot
{
    namespace
    {
        constexpr QC::u32 EV_NO_ACTION = 0x00000003;
        constexpr QC::u32 EV_EFI_VARIABLE_DRIVER_CONFIG = 0x80000001;
        constexpr QC::u16 TPM_ALG_SHA256 = 0x000B;
        constexpr QC::u32 kMaxAlgorithms = 8;
        constexpr QC::u32 kSecureBootPcr = 7;

        // EFI_GLOBAL_VARIABLE {8BE4DF61-93CA-11D2-AA0D-00E098032B8C} in on-disk byte order.
        constexpr QC::u8 kEfiGlobalVariableGuid[16] = {
            0x61, 0xDF, 0xE4, 0x8B, 0xCA, 0x93, 0xD2, 0x11,
            0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C};

        bool g_bFirmwareKnown = false;
        bool g_bFirmwareIsUefi = false;
        bool g_bEvaluated = false;
        FStatus g_Status;
        bool g_bFirmwareVarsKnown = false;
        FStatus g_FirmwareStatus;

        struct FAlg
        {
            QC::u16 Id;
            QC::u16 Size;
        };

        struct FReader
        {
            const QC::u8 *P;
            QC::usize Len;
            QC::usize Pos = 0;

            bool Has(QC::usize N) const { return Pos <= Len && Len - Pos >= N; }

            bool U16(QC::u16 &V)
            {
                if (!Has(2))
                    return false;
                V = static_cast<QC::u16>(P[Pos] | (P[Pos + 1] << 8));
                Pos += 2;
                return true;
            }

            bool U32(QC::u32 &V)
            {
                if (!Has(4))
                    return false;
                V = static_cast<QC::u32>(P[Pos]) | (static_cast<QC::u32>(P[Pos + 1]) << 8) |
                    (static_cast<QC::u32>(P[Pos + 2]) << 16) | (static_cast<QC::u32>(P[Pos + 3]) << 24);
                Pos += 4;
                return true;
            }

            bool Skip(QC::usize N)
            {
                if (!Has(N))
                    return false;
                Pos += N;
                return true;
            }
        };

        static QC::u64 ReadLe64(const QC::u8 *P)
        {
            QC::u64 V = 0;
            for (int i = 7; i >= 0; --i)
                V = (V << 8) | P[i];
            return V;
        }

        static void LogStr(FLogFn Log, const char *S)
        {
            if (Log)
                Log(S);
        }

        static bool MapLog(QC::u64 Phys, QC::u32 Len)
        {
            constexpr QC::usize kPageSize = 4096;
            const auto Flags = QK::Memory::PageFlags::Present | QK::Memory::PageFlags::NoExecute;
            const QC::PhysAddr Start = Phys & ~static_cast<QC::PhysAddr>(kPageSize - 1);
            const QC::PhysAddr End = (Phys + Len + kPageSize - 1) & ~static_cast<QC::PhysAddr>(kPageSize - 1);
            for (QC::PhysAddr P = Start; P < End; P += kPageSize)
            {
                const QC::VirtAddr V = physToVirt(P);
                if (!QK::Memory::VMM::instance().isMapped(V) &&
                    QK::Memory::VMM::instance().map(V, P, Flags) != QC::Status::Success)
                    return false;
            }
            return true;
        }

        static bool NameIs(const QC::u8 *Utf16, QC::u64 Chars, const char *Ascii)
        {
            QC::u64 i = 0;
            for (; Ascii[i]; ++i)
            {
                if (i >= Chars || Utf16[i * 2] != static_cast<QC::u8>(Ascii[i]) || Utf16[i * 2 + 1] != 0)
                    return false;
            }
            return i == Chars;
        }

        struct FVariable
        {
            bool bGlobal = false;
            const QC::u8 *Name = nullptr;
            QC::u64 NameChars = 0;
            const QC::u8 *Data = nullptr;
            QC::u64 DataLen = 0;
        };

        // UEFI_VARIABLE_DATA: GUID, u64 name length (chars), u64 data length, UTF-16 name, data.
        static bool ParseVariable(const QC::u8 *Event, QC::u32 EventLen, FVariable &Out)
        {
            if (EventLen < 32)
                return false;
            Out.bGlobal = QC::String::memcmp(Event, kEfiGlobalVariableGuid, 16) == 0;
            Out.NameChars = ReadLe64(Event + 16);
            Out.DataLen = ReadLe64(Event + 24);
            if (Out.NameChars > (EventLen - 32) / 2)
                return false;
            const QC::u64 NameBytes = Out.NameChars * 2;
            if (Out.DataLen > EventLen - 32 - NameBytes)
                return false;
            Out.Name = Event + 32;
            Out.Data = Out.Name + NameBytes;
            return true;
        }

        // Keeps the firmware-variable state (if any) when the event log is unusable.
        static void Fail(const char *Detail, FLogFn Log)
        {
            if (g_bFirmwareVarsKnown)
                g_Status = g_FirmwareStatus;
            g_Status.Detail = Detail;
            LogStr(Log, "SecureBoot: ");
            LogStr(Log, Detail);
            LogStr(Log, "\r\n");
        }
    }

    void SetFirmwareIsUefi(bool bUefi)
    {
        g_bFirmwareKnown = true;
        g_bFirmwareIsUefi = bUefi;
    }

    void SetFirmwareVariables(bool bSecureBootPresent, QC::u8 SecureBoot, bool bSetupMode, bool bPkPresent)
    {
        g_bFirmwareVarsKnown = true;
        g_FirmwareStatus = FStatus{};
        // A missing SecureBoot variable means the firmware does not support Secure Boot.
        g_FirmwareStatus.State = bSecureBootPresent && SecureBoot == 1 ? EState::On : EState::Off;
        g_FirmwareStatus.bFromFirmware = true;
        g_FirmwareStatus.bSetupMode = bSetupMode;
        g_FirmwareStatus.bPkEnrolled = bPkPresent;
        g_FirmwareStatus.Detail = bSecureBootPresent ? "UEFI runtime variable" : "firmware lacks Secure Boot support";
    }

    void Evaluate(QC::u64 LogPhys, QC::u32 LogLen, FLogFn Log)
    {
        g_bEvaluated = true;
        g_Status = FStatus{};

        if (g_bFirmwareKnown && !g_bFirmwareIsUefi)
        {
            g_Status.State = EState::LegacyBios;
            g_Status.Detail = "legacy BIOS boot";
            LogStr(Log, "SecureBoot: legacy BIOS boot (not applicable)\r\n");
            return;
        }

        if (LogPhys == 0 || LogLen < 32)
            return Fail("no firmware event log", Log);
        if (!MapLog(LogPhys, LogLen))
            return Fail("event log mapping failed", Log);

        FReader R{reinterpret_cast<const QC::u8 *>(physToVirt(LogPhys)), LogLen};

        // First event uses the legacy TCG_PCR_EVENT layout and must carry the
        // "Spec ID Event03" header describing the crypto-agile digest sizes.
        QC::u32 Pcr = 0, Type = 0, EventSize = 0;
        if (!R.U32(Pcr) || !R.U32(Type) || !R.Skip(20) || !R.U32(EventSize) || !R.Has(EventSize))
            return Fail("event log header truncated", Log);
        const QC::u8 *Spec = R.P + R.Pos;
        if (Type != EV_NO_ACTION || EventSize < 28 || QC::String::memcmp(Spec, "Spec ID Event03", 16) != 0)
            return Fail("event log is not crypto-agile", Log);

        FReader S{Spec, EventSize, 24};
        QC::u32 AlgCount = 0;
        if (!S.U32(AlgCount) || AlgCount == 0 || AlgCount > kMaxAlgorithms)
            return Fail("event log algorithm list invalid", Log);
        FAlg Algs[kMaxAlgorithms];
        bool bHasSha256 = false;
        for (QC::u32 i = 0; i < AlgCount; ++i)
        {
            if (!S.U16(Algs[i].Id) || !S.U16(Algs[i].Size) || Algs[i].Size > 64)
                return Fail("event log algorithm list invalid", Log);
            bHasSha256 = bHasSha256 || (Algs[i].Id == TPM_ALG_SHA256 && Algs[i].Size == 32);
        }
        if (!bHasSha256)
            return Fail("event log has no SHA-256 bank", Log);
        R.Pos += EventSize;

        QC::u8 Replay[32]{};
        bool bFoundSecureBoot = false;
        bool bSecureBootOn = false;
        bool bDigestMismatch = false;

        while (true)
        {
            const QC::usize EventStart = R.Pos;
            QC::u32 DigestCount = 0;
            if (!R.U32(Pcr) || !R.U32(Type) || !R.U32(DigestCount))
                break;
            // Unused log space is zero- or 0xFF-filled; stop at the first implausible header.
            if (DigestCount == 0 || DigestCount > AlgCount || Pcr > 23)
            {
                R.Pos = EventStart;
                break;
            }

            const QC::u8 *Sha256 = nullptr;
            bool bOk = true;
            for (QC::u32 d = 0; d < DigestCount && bOk; ++d)
            {
                QC::u16 AlgId = 0;
                bOk = R.U16(AlgId);
                QC::u16 Size = 0;
                bool bKnown = false;
                for (QC::u32 a = 0; a < AlgCount && bOk; ++a)
                {
                    if (Algs[a].Id == AlgId)
                    {
                        Size = Algs[a].Size;
                        bKnown = true;
                    }
                }
                bOk = bOk && bKnown && R.Has(Size);
                if (bOk && AlgId == TPM_ALG_SHA256)
                    Sha256 = R.P + R.Pos;
                bOk = bOk && R.Skip(Size);
            }
            if (!bOk || !R.U32(EventSize) || !R.Has(EventSize))
                return Fail("event log entry truncated", Log);
            const QC::u8 *Event = R.P + R.Pos;
            R.Pos += EventSize;

            if (Pcr != kSecureBootPcr || Type == EV_NO_ACTION)
                continue;
            if (!Sha256)
                return Fail("PCR 7 event without SHA-256 digest", Log);

            QC::u8 Concat[64];
            QC::String::memcpy(Concat, Replay, 32);
            QC::String::memcpy(Concat + 32, Sha256, 32);
            QK::Boot::Security::Sha256(Concat, sizeof(Concat), Replay);
            ++g_Status.Pcr7Events;

            FVariable Var;
            if (Type != EV_EFI_VARIABLE_DRIVER_CONFIG || !ParseVariable(Event, EventSize, Var) || !Var.bGlobal)
                continue;

            const bool bIsSecureBoot = NameIs(Var.Name, Var.NameChars, "SecureBoot");
            const bool bIsPk = NameIs(Var.Name, Var.NameChars, "PK");
            if (!bIsSecureBoot && !bIsPk)
                continue;

            // The PC Client spec requires this digest to cover the whole UEFI_VARIABLE_DATA.
            QC::u8 Computed[32];
            QK::Boot::Security::Sha256(Event, EventSize, Computed);
            if (QC::String::memcmp(Computed, Sha256, 32) != 0)
            {
                bDigestMismatch = true;
                continue;
            }

            if (bIsSecureBoot)
            {
                bFoundSecureBoot = true;
                bSecureBootOn = Var.DataLen >= 1 && Var.Data[0] == 1;
            }
            else
            {
                g_Status.bPkEnrolled = Var.DataLen > 0;
            }
        }

        if (bDigestMismatch)
            return Fail("SecureBoot event digest mismatch", Log);
        if (!bFoundSecureBoot)
            return Fail("SecureBoot variable not measured", Log);

        g_Status.State = bSecureBootOn ? EState::On : EState::Off;
        if (g_bFirmwareVarsKnown)
            g_Status.bSetupMode = g_FirmwareStatus.bSetupMode;
        const bool bFirmwareDisagrees = g_bFirmwareVarsKnown && g_FirmwareStatus.State != g_Status.State;

        QC::u8 Actual[32];
        QC::u32 UpdateCounter = 0;
        if (Cmd::TpmPcrReadSha256(kSecureBootPcr, Actual, UpdateCounter) != QC::Status::Success)
        {
            g_Status.Detail = "PCR 7 read failed";
        }
        else if (bFirmwareDisagrees)
        {
            g_Status.Detail = "UEFI variable disagrees with event log";
        }
        else if (QC::String::memcmp(Actual, Replay, 32) != 0)
        {
            g_Status.Detail = "PCR 7 does not match event log";
        }
        else
        {
            g_Status.bPcr7Verified = true;
            g_Status.Detail = "PCR 7 matches event log";
        }

        LogStr(Log, "SecureBoot: ");
        LogStr(Log, bSecureBootOn ? "enabled" : "disabled");
        LogStr(Log, g_Status.bPkEnrolled ? ", PK enrolled, " : ", no PK, ");
        LogStr(Log, g_Status.Detail);
        LogStr(Log, "\r\n");
    }

    FStatus Status()
    {
        if (!g_bEvaluated && g_bFirmwareKnown && !g_bFirmwareIsUefi)
        {
            FStatus S;
            S.State = EState::LegacyBios;
            S.Detail = "legacy BIOS boot";
            return S;
        }
        if (!g_bEvaluated && g_bFirmwareVarsKnown)
            return g_FirmwareStatus;
        return g_Status;
    }

    const char *Summary()
    {
        const FStatus S = Status();
        switch (S.State)
        {
        case EState::LegacyBios:
            return "off (legacy BIOS)";
        case EState::On:
            if (S.bPcr7Verified)
                return "on (verified)";
            return S.bFromFirmware ? "on (firmware)" : "on (unverified)";
        case EState::Off:
            if (S.bSetupMode)
                return "off (setup mode)";
            if (S.bPcr7Verified)
                return "off (verified)";
            return S.bFromFirmware ? "off (firmware)" : "off (unverified)";
        default:
            return "unknown";
        }
    }
}
