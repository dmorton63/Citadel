#pragma once

#include "QCTypes.h"

namespace QK::Boot::Ramdisk
{
    using FLogFn = void (*)(const char *);

    struct InitOptions
    {
        bool runDemos = true;
        bool loadStartupConfig = true;
        bool applyStartupSideEffects = true;
    };

    // Initializes VFS, mounts the Limine ramdisk module at '/', and runs
    // the existing boot-time FileIO demo + SecureStore self-tests.
    bool InitializeFromLimineModules(QC::u64 ModuleRequest[], FLogFn Log, const InitOptions &Options = {});
}
