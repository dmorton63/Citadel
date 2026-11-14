#pragma once

#include "QCTypes.h"

namespace QK::Boot::Tpm::EventLog
{
    constexpr QC::usize kMaxEntries = 64;
    constexpr QC::usize kMaxLabelLen = 40;

    struct FEntry
    {
        QC::u64 Sequence = 0;
        QC::u32 Pcr = 0;
        bool bExtended = false;
        QC::u8 Digest[32]{};
        char Label[kMaxLabelLen]{};
    };

    struct FFirmwareLog
    {
        QC::u64 Phys = 0;
        QC::u32 Length = 0;
    };

    // Called with the TPM operation lock held.
    void Record(QC::u32 Pcr, const QC::u8 Digest[32], const char *Label, bool bExtended);

    // Copies entries oldest-first; returns the number copied. TotalRecorded
    // reports how many measurements were ever recorded (older ones may have rolled off).
    QC::usize Snapshot(FEntry *Out, QC::usize MaxCount, QC::u64 &TotalRecorded);

    void SetFirmwareLog(QC::u64 Phys, QC::u32 Length);
    FFirmwareLog FirmwareLog();
}
