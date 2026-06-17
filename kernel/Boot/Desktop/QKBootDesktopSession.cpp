#include "Boot/Desktop/QKBootDesktopSession.h"

#include "QCLogger.h"
#include "QCString.h"

#include "QKMemHeap.h"
#include "QArchPCI.h"
#include "QDrvTimer.h"
#include "QDrvVmwareSVGA.h"
#include "QKDrvManager.h"
#include "QKInputSettings.h"
#include "PS2/QKDrvPS2Keyboard.h"
#include "PS2/QKDrvPS2Mouse.h"

#include "QWFramebuffer.h"
#include "QWWindowManager.h"
#include "QWWindow.h"

#include "QKEventManager.h"
#include "QKEventListener.h"
#include "QKShutdownController.h"
#include "QKBootEventLog.h"

#include "QKConsole.h"
#include "QKSecureStore.h"
#include "QKStorageProbe.h"
#include "QKSecurityCenter.h"
#include "QSCSecurityCenter.h"
#include "QFSVFS.h"
#include "QFSFile.h"
#include "QFSVolumeManager.h"

#include "QNetDHCP.h"
#include "QNetStack.h"

#include "QKSystemPump.h"

#include "Debug/Framebuffer/QKDebugFramebufferText.h"

#include "QDDesktop.h"

#include "Boot/Config/QKBootStartupConfig.h"
#include "Boot/Ramdisk/QKBootRamdiskMount.h"

namespace
{
    static QK::Boot::Desktop::FLogFn g_Log = nullptr;
    [[noreturn]] static void enterTerminalOnlyLoop();

    static void secureZero(void *ptr, QC::usize len)
    {
        volatile QC::u8 *p = reinterpret_cast<volatile QC::u8 *>(ptr);
        while (len--)
            *p++ = 0;
    }

    static bool PromptSecretLine(const char *prompt, char *out, QC::usize outCap)
    {
        if (!out || outCap == 0)
            return false;
        out[0] = '\0';
        QK::Console::write(prompt);

        // Keep waiting for input without re-printing the prompt; this avoids
        // serial spam if the underlying read reports transient false.
        for (;;)
        {
            if (QK::Console::readLineBlocking(out, outCap, true))
                return true;
        }
    }

    static bool PromptLine(const char *prompt, char *out, QC::usize outCap)
    {
        if (!out || outCap == 0)
            return false;
        out[0] = '\0';
        QK::Console::write(prompt);
        for (;;)
        {
            if (QK::Console::readLineBlocking(out, outCap, true))
                return true;
        }
    }

    static const char *statusText(QC::Status st)
    {
        switch (st)
        {
        case QC::Status::Success:
            return "Success";
        case QC::Status::Error:
            return "Error";
        case QC::Status::InvalidParam:
            return "InvalidParam";
        case QC::Status::OutOfMemory:
            return "OutOfMemory";
        case QC::Status::NotFound:
            return "NotFound";
        case QC::Status::Timeout:
            return "Timeout";
        case QC::Status::Busy:
            return "Busy";
        case QC::Status::NotSupported:
            return "NotSupported";
        default:
            return "Unknown";
        }
    }

    static void EnsureNonTpmSecureStoreUnlocked()
    {
        constexpr const char *kTpmKey = "WRAPKEY.TPM";
        constexpr const char *kKdfKey = "WRAPKEY.KDF";
        constexpr const char *kPlainKey = "WRAPKEY.BIN";

        const bool hasTpmAnchor = QK::SecureStore::exists(kTpmKey);

        if (QK::SecureStore::tpm_present())
        {
            QK::Console::write("SecureStore: anchor=tpm\r\n");

            // Prefer TPM anchor on TPM-capable systems; legacy recovery/plain
            // artifacts are stale in this mode and can cause confusing parity drift.
            if (QK::SecureStore::exists(kKdfKey) || QK::SecureStore::exists(kPlainKey))
            {
                QK::Console::write("SecureStore: legacy non-TPM anchor artifacts detected; ignoring while TPM anchor path is active.\r\n");
            }
            return;
        }

        if (hasTpmAnchor)
        {
            if (g_Log)
                g_Log("SecureStore: TPM anchor blob present but TPM sealing unavailable; refusing non-TPM fallback\r\n");

            QK::Boot::Config::SetStartupMode(QK::Boot::Config::StartupMode::Terminal);
            QK::Boot::Events::Emit("securestore", "tpm_anchor_terminal", "tpm-anchor-present-no-tpm-path");
            QK::Console::write("SecureStore: TPM anchor was provisioned, but TPM sealing is unavailable in this boot.\r\n");
            QK::Console::write("SecureStore: refusing recovery/plain fallback to preserve TPM anchor parity.\r\n");
            QK::Console::write("SecureStore: verify TPM device/firmware state, then retry boot.\r\n");
            QK::Console::setInputEnabled(true);
            enterTerminalOnlyLoop();
        }

        const bool hasKdf = QK::SecureStore::exists(kKdfKey);
        const bool hasPlain = QK::SecureStore::exists(kPlainKey);

        if (hasKdf)
            QK::Console::write("SecureStore: anchor=recovery\r\n");
        else if (hasPlain)
            QK::Console::write("SecureStore: anchor=legacy-plain\r\n");
        else
            QK::Console::write("SecureStore: anchor=recovery-bootstrap\r\n");

        char a[96];
        char b[96];
        QC::String::memset(a, 0, sizeof(a));
        QC::String::memset(b, 0, sizeof(b));

        char cmdlineRecovery[96];
        QC::String::memset(cmdlineRecovery, 0, sizeof(cmdlineRecovery));
        const bool hasCmdlineRecovery = QK::Boot::Config::TryConsumeDevRecoveryCode(cmdlineRecovery, sizeof(cmdlineRecovery));

        auto tryUnlockWithCmdlineRecovery = [&]() -> bool
        {
            if (!hasCmdlineRecovery)
                return false;

            const QC::Status st = QK::SecureStore::nonTpmUnlockOrInitializeWrapKey(cmdlineRecovery);
            secureZero(cmdlineRecovery, sizeof(cmdlineRecovery));
            if (st == QC::Status::Success)
            {
                QK::Console::write("SecureStore unlocked (dev cmdline override).\r\n");
                return true;
            }
            QK::Console::write("Dev cmdline recovery_code override failed; falling back to interactive prompt.\r\n");
            return false;
        };

        if (!hasKdf)
        {
            QK::Console::write("\r\nNon-TPM SecureStore bootstrapping (recovery code required).\r\n");
            if (hasPlain)
                QK::Console::write("Upgrading legacy plaintext anchor to recovery-code wrapping.\r\n");

            if (tryUnlockWithCmdlineRecovery())
                return;

            for (;;)
            {
                if (!PromptSecretLine("Set recovery code: ", a, sizeof(a)))
                    continue;
                if (!PromptSecretLine("Confirm recovery code: ", b, sizeof(b)))
                {
                    secureZero(a, sizeof(a));
                    continue;
                }

                if (QC::String::strcmp(a, b) != 0)
                {
                    secureZero(a, sizeof(a));
                    secureZero(b, sizeof(b));
                    QK::Console::write("Recovery codes did not match. Try again.\r\n");
                    continue;
                }

                const QC::Status st = QK::SecureStore::nonTpmUnlockOrInitializeWrapKey(a);
                secureZero(a, sizeof(a));
                secureZero(b, sizeof(b));
                if (st == QC::Status::Success)
                {
                    QK::Console::write("SecureStore unlocked.\r\n");
                    return;
                }

                QK::Console::write("Failed to initialize SecureStore. Try again.\r\n");
            }
        }

        if (tryUnlockWithCmdlineRecovery())
            return;

        for (;;)
        {
            if (!PromptLine("\r\nEnter recovery code to unlock SecureStore: ", a, sizeof(a)))
                continue;

            QK::Console::write("Unlocking SecureStore... Please wait...\r\n");
            const QC::Status st = QK::SecureStore::nonTpmUnlockOrInitializeWrapKey(a);
            secureZero(a, sizeof(a));

            if (st == QC::Status::Success)
            {
                QK::Console::write("SecureStore unlocked.\r\n");
                return;
            }

            QK::Console::write("Invalid recovery code. Try again: ");
        }
    }

    static void RunPreDesktopOwnerGate()
    {
        auto &sc = QK::SecurityCenter::instance();
        constexpr QC::u32 kMaxGateRestarts = 3;
        QC::u32 gateRestartCount = 0;

        auto failClosedToTerminal = [&](const char *reason)
        {
            g_Log("Owner gate: repeated restart loop detected; switching to terminal mode\r\n");
            QK::Boot::Config::SetStartupMode(QK::Boot::Config::StartupMode::Terminal);
            QK::Boot::Events::Emit("owner_gate", "fallback_terminal", reason ? reason : "unknown");
            QK::Console::write("Owner gate fallback: ");
            QK::Console::write(reason ? reason : "unspecified");
            QK::Console::write("\r\n");
            QK::Console::write("Desktop startup paused to avoid ambiguous enrollment/unlock restart loops.\r\n");
            QK::Console::write("Use terminal tools to verify owner credentials/system state, then run startx to retry desktop startup.\r\n");
            QK::Console::setInputEnabled(true);
            enterTerminalOnlyLoop();
        };

        for (;;)
        {
            if (!sc.ownerIsEnrolled())
            {
                QK::Console::write("\r\nOwner enrollment required before desktop startup.\r\n");
                char user[48];
                char passA[96];
                char passB[96];
                bool restartGate = false;
                for (;;)
                {
                    QC::String::memset(user, 0, sizeof(user));
                    QC::String::memset(passA, 0, sizeof(passA));
                    QC::String::memset(passB, 0, sizeof(passB));

                    if (!PromptLine("Owner username: ", user, sizeof(user)))
                        continue;
                    if (!PromptSecretLine("Owner password: ", passA, sizeof(passA)))
                        continue;
                    if (!PromptSecretLine("Confirm password: ", passB, sizeof(passB)))
                        continue;

                    if (!user[0])
                    {
                        QK::Console::write("Username cannot be empty. Try again.\r\n");
                        continue;
                    }

                    if (!passA[0])
                    {
                        QK::Console::write("Password cannot be empty. Try again.\r\n");
                        continue;
                    }

                    if (QC::String::strcmp(passA, passB) != 0)
                    {
                        QK::Console::write("Passwords did not match. Try again.\r\n");
                        continue;
                    }

                    const QC::Status st = sc.ownerEnroll(user, passA, true);
                    secureZero(passA, sizeof(passA));
                    secureZero(passB, sizeof(passB));

                    if (st == QC::Status::Success)
                    {
                        QK::Console::write("Owner enrolled and unlocked.\r\n");
                        char recoveryCode[48];
                        QC::String::memset(recoveryCode, 0, sizeof(recoveryCode));
                        const QC::Status recoverySt = sc.consumePendingInstallRecoveryCode(recoveryCode);
                        if (recoverySt == QC::Status::Success)
                        {
                            QK::Console::write("One-time recovery code (displayed once): ");
                            QK::Console::write(recoveryCode);
                            QK::Console::write("\r\nStore it offline before continuing.\r\n");
                            secureZero(recoveryCode, sizeof(recoveryCode));
                        }
                        else if (recoverySt == QC::Status::Busy)
                        {
                            QK::Console::write("Recovery code generation deferred for boot-time enrollment.\r\n");
                        }
                        QK::Console::write("Starting Desktop ... Please wait...\r\n");
                        return;
                    }

                    if (st == QC::Status::Busy)
                    {
                        char existingUser[48];
                        QC::String::memset(existingUser, 0, sizeof(existingUser));
                        if (sc.getEnrolledOwnerUsername(existingUser, sizeof(existingUser)) == QC::Status::Success && existingUser[0] != '\0')
                        {
                            QK::Console::write("Owner record already exists for username: ");
                            QK::Console::write(existingUser);
                            QK::Console::write("\r\nEnrollment did not use the credentials just entered; switching to unlock for the stored owner.\r\n");
                            break;
                        }

                        QK::Console::write("Owner credential record unreadable; restarting enrollment flow.\r\n");
                        restartGate = true;
                        break;
                    }

                    if (sc.bypassEnabled())
                    {
                        QK::Console::write("Enrollment unavailable in BYPASS mode; continuing boot without owner enrollment.\r\n");
                        return;
                    }

                    QK::Console::write("Enrollment failed (status=");
                    QK::Console::write(statusText(st));
                    QK::Console::write("). Try again.\r\n");
                }

                if (restartGate)
                {
                    ++gateRestartCount;
                    if (gateRestartCount >= kMaxGateRestarts)
                        failClosedToTerminal("owner credential record repeatedly unreadable during enrollment");
                    continue;
                }
            }

            gateRestartCount = 0;

            if (sc.bypassEnabled() || sc.ownerUnlocked())
                return;

            if (!sc.ownerIsEnrolled())
            {
                QK::Console::write("Owner credential record is not readable; returning to enrollment.\r\n");
                continue;
            }

            QK::Console::write("\r\nOwner unlock required before desktop startup.\r\n");
            char storedUser[48];
            QC::String::memset(storedUser, 0, sizeof(storedUser));
            const QC::Status storedUserSt = sc.getEnrolledOwnerUsername(storedUser, sizeof(storedUser));
            const bool haveStoredUser = (storedUserSt == QC::Status::Success && storedUser[0] != '\0');
            if (haveStoredUser)
            {
                QK::Console::write("Stored owner username: ");
                QK::Console::write(storedUser);
                QK::Console::write("\r\n");
            }

            char pass[96];
            bool restartGate = false;
            for (;;)
            {
                QC::String::memset(pass, 0, sizeof(pass));

                const char *unlockUser = storedUser;
                char enteredUser[48];
                QC::String::memset(enteredUser, 0, sizeof(enteredUser));

                if (!haveStoredUser)
                {
                    if (!PromptLine("Owner username: ", enteredUser, sizeof(enteredUser)))
                        continue;
                    unlockUser = enteredUser;
                }
                if (!PromptSecretLine("Owner password: ", pass, sizeof(pass)))
                    continue;

                const QC::Status st = sc.ownerUnlock(unlockUser, pass, false);
                secureZero(pass, sizeof(pass));

                if (st == QC::Status::Success)
                {
                    QK::Console::write("Owner unlocked.\r\n");
                    QK::Console::write("Starting Desktop ... Please wait...\r\n");
                    return;
                }

                if (st == QC::Status::Timeout)
                {
                    QK::Console::write("Unlock denied: lockout active. Wait and retry.\r\n");
                    continue;
                }

                if (st == QC::Status::NotFound)
                {
                    QK::Console::write("Owner credential record disappeared or is unreadable; returning to enrollment.\r\n");
                    restartGate = true;
                    break;
                }

                QK::Console::write("Unlock denied (status=");
                QK::Console::write(statusText(st));
                if (haveStoredUser && st == QC::Status::Error)
                {
                    QK::Console::write(", username=");
                    QK::Console::write(storedUser);
                }
                QK::Console::write("). Try again.\r\n");
            }

            if (restartGate)
            {
                ++gateRestartCount;
                if (gateRestartCount >= kMaxGateRestarts)
                    failClosedToTerminal("owner credential record repeatedly unavailable during unlock");
                continue;
            }

            gateRestartCount = 0;
        }
    }

    static void RunPreDesktopDhcpBringUp()
    {
        g_Log("Pre-desktop phase: DHCP bring-up\r\n");
        // Best-effort DHCPv4 (bounded wait); falls back to manual `ip set`.
        // Defer it until after the owner gate so the boot password prompt appears promptly.
        g_Log("Attempting DHCPv4...\r\n");
        const QC::u64 dhcpStartMs = QDrv::Timer::instance().milliseconds();
        {
            QNet::DHCPv4Client dhcp;
            if (dhcp.begin() == QC::Status::Success)
            {
                QNet::DHCPv4Lease lease{};

                const QC::u64 deadlineMs = QDrv::Timer::instance().milliseconds() + 2500;
                while (QDrv::Timer::instance().milliseconds() < deadlineMs)
                {
                    // Pump NIC RX -> QNetwork.
                    QKDrv::Manager::instance().poll();
                    QKDrv::PS2::Keyboard::instance().poll();

                    if (dhcp.poll(&lease))
                    {
                        QNet::Stack::instance().ip()->setAddress(lease.address);
                        QNet::Stack::instance().ip()->setSubnetMask(lease.subnetMask);
                        QNet::Stack::instance().ip()->setGateway(lease.gateway);
                        QNet::Stack::instance().ip()->setDnsServer(lease.dnsServer);

                        g_Log("DHCPv4 lease acquired: ip=");
                        logIPv4(lease.address);
                        g_Log(" mask=");
                        logIPv4(lease.subnetMask);
                        g_Log(" gw=");
                        logIPv4(lease.gateway);

                        if (lease.dnsServer.value != 0)
                        {
                            g_Log(" dns=");
                            logIPv4(lease.dnsServer);
                        }
                        g_Log("\r\n");
                        break;
                    }

                    QDrv::Timer::instance().sleep(10);
                }

                if (lease.address.value == 0)
                {
                    g_Log("DHCPv4 timeout (manual config still available)\r\n");
                }
            }
            else
            {
                g_Log("DHCPv4 skipped (no NIC/MAC or UDP bind failed)\r\n");
            }
        }
        logBootMetric("dhcp_wait", dhcpStartMs);
    }

    static void KeepConsoleOwnershipWithKernelForStartupGates()
    {
        g_Log("Pre-desktop phase: console input stays kernel-owned for SecureStore and owner gates\r\n");
        QK::Console::setInputEnabled(true);
    }

    static void HandOffConsoleOwnershipToDesktop()
    {
        QK::Console::setOwner(QK::Console::Owner::Desktop);
        g_Log("Pre-desktop phase: desktop handoff\r\n");
        g_Log("Console ownership: transferring input to desktop runtime\r\n");
        QK::Console::setInputEnabled(false);
    }

    static char keyToChar(QKDrv::PS2::Key key, bool shift, bool caps)
    {
        auto letter = [&](char c) -> char
        {
            const bool upper = (shift ^ caps);
            return upper ? static_cast<char>(c - 32) : c;
        };

        switch (key)
        {
        // Letters
        case QKDrv::PS2::Key::Q:
            return letter('q');
        case QKDrv::PS2::Key::W:
            return letter('w');
        case QKDrv::PS2::Key::E:
            return letter('e');
        case QKDrv::PS2::Key::R:
            return letter('r');
        case QKDrv::PS2::Key::T:
            return letter('t');
        case QKDrv::PS2::Key::Y:
            return letter('y');
        case QKDrv::PS2::Key::U:
            return letter('u');
        case QKDrv::PS2::Key::I:
            return letter('i');
        case QKDrv::PS2::Key::O:
            return letter('o');
        case QKDrv::PS2::Key::P:
            return letter('p');
        case QKDrv::PS2::Key::A:
            return letter('a');
        case QKDrv::PS2::Key::S:
            return letter('s');
        case QKDrv::PS2::Key::D:
            return letter('d');
        case QKDrv::PS2::Key::F:
            return letter('f');
        case QKDrv::PS2::Key::G:
            return letter('g');
        case QKDrv::PS2::Key::H:
            return letter('h');
        case QKDrv::PS2::Key::J:
            return letter('j');
        case QKDrv::PS2::Key::K:
            return letter('k');
        case QKDrv::PS2::Key::L:
            return letter('l');
        case QKDrv::PS2::Key::Z:
            return letter('z');
        case QKDrv::PS2::Key::X:
            return letter('x');
        case QKDrv::PS2::Key::C:
            return letter('c');
        case QKDrv::PS2::Key::V:
            return letter('v');
        case QKDrv::PS2::Key::B:
            return letter('b');
        case QKDrv::PS2::Key::N:
            return letter('n');
        case QKDrv::PS2::Key::M:
            return letter('m');

        // Digits + shifted symbols
        case QKDrv::PS2::Key::Num1:
            return shift ? '!' : '1';
        case QKDrv::PS2::Key::Num2:
            return shift ? '@' : '2';
        case QKDrv::PS2::Key::Num3:
            return shift ? '#' : '3';
        case QKDrv::PS2::Key::Num4:
            return shift ? '$' : '4';
        case QKDrv::PS2::Key::Num5:
            return shift ? '%' : '5';
        case QKDrv::PS2::Key::Num6:
            return shift ? '^' : '6';
        case QKDrv::PS2::Key::Num7:
            return shift ? '&' : '7';
        case QKDrv::PS2::Key::Num8:
            return shift ? '*' : '8';
        case QKDrv::PS2::Key::Num9:
            return shift ? '(' : '9';
        case QKDrv::PS2::Key::Num0:
            return shift ? ')' : '0';

        // Punctuation
        case QKDrv::PS2::Key::Space:
            return ' ';
        case QKDrv::PS2::Key::Enter:
            return '\n';
        case QKDrv::PS2::Key::Tab:
            return '\t';
        case QKDrv::PS2::Key::Backspace:
            return '\b';

        case QKDrv::PS2::Key::Minus:
            return shift ? '_' : '-';
        case QKDrv::PS2::Key::Equals:
            return shift ? '+' : '=';
        case QKDrv::PS2::Key::LeftBracket:
            return shift ? '{' : '[';
        case QKDrv::PS2::Key::RightBracket:
            return shift ? '}' : ']';
        case QKDrv::PS2::Key::Backslash:
            return shift ? '|' : '\\';
        case QKDrv::PS2::Key::Semicolon:
            return shift ? ':' : ';';
        case QKDrv::PS2::Key::Apostrophe:
            return shift ? '"' : '\'';
        case QKDrv::PS2::Key::Backtick:
            return shift ? '~' : '`';
        case QKDrv::PS2::Key::Comma:
            return shift ? '<' : ',';
        case QKDrv::PS2::Key::Period:
            return shift ? '>' : '.';
        case QKDrv::PS2::Key::Slash:
            return shift ? '?' : '/';

        default:
            return 0;
        }
    }

    struct DesktopSessionState
    {
        QC::u64 *FramebufferRequest = nullptr;
        QC::u64 *ModuleRequest = nullptr;
        QK::Boot::Desktop::EarlyHeap Heap{};

        QC::uptr FbAddress = 0;
        QC::u32 Width = 0;
        QC::u32 Height = 0;
        QC::u32 Pitch = 0;

        bool Prepared = false;
        bool InputInitialized = false;
        bool WindowSystemInitialized = false;
        bool DesktopInitialized = false;
    };

    static DesktopSessionState g_State;
    static QW::Framebuffer g_Framebuffer;
    static QD::Desktop g_Desktop;
    static QK::Event::EventListener g_CtrlQListener;
    static QK::Event::ListenerId g_CtrlQId = QK::Event::InvalidListenerId;

    static bool g_prevLeftBtn = false;
    static bool g_prevRightBtn = false;

    static bool g_prevPosValid = false;
    static QC::i32 g_prevX = 0;
    static QC::i32 g_prevY = 0;
    static bool g_primaryMouseStateValid = false;
    static QC::i32 g_primaryMouseX = 0;
    static QC::i32 g_primaryMouseY = 0;
    static QC::u8 g_primaryMouseButtons = 0;
    static volatile bool g_stopDesktopRequested = false;
    static bool g_backspaceRepeatArmed = false;
    static QC::u64 g_backspaceRepeatNextMs = 0;
    static QC::u64 g_lastPrimaryKeyboardReportMs = 0;
    static QC::u64 g_lastPrimaryMouseReportMs = 0;

    static constexpr QC::u64 kPs2KeyboardFallbackSilenceMs = 250;
    static constexpr QC::u64 kPs2MouseFallbackSilenceMs = 16;

    static void armBackspaceRepeat(bool pressed);
    static void logInt(QC::i32 value);

    static void logBootMetric(const char *label, QC::u64 startMs)
    {
        if (!g_Log)
            return;

        g_Log("BOOTMETRIC ");
        g_Log(label);
        g_Log("=");
        const QC::u64 elapsed = QDrv::Timer::instance().milliseconds() - startMs;
        logInt(static_cast<QC::i32>(elapsed > 0x7fffffffULL ? 0x7fffffff : elapsed));
        g_Log("ms\r\n");
    }

    static bool shouldUsePs2KeyboardFallback()
    {
        const QC::u64 nowMs = QDrv::Timer::instance().milliseconds();
        return g_lastPrimaryKeyboardReportMs == 0 ||
               nowMs >= (g_lastPrimaryKeyboardReportMs + kPs2KeyboardFallbackSilenceMs);
    }

    static bool shouldUsePs2MouseFallback()
    {
        const QC::u64 nowMs = QDrv::Timer::instance().milliseconds();
        return g_lastPrimaryMouseReportMs == 0 ||
               nowMs >= (g_lastPrimaryMouseReportMs + kPs2MouseFallbackSilenceMs);
    }

    static void dispatchDesktopKeyboardReport(const QKDrv::KeyboardReport &rep,
                                              bool markPrimarySource)
    {
        if (markPrimarySource)
            g_lastPrimaryKeyboardReportMs = QDrv::Timer::instance().milliseconds();

        QKDrv::PS2::KeyEvent evt;
        evt.key = static_cast<QKDrv::PS2::Key>(rep.scancode);
        evt.pressed = rep.pressed;

        evt.shift = (rep.modifiers & 0x01) != 0;
        evt.ctrl = (rep.modifiers & 0x02) != 0;
        evt.alt = (rep.modifiers & 0x04) != 0;
        const bool caps = (rep.modifiers & 0x08) != 0;
        evt.character = keyToChar(evt.key, evt.shift, caps);

        if (evt.key == QKDrv::PS2::Key::Backspace)
            armBackspaceRepeat(evt.pressed);

        if (QK::Console::inputEnabled())
        {
            QK::Console::handleKeyEvent(evt);
            return;
        }

        auto &eventMgr = QK::Event::EventManager::instance();

        QK::Event::Modifiers mods = QK::Event::Modifiers::None;
        if (evt.shift)
            mods = mods | QK::Event::Modifiers::Shift;
        if (evt.ctrl)
            mods = mods | QK::Event::Modifiers::Ctrl;
        if (evt.alt)
            mods = mods | QK::Event::Modifiers::Alt;

        eventMgr.postKeyEvent(
            evt.pressed ? QK::Event::Type::KeyDown : QK::Event::Type::KeyUp,
            static_cast<QC::u8>(evt.key),
            static_cast<QC::u8>(evt.key),
            evt.character,
            mods,
            false);
    }

    static void dispatchDesktopMouseReport(const QKDrv::MouseReport &report,
                                           QKDrv::MouseDriver &sourceMouse,
                                           bool markPrimarySource)
    {
        auto &eventMgr = QK::Event::EventManager::instance();
        auto &wm = QW::WindowManager::instance();
        const QC::u64 reportMs = QDrv::Timer::instance().milliseconds();

        const QC::i32 curX = report.isAbsolute ? report.x : sourceMouse.x();
        const QC::i32 curY = report.isAbsolute ? report.y : sourceMouse.y();

        QC::i32 dx = 0;
        QC::i32 dy = 0;
        if (report.isAbsolute)
        {
            if (g_prevPosValid)
            {
                dx = curX - g_prevX;
                dy = curY - g_prevY;
            }
            g_prevX = curX;
            g_prevY = curY;
            g_prevPosValid = true;
        }
        else
        {
            dx = report.deltaX;
            dy = report.deltaY;
        }

        static QC::u32 s_mouseReportCount = 0;
        static QC::u8 s_prevButtons = 0;
        ++s_mouseReportCount;

        const bool buttonsChanged = (report.buttons != s_prevButtons);
        s_prevButtons = report.buttons;

        bool logThis = false;
        if (buttonsChanged)
        {
            logThis = true;
        }
        else
        {
            logThis = ((s_mouseReportCount % 5000u) == 0u);
        }

        if (logThis)
        {
            g_Log("Mouse report (");
            g_Log(report.isAbsolute ? "abs" : "rel");
            g_Log(") pos(");
            logInt(curX);
            g_Log(",");
            logInt(curY);
            g_Log(") d(");
            logInt(dx);
            g_Log(",");
            logInt(dy);
            g_Log(") buttons=");
            logInt(report.buttons);
            g_Log("\r\n");
        }

        auto logClick = [&](const char *label)
        {
            g_Log(label);
            g_Log(" at (");
            logInt(curX);
            g_Log(", ");
            logInt(curY);
            g_Log(") ");
            g_Log(report.isAbsolute ? "abs" : "rel");
            g_Log("\r\n");
        };

        const bool leftBtn = (report.buttons & 0x01) != 0;
        const bool rightBtn = (report.buttons & 0x02) != 0;

        if (markPrimarySource)
        {
            bool primaryActivity = (report.wheel != 0) || buttonsChanged;
            if (report.isAbsolute)
            {
                primaryActivity = primaryActivity || !g_primaryMouseStateValid ||
                                  curX != g_primaryMouseX || curY != g_primaryMouseY;
                g_primaryMouseX = curX;
                g_primaryMouseY = curY;
            }
            else
            {
                primaryActivity = primaryActivity || dx != 0 || dy != 0;
            }

            g_primaryMouseButtons = report.buttons;
            g_primaryMouseStateValid = true;

            if (primaryActivity)
                g_lastPrimaryMouseReportMs = QDrv::Timer::instance().milliseconds();
        }

        bool postedMove = false;
        if (report.isAbsolute || dx != 0 || dy != 0 || buttonsChanged)
        {
            wm.noteMouseEventPosted(reportMs);
            eventMgr.postMouseMove(curX, curY, dx, dy);
            postedMove = true;
        }

        if (report.wheel != 0)
        {
            if (!postedMove)
            {
                wm.noteMouseEventPosted(reportMs);
                eventMgr.postMouseMove(curX, curY, 0, 0);
                postedMove = true;
            }
            const QC::i32 scrollDelta = QKDrv::Input::applyMouseWheelBehavior(report.wheel);
            wm.noteMouseEventPosted(reportMs);
            eventMgr.postMouseScroll(scrollDelta, curX, curY);
        }

        if (leftBtn && !g_prevLeftBtn)
        {
            logClick("Left click");
            wm.noteMouseEventPosted(reportMs);
            eventMgr.postMouseButton(QK::Event::Type::MouseButtonDown,
                                     QK::Event::MouseButton::Left,
                                     curX, curY, QK::Event::Modifiers::None);
        }
        if (!leftBtn && g_prevLeftBtn)
        {
            wm.noteMouseEventPosted(reportMs);
            eventMgr.postMouseButton(QK::Event::Type::MouseButtonUp,
                                     QK::Event::MouseButton::Left,
                                     curX, curY, QK::Event::Modifiers::None);
        }
        if (rightBtn && !g_prevRightBtn)
        {
            logClick("Right click");
            wm.noteMouseEventPosted(reportMs);
            eventMgr.postMouseButton(QK::Event::Type::MouseButtonDown,
                                     QK::Event::MouseButton::Right,
                                     curX, curY, QK::Event::Modifiers::None);
        }
        if (!rightBtn && g_prevRightBtn)
        {
            wm.noteMouseEventPosted(reportMs);
            eventMgr.postMouseButton(QK::Event::Type::MouseButtonUp,
                                     QK::Event::MouseButton::Right,
                                     curX, curY, QK::Event::Modifiers::None);
        }

        g_prevLeftBtn = leftBtn;
        g_prevRightBtn = rightBtn;
    }

    static void armBackspaceRepeat(bool pressed)
    {
        if (!pressed)
        {
            g_backspaceRepeatArmed = false;
            g_backspaceRepeatNextMs = 0;
            return;
        }

        g_backspaceRepeatArmed = true;
        g_backspaceRepeatNextMs = QDrv::Timer::instance().milliseconds() +
                                  static_cast<QC::u64>(QK::Boot::Config::GetKeyboardRepeatDelayMs());
    }

    static void maybePostBackspaceRepeat()
    {
        if (!g_backspaceRepeatArmed || QK::Console::inputEnabled())
            return;

        auto *kbd = QKDrv::Manager::instance().keyboardDriver();
        if (!kbd || !kbd->isKeyPressed(static_cast<QC::u8>(QKDrv::PS2::Key::Backspace)))
        {
            g_backspaceRepeatArmed = false;
            g_backspaceRepeatNextMs = 0;
            return;
        }

        const QC::u64 nowMs = QDrv::Timer::instance().milliseconds();
        if (nowMs < g_backspaceRepeatNextMs)
            return;

        const QC::u8 rawMods = kbd->modifiers();
        const bool shift = (rawMods & 0x01) != 0;
        const bool ctrl = (rawMods & 0x02) != 0;
        const bool alt = (rawMods & 0x04) != 0;

        QK::Event::Modifiers mods = QK::Event::Modifiers::None;
        if (shift)
            mods = mods | QK::Event::Modifiers::Shift;
        if (ctrl)
            mods = mods | QK::Event::Modifiers::Ctrl;
        if (alt)
            mods = mods | QK::Event::Modifiers::Alt;

        QK::Event::EventManager::instance().postKeyEvent(
            QK::Event::Type::KeyDown,
            static_cast<QC::u8>(QKDrv::PS2::Key::Backspace),
            static_cast<QC::u8>(QKDrv::PS2::Key::Backspace),
            '\b',
            mods,
            false);

        g_backspaceRepeatNextMs = nowMs +
                                  static_cast<QC::u64>(QK::Boot::Config::GetKeyboardRepeatIntervalMs());
    }

    static void logInt(QC::i32 value)
    {
        if (!g_Log)
            return;

        char buf[16];
        int idx = 0;

        if (value == 0)
        {
            buf[idx++] = '0';
        }
        else
        {
            bool neg = false;
            QC::u32 mag = 0;
            if (value < 0)
            {
                neg = true;
                // Avoid UB on INT_MIN.
                mag = static_cast<QC::u32>(-(value + 1)) + 1u;
            }
            else
            {
                mag = static_cast<QC::u32>(value);
            }

            char tmp[12];
            int t = 0;
            while (mag > 0 && t < static_cast<int>(sizeof(tmp)))
            {
                tmp[t++] = static_cast<char>('0' + (mag % 10));
                mag /= 10;
            }

            if (neg)
                buf[idx++] = '-';

            while (t > 0)
            {
                buf[idx++] = tmp[--t];
            }
        }

        buf[idx] = 0;
        g_Log(buf);
    }

    static void logDecU64(QC::u64 value)
    {
        if (!g_Log)
            return;

        char buf[21];
        QC::usize pos = 0;

        if (value == 0)
        {
            buf[pos++] = '0';
        }
        else
        {
            char tmp[20];
            QC::usize tmpPos = 0;
            while (value > 0 && tmpPos < sizeof(tmp))
            {
                tmp[tmpPos++] = static_cast<char>('0' + (value % 10));
                value /= 10;
            }
            while (tmpPos > 0)
            {
                buf[pos++] = tmp[--tmpPos];
            }
        }

        buf[pos] = 0;
        g_Log(buf);
    }

    static void logIPv4(QNet::IPv4Address addr)
    {
        if (!g_Log)
            return;

        logDecU64(addr.octets[0]);
        g_Log(".");
        logDecU64(addr.octets[1]);
        g_Log(".");
        logDecU64(addr.octets[2]);
        g_Log(".");
        logDecU64(addr.octets[3]);
    }

    [[noreturn]] static void enterTerminalOnlyLoop()
    {
        QK::Console::setOwner(QK::Console::Owner::TerminalOnly);
        QK::Console::setSafeFallbackEnabled(true);
        g_Log("Entering console-only startup path (mode: ");
        g_Log(QK::Boot::Config::StartupModeName(QK::Boot::Config::GetStartupMode()));
        g_Log(")\r\n");
        QK::Console::showPrompt();

        while (true)
        {
            // Pump the event queue so ShutdownRequest and other system events are handled
            // even when the desktop runtime loop is not running.
            QK::Event::EventManager::instance().processEvents(0);
            QNet::Stack::instance().poll(QDrv::Timer::instance().milliseconds());
            QKDrv::Manager::instance().poll();
            QKDrv::PS2::Keyboard::instance().poll();
            asm volatile("hlt");
        }
    }

} // namespace

namespace QK::Boot::Desktop
{
    bool PrepareFromLimineRequests(QC::u64 FramebufferRequest[], QC::u64 ModuleRequest[], const EarlyHeap &Heap, FLogFn Log)
    {
        g_Log = Log;
        if (!g_Log)
            return false;

        QK::Console::setOwner(QK::Console::Owner::Boot);

        g_State.FramebufferRequest = FramebufferRequest;
        g_State.ModuleRequest = ModuleRequest;
        g_State.Heap = Heap;

        // Draw to Limine framebuffer if available.
        // Access the framebuffer response from our Limine request.
        QC::u64 *fb_response = reinterpret_cast<QC::u64 *>(FramebufferRequest[5]);

        if (fb_response == nullptr)
        {
            g_Log("No framebuffer response!\r\n");
            return false;
        }

        g_Log("Framebuffer response received!\r\n");

        // Limine response structure:
        // [0] = revision
        // [1] = framebuffer_count
        // [2] = framebuffers array pointer
        QC::u64 revision = fb_response[0];
        QC::u64 fb_count = fb_response[1];

        g_Log("  Revision: ");
        logDecU64(revision);
        g_Log("\r\n");

        g_Log("  Count: ");
        logDecU64(fb_count);
        g_Log("\r\n");

        if (fb_count == 0)
        {
            g_Log("No framebuffers available!\r\n");
            return false;
        }

        g_Log("Getting framebuffer pointer...\r\n");

        // Get framebuffers array (pointer to pointer)
        QC::u64 **fb_array = reinterpret_cast<QC::u64 **>(fb_response[2]);
        g_Log("Got fb_array\r\n");

        // Get first framebuffer struct
        QC::u64 *fb = fb_array[0];
        g_Log("Got fb struct\r\n");

        // Limine framebuffer struct layout:
        // [0] = address (void*)
        // [1] = width (uint64_t)
        // [2] = height (uint64_t)
        // [3] = pitch (uint64_t)
        // [4] = bpp (uint16_t, but padded)
        g_State.FbAddress = static_cast<QC::uptr>(fb[0]);
        g_State.Width = static_cast<QC::u32>(fb[1]);
        g_State.Height = static_cast<QC::u32>(fb[2]);
        g_State.Pitch = static_cast<QC::u32>(fb[3]);

        g_State.Prepared = true;
        return true;
    }

    void InitializeInput()
    {
        if (!g_State.Prepared)
        {
            if (g_Log)
                g_Log("Desktop: InitializeInput called before Prepare\r\n");
            return;
        }

        QK::Console::setOwner(QK::Console::Owner::Boot);
        if (g_State.InputInitialized)
            return;

        const QC::u32 width = g_State.Width;
        const QC::u32 height = g_State.Height;
        const QC::u64 inputStartMs = QDrv::Timer::instance().milliseconds();

        g_Log("Initializing QWindowing...\r\n");

        // Initialize heap first - required for memory allocations.
        // Heap is normally initialized during early boot; this call is a safe fallback.
        if (!QK::Memory::Heap::instance().isInitialized())
        {
            g_Log("Initializing heap...\r\n");
            QK::Memory::Heap::instance().initialize(g_State.Heap.Buffer, g_State.Heap.Size);
                // Once the desktop takes over the framebuffer, disable the boot-time
                // framebuffer text mirror (if enabled) so serial logs don't scribble
                // over the GUI.
                QK::Debug::FramebufferText::SetEnabled(false);
            g_Log("Heap initialized\r\n");
        }

        g_Log("Bringing up filesystem...\r\n");
        if (QK::Boot::Ramdisk::InitializeFromLimineModules(g_State.ModuleRequest, g_Log))
        {
            g_Log("Filesystem ready\r\n");
        }
        else
        {
            g_Log("Filesystem initialization failed\r\n");
        }

        // Initialize the event system.
        QK::Event::EventManager::instance().initialize();
        g_Log("Event system initialized\r\n");

        // Bring up shutdown controller early so it can register event listeners.
        QK::Shutdown::Controller::instance();
        g_Log("Shutdown controller ready\r\n");

        // Initialize timer (higher tick reduces input polling latency).
        g_Log("Initializing timer...\r\n");
        QDrv::Timer::instance().initialize(1000);
        g_Log("Timer initialized\r\n");

        // Initialize PCI bus and enumerate devices.
        g_Log("Initializing PCI...\r\n");
        QArch::PCI::instance().initialize();
        g_Log("PCI initialized\r\n");

        // Initialize driver manager (probes USB and PS/2).
        g_Log("Initializing drivers...\r\n");
        const QC::u64 driversStartMs = QDrv::Timer::instance().milliseconds();
        QKDrv::Manager::instance().setScreenSize(width, height);
        QKDrv::Manager::instance().initialize();
        g_Log("Drivers initialized\r\n");
        logBootMetric("drivers_init", driversStartMs);

        if (!QK::SecurityCenter::instance().initialized())
            QK::SecurityCenter::instance().initialize(QK::Boot::Config::GetSecurityCenterMode());

        // Allow subsystems/commands to pump RX/IO without pulling driver headers into QKernel.
        QK::System::setPumpFn([]()
                     {
                         const QC::u64 nowMs = QDrv::Timer::instance().milliseconds();
                         QNet::Stack::instance().poll(nowMs);
                         QKDrv::Manager::instance().poll();
                     });

        QKStorage::probeLimineModules();

        // Set up keyboard callback so the console works in every startup mode.
        g_Log("Setting up keyboard...\r\n");
        auto *kbd = QKDrv::Manager::instance().keyboardDriver();
        if (!kbd)
        {
            g_Log("WARNING: No keyboard driver available\r\n");
        }
        else
        {
            kbd->setCallback([](const QKDrv::KeyboardReport &rep)
                             {
                                 dispatchDesktopKeyboardReport(rep, true);
                             });

            if (kbd->controllerType() != QKDrv::ControllerType::PS2)
            {
                QKDrv::PS2::Keyboard::instance().setCallback([](const QKDrv::KeyboardReport &rep)
                                                             {
                                                                 if (!shouldUsePs2KeyboardFallback())
                                                                     return;
                                                                 dispatchDesktopKeyboardReport(rep, false);
                                                             });
            }
        }
        g_Log("Keyboard initialized\r\n");

        // Keep console input enabled through startup gates (owner enrollment/unlock).
        // We hand keyboard ownership to desktop only after the pre-desktop gate succeeds.
        KeepConsoleOwnershipWithKernelForStartupGates();

        g_Log("Pre-desktop phase: SecureStore unlock\r\n");
        EnsureNonTpmSecureStoreUnlocked();

        if (QK::SecurityCenter::instance().mode() == QK::SecurityCenter::Mode::Enforce &&
            !QFS::VolumeManager::instance().isMounted("QFS_SYSTEM"))
        {
            g_Log("BootTrust gate: /system volume is not mounted\r\n");
            g_Log("Switching startup mode to INSTALLER\r\n");
            QK::Boot::Config::SetStartupMode(QK::Boot::Config::StartupMode::Installer);
            QK::Boot::Events::Emit("boottrust", "missing_system_installer", "mode=enforce");
            QK::Console::write("Installer: persistent system volume not found.\r\n");
            QK::Console::write("Installer: run 'help', then 'admin' twice, then 'sysmount' or 'sysformat'.\r\n");
            QK::Console::setInputEnabled(true);
            enterTerminalOnlyLoop();
        }

        // Ensure SST exists after /system is mounted from the system volume.
        const QC::u64 ensureSstStartMs = QDrv::Timer::instance().milliseconds();
        const QC::Status ensureSstSt = QK::SecurityCenter::instance().ensureSst();
        logBootMetric("ensure_sst", ensureSstStartMs);
        if (ensureSstSt != QC::Status::Success)
        {
            g_Log("BootTrust gate failed: ensureSst did not pass\r\n");
            g_Log("Switching startup mode to TERMINAL\r\n");
            QK::Boot::Config::SetStartupMode(QK::Boot::Config::StartupMode::Terminal);
            QK::Boot::Events::Emit("boottrust", "ensure_sst_terminal", statusText(ensureSstSt));
            QK::Console::write("Security Center: system trust store is not ready.\r\n");
            QK::Console::write("Security Center: ensureSst status=");
            QK::Console::write(statusText(ensureSstSt));
            QK::Console::write("\r\n");
            QK::Console::write("Security Center: securestore tpm=");
            QK::Console::write(QK::SecureStore::tpm_present() ? "on" : "off");
            QK::Console::write(" wrapkey.kdf=");
            QK::Console::write(QK::SecureStore::exists("WRAPKEY.KDF") ? "yes" : "no");
            QK::Console::write(" wrapkey.tpm=");
            QK::Console::write(QK::SecureStore::exists("WRAPKEY.TPM") ? "yes" : "no");
            QK::Console::write(" sstwrap=");
            QK::Console::write(QK::SecureStore::exists("SSTWRAP") ? "yes" : "no");
            QK::Console::write("\r\n");
            QK::Console::write("Security Center: desktop startup skipped; staying in terminal mode.\r\n");
            QK::Console::setInputEnabled(true);
            enterTerminalOnlyLoop();
        }

        // Mark input initialized even for console-only startup modes so a later
        // `startx` can safely bring up the window system.
        g_State.InputInitialized = true;

        // v1 boot trust gate: require TAS/SST-backed SC integrity when enforcement is active.
        if (QK::SecurityCenter::instance().mode() == QK::SecurityCenter::Mode::Enforce)
        {
            const auto sst = QSC::SecurityCenter::instance().sstStatus();
            if (!sst.available || sst.generation == 0)
            {
                g_Log("BootTrust gate failed: TAS/SC integrity check did not pass\r\n");
                g_Log("Switching startup mode to TERMINAL\r\n");
                QK::Boot::Config::SetStartupMode(QK::Boot::Config::StartupMode::Terminal);
                QK::Boot::Events::Emit("boottrust", "integrity_gate_terminal", "sst-unavailable-or-zero-generation");
                QK::Console::write("Security Center: boot trust gate did not pass.\r\n");
                QK::Console::write("Security Center: desktop startup skipped; staying in terminal mode.\r\n");
                QK::Console::setInputEnabled(true);
                enterTerminalOnlyLoop();
            }
        }

        if (QK::Boot::Config::GetStartupMode() != QK::Boot::Config::StartupMode::Desktop)
        {
            g_Log("Startup mode ");
            g_Log(QK::Boot::Config::StartupModeName(QK::Boot::Config::GetStartupMode()));
            g_Log(" selected - skipping desktop bring-up\r\n");
            enterTerminalOnlyLoop();
        }

        // Pre-desktop owner registration/login gate: enrollment/unlock must complete
        // before desktop initialization.
        g_Log("Pre-desktop phase: owner session gate\r\n");
        RunPreDesktopOwnerGate();
        // Do not emit a permanent BOOTMETRIC here: this interval includes human typing time,
        // so it is useful for one-off diagnosis but misleading as a stable boot metric.

        RunPreDesktopDhcpBringUp();

        // Desktop now owns keyboard input; keep serial console non-interactive.
        HandOffConsoleOwnershipToDesktop();
        // Do not emit a permanent BOOTMETRIC for InitializeInput total time because it includes
        // interactive SecureStore/owner gating and is not a machine-only stage duration.
    }

    bool IsPrepared() { return g_State.Prepared; }
    bool IsInputInitialized() { return g_State.InputInitialized; }
    bool IsWindowSystemInitialized() { return g_State.WindowSystemInitialized; }
    bool IsDesktopInitialized() { return g_State.DesktopInitialized; }

    bool RequestStopDesktop()
    {
        if (!g_State.DesktopInitialized)
            return false;
        g_stopDesktopRequested = true;
        return true;
    }

    void InitializeWindowSystem()
    {
        if (!g_State.Prepared)
        {
            if (g_Log)
                g_Log("Desktop: InitializeWindowSystem called before Prepare\r\n");
            return;
        }
        if (!g_State.InputInitialized)
        {
            if (g_Log)
                g_Log("Desktop: InitializeWindowSystem called before Input init\r\n");
            return;
        }
        if (g_State.WindowSystemInitialized)
            return;

        const QC::u64 windowSystemStartMs = QDrv::Timer::instance().milliseconds();

        QC::u32 width = g_State.Width;
        QC::u32 height = g_State.Height;
        QC::u32 pitch = g_State.Pitch;
        const QC::uptr fbAddress = g_State.FbAddress;

        // Create and initialize framebuffer.
        // If we're running under VMware SVGA II (QEMU `-vga vmware`), the device exposes
        // the authoritative pitch via SVGA_REG_BYTES_PER_LINE. Compare it with Limine's
        // pitch and use SVGA's value when it looks safer.
        {
            auto &svga = QDrv::VmwareSVGA::instance();
            if (svga.initialize())
            {
                const QC::u32 svgaPitch = svga.bytesPerLine();
                const QC::u32 svgaFbSize = svga.framebufferSizeBytes();
                QC_LOG_INFO("QKMain", "Framebuffer pitch: limine=%u svga=%u (fb_size=%u)", pitch, svgaPitch, svgaFbSize);

                const QC::u32 minPitch = width * 4u; // ARGB8888
                if (svgaPitch >= minPitch && svgaPitch <= (1024u * 1024u))
                {
                    const QC::u64 needed = static_cast<QC::u64>(svgaPitch) * static_cast<QC::u64>(height);
                    if (svgaFbSize == 0 || needed <= svgaFbSize)
                    {
                        if (svgaPitch != pitch)
                        {
                            QC_LOG_WARN("QKMain", "Overriding Limine pitch %u -> SVGA bytes-per-line %u", pitch, svgaPitch);
                            pitch = svgaPitch;
                        }
                    }
                    else
                    {
                        QC_LOG_WARN("QKMain", "SVGA pitch rejected: need=%llu > fb_size=%u", (unsigned long long)needed, svgaFbSize);
                    }
                }
            }
        }

        g_Framebuffer.initialize(fbAddress, width, height, pitch, QW::PixelFormat::ARGB8888);
        g_Log("Framebuffer initialized\r\n");

        // Initialize window manager.
        g_Log("About to initialize WindowManager...\r\n");
        QW::WindowManager::instance().initialize(&g_Framebuffer);
        g_Log("WindowManager initialized\r\n");

        // Get active mouse driver from manager.
        g_Log("Setting up mouse...\r\n");
        auto *mouseDriver = QKDrv::Manager::instance().mouseDriver();

        // Debug: Print screen dimensions.
        g_Log("Screen: ");
        char dimBuf[32];
        int idx = 0;
        if (width >= 1000)
            dimBuf[idx++] = '0' + (width / 1000) % 10;
        if (width >= 100)
            dimBuf[idx++] = '0' + (width / 100) % 10;
        if (width >= 10)
            dimBuf[idx++] = '0' + (width / 10) % 10;
        dimBuf[idx++] = '0' + width % 10;
        dimBuf[idx++] = 'x';
        if (height >= 1000)
            dimBuf[idx++] = '0' + (height / 1000) % 10;
        if (height >= 100)
            dimBuf[idx++] = '0' + (height / 100) % 10;
        if (height >= 10)
            dimBuf[idx++] = '0' + (height / 10) % 10;
        dimBuf[idx++] = '0' + height % 10;
        dimBuf[idx++] = '\r';
        dimBuf[idx++] = '\n';
        dimBuf[idx] = 0;
        g_Log(dimBuf);

        // Debug: Print button location.
        g_Log("Button at: ");
        idx = 0;
        QC::u32 btnX = width - 120;
        if (btnX >= 1000)
            dimBuf[idx++] = '0' + (btnX / 1000) % 10;
        if (btnX >= 100)
            dimBuf[idx++] = '0' + (btnX / 100) % 10;
        if (btnX >= 10)
            dimBuf[idx++] = '0' + (btnX / 10) % 10;
        dimBuf[idx++] = '0' + btnX % 10;
        dimBuf[idx++] = ',';
        dimBuf[idx++] = '1';
        dimBuf[idx++] = '0';
        dimBuf[idx++] = '-';
        dimBuf[idx++] = '4';
        dimBuf[idx++] = '0';
        dimBuf[idx++] = '\r';
        dimBuf[idx++] = '\n';
        dimBuf[idx] = 0;
        g_Log(dimBuf);

        if (mouseDriver != nullptr)
        {
            mouseDriver->setCallback([](const QKDrv::MouseReport &report)
                                     {
                                         auto *mouse = QKDrv::Manager::instance().mouseDriver();
                                         if (!mouse)
                                             return;
                                         dispatchDesktopMouseReport(report, *mouse, true);
                                     });

            if (mouseDriver->controllerType() != QKDrv::ControllerType::PS2)
            {
                QKDrv::PS2::Mouse::instance().setCallback([](const QKDrv::MouseReport &report)
                                                          {
                                                              if (!shouldUsePs2MouseFallback())
                                                                  return;
                                                              dispatchDesktopMouseReport(report,
                                                                                         QKDrv::PS2::Mouse::instance(),
                                                                                         false);
                                                          });
            }
        }
        g_Log("Mouse configured\r\n");

        // Seed initial cursor position immediately so the hardware cursor isn't stuck at (0,0)
        // until the first mouse movement packet arrives.
        if (auto *drv = QKDrv::Manager::instance().mouseDriver())
        {
            const QC::i32 seedX = drv->x();
            const QC::i32 seedY = drv->y();
            QK::Event::EventManager::instance().postMouseMove(seedX, seedY, 0, 0);
            QK::Event::EventManager::instance().processEvents();
        }

        // Create desktop using QDDesktop module.
        g_State.WindowSystemInitialized = true;
        logBootMetric("window_system_init", windowSystemStartMs);
    }

    [[noreturn]] void InitializeDesktopAndRunLoop()
    {
        if (!g_State.Prepared)
        {
            if (g_Log)
                g_Log("Desktop: InitializeDesktop called before Prepare\r\n");
            for (;;)
                asm volatile("hlt");
        }
        if (!g_State.WindowSystemInitialized)
        {
            if (g_Log)
                g_Log("Desktop: InitializeDesktop called before WindowSystem init\r\n");
            for (;;)
                asm volatile("hlt");
        }

        if (!g_State.DesktopInitialized)
        {
            const QC::u32 width = g_State.Width;
            const QC::u32 height = g_State.Height;
            const QC::u64 desktopInitStartMs = QDrv::Timer::instance().milliseconds();

            g_Log("Creating desktop...\r\n");
            g_Desktop.initialize(width, height);
            g_Log("Desktop initialized\r\n");
            logBootMetric("desktop_init", desktopInitStartMs);

            // Trigger an initial paint via the normal window invalidation path.
            // Avoid repainting the entire desktop every loop (can add input latency).
            if (g_Desktop.window())
            {
                g_Desktop.window()->invalidate();
            }

            // Initial render.
            const QC::u64 firstRenderStartMs = QDrv::Timer::instance().milliseconds();
            QW::WindowManager::instance().render();
            g_Log("Initial render complete!\r\n");
            logBootMetric("desktop_first_render", firstRenderStartMs);

            // Register keyboard listener for Ctrl+Q shutdown.
            g_CtrlQListener.categoryMask = QK::Event::Category::Input;
            g_CtrlQListener.eventType = QK::Event::Type::KeyDown;
            g_CtrlQListener.handler = [](const QK::Event::Event &event, void *) -> bool
            {
                const auto &key = event.asKey();

                // Check for Q key with Ctrl modifier.
                if (key.keycode == static_cast<QC::u8>(QKDrv::PS2::Key::Q) &&
                    QK::Event::hasModifier(key.modifiers, QK::Event::Modifiers::Ctrl))
                {
                    g_Log("Ctrl+Q pressed - requesting shutdown!\r\n");
                    QK::Event::EventManager::instance().postShutdownEvent(
                        QK::Event::Type::ShutdownRequest,
                        static_cast<QC::u32>(QK::Shutdown::Reason::KeyboardShortcut));
                    return true;
                }
                return false;
            };
            g_CtrlQListener.userData = nullptr;
            g_CtrlQId = QK::Event::EventManager::instance().addListener(g_CtrlQListener);
            if (g_CtrlQId == QK::Event::InvalidListenerId)
            {
                g_Log("ERROR: Failed to register Ctrl+Q listener!\r\n");
            }
            else
            {
                g_Log("Ctrl+Q shutdown listener registered\r\n");
            }

            g_State.DesktopInitialized = true;
        }

        // Main loop - process events and render.
        g_Log("Entering main loop...\r\n");

        while (true)
        {
            if (g_stopDesktopRequested)
            {
                g_Log("stopx: tearing down desktop\r\n");
                g_Desktop.shutdown();
                QW::WindowManager::instance().shutdown();
                g_State.DesktopInitialized = false;
                g_State.WindowSystemInitialized = false;
                QK::Console::setInputEnabled(true);
                QK::Debug::FramebufferText::SetEnabled(true);
                g_stopDesktopRequested = false;
                g_Log("stopx: returning to terminal-only mode\r\n");
                enterTerminalOnlyLoop();
            }

            // Poll all active drivers.
            QKDrv::Manager::instance().poll();

            auto &eventMgr = QK::Event::EventManager::instance();
            auto &wm = QW::WindowManager::instance();

            maybePostBackspaceRepeat();

            // Process a bounded batch, then give rendering a chance.
            // This keeps pointer hover/press visuals responsive even under
            // high-frequency mouse reports.
            for (;;)
            {
                const QC::usize processed = eventMgr.processEvents(16);

                if (wm.needsRender())
                {
                    wm.render();
                }

                // No more immediate work to do; exit the inner loop and let the
                // idle pacing logic decide how long to wait.
                if (processed == 0 && !wm.needsRender())
                {
                    break;
                }
            }

            // Idle pacing:
            // Previously this loop used `hlt` to wait for the next interrupt.
            // In practice, when the next interrupt is delayed (e.g., sparse input,
            // timer issues, or interrupt masking in drivers), this can stall UI
            // presentation for multi-second intervals.
            //
            // Instead, run a small periodic tick that:
            // - keeps driver polling/network alive,
            // - processes events,
            // - forces a render at a predictable cadence when idle.
            //
            // Minimal UX: target ~60Hz, but keep CPU usage bounded with short sleeps.
            static constexpr QC::u64 kFrameIntervalMs = 16;
            const QC::u64 nowMs = QDrv::Timer::instance().milliseconds();
            static QC::u64 s_nextFrameMs = 0;
            if (s_nextFrameMs == 0)
                s_nextFrameMs = nowMs;

            if (nowMs >= s_nextFrameMs)
            {
                // Force a compositor tick so hover/cursor/pointer feedback stays snappy.
                wm.render();
                s_nextFrameMs = nowMs + kFrameIntervalMs;
            }
            else
            {
                // Sleep in short slices so input interrupts can still wake us quickly.
                const QC::u64 remainingMs = s_nextFrameMs - nowMs;
                const QC::u64 sleepMs = (remainingMs > 2) ? 2 : remainingMs;
                if (sleepMs > 0)
                    QDrv::Timer::instance().sleep(static_cast<QC::u32>(sleepMs));
            }
        }
    }

    bool RunFromLimineRequests(QC::u64 FramebufferRequest[], QC::u64 ModuleRequest[], const EarlyHeap &Heap, FLogFn Log)
    {
        if (!PrepareFromLimineRequests(FramebufferRequest, ModuleRequest, Heap, Log))
            return false;

        InitializeInput();
        InitializeWindowSystem();
        InitializeDesktopAndRunLoop();
    }

} // namespace QK::Boot::Desktop
