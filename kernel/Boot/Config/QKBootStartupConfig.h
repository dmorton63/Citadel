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
    inline bool ShouldUseInteractiveTerminalFallbackForStartupMode(StartupMode Mode)
    {
        switch (Mode)
        {
        case StartupMode::Terminal:
        case StartupMode::Installer:
        case StartupMode::Recovery:
        case StartupMode::Safe:
            return true;
        case StartupMode::Desktop:
        case StartupMode::Network:
        default:
            return false;
        }
    }
    QC::Status PersistStartupMode(StartupMode Mode, FLogFn Log);
    QC::u32 GetMouseSensitivityPercent();
    void SetMouseSensitivityPercent(QC::u32 Percent);
    QC::Status PersistMouseSensitivityPercent(QC::u32 Percent, FLogFn Log);
    QC::u32 GetMouseUsbRelativePercent();
    QC::u32 GetMousePs2RelativePercent();
    QC::u32 GetMouseWheelLines();
    bool GetMouseInvertWheel();
    void SetMouseUsbRelativePercent(QC::u32 Percent);
    void SetMousePs2RelativePercent(QC::u32 Percent);
    void SetMouseWheelLines(QC::u32 Lines);
    void SetMouseInvertWheel(bool Invert);
    QC::Status PersistMouseBehaviorConfig(QC::u32 UsbPercent,
                                          QC::u32 Ps2Percent,
                                          QC::u32 WheelLines,
                                          bool InvertWheel,
                                          FLogFn Log);
    QC::u32 GetKeyboardRepeatDelayMs();
    QC::u32 GetKeyboardRepeatIntervalMs();
    void SetKeyboardRepeatTiming(QC::u32 DelayMs, QC::u32 IntervalMs);
    QC::Status PersistKeyboardRepeatTiming(QC::u32 DelayMs, QC::u32 IntervalMs, FLogFn Log);
    QK::SecurityCenter::Mode GetSecurityCenterMode();
    bool GetIdeSharedProbeEnabled();
    bool GetPreferUsbSharedVolume();
    bool GetSharedUsbVolumeAutoSelect();
    QC::u32 GetSharedUsbVolumeIndex();

    // Dev-only non-TPM SecureStore recovery code override from kernel cmdline.
    // Supported form: citadel.recovery_code=<code>
    // Returns true once per boot when an override is available and copies it to out.
    bool TryConsumeDevRecoveryCode(char *out, QC::usize outCap);

    // Dev-only test trigger for TPM quarantine/reprovision.
    // Supported forms:
    // - startup.cfg: TPM_QUARANTINE_TEST=1
    // - cmdline: citadel.tpm_quarantine_test=1 or bare tpm_quarantine_test
    bool TryConsumeDevTpmQuarantineTest();

    // SAVETERM support. Currently not invoked by QKMain, but kept here so the
    // policy lives with config parsing rather than the main entrypoint.
    void BootSaveTermOnceIfConfigured(FLogFn Log);
}
