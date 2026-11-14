#pragma once

#include "QCTypes.h"

namespace QK::Boot::EfiRuntime
{
    using FLogFn = void (*)(const char *);

    struct FGlobalVariables
    {
        bool bSecureBootPresent = false;
        QC::u8 SecureBoot = 0;
        bool bSetupModePresent = false;
        QC::u8 SetupMode = 0;
        bool bPkPresent = false;
    };

    // Reads the EFI global SecureBoot/SetupMode/PK variables through UEFI
    // runtime GetVariable. Firmware runtime regions are identity-mapped only for
    // the duration of the call. Call once on the BSP during early boot, while
    // Limine's bootloader-reclaimable memory (the EFI memory map copy) is intact.
    bool ReadGlobalVariables(QC::u64 SystemTablePhys, const void *Memmap, QC::u64 MemmapSize,
                             QC::u64 DescSize, FGlobalVariables &Out, FLogFn Log);
}
