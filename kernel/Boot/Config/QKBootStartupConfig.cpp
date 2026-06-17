#include "QKBootStartupConfig.h"

#include "QCBuiltins.h"
#include "QCString.h"
#include "QFSFile.h"
#include "QFSVFS.h"
#include "QFSVolumeManager.h"
#include "QKInputSettings.h"
#include "QKConsole.h"
#include "QKShutdownController.h"

namespace QK::Boot::Config
{
    namespace
    {
        static StartupMode g_StartupMode = StartupMode::Desktop;
        static QK::SecurityCenter::Mode g_ScMode = QK::SecurityCenter::Mode::Bypass;
        static bool g_IdeSharedProbeEnabled = false;

        static bool g_HasCmdlineStartupModeOverride = false;
        static StartupMode g_CmdlineStartupModeOverride = StartupMode::Desktop;

        static char g_CmdlineRecoveryCode[96] = {0};
        static bool g_HasCmdlineRecoveryCode = false;

        static char g_BootSaveTermValue[256] = {0};
        static bool g_PowerOffAfterSaveTerm = false;
        static bool g_BootSaveTermDone = false;

        static void LogStr(FLogFn Log, const char *Msg)
        {
            if (Log)
                Log(Msg);
        }

        static bool isWhitespace(char ch)
        {
            return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
        }

        static bool isSpaceOnly(char ch)
        {
            return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
        }

        static QC::usize stringLength(const char *str)
        {
            if (!str)
                return 0;

            QC::usize len = 0;
            while (str[len] != '\0')
            {
                ++len;
            }
            return len;
        }

        static char toLowerChar(char ch)
        {
            if (ch >= 'A' && ch <= 'Z')
                return static_cast<char>(ch + ('a' - 'A'));
            return ch;
        }

        static bool equalsIgnoreCase(const char *a, const char *b)
        {
            if (!a || !b)
                return false;

            while (*a && *b)
            {
                if (toLowerChar(*a) != toLowerChar(*b))
                    return false;
                ++a;
                ++b;
            }

            return *a == '\0' && *b == '\0';
        }

        static void trimTrailingWhitespace(char *str)
        {
            if (!str)
                return;

            QC::isize len = static_cast<QC::isize>(stringLength(str));
            while (len > 0 && isWhitespace(str[len - 1]))
            {
                str[len - 1] = '\0';
                --len;
            }
        }

        static char *trimWhitespace(char *str)
        {
            if (!str)
                return str;

            while (*str && isWhitespace(*str))
            {
                ++str;
            }

            trimTrailingWhitespace(str);
            return str;
        }

        static void stripInlineComment(char *value)
        {
            if (!value)
                return;

            for (char *ptr = value; *ptr; ++ptr)
            {
                if (*ptr == '#' || *ptr == ';' || (*ptr == '/' && *(ptr + 1) == '/'))
                {
                    *ptr = '\0';
                    break;
                }
            }

            trimTrailingWhitespace(value);
        }

        static StartupMode parseStartupModeValue(FLogFn Log, const char *value)
        {
            if (!value || *value == '\0')
                return StartupMode::Desktop;
            if (equalsIgnoreCase(value, "DESKTOP"))
                return StartupMode::Desktop;
            if (equalsIgnoreCase(value, "TERMINAL"))
                return StartupMode::Terminal;
            if (equalsIgnoreCase(value, "SAFE"))
                return StartupMode::Safe;
            if (equalsIgnoreCase(value, "RECOVERY"))
                return StartupMode::Recovery;
            if (equalsIgnoreCase(value, "INSTALLER"))
                return StartupMode::Installer;
            if (equalsIgnoreCase(value, "NETWORK"))
                return StartupMode::Network;

            LogStr(Log, "Unknown startup MODE value: ");
            LogStr(Log, value);
            LogStr(Log, " (defaulting to DESKTOP)\r\n");
            return StartupMode::Desktop;
        }

        static bool tokenEqualsIgnoreCase(const char *begin, const char *end, const char *lit)
        {
            if (!begin || !end || end < begin || !lit)
                return false;

            const char *p = begin;
            const char *q = lit;
            while (p < end && *q)
            {
                char a = toLowerChar(*p++);
                char b = toLowerChar(*q++);
                if (a != b)
                    return false;
            }

            return (p == end) && (*q == '\0');
        }

        static void setCmdlineStartupModeOverride(FLogFn Log, StartupMode Mode)
        {
            g_HasCmdlineStartupModeOverride = true;
            g_CmdlineStartupModeOverride = Mode;

            LogStr(Log, "Startup mode override from cmdline: ");
            LogStr(Log, StartupModeName(Mode));
            LogStr(Log, "\r\n");
        }

        static bool parseBoolValue(const char *value, bool defaultValue)
        {
            if (!value || *value == '\0')
                return defaultValue;

            if (equalsIgnoreCase(value, "1") || equalsIgnoreCase(value, "TRUE") || equalsIgnoreCase(value, "YES") ||
                equalsIgnoreCase(value, "ON"))
                return true;
            if (equalsIgnoreCase(value, "0") || equalsIgnoreCase(value, "FALSE") || equalsIgnoreCase(value, "NO") ||
                equalsIgnoreCase(value, "OFF"))
                return false;

            return defaultValue;
        }

        static bool parseU32Value(const char *value, QC::u32 &out)
        {
            out = 0;
            if (!value || *value == '\0')
                return false;

            for (const char *p = value; *p; ++p)
            {
                if (*p < '0' || *p > '9')
                    return false;
                out = out * 10u + static_cast<QC::u32>(*p - '0');
            }

            return true;
        }

        static bool appendU32(char *dest, QC::usize destCap, QC::u32 value)
        {
            if (!dest || destCap == 0)
                return false;

            char tmp[16];
            QC::String::memset(tmp, 0, sizeof(tmp));
            int idx = 0;
            if (value == 0)
            {
                tmp[idx++] = '0';
            }
            else
            {
                char rev[16];
                int ridx = 0;
                while (value > 0 && ridx < 15)
                {
                    rev[ridx++] = static_cast<char>('0' + (value % 10u));
                    value /= 10u;
                }
                while (ridx > 0)
                    tmp[idx++] = rev[--ridx];
            }
            tmp[idx] = '\0';

            const QC::usize used = QC::String::strlen(dest);
            if (used >= destCap)
                return false;
            QC::String::strncpy(dest + used, tmp, destCap - 1 - used);
            return true;
        }

            static bool devRecoveryCodeOverrideAllowed()
            {
        #if defined(CITADEL_PRODUCTION) && (CITADEL_PRODUCTION != 0)
                return false;
        #else
                return true;
        #endif
            }

        static QK::SecurityCenter::Mode parseScModeValue(FLogFn Log, const char *value)
        {
            if (!value || *value == '\0')
                return QK::SecurityCenter::Mode::Bypass;

            if (equalsIgnoreCase(value, "BYPASS"))
                return QK::SecurityCenter::Mode::Bypass;
            if (equalsIgnoreCase(value, "ENFORCE"))
                return QK::SecurityCenter::Mode::Enforce;

            LogStr(Log, "Unknown SC_MODE value: ");
            LogStr(Log, value);
            LogStr(Log, " (defaulting to BYPASS)\r\n");
            return QK::SecurityCenter::Mode::Bypass;
        }

        static void handleStartupConfigLine(FLogFn Log, char *line)
        {
            if (!line)
                return;

            char *trimmed = trimWhitespace(line);
            if (*trimmed == '\0')
                return;

            if (*trimmed == '#' || (*trimmed == '/' && *(trimmed + 1) == '/'))
                return;

            char *key = nullptr;
            char *value = nullptr;
            char *delimiter = nullptr;
            for (char *ptr = trimmed; *ptr; ++ptr)
            {
                if (*ptr == '=')
                {
                    delimiter = ptr;
                    break;
                }
            }

            if (delimiter)
            {
                *delimiter = '\0';
                key = trimWhitespace(trimmed);
                value = trimWhitespace(delimiter + 1);
            }
            else
            {
                // Support whitespace-delimited key/value pairs like "MODE TERMINAL"
                char *split = trimmed;
                while (*split && !isWhitespace(*split))
                {
                    ++split;
                }

                if (!*split)
                    return;

                *split = '\0';
                key = trimWhitespace(trimmed);
                value = trimWhitespace(split + 1);
            }

            stripInlineComment(value);

            if (*key == '\0' || *value == '\0')
                return;

            if (equalsIgnoreCase(key, "MODE"))
            {
                g_StartupMode = parseStartupModeValue(Log, value);
                return;
            }

            if (equalsIgnoreCase(key, "SC_MODE"))
            {
                g_ScMode = parseScModeValue(Log, value);
                return;
            }

            if (equalsIgnoreCase(key, "SC_BYPASS"))
            {
                const bool bypass = parseBoolValue(value, true);
                g_ScMode = bypass ? QK::SecurityCenter::Mode::Bypass : QK::SecurityCenter::Mode::Enforce;
                return;
            }

            if (equalsIgnoreCase(key, "IDE_SHARED"))
            {
                g_IdeSharedProbeEnabled = parseBoolValue(value, false);
                return;
            }

            if (equalsIgnoreCase(key, "SAVETERM"))
            {
                QC::String::strncpy(g_BootSaveTermValue, value, sizeof(g_BootSaveTermValue) - 1);
                g_BootSaveTermValue[sizeof(g_BootSaveTermValue) - 1] = '\0';
                g_BootSaveTermDone = false;
                return;
            }

            if (equalsIgnoreCase(key, "MOUSE_SENSITIVITY"))
            {
                QC::u32 percent = 0;
                if (parseU32Value(value, percent))
                    QKDrv::Input::setMouseSensitivityPercent(percent);
                return;
            }

            if (equalsIgnoreCase(key, "MOUSE_USB_RELATIVE_PERCENT"))
            {
                QC::u32 percent = 0;
                if (parseU32Value(value, percent))
                    QKDrv::Input::setMouseUsbRelativePercent(percent);
                return;
            }

            if (equalsIgnoreCase(key, "MOUSE_PS2_RELATIVE_PERCENT"))
            {
                QC::u32 percent = 0;
                if (parseU32Value(value, percent))
                    QKDrv::Input::setMousePs2RelativePercent(percent);
                return;
            }

            if (equalsIgnoreCase(key, "MOUSE_WHEEL_LINES"))
            {
                QC::u32 lines = 0;
                if (parseU32Value(value, lines))
                    QKDrv::Input::setMouseWheelLines(lines);
                return;
            }

            if (equalsIgnoreCase(key, "MOUSE_INVERT_WHEEL"))
            {
                QKDrv::Input::setMouseInvertWheel(parseBoolValue(value, false));
                return;
            }

            if (equalsIgnoreCase(key, "KEY_REPEAT_DELAY_MS"))
            {
                QC::u32 delayMs = 0;
                if (parseU32Value(value, delayMs))
                    QKDrv::Input::setKeyboardRepeatDelayMs(delayMs);
                return;
            }

            if (equalsIgnoreCase(key, "KEY_REPEAT_INTERVAL_MS"))
            {
                QC::u32 intervalMs = 0;
                if (parseU32Value(value, intervalMs))
                    QKDrv::Input::setKeyboardRepeatIntervalMs(intervalMs);
                return;
            }

            if (equalsIgnoreCase(key, "POWEROFF_AFTER_SAVETERM"))
            {
                g_PowerOffAfterSaveTerm = parseBoolValue(value, false);
                return;
            }
        }

        static bool writeAll(QFS::File *file, const char *text)
        {
            if (!file || !text)
                return false;

            const QC::usize total = QC::String::strlen(text);
            QC::usize off = 0;
            while (off < total)
            {
                const QC::isize n = file->write(text + off, total - off);
                if (n <= 0)
                    return false;
                off += static_cast<QC::usize>(n);
            }
            return true;
        }

        static bool writeKeyValueLine(QFS::File *file, const char *key, const char *value)
        {
            char line[320];
            QC::String::memset(line, 0, sizeof(line));
            QC::String::strncpy(line, key, sizeof(line) - 1);
            const QC::usize used = QC::String::strlen(line);
            if (used + 2 >= sizeof(line))
                return false;
            line[used] = ' ';
            line[used + 1] = '\0';
            QC::String::strncpy(line + used + 1, value, sizeof(line) - 2 - used);
            const QC::usize done = QC::String::strlen(line);
            if (done + 1 >= sizeof(line))
                return false;
            line[done] = '\n';
            line[done + 1] = '\0';
            return writeAll(file, line);
        }

        static QC::Status writeStartupConfig(FLogFn Log)
        {
            QFS::File *file = QFS::VFS::instance().open("/startup.cfg",
                                                        QFS::OpenMode::Write | QFS::OpenMode::Create | QFS::OpenMode::Truncate);
            if (!file)
                return QC::Status::Error;

            bool ok = writeKeyValueLine(file, "MODE", StartupModeName(g_StartupMode)) &&
                      writeKeyValueLine(file, "SC_MODE", QK::SecurityCenter::modeName(g_ScMode)) &&
                      writeKeyValueLine(file, "IDE_SHARED", g_IdeSharedProbeEnabled ? "ON" : "OFF");

            char percent[16];
            QC::String::memset(percent, 0, sizeof(percent));
            ok = ok && appendU32(percent, sizeof(percent), QKDrv::Input::mouseSensitivityPercent()) &&
                 writeKeyValueLine(file, "MOUSE_SENSITIVITY", percent);

              char mouseUsbRelativePercent[16];
              QC::String::memset(mouseUsbRelativePercent, 0, sizeof(mouseUsbRelativePercent));
              ok = ok && appendU32(mouseUsbRelativePercent, sizeof(mouseUsbRelativePercent), QKDrv::Input::mouseUsbRelativePercent()) &&
                  writeKeyValueLine(file, "MOUSE_USB_RELATIVE_PERCENT", mouseUsbRelativePercent);

              char mousePs2RelativePercent[16];
              QC::String::memset(mousePs2RelativePercent, 0, sizeof(mousePs2RelativePercent));
              ok = ok && appendU32(mousePs2RelativePercent, sizeof(mousePs2RelativePercent), QKDrv::Input::mousePs2RelativePercent()) &&
                  writeKeyValueLine(file, "MOUSE_PS2_RELATIVE_PERCENT", mousePs2RelativePercent);

              char mouseWheelLines[16];
              QC::String::memset(mouseWheelLines, 0, sizeof(mouseWheelLines));
              ok = ok && appendU32(mouseWheelLines, sizeof(mouseWheelLines), QKDrv::Input::mouseWheelLines()) &&
                  writeKeyValueLine(file, "MOUSE_WHEEL_LINES", mouseWheelLines);

              ok = ok && writeKeyValueLine(file, "MOUSE_INVERT_WHEEL", QKDrv::Input::mouseInvertWheel() ? "ON" : "OFF");

            char keyRepeatDelayMs[16];
            QC::String::memset(keyRepeatDelayMs, 0, sizeof(keyRepeatDelayMs));
            ok = ok && appendU32(keyRepeatDelayMs, sizeof(keyRepeatDelayMs), QKDrv::Input::keyboardRepeatDelayMs()) &&
                 writeKeyValueLine(file, "KEY_REPEAT_DELAY_MS", keyRepeatDelayMs);

            char keyRepeatIntervalMs[16];
            QC::String::memset(keyRepeatIntervalMs, 0, sizeof(keyRepeatIntervalMs));
            ok = ok && appendU32(keyRepeatIntervalMs, sizeof(keyRepeatIntervalMs), QKDrv::Input::keyboardRepeatIntervalMs()) &&
                 writeKeyValueLine(file, "KEY_REPEAT_INTERVAL_MS", keyRepeatIntervalMs);

            if (ok && g_BootSaveTermValue[0] != '\0')
                ok = writeKeyValueLine(file, "SAVETERM", g_BootSaveTermValue);

            if (ok)
                ok = writeKeyValueLine(file, "POWEROFF_AFTER_SAVETERM", g_PowerOffAfterSaveTerm ? "ON" : "OFF");

            QFS::VFS::instance().close(file);
            if (!ok)
                return QC::Status::Error;

            LogStr(Log, "startup.cfg updated\r\n");
            return QC::Status::Success;
        }
    } // namespace

    const char *StartupModeName(StartupMode Mode)
    {
        switch (Mode)
        {
        case StartupMode::Desktop:
            return "DESKTOP";
        case StartupMode::Terminal:
            return "TERMINAL";
        case StartupMode::Safe:
            return "SAFE";
        case StartupMode::Recovery:
            return "RECOVERY";
        case StartupMode::Installer:
            return "INSTALLER";
        case StartupMode::Network:
            return "NETWORK";
        default:
            return "UNKNOWN";
        }
    }

    void LoadFromVfs(FLogFn Log)
    {
        auto *vfs = &QFS::VFS::instance();

        QFS::File *file = vfs->open("/startup.cfg", QFS::OpenMode::Read);
        if (!file)
        {
            LogStr(Log, "startup.cfg not found; defaulting to DESKTOP\r\n");
            g_StartupMode = StartupMode::Desktop;
            return;
        }

        char chunk[128];
        char lineBuffer[256];
        QC::usize lineLength = 0;

        QC::isize bytesRead = 0;
        while ((bytesRead = file->read(chunk, sizeof(chunk))) > 0)
        {
            for (QC::isize i = 0; i < bytesRead; ++i)
            {
                char ch = chunk[i];
                if (ch == '\r')
                    continue;

                if (ch == '\n')
                {
                    lineBuffer[lineLength] = '\0';
                    handleStartupConfigLine(Log, lineBuffer);
                    lineLength = 0;
                    continue;
                }

                if (lineLength + 1 < sizeof(lineBuffer))
                {
                    lineBuffer[lineLength++] = ch;
                }
            }
        }

        if (lineLength > 0)
        {
            lineBuffer[lineLength] = '\0';
            handleStartupConfigLine(Log, lineBuffer);
        }

        vfs->close(file);

        LogStr(Log, "Startup mode loaded: ");
        LogStr(Log, StartupModeName(g_StartupMode));
        LogStr(Log, "\r\n");

        LogStr(Log, "Security Center mode loaded: ");
        LogStr(Log, QK::SecurityCenter::modeName(g_ScMode));
        LogStr(Log, "\r\n");

        LogStr(Log, "IDE_SHARED loaded: ");
        LogStr(Log, g_IdeSharedProbeEnabled ? "ON" : "OFF");
        LogStr(Log, "\r\n");

        if (g_HasCmdlineStartupModeOverride)
        {
            g_StartupMode = g_CmdlineStartupModeOverride;
            LogStr(Log, "Startup mode forced by cmdline: ");
            LogStr(Log, StartupModeName(g_StartupMode));
            LogStr(Log, "\r\n");
        }

        // Apply default console access policy per startup mode.
        // Keep a conservative default (user) unless the operator explicitly elevates.
        QK::Console::setRole(QC::Cmd::AccessLevel::User);

        // User-facing guidance.
        if (g_StartupMode == StartupMode::Terminal)
        {
            LogStr(Log, "\r\n=== TERMINAL MODE ===\r\n");
            LogStr(Log, "Role: user\r\n");
            LogStr(Log, "- View commands: help\r\n");
            LogStr(Log, "- Start desktop: startx\r\n");
            LogStr(Log, "- File changes require admin\r\n");
            LogStr(Log, "- Enable admin commands: admin enable (run twice)\r\n");
            LogStr(Log, "\r\n");
        }
        else if (g_StartupMode == StartupMode::Recovery)
        {
            LogStr(Log, "\r\n=== RESCUE MODE (RECOVERY) ===\r\n");
            LogStr(Log, "Role: user (protected)\r\n");
            LogStr(Log, "- View commands: help\r\n");
            LogStr(Log, "- Power off: shutdown\r\n");
            LogStr(Log, "- File changes require system\r\n");
            LogStr(Log, "- Enable system role: system enable (run twice)\r\n");
            LogStr(Log, "- Guided restore: recover config|desktop|services\r\n");
            LogStr(Log, "- Validate configs: validate\r\n");
            LogStr(Log, "- Reboot: reboot now\r\n");
            LogStr(Log, "\r\n");
        }
        else if (g_StartupMode == StartupMode::Installer)
        {
            LogStr(Log, "\r\n=== INSTALLER MODE ===\r\n");
            LogStr(Log, "Role: user\r\n");
            LogStr(Log, "- View commands: help\r\n");
            LogStr(Log, "- Enable admin commands: admin enable (run twice)\r\n");
            LogStr(Log, "- Probe and mount system volume: sysmount\r\n");
            LogStr(Log, "- Partition+format a blank system disk: sysformat\r\n");
            LogStr(Log, "- Reboot after mount/format: reboot now\r\n");
            LogStr(Log, "\r\n");
        }
    }

    void LoadFromCmdline(FLogFn Log, const char *Cmdline)
    {
        if (!Cmdline || *Cmdline == '\0')
            return;

        const char *p = Cmdline;
        while (*p)
        {
            while (*p && isSpaceOnly(*p))
                ++p;
            if (!*p)
                break;

            const char *tokBegin = p;
            while (*p && !isSpaceOnly(*p))
                ++p;
            const char *tokEnd = p;

            // Bare flags.
            if (tokenEqualsIgnoreCase(tokBegin, tokEnd, "terminal"))
            {
                setCmdlineStartupModeOverride(Log, StartupMode::Terminal);
                continue;
            }
            if (tokenEqualsIgnoreCase(tokBegin, tokEnd, "safe"))
            {
                setCmdlineStartupModeOverride(Log, StartupMode::Safe);
                continue;
            }
            if (tokenEqualsIgnoreCase(tokBegin, tokEnd, "recovery") || tokenEqualsIgnoreCase(tokBegin, tokEnd, "rescue"))
            {
                setCmdlineStartupModeOverride(Log, StartupMode::Recovery);
                continue;
            }
            if (tokenEqualsIgnoreCase(tokBegin, tokEnd, "installer"))
            {
                setCmdlineStartupModeOverride(Log, StartupMode::Installer);
                continue;
            }
            if (tokenEqualsIgnoreCase(tokBegin, tokEnd, "network"))
            {
                setCmdlineStartupModeOverride(Log, StartupMode::Network);
                continue;
            }
            if (tokenEqualsIgnoreCase(tokBegin, tokEnd, "desktop"))
            {
                setCmdlineStartupModeOverride(Log, StartupMode::Desktop);
                continue;
            }

            // key=value.
            const char *eq = tokBegin;
            while (eq < tokEnd && *eq != '=')
                ++eq;
            if (eq >= tokEnd || *eq != '=')
                continue;

            const char *keyBegin = tokBegin;
            const char *keyEnd = eq;
            const char *valBegin = eq + 1;
            const char *valEnd = tokEnd;
            if (valBegin >= valEnd)
                continue;

            if (tokenEqualsIgnoreCase(keyBegin, keyEnd, "citadel.mode") || tokenEqualsIgnoreCase(keyBegin, keyEnd, "citadel.startup"))
            {
                char valBuf[32];
                QC::usize n = static_cast<QC::usize>(valEnd - valBegin);
                if (n >= sizeof(valBuf))
                    n = sizeof(valBuf) - 1;
                QC::String::memcpy(valBuf, valBegin, n);
                valBuf[n] = '\0';
                setCmdlineStartupModeOverride(Log, parseStartupModeValue(Log, valBuf));
                continue;
            }

            if (tokenEqualsIgnoreCase(keyBegin, keyEnd, "citadel.recovery_code"))
            {
                if (!devRecoveryCodeOverrideAllowed())
                {
                    LogStr(Log, "Ignoring cmdline recovery_code override in production build\r\n");
                    continue;
                }

                QC::usize n = static_cast<QC::usize>(valEnd - valBegin);
                if (n >= sizeof(g_CmdlineRecoveryCode))
                    n = sizeof(g_CmdlineRecoveryCode) - 1;

                if (n == 0)
                {
                    g_CmdlineRecoveryCode[0] = '\0';
                    g_HasCmdlineRecoveryCode = false;
                    continue;
                }

                QC::String::memcpy(g_CmdlineRecoveryCode, valBegin, n);
                g_CmdlineRecoveryCode[n] = '\0';
                g_HasCmdlineRecoveryCode = true;
                LogStr(Log, "Dev cmdline recovery_code override loaded\r\n");
            }
        }
    }

    StartupMode GetStartupMode()
    {
        return g_StartupMode;
    }

    void SetStartupMode(StartupMode Mode)
    {
        g_StartupMode = Mode;

        // Runtime transitions (for example, missing /system -> installer) should
        // not be overridden by a stale cmdline mode later in the boot session.
        g_HasCmdlineStartupModeOverride = false;
    }

    QC::Status PersistStartupMode(StartupMode Mode, FLogFn Log)
    {
        g_StartupMode = Mode;
        return writeStartupConfig(Log);
    }

    QC::u32 GetMouseSensitivityPercent()
    {
        return QKDrv::Input::mouseSensitivityPercent();
    }

    void SetMouseSensitivityPercent(QC::u32 Percent)
    {
        QKDrv::Input::setMouseSensitivityPercent(Percent);
    }

    QC::Status PersistMouseSensitivityPercent(QC::u32 Percent, FLogFn Log)
    {
        QKDrv::Input::setMouseSensitivityPercent(Percent);
        return writeStartupConfig(Log);
    }

    QC::u32 GetMouseUsbRelativePercent()
    {
        return QKDrv::Input::mouseUsbRelativePercent();
    }

    QC::u32 GetMousePs2RelativePercent()
    {
        return QKDrv::Input::mousePs2RelativePercent();
    }

    QC::u32 GetMouseWheelLines()
    {
        return QKDrv::Input::mouseWheelLines();
    }

    bool GetMouseInvertWheel()
    {
        return QKDrv::Input::mouseInvertWheel();
    }

    void SetMouseUsbRelativePercent(QC::u32 Percent)
    {
        QKDrv::Input::setMouseUsbRelativePercent(Percent);
    }

    void SetMousePs2RelativePercent(QC::u32 Percent)
    {
        QKDrv::Input::setMousePs2RelativePercent(Percent);
    }

    void SetMouseWheelLines(QC::u32 Lines)
    {
        QKDrv::Input::setMouseWheelLines(Lines);
    }

    void SetMouseInvertWheel(bool Invert)
    {
        QKDrv::Input::setMouseInvertWheel(Invert);
    }

    QC::Status PersistMouseBehaviorConfig(QC::u32 UsbPercent,
                                          QC::u32 Ps2Percent,
                                          QC::u32 WheelLines,
                                          bool InvertWheel,
                                          FLogFn Log)
    {
        QKDrv::Input::setMouseUsbRelativePercent(UsbPercent);
        QKDrv::Input::setMousePs2RelativePercent(Ps2Percent);
        QKDrv::Input::setMouseWheelLines(WheelLines);
        QKDrv::Input::setMouseInvertWheel(InvertWheel);
        return writeStartupConfig(Log);
    }

    QC::u32 GetKeyboardRepeatDelayMs()
    {
        return QKDrv::Input::keyboardRepeatDelayMs();
    }

    QC::u32 GetKeyboardRepeatIntervalMs()
    {
        return QKDrv::Input::keyboardRepeatIntervalMs();
    }

    void SetKeyboardRepeatTiming(QC::u32 DelayMs, QC::u32 IntervalMs)
    {
        QKDrv::Input::setKeyboardRepeatTiming(DelayMs, IntervalMs);
    }

    QC::Status PersistKeyboardRepeatTiming(QC::u32 DelayMs, QC::u32 IntervalMs, FLogFn Log)
    {
        QKDrv::Input::setKeyboardRepeatTiming(DelayMs, IntervalMs);
        return writeStartupConfig(Log);
    }

    QK::SecurityCenter::Mode GetSecurityCenterMode()
    {
        return g_ScMode;
    }

    bool GetIdeSharedProbeEnabled()
    {
        return g_IdeSharedProbeEnabled;
    }

    bool TryConsumeDevRecoveryCode(char *out, QC::usize outCap)
    {
        if (!out || outCap == 0)
            return false;

        out[0] = '\0';

        if (!g_HasCmdlineRecoveryCode || g_CmdlineRecoveryCode[0] == '\0')
            return false;

        const QC::usize n = stringLength(g_CmdlineRecoveryCode);
        if (n + 1 > outCap)
            return false;

        QC::String::memcpy(out, g_CmdlineRecoveryCode, n);
        out[n] = '\0';

        // One-shot consume and wipe.
        QC::String::memset(g_CmdlineRecoveryCode, 0, sizeof(g_CmdlineRecoveryCode));
        g_HasCmdlineRecoveryCode = false;
        return true;
    }

    void BootSaveTermOnceIfConfigured(FLogFn Log)
    {
        if (g_BootSaveTermDone)
            return;
        if (g_BootSaveTermValue[0] == '\0')
            return;
        if (equalsIgnoreCase(g_BootSaveTermValue, "0"))
            return;

        g_BootSaveTermDone = true;

        char cmd[320];
        QC::String::memset(cmd, 0, sizeof(cmd));
        if (equalsIgnoreCase(g_BootSaveTermValue, "1"))
        {
            QC::String::strncpy(cmd, "saveterm", sizeof(cmd) - 1);
        }
        else
        {
            QC::String::strncpy(cmd, "saveterm ", sizeof(cmd) - 1);
            QC::usize used = QC::String::strlen(cmd);
            QC::String::strncpy(cmd + used, g_BootSaveTermValue, sizeof(cmd) - 1 - used);
        }

        QK::Console::executeLine(cmd);

        if (g_PowerOffAfterSaveTerm)
        {
            QK::Shutdown::Controller::instance().requestShutdown(QK::Shutdown::Reason::SystemPolicy);
        }
    }
}
