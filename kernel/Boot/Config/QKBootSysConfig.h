#pragma once

#include "QCTypes.h"

namespace QK::Boot::Config
{
    using FLogFn = void (*)(const char *);

    struct SysConfigModule
    {
        char id[32] = {0};
        char path[160] = {0};
        char type[32] = {0};
        bool required = false;
    };

    struct SysConfig
    {
        QC::u64 version = 0;
        char profile[32] = {0};

        char goldenRoot[160] = {0};
        char goldenHash[96] = {0};
        char productionRoot[160] = {0};
        char productionHash[96] = {0};

        QC::u32 moduleCount = 0;
        SysConfigModule modules[32] = {};

        QC::u32 earlyCount = 0;
        char earlyIds[16][32] = {};
    };

    // Attempts to load SYSCFG.JSN directly from the Limine ramdisk module buffer
    // without requiring VFS mount.
    // Returns true if the file was found and parsed; false otherwise.
    bool LoadSysConfigFromLimineRamdiskModule(FLogFn Log, QC::u64 ModuleRequest[], SysConfig &Out);
}
