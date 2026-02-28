#pragma once

#include "QCTypes.h"

namespace QK::Boot::Security
{
    using FLogFn = void (*)(const char *);

    // Attempts to verify BOOT.JSN using BOOT.SIG from the Limine ramdisk module.
    // Dev behavior: if BOOT.SIG is missing, returns true (warn-only) to avoid trapping development.
    bool VerifyBootJsnSignatureFromLimineRamdiskModule(FLogFn Log, QC::u64 ModuleRequest[], QC::u64 ExecutableFileRequest[]);
}
