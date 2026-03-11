#include "QKBootStartupConfig.h"

#include "QCBuiltins.h"
#include "QCString.h"
#include "QFSFile.h"
#include "QFSVFS.h"
#include "QFSVolumeManager.h"
#include "QKConsole.h"
#include "QKShutdownController.h"

namespace QK::Boot::Config
{
    namespace
    {
        static StartupMode g_StartupMode = StartupMode::Desktop;
        static QK::SecurityCenter::Mode g_ScMode = QK::SecurityCenter::Mode::Bypass;
        static bool g_IdeSharedProbeEnabled = false;

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

            if (equalsIgnoreCase(key, "POWEROFF_AFTER_SAVETERM"))
            {
                g_PowerOffAfterSaveTerm = parseBoolValue(value, false);
                return;
            }
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
    }

    StartupMode GetStartupMode()
    {
        return g_StartupMode;
    }

    QK::SecurityCenter::Mode GetSecurityCenterMode()
    {
        return g_ScMode;
    }

    bool GetIdeSharedProbeEnabled()
    {
        return g_IdeSharedProbeEnabled;
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

        if (!QFS::VolumeManager::instance().isMounted("QFS_SHARED"))
        {
            LogStr(Log, "SAVETERM: /shared not mounted; skipping\r\n");
            return;
        }

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
