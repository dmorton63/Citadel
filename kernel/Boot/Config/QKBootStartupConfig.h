#pragma once

#include "QCTypes.h"
#include "QKSecurityCenter.h"

namespace QK::Boot::Config
{
    using FLogFn = void (*)(const char *);

    enum class StartupMode : QC::u8
    {
        Desktop,
        Terminal,
        Safe,
        Recovery,
        Installer,
        Network
    };

    const char *StartupModeName(StartupMode Mode);

    // Parse /startup.cfg from the mounted VFS and update the cached config.
    void LoadFromVfs(FLogFn Log);

    StartupMode GetStartupMode();
    QK::SecurityCenter::Mode GetSecurityCenterMode();
    bool GetIdeSharedProbeEnabled();

    // SAVETERM support. Currently not invoked by QKMain, but kept here so the
    // policy lives with config parsing rather than the main entrypoint.
    void BootSaveTermOnceIfConfigured(FLogFn Log);
}
