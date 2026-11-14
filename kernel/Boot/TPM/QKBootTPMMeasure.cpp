#include "QKBootTPMCommands.h"
#include "QKBootTPMEventLog.h"
#include "QKBootTPMLock.h"
#include "QKBootTPMSecureStore.h"

namespace QK::Boot::Tpm
{
    using namespace QK::Boot::Tpm::Internal;

    namespace EventLog
    {
        namespace
        {
            static FEntry g_Entries[kMaxEntries];
            static QC::u64 g_TotalRecorded = 0;
            static FFirmwareLog g_FirmwareLog{};
        }

        void Record(QC::u32 Pcr, const QC::u8 Digest[32], const char *Label, bool bExtended)
        {
            FEntry &E = g_Entries[g_TotalRecorded % kMaxEntries];
            E = FEntry{};
            E.Sequence = g_TotalRecorded;
            E.Pcr = Pcr;
            E.bExtended = bExtended;
            if (Digest)
            {
                for (QC::usize i = 0; i < sizeof(E.Digest); ++i)
                    E.Digest[i] = Digest[i];
            }
            const char *Src = Label ? Label : "(unlabeled)";
            QC::usize i = 0;
            for (; Src[i] && i + 1 < kMaxLabelLen; ++i)
                E.Label[i] = Src[i];
            E.Label[i] = 0;
            ++g_TotalRecorded;
        }

        QC::usize Snapshot(FEntry *Out, QC::usize MaxCount, QC::u64 &TotalRecorded)
        {
            TotalRecorded = g_TotalRecorded;
            const QC::usize Available = g_TotalRecorded < kMaxEntries ? static_cast<QC::usize>(g_TotalRecorded) : kMaxEntries;
            const QC::usize Count = Available < MaxCount ? Available : MaxCount;
            const QC::u64 First = g_TotalRecorded - Count;
            for (QC::usize i = 0; i < Count; ++i)
                Out[i] = g_Entries[(First + i) % kMaxEntries];
            return Count;
        }

        void SetFirmwareLog(QC::u64 Phys, QC::u32 Length)
        {
            g_FirmwareLog.Phys = Phys;
            g_FirmwareLog.Length = Length;
        }

        FFirmwareLog FirmwareLog()
        {
            return g_FirmwareLog;
        }
    }

    bool ExtendPcrSha256Digest(QC::u32 PcrIndex, const QC::u8 Digest[32], FLogFn Log, const char *Label)
    {
        if (!IsReady() || !Digest)
            return false;

        FTpmOpGuard Guard;
        if (!Guard.Acquired())
        {
            LogStr(Log, "TPM2: busy; PCR_Extend skipped\r\n");
            return false;
        }

        const QC::Status St = Cmd::TpmPcrExtendSha256(Log, PcrIndex, Digest);
        EventLog::Record(PcrIndex, Digest, Label, St == QC::Status::Success);
        if (St != QC::Status::Success)
        {
            LogStr(Log, "TPM2: PCR_Extend failed\r\n");
            return false;
        }
        return true;
    }

    bool VerifyRsa2048RsassaSha256Digest(const QC::u8 Modulus[256], const QC::u8 Digest[32], const QC::u8 Signature[256], FLogFn Log)
    {
        if (!IsReady())
        {
            LogStr(Log, "TPM2: not ready; cannot verify BOOT.JSN signature\r\n");
            return false;
        }
        if (!Modulus || !Digest || !Signature)
            return false;

        FTpmOpGuard Guard;
        if (!Guard.Acquired())
        {
            LogStr(Log, "TPM2: busy; cannot verify signature\r\n");
            return false;
        }

        QC::u32 KeyHandle = 0;
        const QC::Status LoadSt = Cmd::TpmLoadExternalRsa2048RsassaSha256PublicKey(Log, Modulus, KeyHandle);
        if (LoadSt != QC::Status::Success)
        {
            LogStr(Log, "TPM2: LoadExternal failed\r\n");
            LogStr(Log, "TPM2: rspCode 0x");
            LogHex32Fixed(Log, Cmd::LastResponseCode());
            LogStr(Log, "\r\n");
            return false;
        }

        const QC::Status VerSt = Cmd::TpmVerifyRsa2048RsassaSha256Digest(Log, KeyHandle, Digest, Signature);
        const QC::u32 VerRc = Cmd::LastResponseCode();
        (void)Cmd::TpmFlushContext(Log, KeyHandle);

        if (VerSt != QC::Status::Success)
        {
            LogStr(Log, "TPM2: VerifySignature failed\r\n");
            LogStr(Log, "TPM2: rspCode 0x");
            LogHex32Fixed(Log, VerRc);
            LogStr(Log, "\r\n");
            return false;
        }
        return true;
    }
}
