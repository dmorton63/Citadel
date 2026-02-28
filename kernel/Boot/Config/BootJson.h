#pragma once

#include "QCTypes.h"

namespace QK::Boot::Config
{
    using FLogFn = void (*)(const char *);

    struct BootCpuRequirements
    {
        bool require64bit = false;
        bool requireSse2 = false;
        bool requireNx = false;
        bool requireLongmode = false;
    };

    struct BootRequirements
    {
        QC::u64 minRamMiB = 0;
        QC::u64 recommendedRamMiB = 0;
        bool requireTpm = false;
        BootCpuRequirements cpu{};
    };

    struct BootPolicy
    {
        BootRequirements requirements;
    };

    // Compiled-in floors (boot.json may tighten, never loosen).
    constexpr QC::u64 kCompiledMinRamMiB = 2048;
    constexpr QC::u64 kCompiledRecommendedRamMiB = 2048;

    // Attempts to load /boot.json or /BOOT.JSN from the mounted VFS.
    // Returns true if a file was found and parsed; false otherwise.
    bool LoadBootPolicyFromVfs(FLogFn Log, BootPolicy &OutPolicy);

    // Attempts to load BOOT.JSN directly from the Limine ramdisk module buffer
    // without requiring VFS mount.
    // Returns true if a file was found and parsed; false otherwise.
    bool LoadBootPolicyFromLimineRamdiskModule(FLogFn Log, QC::u64 ModuleRequest[], BootPolicy &OutPolicy);
}
