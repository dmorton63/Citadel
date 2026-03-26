#pragma once

#include "QCTypes.h"

namespace QK::Boot::Security
{
    using FLogFn = void (*)(const char *);

    enum class SigPolicy : QC::u8
    {
        // Require the .SIG to exist and be valid.
        // Intended for production enforced artifacts.
        RequireValid = 0,

        // If the .SIG is missing or invalid, log and allow.
        // Intended for developer convenience.
        WarnOnly = 1,
    };

    // Verifies an arbitrary file located in the Limine ramdisk module.
    // Signature file naming: appends ".SIG" to the given path.
    // Returns true if allowed under the given policy.
    bool VerifySignedFileFromLimineRamdiskPath(FLogFn Log, QC::u64 ModuleRequest[], const char *RamdiskPath, SigPolicy Policy);

    // Attempts to verify BOOT.JSN using BOOT.SIG from the Limine ramdisk module.
    // Dev behavior: if BOOT.SIG is missing, returns true (warn-only) to avoid trapping development.
    bool VerifyBootJsnSignatureFromLimineRamdiskModule(FLogFn Log, QC::u64 ModuleRequest[], QC::u64 ExecutableFileRequest[]);

    // Attempts to verify DESKTOP.CML using DESKTOP.SIG from the Limine ramdisk module.
    // Production behavior: if DESKTOP.CML exists but DESKTOP.SIG is missing/invalid, returns false.
    // Dev behavior: missing/invalid signature is warn-only (returns true).
    bool VerifyDesktopCmlSignatureFromLimineRamdiskModule(FLogFn Log, QC::u64 ModuleRequest[]);
}
