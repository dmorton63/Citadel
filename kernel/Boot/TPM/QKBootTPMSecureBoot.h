#pragma once

#include "QCTypes.h"

namespace QK::Boot::Tpm::SecureBoot
{
    using FLogFn = void (*)(const char *);

    enum class EState : QC::u8
    {
        Unknown,
        LegacyBios,
        Off,
        On,
    };

    struct FStatus
    {
        EState State = EState::Unknown;
        // True when the firmware PCR 7 events replay to the TPM's PCR 7 value.
        bool bPcr7Verified = false;
        // True when the platform key (PK) is enrolled.
        bool bPkEnrolled = false;
        // True when State came from the UEFI runtime variables rather than the event log.
        bool bFromFirmware = false;
        bool bSetupMode = false;
        QC::u32 Pcr7Events = 0;
        // Short reason when State is Unknown or verification failed.
        const char *Detail = "not evaluated";
    };

    void SetFirmwareIsUefi(bool bUefi);

    // Records the UEFI runtime SecureBoot/SetupMode/PK variables (call only after a
    // successful GetVariable session). They provide
    // the state when no usable event log exists (e.g. QEMU with OVMF).
    void SetFirmwareVariables(bool bSecureBootPresent, QC::u8 SecureBoot, bool bSetupMode, bool bPkPresent);

    // Parses the firmware TCG event log and replays PCR 7 against the TPM.
    // Must be called with the TPM operation lock held, before the kernel
    // extends PCR 7 itself.
    void Evaluate(QC::u64 LogPhys, QC::u32 LogLen, FLogFn Log);

    FStatus Status();

    // Short human-readable summary, e.g. "on (verified)", "on (firmware)" or "off (legacy BIOS)".
    const char *Summary();
}
