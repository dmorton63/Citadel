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

    // Parse Limine kernel cmdline overrides. These take precedence over /startup.cfg.
    // Supported forms:
    // - citadel.mode=TERMINAL|DESKTOP|SAFE|RECOVERY|INSTALLER|NETWORK
    // - citadel.startup=... (alias)
    // - bare flags: terminal, safe, recovery, rescue
    void LoadFromCmdline(FLogFn Log, const char *Cmdline);

    StartupMode GetStartupMode();
    void SetStartupMode(StartupMode Mode);
    QC::Status PersistStartupMode(StartupMode Mode, FLogFn Log);
    QC::u32 GetMouseSensitivityPercent();
    void SetMouseSensitivityPercent(QC::u32 Percent);
    QC::Status PersistMouseSensitivityPercent(QC::u32 Percent, FLogFn Log);
    QK::SecurityCenter::Mode GetSecurityCenterMode();
    bool GetIdeSharedProbeEnabled();

    // Dev-only non-TPM SecureStore recovery code override from kernel cmdline.
    // Supported form: citadel.recovery_code=<code>
    // Returns true once per boot when an override is available and copies it to out.
    bool TryConsumeDevRecoveryCode(char *out, QC::usize outCap);

    // SAVETERM support. Currently not invoked by QKMain, but kept here so the
    // policy lives with config parsing rather than the main entrypoint.
    void BootSaveTermOnceIfConfigured(FLogFn Log);
}
