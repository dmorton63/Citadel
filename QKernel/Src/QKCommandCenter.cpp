// QKernel CommandCenter - shared command registration for terminals
// Namespace: QK::CmdCenter

#include "QKCommandCenter.h"

#include "QCCommandRegistry.h"

#include "QCString.h"

#include "QCBuiltins.h"
#include "QCJson.h"

#include "QFSDirectory.h"
#include "QFSFile.h"
#include "QFSStat.h"
#include "QFSVFS.h"
#include "QFSVolumeManager.h"

#include "QKEventManager.h"
#include "QKShutdownController.h"

#include "QKBootConfigTier.h"
#include "QKBootStagedConfig.h"

#include "QCCanonicalArgs.h"
#include "QCSha256.h"
#include "QKBootLog.h"
#include "QKBootEventLog.h"
#include "QKRuntimeRegistries.h"

#include "QKTime.h"
#include "QQExecutor.h"
#include "QKSecureStore.h"
#include "QKSecurityCenter.h"
#include "QKAIRuntime.h"
#include "QKImageReader.h"
#include "QKModuleLoader.h"
#include "QKSimpleDb.h"
#include "QKCmdAuth.h"
#include "QKCmdParse.h"
#include "QKCmdPathFs.h"
#include "QKCmdBuiltins.h"
#include "QKCmdDebugTest.h"
#include "QKCmdNet.h"
#include "QKBootEventLog.h"

#include "QCQLEngine.h"

#include "QWWindowManager.h"
#include "QWCompositor.h"
#include "QDrvDisplayBootstrap.h"

#include "QSCSecurityCenter.h"
#include "QKSystemPump.h"

#include "QNetStack.h"
#include "QNetIP.h"
#include "QNetEthernet.h"
#include "QNetUDP.h"
#include "QNetDHCP.h"
#include "QNetDNS.h"
#include "QNetTCP.h"

namespace QK::Boot::Config
{
    enum class StartupMode : QC::u8
    {
        Desktop,
        Terminal,
        Safe,
        Recovery,
        Installer,
        Network
    };

    StartupMode GetStartupMode();
    const char *StartupModeName(StartupMode Mode);
    QC::Status PersistStartupMode(StartupMode Mode, void (*Log)(const char *));
    QC::u32 GetMouseSensitivityPercent();
    void SetMouseSensitivityPercent(QC::u32 Percent);
    QC::Status PersistMouseSensitivityPercent(QC::u32 Percent, void (*Log)(const char *));
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
                                          void (*Log)(const char *));
    QC::u32 GetKeyboardRepeatDelayMs();
    QC::u32 GetKeyboardRepeatIntervalMs();
    void SetKeyboardRepeatTiming(QC::u32 DelayMs, QC::u32 IntervalMs);
    QC::Status PersistKeyboardRepeatTiming(QC::u32 DelayMs, QC::u32 IntervalMs, void (*Log)(const char *));
}

namespace QK::Boot::Desktop
{
    bool RequestStopDesktop();
}

namespace QK::CmdCenter
{
    namespace
    {

        static const char *accessName(QC::Cmd::AccessLevel a)
        {
            switch (a)
            {
            case QC::Cmd::AccessLevel::Everyone:
                return "everyone";
            case QC::Cmd::AccessLevel::User:
                return "user";
            case QC::Cmd::AccessLevel::Admin:
                return "admin";
            case QC::Cmd::AccessLevel::SysAdmin:
                return "su";
            case QC::Cmd::AccessLevel::System:
                return "system";
            }
            return "?";
        }

        static const char *statusName(QC::Status st)
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
            }
            return "?";
        }

        // Forward declarations for helpers used across command implementations.
        static bool appendString(char *dest, QC::usize destSize, const char *src);
        static bool appendU64Dec(char *dest, QC::usize destSize, QC::u64 value);
        static bool readFileToNullTerminatedBuffer(const char *absPath, QC::Vector<char> &out, QC::usize maxBytes);
        static void inferExportPathMetadata(const char *absPath,
                                            const char *targetHint,
                                            char *outTarget,
                                            QC::usize outTargetCap,
                                            char *outPersistence,
                                            QC::usize outPersistenceCap);

        static QC::Status loadFstabStore(QK::Db::Store &db)
        {
            (void)QFS::VFS::instance().createDir("/system");
            (void)QFS::VFS::instance().createDir("/system/db");
            return db.load("/system/db/FSTAB.DB");
        }

        static void fstabKeyForVolume(const char *name, char *out, QC::usize outCap)
        {
            if (!out || outCap == 0)
                return;
            QC::String::memset(out, 0, outCap);
            QC::String::strncpy(out, "fstab.", outCap - 1);
            const QC::usize used = QC::String::strlen(out);
            if (used + 1 < outCap)
                QC::String::strncpy(out + used, name ? name : "", outCap - used - 1);
        }

        static QC::Status applyFstabAutoMountOverrides()
        {
            QK::Db::Store &db = QK::Db::Store::instance();
            QC::Status st = loadFstabStore(db);
            if (st != QC::Status::Success)
                return st;

            QK::Db::Entry entries[64] = {};
            const QC::usize count = db.list(entries, sizeof(entries) / sizeof(entries[0]));
            for (QC::usize i = 0; i < count; ++i)
            {
                if (QC::String::memcmp(entries[i].key, "fstab.", 6) != 0)
                    continue;
                const char *name = entries[i].key + 6;
                const bool enabled = entries[i].value[0] == '1';
                (void)QFS::VolumeManager::instance().setAutoMount(name, enabled);
            }

            return QFS::VolumeManager::instance().mountPending();
        }

        static bool cmdWhoami(const char *, const QC::Cmd::Context &ctx, void *)
        {
            char line[96];
            QC::String::memset(line, 0, sizeof(line));
            QC::String::strncpy(line, "role=", sizeof(line) - 1);
            line[sizeof(line) - 1] = '\0';

            const char *role = accessName(ctx.callerAccess);
            const QC::usize used = QC::String::strlen(line);
            if (used + 1 < sizeof(line) && role && *role)
            {
                QC::String::strncpy(line + used, role, sizeof(line) - used - 1);
                line[sizeof(line) - 1] = '\0';
            }
            ctx.writeLine(line);
            return true;
        }

        static bool cmdRoleDenied(const char *roleName, const QC::Cmd::Context &ctx)
        {
            (void)roleName;
            ctx.writeLine("permission denied");
            return true;
        }

        static bool cmdScDumpOwnerCred(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            // usage: scdumpownercred [raw|plain]
            // Default: dump the underlying on-disk blob (unverified) using readBlob.
            // plain: read and verify sealed blob then dump the unsealed payload.

            bool raw = true;
            const char *a = args;
            while (a && (*a == ' ' || *a == '\t'))
                ++a;
            if (a && *a)
            {
                if (QC::String::strcmp(a, "plain") == 0)
                    raw = false;
                else if (QC::String::strcmp(a, "raw") == 0)
                    raw = true;
            }

            if (!QK::SecurityCenter::instance().ownerIsEnrolled())
            {
                ctx.writeLine("scdumpownercred: not enrolled");
                return true;
            }

            QC::Vector<QC::u8> blob;
            const char *key = "OWNERCRD";
            QC::Status st = raw ? QK::SecureStore::readBlob(key, blob) : QK::SecureStore::readSealedBlob(key, blob);
            if (st != QC::Status::Success)
            {
                ctx.writeLine("scdumpownercred: read failed");
                return true;
            }

            ctx.writeLine("-----BEGIN CITADEL OWNERCRD HEX-----");
            ctx.writeLine(raw ? "mode=raw" : "mode=plain");

            static const char kHex[] = "0123456789abcdef";
            char out[65];
            QC::usize oi = 0;
            for (QC::usize i = 0; i < blob.size(); ++i)
            {
                const QC::u8 b = blob[i];
                out[oi++] = kHex[(b >> 4) & 0xF];
                out[oi++] = kHex[b & 0xF];
                if (oi >= 64)
                {
                    out[64] = '\0';
                    ctx.writeLine(out);
                    oi = 0;
                }
            }
            if (oi)
            {
                out[oi] = '\0';
                ctx.writeLine(out);
            }
            ctx.writeLine("-----END CITADEL OWNERCRD HEX-----");
            return true;
        }

        // Forward declaration (definition is later in this TU).
        static bool readToken(const char *&p, char *out, QC::usize outSize);

        static bool cmdSysUserEnroll(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            // usage: sys_user_enroll <user> <pass>
            char user[33];
            char pass[65];
            QC::String::memset(user, 0, sizeof(user));
            QC::String::memset(pass, 0, sizeof(pass));

            const char *p = args;
            const bool haveUser = readToken(p, user, sizeof(user));
            const bool havePass = readToken(p, pass, sizeof(pass));

            if (!haveUser || !havePass || !user[0] || !pass[0])
            {
                ctx.writeLine("usage: sys_user_enroll <user> <pass>");
                return true;
            }

            const QC::Status st = QK::SecurityCenter::instance().ownerEnroll(user, pass);
            if (st == QC::Status::Success)
                ctx.writeLine("sys_user_enroll: ok");
            else if (st == QC::Status::Busy)
                ctx.writeLine("sys_user_enroll: already enrolled");
            else
                ctx.writeLine("sys_user_enroll: failed");
            return true;
        }

        static bool cmdSysUserUnlock(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            // usage: sys_user_unlock <user> <pass>
            char user[33];
            char pass[65];
            QC::String::memset(user, 0, sizeof(user));
            QC::String::memset(pass, 0, sizeof(pass));

            const char *p = args;
            const bool haveUser = readToken(p, user, sizeof(user));
            const bool havePass = readToken(p, pass, sizeof(pass));

            if (!haveUser || !havePass || !user[0] || !pass[0])
            {
                ctx.writeLine("usage: sys_user_unlock <user> <pass>");
                return true;
            }

            const QC::Status st = QK::SecurityCenter::instance().ownerUnlock(user, pass);
            if (st == QC::Status::Success)
            {
                ctx.writeLine("sys_user_unlock: ok");
                return true;
            }

            char msg[128];
            QC::String::memset(msg, 0, sizeof(msg));
            QC::String::strncpy(msg, "sys_user_unlock: denied", sizeof(msg) - 1);
            const QC::u32 backoff = QK::SecurityCenter::instance().ownerUnlockBackoffMs();
            if (backoff)
            {
                const QC::usize used0 = QC::String::strlen(msg);
                QC::String::strncpy(msg + used0, " (backoff ", sizeof(msg) - 1 - used0);

                char num[16];
                QC::String::memset(num, 0, sizeof(num));
                QC::u32 v = backoff;
                char tmp[16];
                QC::String::memset(tmp, 0, sizeof(tmp));
                QC::usize n = 0;
                do
                {
                    tmp[n++] = static_cast<char>('0' + (v % 10));
                    v /= 10;
                } while (v && n < sizeof(tmp));
                for (QC::usize i = 0; i < n && i < sizeof(num) - 1; ++i)
                    num[i] = tmp[n - 1 - i];

                const QC::usize used1 = QC::String::strlen(msg);
                QC::String::strncpy(msg + used1, num, sizeof(msg) - 1 - used1);
                const QC::usize used2 = QC::String::strlen(msg);
                QC::String::strncpy(msg + used2, "ms)", sizeof(msg) - 1 - used2);
            }
            ctx.writeLine(msg);
            return true;
        }

        static bool cmdSysUserLock(const char *, const QC::Cmd::Context &ctx, void *)
        {
            QK::SecurityCenter::instance().ownerLock();
            ctx.writeLine("sys_user_lock: ok");
            return true;
        }

        static bool cmdUser(const char *, const QC::Cmd::Context &ctx, void *)
        {
            // Desktop terminal must implement changing env->param2; this is a placeholder.
            // Keep the command so help/UX can be built around it.
            if (ctx.callerAccess == QC::Cmd::AccessLevel::System)
                return cmdRoleDenied("user", ctx);
            ctx.writeLine("role: use terminal session controls to switch role (not wired)");
            return true;
        }

        static bool cmdAdmin(const char *, const QC::Cmd::Context &ctx, void *)
        {
            if (ctx.callerAccess == QC::Cmd::AccessLevel::System)
                return cmdRoleDenied("admin", ctx);
            ctx.writeLine("role: use terminal session controls to switch role (not wired)");
            return true;
        }

        static bool cmdSu(const char *, const QC::Cmd::Context &ctx, void *)
        {
            if (ctx.callerAccess == QC::Cmd::AccessLevel::System)
                return cmdRoleDenied("su", ctx);
            ctx.writeLine("role: use terminal session controls to switch role (not wired)");
            return true;
        }
        static Session g_session;
        static bool g_sessionInitialized = false;
        static constexpr const char *kAliasMapPath = "/system/config/CMDALIAS.CFG";
        static constexpr QC::usize kMaxScriptDepth = 4;
        static QC::usize g_scriptDepth = 0;
        static IpcHookFn g_ipcHook = nullptr;
        static void *g_ipcHookUser = nullptr;

        static bool isSpace(char c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        static const char *skipSpaces(const char *p)
        {
            while (p && isSpace(*p))
                ++p;
            return p;
        }

        static bool streqIgnoreCase(const char *a, const char *b)
        {
            if (!a || !b)
                return a == b;
            while (*a && *b)
            {
                char ca = *a;
                char cb = *b;
                if (ca >= 'A' && ca <= 'Z')
                    ca = static_cast<char>(ca + 32);
                if (cb >= 'A' && cb <= 'Z')
                    cb = static_cast<char>(cb + 32);
                if (ca != cb)
                    return false;
                ++a;
                ++b;
            }
            return *a == '\0' && *b == '\0';
        }

        static void writeKeyValue(const QC::Cmd::Context &ctx, const char *key, const char *value)
        {
            char line[256];
            QC::String::memset(line, 0, sizeof(line));
            QC::usize idx = 0;
            for (QC::usize i = 0; key && key[i] && idx + 1 < sizeof(line); ++i)
                line[idx++] = key[i];
            if (idx + 2 < sizeof(line))
            {
                line[idx++] = ':';
                line[idx++] = ' ';
            }
            for (QC::usize i = 0; value && value[i] && idx + 1 < sizeof(line); ++i)
                line[idx++] = value[i];
            line[idx] = '\0';
            ctx.writeLine(line);
        }

        static void writeKeyValueU32(const QC::Cmd::Context &ctx, const char *key, QC::u32 value)
        {
            char valueBuf[32];
            QC::String::memset(valueBuf, 0, sizeof(valueBuf));
            QC::u32 remaining = value;
            int idx = 0;
            if (remaining == 0)
            {
                valueBuf[idx++] = '0';
            }
            else
            {
                char tmp[32];
                int tmpIdx = 0;
                while (remaining > 0 && tmpIdx < 31)
                {
                    tmp[tmpIdx++] = static_cast<char>('0' + (remaining % 10));
                    remaining /= 10;
                }
                for (int i = tmpIdx - 1; i >= 0; --i)
                    valueBuf[idx++] = tmp[i];
            }
            valueBuf[idx] = '\0';
            writeKeyValue(ctx, key, valueBuf);
        }

        static void writeKeyValueU64(const QC::Cmd::Context &ctx, const char *key, QC::u64 value)
        {
            char valueBuf[32];
            QC::String::memset(valueBuf, 0, sizeof(valueBuf));
            QC::u64 remaining = value;
            int idx = 0;
            if (remaining == 0)
            {
                valueBuf[idx++] = '0';
            }
            else
            {
                char tmp[32];
                int tmpIdx = 0;
                while (remaining > 0 && tmpIdx < 31)
                {
                    tmp[tmpIdx++] = static_cast<char>('0' + (remaining % 10));
                    remaining /= 10;
                }
                for (int i = tmpIdx - 1; i >= 0; --i)
                    valueBuf[idx++] = tmp[i];
            }
            valueBuf[idx] = '\0';
            writeKeyValue(ctx, key, valueBuf);
        }

        static void writeKeyValueBool(const QC::Cmd::Context &ctx, const char *key, bool value)
        {
            writeKeyValue(ctx, key, value ? "yes" : "no");
        }

        static bool segmentEquals(const char *start, QC::usize len, const char *literal)
        {
            if (!start || !literal)
                return false;
            QC::usize litLen = QC::String::strlen(literal);
            if (len != litLen)
                return false;
            for (QC::usize i = 0; i < len; ++i)
            {
                if (start[i] != literal[i])
                    return false;
            }
            return true;
        }

        static bool isSystemPath(const char *absPath)
        {
            if (!absPath)
                return false;
            const char *prefix = "/system";
            const QC::usize n = QC::String::strlen(prefix);
            if (QC::String::memcmp(absPath, prefix, n) != 0)
                return false;
            return absPath[n] == '\0' || absPath[n] == '/';
        }

        static bool isProdPath(const char *absPath)
        {
            if (!absPath)
                return false;
            const char *prefix = "/PROD";
            const QC::usize n = QC::String::strlen(prefix);
            if (QC::String::memcmp(absPath, prefix, n) != 0)
                return false;
            return absPath[n] == '\0' || absPath[n] == '/';
        }

        static bool isSharedPath(const char *absPath)
        {
            if (!absPath)
                return false;
            const char *prefix = "/shared";
            const QC::usize n = QC::String::strlen(prefix);
            if (QC::String::memcmp(absPath, prefix, n) != 0)
                return false;
            return absPath[n] == '\0' || absPath[n] == '/';
        }

        static bool pathMatchesMountPrefix(const char *path, const char *mountPath)
        {
            if (!path || !mountPath || mountPath[0] == '\0')
                return false;
            const QC::usize n = QC::String::strlen(mountPath);
            if (QC::String::memcmp(path, mountPath, n) != 0)
                return false;
            return path[n] == '\0' || path[n] == '/';
        }

        static bool allowWriteToPath(const char *absPath, const QC::Cmd::Context &ctx, const char *cmdName)
        {
            (void)cmdName;

            const QC::u8 caller = static_cast<QC::u8>(ctx.callerAccess);

            if (isSystemPath(absPath))
            {
                const QC::u8 admin = static_cast<QC::u8>(QC::Cmd::AccessLevel::Admin);
                if (caller < admin)
                {
                    ctx.writeLine("permission denied: /system requires admin");
                    return false;
                }
            }

            if (isProdPath(absPath))
            {
                const QC::u8 su = static_cast<QC::u8>(QC::Cmd::AccessLevel::SysAdmin);
                if (caller < su)
                {
                    ctx.writeLine("permission denied: /PROD requires su");
                    return false;
                }
            }

            return true;
        }

        enum class ExportTarget : QC::u8
        {
            Auto,
            System,
            Shared,
            Usb
        };

        static bool parseExportTarget(const char *token, ExportTarget &out)
        {
            if (!token || token[0] == '\0')
                return false;
            if (streqIgnoreCase(token, "auto"))
            {
                out = ExportTarget::Auto;
                return true;
            }
            if (streqIgnoreCase(token, "system"))
            {
                out = ExportTarget::System;
                return true;
            }
            if (streqIgnoreCase(token, "shared"))
            {
                out = ExportTarget::Shared;
                return true;
            }
            if (streqIgnoreCase(token, "usb"))
            {
                out = ExportTarget::Usb;
                return true;
            }
            return false;
        }

        static bool hasEphemeralOverrideToken(const char *args)
        {
            char tok[32];
            QC::String::memset(tok, 0, sizeof(tok));
            const char *p = args;
            while (readToken(p, tok, sizeof(tok)))
            {
                if (streqIgnoreCase(tok, "ephemeral-ok"))
                    return true;
            }
            return false;
        }

        static bool ensureDirectoryPath(const char *path)
        {
            if (!path || path[0] != '/')
                return false;
            if (QC::String::strcmp(path, "/") == 0)
                return true;

            char partial[256];
            QC::usize partialLen = 0;
            partial[partialLen++] = '/';
            partial[partialLen] = '\0';

            const char *p = path;
            while (*p == '/')
                ++p;

            while (*p)
            {
                const char *segStart = p;
                while (*p && *p != '/')
                    ++p;
                const QC::usize segLen = static_cast<QC::usize>(p - segStart);

                while (*p == '/')
                    ++p;

                if (segLen == 0)
                    continue;

                if (partialLen > 1 && partial[partialLen - 1] != '/')
                {
                    if (partialLen + 1 >= sizeof(partial))
                        return false;
                    partial[partialLen++] = '/';
                    partial[partialLen] = '\0';
                }

                if (partialLen + segLen >= sizeof(partial))
                    return false;

                QC::String::memcpy(partial + partialLen, segStart, segLen);
                partialLen += segLen;
                partial[partialLen] = '\0';

                QFS::FileInfo info{};
                const QC::Status st = QFS::VFS::instance().stat(partial, &info);
                if (st == QC::Status::Success)
                {
                    if (info.type != QFS::FileType::Directory)
                        return false;
                    continue;
                }

                if (st != QC::Status::NotFound)
                    return false;

                if (QFS::VFS::instance().createDir(partial) != QC::Status::Success)
                {
                    QFS::FileInfo info2{};
                    if (QFS::VFS::instance().stat(partial, &info2) != QC::Status::Success || info2.type != QFS::FileType::Directory)
                        return false;
                }
            }

            return true;
        }

        static bool resolveExportBaseForTarget(ExportTarget target,
                                               char *outBase,
                                               QC::usize outBaseCap,
                                               char *outRefusal,
                                               QC::usize outRefusalCap)
        {
            if (!outBase || outBaseCap == 0)
                return false;

            QC::String::memset(outBase, 0, outBaseCap);
            if (outRefusal && outRefusalCap)
                QC::String::memset(outRefusal, 0, outRefusalCap);

            QFS::VolumeInfo volumes[32] = {};
            const QC::usize count = QFS::VolumeManager::instance().copyVolumeInfo(volumes, sizeof(volumes) / sizeof(volumes[0]));

            auto copyPath = [&](const char *path) -> bool {
                if (!path || path[0] == '\0')
                    return false;
                QC::String::strncpy(outBase, path, outBaseCap - 1);
                outBase[outBaseCap - 1] = '\0';
                return true;
            };

            auto writeRefusal = [&](const char *msg) {
                if (!outRefusal || outRefusalCap == 0)
                    return;
                QC::String::strncpy(outRefusal, msg ? msg : "target unavailable", outRefusalCap - 1);
                outRefusal[outRefusalCap - 1] = '\0';
            };

            auto mountedPath = [&](const char *wanted) -> const char * {
                for (QC::usize i = 0; i < count; ++i)
                {
                    if (!volumes[i].mounted)
                        continue;
                    if (QC::String::strcmp(volumes[i].mountPath, wanted) == 0)
                        return volumes[i].mountPath;
                }
                return nullptr;
            };

            auto mountedUsbPath = [&]() -> const char * {
                for (QC::usize i = 0; i < count; ++i)
                {
                    if (!volumes[i].mounted)
                        continue;
                    if (QC::String::strcmp(volumes[i].persistenceClass, "removable") == 0)
                        return volumes[i].mountPath;
                    if (volumes[i].sourceKind[0] && QC::String::strcmp(volumes[i].sourceKind, "xhci-usb") == 0)
                        return volumes[i].mountPath;
                }
                return nullptr;
            };

            switch (target)
            {
            case ExportTarget::System:
            {
                const char *p = mountedPath("/system");
                if (!p)
                {
                    writeRefusal("target 'system' unavailable: /system not mounted");
                    return false;
                }
                return copyPath(p);
            }
            case ExportTarget::Shared:
            {
                const char *p = mountedPath("/shared");
                if (!p)
                {
                    writeRefusal("target 'shared' unavailable: /shared not mounted");
                    return false;
                }
                return copyPath(p);
            }
            case ExportTarget::Usb:
            {
                const char *p = mountedUsbPath();
                if (!p)
                {
                    writeRefusal("target 'usb' unavailable: no mounted removable volume");
                    return false;
                }
                return copyPath(p);
            }
            case ExportTarget::Auto:
            default:
            {
                const char *systemPath = mountedPath("/system");
                if (systemPath)
                    return copyPath(systemPath);
                const char *sharedPath = mountedPath("/shared");
                if (sharedPath)
                    return copyPath(sharedPath);
                const char *usbPath = mountedUsbPath();
                if (usbPath)
                    return copyPath(usbPath);

                writeRefusal("target 'auto' unavailable: no mounted /system, /shared, or removable usb volume");
                return false;
            }
            }
        }

        static bool buildExportPathForTarget(ExportTarget target,
                                             const char *leafName,
                                             char *outPath,
                                             QC::usize outPathCap,
                                             char *outRefusal,
                                             QC::usize outRefusalCap)
        {
            if (!leafName || !outPath || outPathCap == 0)
                return false;

            char base[160];
            QC::String::memset(base, 0, sizeof(base));
            if (!resolveExportBaseForTarget(target, base, sizeof(base), outRefusal, outRefusalCap))
                return false;

            char logsDir[192];
            QC::String::memset(logsDir, 0, sizeof(logsDir));
            QC::String::strncpy(logsDir, base, sizeof(logsDir) - 1);
            const QC::usize used = QC::String::strlen(logsDir);
            if (used + 6 >= sizeof(logsDir))
            {
                if (outRefusal && outRefusalCap)
                    QC::String::strncpy(outRefusal, "target path too long", outRefusalCap - 1);
                return false;
            }
            if (used > 1 && logsDir[used - 1] == '/')
                QC::String::strncpy(logsDir + used - 1, "logs", sizeof(logsDir) - used);
            else
                QC::String::strncpy(logsDir + used, "/logs", sizeof(logsDir) - used - 1);

            if (!ensureDirectoryPath(logsDir))
            {
                if (outRefusal && outRefusalCap)
                    QC::String::strncpy(outRefusal, "failed to create target logs directory", outRefusalCap - 1);
                return false;
            }

            QC::String::memset(outPath, 0, outPathCap);
            QC::String::strncpy(outPath, logsDir, outPathCap - 1);
            const QC::usize pathUsed = QC::String::strlen(outPath);
            if (pathUsed + 1 >= outPathCap)
                return false;
            if (outPath[pathUsed - 1] != '/')
            {
                outPath[pathUsed] = '/';
                outPath[pathUsed + 1] = '\0';
            }

            const QC::usize used2 = QC::String::strlen(outPath);
            if (used2 + QC::String::strlen(leafName) >= outPathCap)
                return false;
            QC::String::strncpy(outPath + used2, leafName, outPathCap - used2 - 1);
            return true;
        }

        static bool parentPathOf(const char *path, char *outParent, QC::usize outParentCap)
        {
            if (!path || !outParent || outParentCap == 0)
                return false;
            if (path[0] != '/')
                return false;

            const QC::usize len = QC::String::strlen(path);
            if (len == 0 || len >= outParentCap)
                return false;

            QC::String::memset(outParent, 0, outParentCap);
            QC::String::strncpy(outParent, path, outParentCap - 1);

            QC::usize i = len;
            while (i > 0 && outParent[i - 1] == '/')
                --i;
            while (i > 0 && outParent[i - 1] != '/')
                --i;

            if (i == 0)
                return false;

            if (i == 1)
            {
                outParent[1] = '\0';
                return true;
            }

            outParent[i - 1] = '\0';
            return true;
        }

        static bool preflightExportPath(const char *absPath,
                                        QC::usize reserveBytes,
                                        const QC::Cmd::Context &ctx,
                                        const char *cmdName)
        {
            if (!absPath || absPath[0] != '/')
            {
                ctx.writeLine("export preflight: invalid absolute path");
                return false;
            }

            if (!allowWriteToPath(absPath, ctx, cmdName))
                return false;

            char parent[256];
            QC::String::memset(parent, 0, sizeof(parent));
            if (!parentPathOf(absPath, parent, sizeof(parent)))
            {
                ctx.writeLine("export preflight: cannot resolve parent directory");
                return false;
            }

            QFS::FileInfo parentInfo{};
            const QC::Status pst = QFS::VFS::instance().stat(parent, &parentInfo);
            if (pst != QC::Status::Success)
            {
                char line[256];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "export preflight: target parent missing: ");
                (void)appendString(line, sizeof(line), parent);
                ctx.writeLine(line);
                return false;
            }
            if (parentInfo.type != QFS::FileType::Directory)
            {
                char line[256];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "export preflight: target parent is not a directory: ");
                (void)appendString(line, sizeof(line), parent);
                ctx.writeLine(line);
                return false;
            }

            char probePath[320];
            QC::String::memset(probePath, 0, sizeof(probePath));
            QC::String::strncpy(probePath, absPath, sizeof(probePath) - 1);
            const QC::usize probeUsed = QC::String::strlen(probePath);
            constexpr const char *kProbeSuffix = ".preflight.tmp";
            const QC::usize suffixLen = QC::String::strlen(kProbeSuffix);
            if (probeUsed + suffixLen >= sizeof(probePath))
            {
                ctx.writeLine("export preflight: probe path too long");
                return false;
            }
            QC::String::strncpy(probePath + probeUsed, kProbeSuffix, sizeof(probePath) - probeUsed - 1);

            (void)QFS::VFS::instance().remove(probePath);
            QFS::File *probe = QFS::VFS::instance().open(
                probePath,
                QFS::OpenMode::Write | QFS::OpenMode::Create | QFS::OpenMode::Truncate);
            if (!probe)
            {
                ctx.writeLine("export preflight: target is not writable (probe open failed)");
                return false;
            }

            const QC::usize reserve = (reserveBytes == 0) ? 1 : reserveBytes;
            char zero[512];
            QC::String::memset(zero, 0, sizeof(zero));
            QC::usize remaining = reserve;
            while (remaining > 0)
            {
                const QC::usize chunk = remaining < sizeof(zero) ? remaining : sizeof(zero);
                const QC::isize w = probe->write(zero, chunk);
                if (w <= 0)
                {
                    QFS::VFS::instance().close(probe);
                    (void)QFS::VFS::instance().remove(probePath);

                    char line[256];
                    QC::String::memset(line, 0, sizeof(line));
                    (void)appendString(line, sizeof(line), "export preflight: insufficient space for ");
                    (void)appendU64Dec(line, sizeof(line), static_cast<QC::u64>(reserveBytes));
                    (void)appendString(line, sizeof(line), " bytes (reservation failed)");
                    ctx.writeLine(line);
                    return false;
                }
                remaining -= static_cast<QC::usize>(w);
            }

            QFS::VFS::instance().close(probe);
            (void)QFS::VFS::instance().remove(probePath);
            return true;
        }

        static bool enforceEphemeralWriteGuard(const char *absPath,
                                               const char *targetHint,
                                               const char *args,
                                               const QC::Cmd::Context &ctx,
                                               const char *cmdName)
        {
            char resolvedTarget[24];
            char persistenceClass[24];
            QC::String::memset(resolvedTarget, 0, sizeof(resolvedTarget));
            QC::String::memset(persistenceClass, 0, sizeof(persistenceClass));
            inferExportPathMetadata(absPath,
                                    targetHint,
                                    resolvedTarget,
                                    sizeof(resolvedTarget),
                                    persistenceClass,
                                    sizeof(persistenceClass));

            if (!streqIgnoreCase(persistenceClass, "ephemeral"))
                return true;

            if (hasEphemeralOverrideToken(args))
                return true;

            char line[256];
            QC::String::memset(line, 0, sizeof(line));
            (void)appendString(line, sizeof(line), cmdName);
            (void)appendString(line, sizeof(line), ": target is ephemeral (not durable). add 'ephemeral-ok' to confirm");
            ctx.writeLine(line);
            return false;
        }

        static QC::usize estimateAuditExportBytes()
        {
            const QC::usize total = QK::Boot::Events::Count();
            QK::Boot::Events::Record recs[8] = {};
            QC::usize estimate = 0;
            QC::usize offset = 0;
            while (offset < total)
            {
                const QC::usize n = QK::Boot::Events::CopyOut(offset, recs, sizeof(recs) / sizeof(recs[0]));
                if (n == 0)
                    break;
                offset += n;

                for (QC::usize i = 0; i < n; ++i)
                {
                    char line[384];
                    char detail[160];
                    QC::String::memset(line, 0, sizeof(line));
                    QC::String::memset(detail, 0, sizeof(detail));
                    (void)appendString(line, sizeof(line), "EV seq=");
                    (void)appendU64Dec(line, sizeof(line), recs[i].seq);
                    (void)appendString(line, sizeof(line), " t_ms=");
                    (void)appendU64Dec(line, sizeof(line), recs[i].t_ms);
                    (void)appendString(line, sizeof(line), " stage=");
                    (void)appendString(line, sizeof(line), recs[i].stage[0] ? recs[i].stage : "(none)");
                    (void)appendString(line, sizeof(line), " type=");
                    (void)appendString(line, sizeof(line), recs[i].type[0] ? recs[i].type : "(none)");
                    if (recs[i].details[0])
                    {
                        QK::SecurityCenter::instance().redactAuditText(recs[i].details, detail, sizeof(detail));
                        (void)appendString(line, sizeof(line), " ");
                        (void)appendString(line, sizeof(line), detail);
                    }
                    (void)appendString(line, sizeof(line), "\n");
                    estimate += QC::String::strlen(line);
                }
            }

            return estimate;
        }

        static bool computeFileSha256Hex(const char *absPath, char *outHex, QC::usize outHexCap)
        {
            if (!absPath || !outHex || outHexCap < 65)
                return false;

            QC::String::memset(outHex, 0, outHexCap);

            QFS::FileInfo info{};
            if (QFS::VFS::instance().stat(absPath, &info) != QC::Status::Success)
                return false;
            if (info.type != QFS::FileType::Regular)
                return false;

            const QC::usize size = static_cast<QC::usize>(info.size);
            QC::Vector<QC::u8> data;
            if (size > 0)
            {
                data.resize(size);
                if (data.size() != size)
                    return false;
            }

            QFS::File *f = QFS::VFS::instance().open(absPath, QFS::OpenMode::Read);
            if (!f)
                return false;

            QC::usize readTotal = 0;
            while (readTotal < size)
            {
                const QC::isize n = f->read(data.data() + readTotal, size - readTotal);
                if (n <= 0)
                {
                    QFS::VFS::instance().close(f);
                    return false;
                }
                readTotal += static_cast<QC::usize>(n);
            }
            QFS::VFS::instance().close(f);

            QC::u8 digest[32];
            const QC::u8 zero = 0;
            const QC::u8 *ptr = (size > 0) ? data.data() : &zero;
            QC::Sha256(ptr, size, digest);
            return QC::Sha256DigestToLowerHex(digest, outHex, outHexCap);
        }

        static void inferExportPathMetadata(const char *absPath,
                                            const char *targetHint,
                                            char *outTarget,
                                            QC::usize outTargetCap,
                                            char *outPersistence,
                                            QC::usize outPersistenceCap)
        {
            if (outTarget && outTargetCap)
            {
                QC::String::memset(outTarget, 0, outTargetCap);
                if (targetHint && targetHint[0])
                    QC::String::strncpy(outTarget, targetHint, outTargetCap - 1);
            }
            if (outPersistence && outPersistenceCap)
            {
                QC::String::memset(outPersistence, 0, outPersistenceCap);
                QC::String::strncpy(outPersistence, "unknown", outPersistenceCap - 1);
            }

            if (!absPath)
                return;

            QFS::VolumeInfo volumes[32] = {};
            const QC::usize count = QFS::VolumeManager::instance().copyVolumeInfo(volumes, sizeof(volumes) / sizeof(volumes[0]));
            const QFS::VolumeInfo *best = nullptr;
            QC::usize bestLen = 0;
            for (QC::usize i = 0; i < count; ++i)
            {
                if (!volumes[i].mounted)
                    continue;
                if (!pathMatchesMountPrefix(absPath, volumes[i].mountPath))
                    continue;
                const QC::usize n = QC::String::strlen(volumes[i].mountPath);
                if (!best || n > bestLen)
                {
                    best = &volumes[i];
                    bestLen = n;
                }
            }

            if (best)
            {
                if (outPersistence && outPersistenceCap)
                {
                    QC::String::memset(outPersistence, 0, outPersistenceCap);
                    QC::String::strncpy(outPersistence,
                                        best->persistenceClass[0] ? best->persistenceClass : "unknown",
                                        outPersistenceCap - 1);
                }

                if (outTarget && outTargetCap && (!targetHint || !targetHint[0] || streqIgnoreCase(targetHint, "auto")))
                {
                    QC::String::memset(outTarget, 0, outTargetCap);
                    if (QC::String::strcmp(best->mountPath, "/system") == 0)
                        QC::String::strncpy(outTarget, "system", outTargetCap - 1);
                    else if (QC::String::strcmp(best->mountPath, "/shared") == 0)
                        QC::String::strncpy(outTarget, "shared", outTargetCap - 1);
                    else if (QC::String::strcmp(best->persistenceClass, "removable") == 0)
                        QC::String::strncpy(outTarget, "usb", outTargetCap - 1);
                    else
                        QC::String::strncpy(outTarget, "path", outTargetCap - 1);
                }
            }
            else if (outTarget && outTargetCap && (!targetHint || !targetHint[0]))
            {
                QC::String::memset(outTarget, 0, outTargetCap);
                if (isSystemPath(absPath))
                    QC::String::strncpy(outTarget, "system", outTargetCap - 1);
                else if (isSharedPath(absPath))
                    QC::String::strncpy(outTarget, "shared", outTargetCap - 1);
                else
                    QC::String::strncpy(outTarget, "path", outTargetCap - 1);
            }
        }

        static bool writeExportMetadataSidecar(const char *artifactPath,
                                               const char *sourceName,
                                               const char *targetHint,
                                               const QC::Cmd::Context &ctx,
                                               const char *cmdName)
        {
            if (!artifactPath || artifactPath[0] == '\0')
                return false;

            char hashHex[65];
            QC::String::memset(hashHex, 0, sizeof(hashHex));
            if (!computeFileSha256Hex(artifactPath, hashHex, sizeof(hashHex)))
            {
                ctx.writeLine("export metadata: failed to hash artifact");
                return false;
            }

            char targetLabel[24];
            char persistenceClass[24];
            QC::String::memset(targetLabel, 0, sizeof(targetLabel));
            QC::String::memset(persistenceClass, 0, sizeof(persistenceClass));
            inferExportPathMetadata(artifactPath,
                                    targetHint,
                                    targetLabel,
                                    sizeof(targetLabel),
                                    persistenceClass,
                                    sizeof(persistenceClass));

            char metaPath[320];
            QC::String::memset(metaPath, 0, sizeof(metaPath));
            QC::String::strncpy(metaPath, artifactPath, sizeof(metaPath) - 1);
            const QC::usize used = QC::String::strlen(metaPath);
            constexpr const char *kSuffix = ".meta.json";
            const QC::usize suffixLen = QC::String::strlen(kSuffix);
            if (used + suffixLen >= sizeof(metaPath))
            {
                ctx.writeLine("export metadata: sidecar path too long");
                return false;
            }
            QC::String::strncpy(metaPath + used, kSuffix, sizeof(metaPath) - used - 1);

            if (!allowWriteToPath(metaPath, ctx, cmdName))
                return false;

            QFS::File *meta = QFS::VFS::instance().open(
                metaPath,
                QFS::OpenMode::Write | QFS::OpenMode::Create | QFS::OpenMode::Truncate);
            if (!meta)
            {
                ctx.writeLine("export metadata: cannot open sidecar");
                return false;
            }

            char body[1024];
            QC::String::memset(body, 0, sizeof(body));
            (void)appendString(body, sizeof(body), "{\n");
            (void)appendString(body, sizeof(body), "  \"timestamp_ms\": ");
            (void)appendU64Dec(body, sizeof(body), QK::Time::milliseconds());
            (void)appendString(body, sizeof(body), ",\n");
            (void)appendString(body, sizeof(body), "  \"source\": \"");
            (void)appendString(body, sizeof(body), sourceName ? sourceName : "unknown");
            (void)appendString(body, sizeof(body), "\",\n");
            (void)appendString(body, sizeof(body), "  \"target\": \"");
            (void)appendString(body, sizeof(body), targetLabel[0] ? targetLabel : "unknown");
            (void)appendString(body, sizeof(body), "\",\n");
            (void)appendString(body, sizeof(body), "  \"persistence_class\": \"");
            (void)appendString(body, sizeof(body), persistenceClass[0] ? persistenceClass : "unknown");
            (void)appendString(body, sizeof(body), "\",\n");
            (void)appendString(body, sizeof(body), "  \"artifact_path\": \"");
            (void)appendString(body, sizeof(body), artifactPath);
            (void)appendString(body, sizeof(body), "\",\n");
            (void)appendString(body, sizeof(body), "  \"artifact_sha256\": \"");
            (void)appendString(body, sizeof(body), hashHex);
            (void)appendString(body, sizeof(body), "\"\n");
            (void)appendString(body, sizeof(body), "}\n");

            const QC::usize total = QC::String::strlen(body);
            QC::usize off = 0;
            while (off < total)
            {
                const QC::isize w = meta->write(body + off, total - off);
                if (w <= 0)
                {
                    QFS::VFS::instance().close(meta);
                    ctx.writeLine("export metadata: write failed");
                    return false;
                }
                off += static_cast<QC::usize>(w);
            }

            QFS::VFS::instance().close(meta);
            return true;
        }

        static void trimInPlace(char *text)
        {
            if (!text)
                return;

            QC::usize len = QC::String::strlen(text);
            QC::usize start = 0;
            while (start < len && isSpace(text[start]))
                ++start;

            QC::usize end = len;
            while (end > start && isSpace(text[end - 1]))
                --end;

            QC::usize out = 0;
            for (QC::usize i = start; i < end; ++i)
                text[out++] = text[i];
            text[out] = '\0';
        }

        static void clearAliasMap()
        {
            auto &reg = QC::Cmd::Registry::instance();
            char names[64][48];
            QC::usize count = reg.aliasCount();
            if (count > 64)
                count = 64;

            for (QC::usize i = 0; i < count; ++i)
            {
                QC::String::memset(names[i], 0, sizeof(names[i]));
                const char *n = reg.aliasNameAt(i);
                if (n)
                    QC::String::strncpy(names[i], n, sizeof(names[i]) - 1);
            }

            for (QC::usize i = 0; i < count; ++i)
            {
                if (names[i][0])
                    (void)reg.removeAlias(names[i]);
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

        static bool saveAliasMap(const QC::Cmd::Context *ctx)
        {
            QFS::FileInfo info;
            QC::String::memset(&info, 0, sizeof(info));
            if (QFS::VFS::instance().stat("/system/config", &info) != QC::Status::Success)
                (void)QFS::VFS::instance().createDir("/system/config");

            QFS::File *file = QFS::VFS::instance().open(kAliasMapPath,
                                                        QFS::OpenMode::Write | QFS::OpenMode::Create | QFS::OpenMode::Truncate);
            if (!file)
            {
                if (ctx)
                    ctx->writeLine("alias: failed to persist map");
                return false;
            }

            bool ok = true;
            ok = ok && writeAll(file, "# Citadel command alias map\n");
            ok = ok && writeAll(file, "# format: alias=command expansion\n");

            auto &reg = QC::Cmd::Registry::instance();
            for (QC::usize i = 0; i < reg.aliasCount(); ++i)
            {
                const char *name = reg.aliasNameAt(i);
                const char *exp = reg.aliasExpansionAt(i);
                if (!name || !*name || !exp || !*exp)
                    continue;

                char line[320];
                QC::String::memset(line, 0, sizeof(line));
                if (!appendString(line, sizeof(line), name) ||
                    !appendString(line, sizeof(line), "=") ||
                    !appendString(line, sizeof(line), exp) ||
                    !appendString(line, sizeof(line), "\n"))
                {
                    ok = false;
                    break;
                }
                ok = ok && writeAll(file, line);
            }

            QFS::VFS::instance().close(file);
            if (!ok && ctx)
                ctx->writeLine("alias: failed while writing map");
            return ok;
        }

        static bool loadAliasMap(const QC::Cmd::Context *ctx, bool report)
        {
            QFS::FileInfo info;
            QC::String::memset(&info, 0, sizeof(info));
            if (QFS::VFS::instance().stat(kAliasMapPath, &info) != QC::Status::Success)
            {
                if (report && ctx)
                    ctx->writeLine("aliasreload: no persisted alias map");
                return true;
            }

            QC::Vector<char> fileBuf;
            if (!readFileToNullTerminatedBuffer(kAliasMapPath, fileBuf, 64 * 1024))
            {
                if (report && ctx)
                    ctx->writeLine("aliasreload: read failed");
                return false;
            }

            clearAliasMap();

            auto &reg = QC::Cmd::Registry::instance();
            QC::usize loaded = 0;

            char line[320];
            QC::String::memset(line, 0, sizeof(line));
            QC::usize li = 0;

            auto parseLine = [&](char *raw)
            {
                trimInPlace(raw);
                if (raw[0] == '\0' || raw[0] == '#')
                    return;

                char *eq = raw;
                while (*eq && *eq != '=')
                    ++eq;
                if (*eq != '=')
                    return;

                *eq = '\0';
                char *rhs = eq + 1;
                trimInPlace(raw);
                trimInPlace(rhs);
                if (raw[0] == '\0' || rhs[0] == '\0')
                    return;

                if (reg.registerAlias(raw, rhs, true))
                    ++loaded;
            };

            const char *p = fileBuf.data();
            while (p && *p)
            {
                char c = *p++;
                if (c == '\r')
                    continue;
                if (c == '\n')
                {
                    line[li] = '\0';
                    parseLine(line);
                    li = 0;
                    line[0] = '\0';
                    continue;
                }
                if (li + 1 < sizeof(line))
                    line[li++] = c;
            }
            if (li)
            {
                line[li] = '\0';
                parseLine(line);
            }

            if (report && ctx)
            {
                char msg[96];
                QC::String::memset(msg, 0, sizeof(msg));
                (void)appendString(msg, sizeof(msg), "aliasreload: loaded ");
                (void)appendU64Dec(msg, sizeof(msg), static_cast<QC::u64>(loaded));
                ctx->writeLine(msg);
            }

            return true;
        }

        static bool readFileToNullTerminatedBuffer(const char *absPath, QC::Vector<char> &out, QC::usize maxBytes)
        {
            out.clear();

            if (!absPath || absPath[0] == '\0')
                return false;

            QFS::FileInfo info;
            QC::String::memset(&info, 0, sizeof(info));
            const QC::Status st = QFS::VFS::instance().stat(absPath, &info);
            if (st != QC::Status::Success)
                return false;
            if (info.type != QFS::FileType::Regular)
                return false;
            if (info.size > maxBytes)
                return false;

            const QC::usize size = static_cast<QC::usize>(info.size);
            out.resize(size + 1);
            if (out.size() != size + 1)
            {
                out.clear();
                return false;
            }

            QC::usize bytesRead = 0;
            const QC::Status readSt = QK::SecurityCenter::instance().secureReadFile(absPath, out.data(), size, &bytesRead);
            if (readSt != QC::Status::Success || bytesRead != size)
            {
                out.clear();
                return false;
            }

            out[size] = '\0';
            return true;
        }

        static bool validateJsonFile(const char *absPath, const QC::Cmd::Context &ctx, const char *label)
        {
            QC::Vector<char> buf;
            if (!readFileToNullTerminatedBuffer(absPath, buf, 1024 * 1024))
            {
                char line[256];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), label);
                (void)appendString(line, sizeof(line), ": missing or unreadable: ");
                (void)appendString(line, sizeof(line), absPath);
                ctx.writeLine(line);
                return false;
            }

            QC::JSON::Value root;
            const bool ok = QC::JSON::parse(buf.data(), root);
            if (!ok)
            {
                char line[256];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), label);
                (void)appendString(line, sizeof(line), ": JSON parse failed: ");
                (void)appendString(line, sizeof(line), absPath);
                ctx.writeLine(line);
                return false;
            }

            char line[256];
            QC::String::memset(line, 0, sizeof(line));
            (void)appendString(line, sizeof(line), label);
            (void)appendString(line, sizeof(line), ": ok ");
            (void)appendString(line, sizeof(line), absPath);
            ctx.writeLine(line);
            return true;
        }

        static bool copyFileTruncate(const char *srcAbsPath, const char *dstAbsPath, const QC::Cmd::Context &ctx, const char *label)
        {
            if (!srcAbsPath || !dstAbsPath)
                return false;

            if (!allowWriteToPath(dstAbsPath, ctx, label))
                return false;

            QFS::File *src = QFS::VFS::instance().open(srcAbsPath, QFS::OpenMode::Read);
            if (!src)
            {
                char line[256];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), label);
                (void)appendString(line, sizeof(line), ": source missing: ");
                (void)appendString(line, sizeof(line), srcAbsPath);
                ctx.writeLine(line);
                return false;
            }

            QFS::File *dst = QFS::VFS::instance().open(dstAbsPath, QFS::OpenMode::Write | QFS::OpenMode::Create | QFS::OpenMode::Truncate);
            if (!dst)
            {
                QFS::VFS::instance().close(src);
                char line[256];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), label);
                (void)appendString(line, sizeof(line), ": cannot open dest: ");
                (void)appendString(line, sizeof(line), dstAbsPath);
                ctx.writeLine(line);
                return false;
            }

            char buf[4096];
            while (true)
            {
                QC::isize n = src->read(buf, sizeof(buf));
                if (n <= 0)
                    break;

                QC::usize off = 0;
                while (off < static_cast<QC::usize>(n))
                {
                    QC::isize w = dst->write(buf + off, static_cast<QC::usize>(n) - off);
                    if (w <= 0)
                    {
                        QFS::VFS::instance().close(dst);
                        QFS::VFS::instance().close(src);
                        ctx.writeLine("recover: write failed");
                        return false;
                    }
                    off += static_cast<QC::usize>(w);
                }
            }

            QFS::VFS::instance().close(dst);
            QFS::VFS::instance().close(src);

            char line[256];
            QC::String::memset(line, 0, sizeof(line));
            (void)appendString(line, sizeof(line), label);
            (void)appendString(line, sizeof(line), ": restored ");
            (void)appendString(line, sizeof(line), dstAbsPath);
            ctx.writeLine(line);
            return true;
        }

        static bool cmdRecover(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            const char *p = args ? skipSpaces(args) : nullptr;
            if (!p || *p == '\0')
            {
                ctx.writeLine("usage: recover config|desktop|services");
                return true;
            }

            char which[32];
            QC::String::memset(which, 0, sizeof(which));
            (void)readToken(p, which, sizeof(which));

            if (streqIgnoreCase(which, "config"))
            {
                // Default to boot.json (paths.golden_config -> paths.production_config). If parse fails, use known defaults.
                char srcBuf[256];
                char dstBuf[256];
                QC::String::memset(srcBuf, 0, sizeof(srcBuf));
                QC::String::memset(dstBuf, 0, sizeof(dstBuf));
                QC::String::strncpy(srcBuf, "/system/golden/config.json", sizeof(srcBuf) - 1);
                QC::String::strncpy(dstBuf, "/system/config/config.json", sizeof(dstBuf) - 1);
                srcBuf[sizeof(srcBuf) - 1] = '\0';
                dstBuf[sizeof(dstBuf) - 1] = '\0';

                QC::Vector<char> bootBuf;
                const char *bootCandidates[] = {"/boot.json", "/BOOT.JSN"};
                for (QC::usize i = 0; i < sizeof(bootCandidates) / sizeof(bootCandidates[0]); ++i)
                {
                    if (!readFileToNullTerminatedBuffer(bootCandidates[i], bootBuf, 256 * 1024))
                        continue;

                    QC::JSON::Value root;
                    if (!QC::JSON::parse(bootBuf.data(), root))
                        continue;

                    // Expect: { paths: { golden_config: <str>, production_config: <str> } }
                    const QC::JSON::Value *paths = root.find("paths");
                    if (paths && paths->isObject())
                    {
                        const QC::JSON::Value *g = paths->find("golden_config");
                        const QC::JSON::Value *r = paths->find("production_config");
                        if (g && r && g->isString() && r->isString())
                        {
                            const char *gs = g->asString(nullptr);
                            const char *rs = r->asString(nullptr);
                            if (gs && *gs)
                            {
                                QC::String::strncpy(srcBuf, gs, sizeof(srcBuf) - 1);
                                srcBuf[sizeof(srcBuf) - 1] = '\0';
                            }
                            if (rs && *rs)
                            {
                                QC::String::strncpy(dstBuf, rs, sizeof(dstBuf) - 1);
                                dstBuf[sizeof(dstBuf) - 1] = '\0';
                            }
                        }
                    }
                    break;
                }

                bool ok = true;
                ok = copyFileTruncate(srcBuf, dstBuf, ctx, "recover config") && ok;
                ok = validateJsonFile(dstBuf, ctx, "validate config") && ok;
                ctx.writeLine(ok ? "recover config: ok" : "recover config: failed");
                return true;
            }

            if (streqIgnoreCase(which, "desktop"))
            {
                bool ok = true;
                ok = copyFileTruncate("/GOLDEN/DESKTOP.JSN", "/PROD/DESKTOP.JSN", ctx, "recover desktop") && ok;

                // Overrides are optional; copy only if present.
                QFS::FileInfo info;
                QC::String::memset(&info, 0, sizeof(info));
                if (QFS::VFS::instance().stat("/GOLDEN/DESKOVR.JSN", &info) == QC::Status::Success)
                {
                    ok = copyFileTruncate("/GOLDEN/DESKOVR.JSN", "/PROD/DESKOVR.JSN", ctx, "recover desktop") && ok;
                }
                else
                {
                    ctx.writeLine("recover desktop: /GOLDEN/DESKOVR.JSN missing (skipping overrides)");
                }

                ok = validateJsonFile("/PROD/DESKTOP.JSN", ctx, "validate desktop") && ok;
                if (QFS::VFS::instance().stat("/PROD/DESKOVR.JSN", &info) == QC::Status::Success)
                    ok = validateJsonFile("/PROD/DESKOVR.JSN", ctx, "validate desktop_overrides") && ok;

                ctx.writeLine(ok ? "recover desktop: ok" : "recover desktop: failed");
                return true;
            }

            if (streqIgnoreCase(which, "services"))
            {
                bool ok = true;
                ok = copyFileTruncate("/GOLDEN/SERVICES.JSN", "/PROD/SERVICES.JSN", ctx, "recover services") && ok;
                ok = validateJsonFile("/PROD/SERVICES.JSN", ctx, "validate services") && ok;
                ctx.writeLine(ok ? "recover services: ok" : "recover services: failed");
                return true;
            }

            ctx.writeLine("recover: unknown target");
            ctx.writeLine("usage: recover config|desktop|services");
            return true;
        }

        static bool cmdValidate(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            const char *p = args ? skipSpaces(args) : nullptr;
            char which[32];
            QC::String::memset(which, 0, sizeof(which));
            if (p && *p)
                (void)readToken(p, which, sizeof(which));

            bool ok = true;
            if (which[0] == 0 || streqIgnoreCase(which, "all"))
            {
                // config
                ok = validateJsonFile("/system/config/config.json", ctx, "validate config") && ok;
                // desktop
                ok = validateJsonFile("/PROD/DESKTOP.JSN", ctx, "validate desktop") && ok;
                QFS::FileInfo info;
                QC::String::memset(&info, 0, sizeof(info));
                if (QFS::VFS::instance().stat("/PROD/DESKOVR.JSN", &info) == QC::Status::Success)
                    ok = validateJsonFile("/PROD/DESKOVR.JSN", ctx, "validate desktop_overrides") && ok;
                // services
                ok = validateJsonFile("/PROD/SERVICES.JSN", ctx, "validate services") && ok;

                ctx.writeLine(ok ? "validate: ok" : "validate: failed");
                return true;
            }

            if (streqIgnoreCase(which, "config"))
                ok = validateJsonFile("/system/config/config.json", ctx, "validate config");
            else if (streqIgnoreCase(which, "desktop"))
            {
                ok = validateJsonFile("/PROD/DESKTOP.JSN", ctx, "validate desktop");
                QFS::FileInfo info;
                QC::String::memset(&info, 0, sizeof(info));
                if (QFS::VFS::instance().stat("/PROD/DESKOVR.JSN", &info) == QC::Status::Success)
                    ok = validateJsonFile("/PROD/DESKOVR.JSN", ctx, "validate desktop_overrides") && ok;
            }
            else if (streqIgnoreCase(which, "services"))
                ok = validateJsonFile("/PROD/SERVICES.JSN", ctx, "validate services");
            else
            {
                ctx.writeLine("usage: validate [all|config|desktop|services]");
                return true;
            }

            ctx.writeLine(ok ? "validate: ok" : "validate: failed");
            return true;
        }

        static void rebootHardwareNow()
        {
            // Try common reboot mechanisms.
            // 1) PCI reset control port (many chipsets/QEMU)
            QC::outb(0xCF9, 0x02);
            QC::outb(0xCF9, 0x06);

            // 2) Keyboard controller reset
            for (QC::u32 i = 0; i < 100000; ++i)
            {
                if ((QC::inb(0x64) & 0x02) == 0)
                    break;
            }
            QC::outb(0x64, 0xFE);

            // 3) Fast A20/reset port (legacy)
            QC::u8 p92 = QC::inb(0x92);
            QC::outb(0x92, static_cast<QC::u8>(p92 | 0x01));

            QC::cli();
            for (;;)
                QC::halt();
        }

        static bool cmdReboot(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            const char *p = args ? skipSpaces(args) : nullptr;
            if (!p || *p == '\0')
            {
                ctx.writeLine("reboot: confirmation required");
                ctx.writeLine("usage: reboot now");
                return true;
            }

            char tok[16];
            QC::String::memset(tok, 0, sizeof(tok));
            (void)readToken(p, tok, sizeof(tok));
            if (!streqIgnoreCase(tok, "now"))
            {
                ctx.writeLine("reboot: confirmation required");
                ctx.writeLine("usage: reboot now");
                return true;
            }

            ctx.writeLine("reboot: restarting");
            rebootHardwareNow();
            return true;
        }

        static bool normalizeAbsolutePath(const char *input, char *out, QC::usize outSize)
        {
            if (!input || !out || outSize == 0)
                return false;
            if (input[0] != '/')
                return false;

            const char *segments[32];
            QC::usize segLens[32];
            QC::usize segCount = 0;

            const char *p = input;
            while (*p)
            {
                while (*p == '/')
                    ++p;
                if (*p == '\0')
                    break;

                const char *segStart = p;
                while (*p && *p != '/')
                    ++p;
                QC::usize segLen = static_cast<QC::usize>(p - segStart);

                if (segLen == 0 || segmentEquals(segStart, segLen, "."))
                    continue;
                if (segmentEquals(segStart, segLen, ".."))
                {
                    if (segCount > 0)
                        --segCount;
                    continue;
                }
                if (segCount < 32)
                {
                    segments[segCount] = segStart;
                    segLens[segCount] = segLen;
                    ++segCount;
                }
            }

            if (outSize < 2)
                return false;

            QC::usize idx = 0;
            out[idx++] = '/';
            for (QC::usize i = 0; i < segCount; ++i)
            {
                if (i != 0)
                {
                    if (idx + 1 >= outSize)
                        return false;
                    out[idx++] = '/';
                }

                if (idx + segLens[i] >= outSize)
                    return false;
                for (QC::usize j = 0; j < segLens[i]; ++j)
                    out[idx++] = segments[i][j];
            }

            out[idx] = '\0';
            return true;
        }

        static bool appendString(char *dest, QC::usize destSize, const char *src)
        {
            if (!dest || destSize == 0)
                return false;
            if (!src || *src == '\0')
                return true;

            QC::usize destLen = QC::String::strlen(dest);
            QC::usize srcLen = QC::String::strlen(src);
            if (destLen >= destSize)
                return false;

            QC::usize remaining = destSize - destLen - 1;
            if (srcLen > remaining)
            {
                QC::String::memcpy(dest + destLen, src, remaining);
                dest[destLen + remaining] = '\0';
                return false;
            }

            QC::String::memcpy(dest + destLen, src, srcLen);
            dest[destLen + srcLen] = '\0';
            return true;
        }

        static bool appendU64Dec(char *dest, QC::usize destSize, QC::u64 v)
        {
            char num[32];
            QC::String::memset(num, 0, sizeof(num));

            int numIdx = 0;
            if (v == 0)
            {
                num[numIdx++] = '0';
            }
            else
            {
                char tmp[32];
                int tmpIdx = 0;
                while (v > 0 && tmpIdx < 31)
                {
                    tmp[tmpIdx++] = static_cast<char>('0' + (v % 10));
                    v /= 10;
                }
                for (int i = tmpIdx - 1; i >= 0; --i)
                    num[numIdx++] = tmp[i];
            }
            num[numIdx] = '\0';
            return appendString(dest, destSize, num);
        }

        static bool appendI64Dec(char *dest, QC::usize destSize, QC::i64 v)
        {
            if (v < 0)
            {
                if (!appendString(dest, destSize, "-"))
                    return false;
                // Avoid UB for INT64_MIN: convert via u64.
                const QC::u64 mag = static_cast<QC::u64>(-(v + 1)) + 1;
                return appendU64Dec(dest, destSize, mag);
            }
            return appendU64Dec(dest, destSize, static_cast<QC::u64>(v));
        }

        static bool resolvePath(const Session *session, const char *input, char *out, QC::usize outSize)
        {
            if (!out || outSize == 0)
                return false;

            const char *arg = input;
            if (!arg || *arg == '\0')
                arg = ".";

            const char *cwd = (session && session->cwd[0]) ? session->cwd : "/";

            char tmp[256];
            QC::String::memset(tmp, 0, sizeof(tmp));

            if (arg[0] == '/')
            {
                QC::String::strncpy(tmp, arg, sizeof(tmp) - 1);
            }
            else
            {
                if (QC::String::strcmp(cwd, "/") == 0)
                {
                    QC::String::strncpy(tmp, "/", sizeof(tmp) - 1);
                    if (!appendString(tmp, sizeof(tmp), arg))
                        return false;
                }
                else
                {
                    QC::String::strncpy(tmp, cwd, sizeof(tmp) - 1);
                    if (!appendString(tmp, sizeof(tmp), "/"))
                        return false;
                    if (!appendString(tmp, sizeof(tmp), arg))
                        return false;
                }
            }

            return normalizeAbsolutePath(tmp, out, outSize);
        }

        static Session *sessionFrom()
        {
            if (!g_sessionInitialized)
            {
                initSession(g_session);
                g_sessionInitialized = true;
            }
            return &g_session;
        }

        static bool parseU32(const char *text, QC::u32 &out)
        {
            out = 0;
            if (!text)
                return false;
            const char *p = skipSpaces(text);
            if (*p == '\0')
                return false;
            while (*p)
            {
                if (*p < '0' || *p > '9')
                    break;
                out = out * 10 + static_cast<QC::u32>(*p - '0');
                ++p;
            }
            return true;
        }

        static bool parseIPv4(const char *text, QNet::IPv4Address &out)
        {
            if (!text)
                return false;

            const char *p = skipSpaces(text);
            QC::u32 parts[4] = {0, 0, 0, 0};
            for (int i = 0; i < 4; ++i)
            {
                if (*p < '0' || *p > '9')
                    return false;
                QC::u32 v = 0;
                while (*p >= '0' && *p <= '9')
                {
                    v = v * 10 + static_cast<QC::u32>(*p - '0');
                    if (v > 255)
                        return false;
                    ++p;
                }
                parts[i] = v;
                if (i != 3)
                {
                    if (*p != '.')
                        return false;
                    ++p;
                }
            }

            out.octets[0] = static_cast<QC::u8>(parts[0]);
            out.octets[1] = static_cast<QC::u8>(parts[1]);
            out.octets[2] = static_cast<QC::u8>(parts[2]);
            out.octets[3] = static_cast<QC::u8>(parts[3]);
            return true;
        }

        static bool readToken(const char *&p, char *out, QC::usize outSize)
        {
            if (!out || outSize == 0)
                return false;
            QC::String::memset(out, 0, outSize);

            p = p ? skipSpaces(p) : nullptr;
            if (!p || *p == '\0')
                return false;

            QC::usize i = 0;
            if (*p == '"')
            {
                ++p;
                while (*p && i + 1 < outSize)
                {
                    if (*p == '"')
                    {
                        ++p;
                        break;
                    }
                    if (*p == '\\' && p[1])
                    {
                        out[i++] = p[1];
                        p += 2;
                        continue;
                    }
                    out[i++] = *p++;
                }
            }
            else
            {
                while (*p && !isSpace(*p) && i + 1 < outSize)
                    out[i++] = *p++;
            }
            out[i] = '\0';
            p = skipSpaces(p);
            return i > 0;
        }

        static bool resolveHostOrIPv4(const char *token, QNet::IPv4Address &out, const QC::Cmd::Context &ctx, QC::u32 timeoutMs)
        {
            if (!token || *token == '\0')
                return false;
            if (parseIPv4(token, out))
                return true;

            const QNet::IPv4Address dns = QNet::Stack::instance().ip()->dnsServer();
            if (dns.value == 0)
            {
                ctx.writeLine("resolve: no dns configured");
                return false;
            }
            if (!QK::Time::available() || !QK::System::pumpAvailable())
            {
                ctx.writeLine("resolve: unavailable (no time/pump)");
                return false;
            }

            QNet::DNSClient client;
            const QC::u16 txid = static_cast<QC::u16>((QK::Time::milliseconds() & 0xFFFF) ^ 0x5A5A);
            if (client.begin(dns, token, txid) != QC::Status::Success)
            {
                ctx.writeLine("resolve: query failed");
                return false;
            }

            const QC::u64 deadlineMs = QK::Time::milliseconds() + static_cast<QC::u64>(timeoutMs);
            while (QK::Time::milliseconds() < deadlineMs)
            {
                QK::System::pump();
                if (client.poll(&out))
                    return true;
                QK::Time::sleep(10);
            }

            ctx.writeLine("resolve: timeout");
            return false;
        }

        static void ipv4ToString(QNet::IPv4Address addr, char *out, QC::usize outSize)
        {
            if (!out || outSize == 0)
                return;
            QC::String::memset(out, 0, outSize);

            auto appendDec = [&](QC::u32 v)
            {
                char rev[10];
                int ri = 0;
                if (v == 0)
                    rev[ri++] = '0';
                while (v > 0 && ri < (int)sizeof(rev))
                {
                    rev[ri++] = (char)('0' + (v % 10));
                    v /= 10;
                }

                char tmp[12];
                QC::String::memset(tmp, 0, sizeof(tmp));
                int ti = 0;
                while (ri > 0)
                    tmp[ti++] = rev[--ri];
                tmp[ti] = '\0';
                (void)appendString(out, outSize, tmp);
            };

            appendDec(addr.octets[0]);
            (void)appendString(out, outSize, ".");
            appendDec(addr.octets[1]);
            (void)appendString(out, outSize, ".");
            appendDec(addr.octets[2]);
            (void)appendString(out, outSize, ".");
            appendDec(addr.octets[3]);
        }

        static void macToString(const QNet::MACAddress &mac, char *out, QC::usize outSize)
        {
            if (!out || outSize == 0)
                return;
            QC::String::memset(out, 0, outSize);

            const char hex[] = "0123456789abcdef";
            QC::usize idx = 0;
            for (int i = 0; i < 6; ++i)
            {
                if (i != 0)
                {
                    if (idx + 1 >= outSize)
                        break;
                    out[idx++] = ':';
                }

                const QC::u8 b = mac.bytes[i];
                if (idx + 2 >= outSize)
                    break;
                out[idx++] = hex[(b >> 4) & 0xF];
                out[idx++] = hex[b & 0xF];
            }
            if (idx < outSize)
                out[idx] = '\0';
        }

        // ---------------- Commands ----------------

        static bool cmdHelp(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            const char *q = args ? skipSpaces(args) : nullptr;
            if (q && *q)
            {
                // Extract first token as command name.
                char name[48];
                QC::String::memset(name, 0, sizeof(name));
                QC::usize ni = 0;
                while (*q && !isSpace(*q) && ni + 1 < sizeof(name))
                {
                    name[ni++] = *q++;
                }
                name[ni] = '\0';

                auto &reg = QC::Cmd::Registry::instance();
                bool found = false;
                for (QC::usize i = 0; i < reg.commandCount(); ++i)
                {
                    const char *n = reg.commandNameAt(i);
                    if (!n)
                        continue;
                    if (!streqIgnoreCase(n, name))
                        continue;

                    found = true;
                    if (static_cast<QC::u8>(ctx.callerAccess) < static_cast<QC::u8>(reg.commandAccessAt(i)))
                    {
                        ctx.writeLine("help: permission denied");
                        return true;
                    }

                    const char *desc = reg.commandDescriptionAt(i);
                    writeKeyValue(ctx, name, desc ? desc : "");
                        const char *usage = reg.commandUsageAt(i);
                        const char *schema = reg.commandArgSchemaAt(i);
                        if (usage && *usage)
                            writeKeyValue(ctx, "usage", usage);
                        if (schema && *schema)
                            writeKeyValue(ctx, "schema", schema);
                    return true;
                }
                if (!found)
                    ctx.writeLine("help: command not found");
                return true;
            }

            ctx.writeLine("Commands:");
            auto &reg = QC::Cmd::Registry::instance();
            for (QC::usize i = 0; i < reg.commandCount(); ++i)
            {
                const char *name = reg.commandNameAt(i);
                if (!name)
                    continue;
                if (static_cast<QC::u8>(ctx.callerAccess) < static_cast<QC::u8>(reg.commandAccessAt(i)))
                    continue;
                const char *desc = reg.commandDescriptionAt(i);

                char line[256];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "  ");
                (void)appendString(line, sizeof(line), name);
                if (desc && *desc)
                {
                    (void)appendString(line, sizeof(line), " - ");
                    (void)appendString(line, sizeof(line), desc);
                }
                const char *usage = reg.commandUsageAt(i);
                if (usage && *usage)
                {
                    (void)appendString(line, sizeof(line), " [");
                    (void)appendString(line, sizeof(line), usage);
                    (void)appendString(line, sizeof(line), "]");
                }
                ctx.writeLine(line);
            }
            return true;
        }

        static bool cmdVideo(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            char sub[32];
            QC::String::memset(sub, 0, sizeof(sub));
            const char *p = args ? skipSpaces(args) : nullptr;
            if (!p || *p == '\0' || !readToken(p, sub, sizeof(sub)))
            {
                ctx.writeLine("usage: video <stats|reset>");
                return true;
            }

            auto &wm = QW::WindowManager::instance();
            QW::Compositor *compositor = wm.compositor();
            if (streqIgnoreCase(sub, "reset"))
            {
                QDrv::Display::cvd_reset_present_debug_stats();
                wm.resetInputLatencyStats();
                if (compositor)
                    compositor->resetStats();
                ctx.writeLine("video: stats reset");
                return true;
            }

            if (!streqIgnoreCase(sub, "stats"))
            {
                ctx.writeLine("usage: video <stats|reset>");
                return true;
            }

            ctx.writeLine("Video Stats:");
            QDrv::Display::cvd_present_debug_stats_t presentStats{};
            (void)QDrv::Display::cvd_get_present_debug_stats(&presentStats);
            writeKeyValueBool(ctx, "display_accelerated_present", presentStats.accelerated_present);
            writeKeyValueBool(ctx, "display_accelerated_rect_copy", presentStats.accelerated_rect_copy);
            writeKeyValueBool(ctx, "display_hardware_cursor", presentStats.hardware_cursor);

            const auto &backendStats = presentStats.backend_stats;
            writeKeyValueU32(ctx, "present_backend_update_rect_calls", backendStats.updateRectCalls);
            writeKeyValueU32(ctx, "present_backend_update_rects_calls", backendStats.updateRectsCalls);
            writeKeyValueU32(ctx, "present_backend_queued_rects", backendStats.queuedRectCount);
            writeKeyValueU32(ctx, "present_backend_last_batch_rects", backendStats.lastBatchRectCount);
            writeKeyValueU32(ctx, "present_backend_fifo_syncs", backendStats.fifoSyncCount);
            writeKeyValueU32(ctx, "present_backend_fifo_drops", backendStats.fifoDropCount);
            writeKeyValueU32(ctx, "present_backend_last_sync_busy", backendStats.lastSyncBusyValue);
            writeKeyValueU64(ctx, "mouse_event_queue_delay_ms", wm.lastMouseQueueDelayMs());
            writeKeyValueU64(ctx, "mouse_event_queue_delay_max_ms", wm.maxMouseQueueDelayMs());

            if (!compositor)
            {
                ctx.writeLine("video: desktop compositor not active");
                return true;
            }

            const auto &stats = compositor->stats();
            const auto &accel = compositor->accelerationStats();
            writeKeyValueU64(ctx, "compose_last_ms", stats.lastComposeTimeMs);
            writeKeyValueU64(ctx, "present_last_ms", stats.lastPresentTimeMs);
            writeKeyValueU64(ctx, "present_max_ms", stats.maxPresentTimeMs);
            writeKeyValueU64(ctx, "input_to_present_ms", stats.lastInputToPresentMs);
            writeKeyValueU64(ctx, "input_to_present_max_ms", stats.maxInputToPresentMs);
            writeKeyValueU32(ctx, "compose_frames", stats.frameCount);
            writeKeyValueU64(ctx, "dirty_area", stats.lastDirtyArea);
            writeKeyValueU32(ctx, "dirty_coverage_pct", stats.lastDirtyCoveragePercent);
            writeKeyValueU64(ctx, "dirty_regions_live", static_cast<QC::u64>(stats.dirtyRegionCount));
            writeKeyValueU64(ctx, "dirty_regions_merged", static_cast<QC::u64>(stats.lastMergedDirtyRegionCount));
            writeKeyValueU64(ctx, "present_dirty_rects", static_cast<QC::u64>(stats.lastPresentedDirtyRectCount));
            writeKeyValueU32(ctx, "dirty_collapse_count", stats.dirtyCollapseCount);
            writeKeyValueBool(ctx, "present_full_frame", stats.lastPresentWasFullFrame);
            writeKeyValueBool(ctx, "present_batched", stats.lastPresentUsedBatching);
            writeKeyValueBool(ctx, "hardware_cursor_active", stats.hardwareCursorActive);
            writeKeyValueBool(ctx, "qgfx_active", accel.qgfxActive);
            writeKeyValueBool(ctx, "qgfx_scanout_uploads_active", accel.qgfxScanoutUploadsActive);
            writeKeyValueBool(ctx, "qgfx_rect_copy_active", accel.qgfxRectCopyActive);
            writeKeyValueU32(ctx, "qgfx_present_calls", accel.qgfxPresentCalls);
            writeKeyValueU32(ctx, "qgfx_present_successes", accel.qgfxPresentSuccesses);
            writeKeyValueU32(ctx, "qgfx_scanout_upload_calls", accel.qgfxScanoutUploadCalls);
            writeKeyValueU32(ctx, "qgfx_scanout_upload_rects", accel.qgfxScanoutUploadRects);
            writeKeyValueU32(ctx, "qgfx_scanout_upload_fallbacks", accel.qgfxScanoutUploadFallbacks);
            writeKeyValueU32(ctx, "qgfx_rect_copy_batches", accel.qgfxRectCopyBatches);
            writeKeyValueU32(ctx, "qgfx_rect_copy_ops", accel.qgfxRectCopyOps);
            return true;
        }

        static bool cmdEcho(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            Session *s = sessionFrom();
            const char *p = args ? skipSpaces(args) : nullptr;
            if (!p || *p == '\0')
            {
                ctx.writeLine("");
                return true;
            }

            // Support basic stdout redirection:
            //   echo "text" > /path/file
            //   echo "text" >> /path/file
            // If no redirection operator is present, preserve previous behavior.
            bool inQuotes = false;
            const char *redir = nullptr;
            for (const char *q = p; *q; ++q)
            {
                if (*q == '"')
                {
                    inQuotes = !inQuotes;
                    continue;
                }
                if (!inQuotes && *q == '>')
                {
                    redir = q;
                    break;
                }
            }

            if (!redir)
            {
                ctx.writeLine(p);
                return true;
            }

            // Redirection writes to the filesystem; require admin.
            if (static_cast<QC::u8>(ctx.callerAccess) < static_cast<QC::u8>(QC::Cmd::AccessLevel::Admin))
            {
                ctx.writeLine("echo: permission denied (redirection requires admin)");
                return true;
            }

            bool appendMode = false;
            const char *opEnd = redir + 1;
            if (*opEnd == '>')
            {
                appendMode = true;
                ++opEnd;
            }

            // Left side (text): [p, redir)
            const char *textStart = p;
            const char *textEnd = redir;
            while (textEnd > textStart && isSpace(*(textEnd - 1)))
                --textEnd;
            if ((textEnd - textStart) >= 2 && *textStart == '"' && *(textEnd - 1) == '"')
            {
                ++textStart;
                --textEnd;
            }

            // Right side (path): after operator
            const char *pathStart = skipSpaces(opEnd);
            if (!pathStart || *pathStart == '\0')
            {
                ctx.writeLine("echo: missing redirection target");
                return true;
            }

            const char *pathEnd = pathStart + QC::String::strlen(pathStart);
            while (pathEnd > pathStart && isSpace(*(pathEnd - 1)))
                --pathEnd;
            if ((pathEnd - pathStart) >= 2 && *pathStart == '"' && *(pathEnd - 1) == '"')
            {
                ++pathStart;
                --pathEnd;
            }
            if (pathEnd <= pathStart)
            {
                ctx.writeLine("echo: missing redirection target");
                return true;
            }

            char fileArg[256];
            QC::String::memset(fileArg, 0, sizeof(fileArg));
            QC::usize fi = 0;
            for (const char *q = pathStart; q < pathEnd && fi + 1 < sizeof(fileArg); ++q)
                fileArg[fi++] = *q;
            fileArg[fi] = '\0';

            if (pathStart + fi != pathEnd)
            {
                ctx.writeLine("echo: path too long");
                return true;
            }

            char path[256];
            QC::String::memset(path, 0, sizeof(path));
            if (!resolvePath(s, fileArg, path, sizeof(path)))
            {
                ctx.writeLine("echo: invalid path");
                return true;
            }

            if (!allowWriteToPath(path, ctx, "echo"))
                return true;

            QFS::OpenMode mode = QFS::OpenMode::Write | QFS::OpenMode::Create;
            if (!appendMode)
                mode = mode | QFS::OpenMode::Truncate;

            char lineOut[384];
            QC::String::memset(lineOut, 0, sizeof(lineOut));
            QC::usize li = 0;
            for (const char *q = textStart; q < textEnd && li + 1 < sizeof(lineOut); ++q)
                lineOut[li++] = *q;
            if (li + 2 < sizeof(lineOut))
            {
                lineOut[li++] = '\r';
                lineOut[li++] = '\n';
            }
            lineOut[li] = '\0';

            const QC::Status wst = QK::SecurityCenter::instance().secureWriteFile(path, lineOut, li, appendMode);
            if (wst != QC::Status::Success)
            {
                ctx.writeLine("echo: cannot open output file");
                return true;
            }
            return true;
        }

        static bool cmdPwd(const char *, const QC::Cmd::Context &ctx, void *)
        {
            Session *s = sessionFrom();
            ctx.writeLine((s && s->cwd[0]) ? s->cwd : "/");
            return true;
        }

        static bool cmdCd(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            Session *s = sessionFrom();

            const char *p = args ? skipSpaces(args) : nullptr;
            const char *arg = (p && *p) ? p : "/";

            char path[256];
            QC::String::memset(path, 0, sizeof(path));
            if (!resolvePath(s, arg, path, sizeof(path)))
            {
                ctx.writeLine("cd: invalid path");
                return true;
            }

            QFS::Directory *dir = QFS::VFS::instance().openDir(path);
            if (!dir)
            {
                ctx.writeLine("cd: no such directory");
                return true;
            }
            QFS::VFS::instance().closeDir(dir);

            QC::String::strncpy(s->cwd, path, sizeof(s->cwd) - 1);
            s->cwd[sizeof(s->cwd) - 1] = '\0';
            return true;
        }

        static bool cmdLs(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            Session *s = sessionFrom();
            const char *p = args ? skipSpaces(args) : nullptr;
            const char *arg = (p && *p) ? p : ".";

            char path[256];
            QC::String::memset(path, 0, sizeof(path));
            if (!resolvePath(s, arg, path, sizeof(path)))
            {
                ctx.writeLine("ls: invalid path");
                return true;
            }

            QFS::Directory *dir = QFS::VFS::instance().openDir(path);
            if (!dir)
            {
                ctx.writeLine("ls: cannot open path");
                return true;
            }

            char heading[256];
            QC::String::memset(heading, 0, sizeof(heading));
            (void)appendString(heading, sizeof(heading), "Listing ");
            (void)appendString(heading, sizeof(heading), path);
            ctx.writeLine(heading);

            QFS::DirEntry entry;
            while (dir->read(&entry))
            {
                char line[256];
                QC::String::memset(line, 0, sizeof(line));
                QC::usize pos = 0;

                char typeChar = '-';
                if (entry.type == QFS::FileType::Directory)
                    typeChar = 'd';
                else if (entry.type == QFS::FileType::SymLink)
                    typeChar = 'l';
                line[pos++] = typeChar;
                line[pos++] = ' ';

                // Size (decimal)
                char sizeBuf[32];
                QC::String::memset(sizeBuf, 0, sizeof(sizeBuf));
                QC::u64 value = entry.size;
                int sizeIdx = 0;
                if (value == 0)
                {
                    sizeBuf[sizeIdx++] = '0';
                }
                else
                {
                    char temp[32];
                    int tempIdx = 0;
                    while (value > 0 && tempIdx < 31)
                    {
                        temp[tempIdx++] = static_cast<char>('0' + (value % 10));
                        value /= 10;
                    }
                    for (int i = tempIdx - 1; i >= 0; --i)
                        sizeBuf[sizeIdx++] = temp[i];
                }
                for (int i = 0; i < sizeIdx && pos + 1 < sizeof(line); ++i)
                    line[pos++] = sizeBuf[i];
                line[pos++] = ' ';

                for (int i = 0; entry.name[i] && pos + 1 < sizeof(line); ++i)
                    line[pos++] = entry.name[i];

                line[pos] = '\0';
                ctx.writeLine(line);
            }

            QFS::VFS::instance().closeDir(dir);
            return true;
        }

        static bool cmdCat(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            Session *s = sessionFrom();
            const char *p = args ? skipSpaces(args) : nullptr;
            if (!p || *p == '\0')
            {
                ctx.writeLine("cat: missing file operand");
                return true;
            }

            // Extract first token as path.
            char fileArg[256];
            QC::String::memset(fileArg, 0, sizeof(fileArg));
            QC::usize fi = 0;
            while (*p && !isSpace(*p) && fi + 1 < sizeof(fileArg))
            {
                fileArg[fi++] = *p++;
            }
            fileArg[fi] = '\0';

            char path[256];
            QC::String::memset(path, 0, sizeof(path));
            if (!resolvePath(s, fileArg, path, sizeof(path)))
            {
                ctx.writeLine("cat: invalid path");
                return true;
            }

            QC::Vector<char> fileBuf;
            if (!readFileToNullTerminatedBuffer(path, fileBuf, 256 * 1024))
            {
                ctx.writeLine("cat: cannot open file");
                return true;
            }

            // Stream as lines.
            char lineBuf[512];
            QC::usize lineLen = 0;
            QC::String::memset(lineBuf, 0, sizeof(lineBuf));

            const char *in = fileBuf.data();
            for (QC::usize k = 0; in && in[k]; ++k)
            {
                char c = in[k];
                if (c == '\r')
                    continue;
                if (c == '\n')
                {
                    lineBuf[lineLen] = '\0';
                    ctx.writeLine(lineBuf);
                    lineLen = 0;
                    continue;
                }

                if (lineLen + 1 >= sizeof(lineBuf))
                {
                    lineBuf[lineLen] = '\0';
                    ctx.writeLine(lineBuf);
                    lineLen = 0;
                }
                lineBuf[lineLen++] = c;
            }

            if (lineLen > 0)
            {
                lineBuf[lineLen] = '\0';
                ctx.writeLine(lineBuf);
            }
            return true;
        }

        static bool cmdStat(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            Session *s = sessionFrom();
            char tok[256];
            QC::String::memset(tok, 0, sizeof(tok));
            const char *p = args;
            if (!readToken(p, tok, sizeof(tok)))
            {
                ctx.writeLine("usage: stat <path>");
                return true;
            }

            char path[256];
            QC::String::memset(path, 0, sizeof(path));
            if (!resolvePath(s, tok, path, sizeof(path)))
            {
                ctx.writeLine("stat: invalid path");
                return true;
            }

            QFS::FileInfo info;
            QC::String::memset(&info, 0, sizeof(info));
            const QC::Status st = QFS::statPath(path, &info);
            if (st != QC::Status::Success)
            {
                ctx.writeLine("stat: not found");
                return true;
            }

            const char *type = "other";
            if (info.type == QFS::FileType::Regular)
                type = "file";
            else if (info.type == QFS::FileType::Directory)
                type = "dir";
            else if (info.type == QFS::FileType::SymLink)
                type = "symlink";

            char line[256];
            QC::String::memset(line, 0, sizeof(line));
            (void)appendString(line, sizeof(line), "path=");
            (void)appendString(line, sizeof(line), path);
            (void)appendString(line, sizeof(line), " type=");
            (void)appendString(line, sizeof(line), type);
            (void)appendString(line, sizeof(line), " size=");
            (void)appendU64Dec(line, sizeof(line), info.size);
            ctx.writeLine(line);
            return true;
        }

        static bool cmdSync(const char *, const QC::Cmd::Context &ctx, void *)
        {
            const QC::Status st = QFS::VFS::instance().syncAll();
            if (st == QC::Status::Success)
                ctx.writeLine("sync: ok");
            else
                ctx.writeLine("sync: completed with filesystem errors");
            return true;
        }

        static bool cmdMount(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            char tok[128];
            QC::String::memset(tok, 0, sizeof(tok));
            const char *p = args;
            if (!readToken(p, tok, sizeof(tok)) || QC::String::strcmp(tok, "list") == 0)
            {
                QFS::VolumeInfo info[32] = {};
                const QC::usize count = QFS::VolumeManager::instance().copyVolumeInfo(info, sizeof(info) / sizeof(info[0]));
                if (count == 0)
                {
                    ctx.writeLine("mount: no registered volumes");
                    return true;
                }
                for (QC::usize i = 0; i < count; ++i)
                {
                    char line[384];
                    QC::String::memset(line, 0, sizeof(line));
                    (void)appendString(line, sizeof(line), info[i].name);
                    (void)appendString(line, sizeof(line), " -> ");
                    (void)appendString(line, sizeof(line), info[i].mountPath);
                    (void)appendString(line, sizeof(line), info[i].mounted ? " mounted=" : " mounted=");
                    (void)appendString(line, sizeof(line), info[i].mounted ? "1" : "0");
                    (void)appendString(line, sizeof(line), " auto=");
                    (void)appendString(line, sizeof(line), info[i].autoMount ? "1" : "0");
                    (void)appendString(line, sizeof(line), " persistent=");
                    (void)appendString(line, sizeof(line), info[i].persistent ? "1" : "0");
                    (void)appendString(line, sizeof(line), " class=");
                    (void)appendString(line, sizeof(line), info[i].persistenceClass[0] ? info[i].persistenceClass : "unknown");
                    (void)appendString(line, sizeof(line), " driver=");
                    (void)appendString(line, sizeof(line), info[i].backingDriver[0] ? info[i].backingDriver : "unknown");
                    (void)appendString(line, sizeof(line), " devId=");
                    (void)appendString(line, sizeof(line), info[i].deviceId[0] ? info[i].deviceId : "unknown");
                    if (info[i].sourceKind[0])
                    {
                        (void)appendString(line, sizeof(line), " src=");
                        (void)appendString(line, sizeof(line), info[i].sourceKind);
                    }
                    if (info[i].sourceDetail[0])
                    {
                        (void)appendString(line, sizeof(line), " (");
                        (void)appendString(line, sizeof(line), info[i].sourceDetail);
                        (void)appendString(line, sizeof(line), ")");
                    }
                    ctx.writeLine(line);
                }
                return true;
            }

            QC::Status st = QC::Status::Success;
            if (QC::String::strcmp(tok, "all") == 0)
            {
                (void)applyFstabAutoMountOverrides();
                st = QFS::VolumeManager::instance().mountAll();
            }
            else
            {
                st = QFS::VolumeManager::instance().mountVolume(tok);
            }

            char line[96];
            QC::String::memset(line, 0, sizeof(line));
            (void)appendString(line, sizeof(line), "mount: ");
            (void)appendString(line, sizeof(line), statusName(st));
            ctx.writeLine(line);
            return true;
        }

        static bool cmdUmount(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            char tok[128];
            QC::String::memset(tok, 0, sizeof(tok));
            const char *p = args;
            if (!readToken(p, tok, sizeof(tok)))
            {
                ctx.writeLine("usage: umount <volume|mount_path>");
                return true;
            }

            const QC::Status st = QFS::VolumeManager::instance().unmountVolume(tok);
            char line[96];
            QC::String::memset(line, 0, sizeof(line));
            (void)appendString(line, sizeof(line), "umount: ");
            (void)appendString(line, sizeof(line), statusName(st));
            ctx.writeLine(line);
            return true;
        }

        static bool cmdFstab(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            char op[64];
            QC::String::memset(op, 0, sizeof(op));
            const char *p = args;
            if (!readToken(p, op, sizeof(op)) || QC::String::strcmp(op, "list") == 0)
            {
                QK::Db::Store &db = QK::Db::Store::instance();
                (void)loadFstabStore(db);
                QK::Db::Entry entries[64] = {};
                const QC::usize count = db.list(entries, sizeof(entries) / sizeof(entries[0]));
                if (count == 0)
                {
                    ctx.writeLine("fstab: empty");
                    return true;
                }
                for (QC::usize i = 0; i < count; ++i)
                {
                    if (QC::String::memcmp(entries[i].key, "fstab.", 6) != 0)
                        continue;
                    char line[256];
                    QC::String::memset(line, 0, sizeof(line));
                    (void)appendString(line, sizeof(line), entries[i].key + 6);
                    (void)appendString(line, sizeof(line), " auto=");
                    (void)appendString(line, sizeof(line), entries[i].value);
                    ctx.writeLine(line);
                }
                return true;
            }

            if (QC::String::strcmp(op, "apply") == 0)
            {
                const QC::Status st = applyFstabAutoMountOverrides();
                char line[96];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "fstab: apply=");
                (void)appendString(line, sizeof(line), statusName(st));
                ctx.writeLine(line);
                return true;
            }

            char vol[64];
            QC::String::memset(vol, 0, sizeof(vol));
            if (!readToken(p, vol, sizeof(vol)))
            {
                ctx.writeLine("usage: fstab <list|apply|add|del> <volume>");
                return true;
            }

            QK::Db::Store &db = QK::Db::Store::instance();
            QC::Status st = loadFstabStore(db);
            if (st != QC::Status::Success)
            {
                ctx.writeLine("fstab: load failed");
                return true;
            }

            char key[96];
            fstabKeyForVolume(vol, key, sizeof(key));
            if (QC::String::strcmp(op, "add") == 0)
            {
                (void)QFS::VolumeManager::instance().setAutoMount(vol, true);
                st = db.set(key, "1");
                if (st == QC::Status::Success)
                    st = db.save();
            }
            else if (QC::String::strcmp(op, "del") == 0)
            {
                (void)QFS::VolumeManager::instance().setAutoMount(vol, false);
                st = db.erase(key);
                if (st == QC::Status::Success)
                    st = db.save();
            }
            else
            {
                ctx.writeLine("usage: fstab <list|apply|add|del> <volume>");
                return true;
            }

            char line[96];
            QC::String::memset(line, 0, sizeof(line));
            (void)appendString(line, sizeof(line), "fstab: ");
            (void)appendString(line, sizeof(line), statusName(st));
            ctx.writeLine(line);
            return true;
        }

        static bool cmdTodoAdd(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            const char *p = args ? skipSpaces(args) : nullptr;
            if (!p || !*p)
            {
                ctx.writeLine("usage: todoadd <note text>");
                return true;
            }

            static constexpr const char *kTodoInboxPath = "/system/config/TODO_INBOX.TXT";
            if (!allowWriteToPath(kTodoInboxPath, ctx, "todoadd"))
                return true;

            (void)QFS::VFS::instance().createDir("/system/config");
            QFS::File *f = QFS::VFS::instance().open(kTodoInboxPath,
                                                     QFS::OpenMode::Write | QFS::OpenMode::Create | QFS::OpenMode::Append);
            if (!f)
            {
                ctx.writeLine("todoadd: cannot open inbox");
                return true;
            }

            char line[384];
            QC::String::memset(line, 0, sizeof(line));
            (void)appendString(line, sizeof(line), "- [ ] ");
            (void)appendString(line, sizeof(line), p);
            (void)appendString(line, sizeof(line), "\\n");

            const QC::usize total = QC::String::strlen(line);
            QC::usize off = 0;
            while (off < total)
            {
                const QC::isize n = f->write(line + off, total - off);
                if (n <= 0)
                {
                    QFS::VFS::instance().close(f);
                    ctx.writeLine("todoadd: write failed");
                    return true;
                }
                off += static_cast<QC::usize>(n);
            }

            (void)f->sync();
            QFS::VFS::instance().close(f);
            ctx.writeLine("todoadd: added");
            return true;
        }

        static bool cmdAlias(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            const char *p = args;
            p = p ? skipSpaces(p) : nullptr;

            auto &reg = QC::Cmd::Registry::instance();
            if (!p || *p == '\0')
            {
                if (reg.aliasCount() == 0)
                {
                    ctx.writeLine("alias: (empty)");
                    return true;
                }

                for (QC::usize i = 0; i < reg.aliasCount(); ++i)
                {
                    const char *name = reg.aliasNameAt(i);
                    const char *exp = reg.aliasExpansionAt(i);
                    if (!name || !*name || !exp || !*exp)
                        continue;

                    char line[320];
                    QC::String::memset(line, 0, sizeof(line));
                    (void)appendString(line, sizeof(line), name);
                    (void)appendString(line, sizeof(line), " => ");
                    (void)appendString(line, sizeof(line), exp);
                    ctx.writeLine(line);
                }
                return true;
            }

            char name[48];
            QC::String::memset(name, 0, sizeof(name));
            if (!readToken(p, name, sizeof(name)))
            {
                ctx.writeLine("usage: alias <name> <expansion>");
                return true;
            }

            p = skipSpaces(p);
            if (!p || *p == '\0')
            {
                ctx.writeLine("usage: alias <name> <expansion>");
                return true;
            }

            if (!reg.registerAlias(name, p, true))
            {
                ctx.writeLine("alias: failed");
                return true;
            }

            if (!saveAliasMap(&ctx))
            {
                ctx.writeLine("alias: set but not persisted");
                return true;
            }

            ctx.writeLine("alias: ok");
            return true;
        }

        static bool cmdUnalias(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            const char *p = args;
            char name[48];
            QC::String::memset(name, 0, sizeof(name));
            if (!readToken(p, name, sizeof(name)))
            {
                ctx.writeLine("usage: unalias <name>");
                return true;
            }

            auto &reg = QC::Cmd::Registry::instance();
            if (!reg.removeAlias(name))
            {
                ctx.writeLine("unalias: not found");
                return true;
            }

            if (!saveAliasMap(&ctx))
            {
                ctx.writeLine("unalias: removed but not persisted");
                return true;
            }

            ctx.writeLine("unalias: ok");
            return true;
        }

        static bool cmdAliasReload(const char *, const QC::Cmd::Context &ctx, void *)
        {
            (void)loadAliasMap(&ctx, true);
            return true;
        }

        static bool cmdSource(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            Session *s = sessionFrom();
            char tok[256];
            QC::String::memset(tok, 0, sizeof(tok));
            const char *p = args;
            if (!readToken(p, tok, sizeof(tok)))
            {
                ctx.writeLine("usage: source <file.cmd>");
                return true;
            }

            char path[256];
            QC::String::memset(path, 0, sizeof(path));
            if (!resolvePath(s, tok, path, sizeof(path)))
            {
                ctx.writeLine("source: invalid path");
                return true;
            }

            if (g_scriptDepth >= kMaxScriptDepth)
            {
                ctx.writeLine("source: script nesting limit reached");
                return true;
            }

            QC::Vector<char> buf;
            if (!readFileToNullTerminatedBuffer(path, buf, 128 * 1024))
            {
                ctx.writeLine("source: cannot read script");
                return true;
            }

            ++g_scriptDepth;

            QC::usize commands = 0;
            QC::usize failures = 0;
            char line[320];
            QC::String::memset(line, 0, sizeof(line));
            QC::usize li = 0;

            auto runLine = [&](char *raw)
            {
                trimInPlace(raw);
                if (raw[0] == '\0' || raw[0] == '#')
                    return;
                ++commands;
                if (!QC::Cmd::Registry::instance().execute(raw, ctx))
                    ++failures;
            };

            const char *it = buf.data();
            while (it && *it)
            {
                char c = *it++;
                if (c == '\r')
                    continue;
                if (c == '\n')
                {
                    line[li] = '\0';
                    runLine(line);
                    li = 0;
                    line[0] = '\0';
                    continue;
                }
                if (li + 1 < sizeof(line))
                    line[li++] = c;
            }
            if (li)
            {
                line[li] = '\0';
                runLine(line);
            }

            --g_scriptDepth;

            char summary[128];
            QC::String::memset(summary, 0, sizeof(summary));
            (void)appendString(summary, sizeof(summary), "source: commands=");
            (void)appendU64Dec(summary, sizeof(summary), static_cast<QC::u64>(commands));
            (void)appendString(summary, sizeof(summary), " failures=");
            (void)appendU64Dec(summary, sizeof(summary), static_cast<QC::u64>(failures));
            ctx.writeLine(summary);

            return true;
        }

        static bool cmdImgPreview(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            Session *s = sessionFrom();
            char tok[256];
            QC::String::memset(tok, 0, sizeof(tok));
            const char *p = args;
            if (!readToken(p, tok, sizeof(tok)))
            {
                ctx.writeLine("usage: imgpreview <path>");
                return true;
            }

            char path[256];
            QC::String::memset(path, 0, sizeof(path));
            if (!resolvePath(s, tok, path, sizeof(path)))
            {
                ctx.writeLine("imgpreview: invalid path");
                return true;
            }

            QK::ImageReader::LoadResult res;
            const QC::Status st = QK::ImageReader::loadAsset(path, res);
            if (st != QC::Status::Success)
            {
                ctx.writeLine("imgpreview: decode failed");
                return true;
            }

            char line[192];
            QC::String::memset(line, 0, sizeof(line));
            (void)appendString(line, sizeof(line), "imgpreview: format=");
            (void)appendString(line, sizeof(line), QK::ImageReader::formatName(res.format));
            (void)appendString(line, sizeof(line), " size=");
            (void)appendU64Dec(line, sizeof(line), res.surface.width);
            (void)appendString(line, sizeof(line), "x");
            (void)appendU64Dec(line, sizeof(line), res.surface.height);
            (void)appendString(line, sizeof(line), " pixels=");
            (void)appendU64Dec(line, sizeof(line), static_cast<QC::u64>(res.surface.pixels.size()));
            ctx.writeLine(line);

            if (!res.surface.pixels.empty())
            {
                char p0[64];
                QC::String::memset(p0, 0, sizeof(p0));
                (void)appendString(p0, sizeof(p0), "imgpreview: argb0=");
                (void)appendU64Dec(p0, sizeof(p0), static_cast<QC::u64>(res.surface.pixels[0]));
                ctx.writeLine(p0);
            }

            return true;
        }

        static bool cmdModFetch(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            char id[48];
            QC::String::memset(id, 0, sizeof(id));
            const char *p = args;
            if (!readToken(p, id, sizeof(id)))
            {
                ctx.writeLine("usage: modfetch <module_id>");
                return true;
            }

            QK::Module::FetchReport rep;
            const QC::Status st = QK::Module::Loader::instance().fetchWithDependencies(id, &rep);
            if (st != QC::Status::Success)
            {
                QK::Module::InspectionState insp{};
                (void)QK::Module::Loader::instance().lastInspectionState(insp);
                if (insp.detail[0])
                {
                    char fail[256];
                    QC::String::memset(fail, 0, sizeof(fail));
                    (void)appendString(fail, sizeof(fail), "modfetch: failed (");
                    (void)appendString(fail, sizeof(fail), insp.detail);
                    if (insp.quarantinePath[0])
                    {
                        (void)appendString(fail, sizeof(fail), "; quarantine=");
                        (void)appendString(fail, sizeof(fail), insp.quarantinePath);
                    }
                    (void)appendString(fail, sizeof(fail), ")");
                    ctx.writeLine(fail);
                }
                else
                {
                    ctx.writeLine("modfetch: failed (catalog/module/dependency error)");
                }
                return true;
            }

            char line[160];
            QC::String::memset(line, 0, sizeof(line));
            (void)appendString(line, sizeof(line), "modfetch: loaded modules=");
            (void)appendU64Dec(line, sizeof(line), rep.loadedModules);
            (void)appendString(line, sizeof(line), " bytes=");
            (void)appendU64Dec(line, sizeof(line), rep.loadedBytes);
            (void)appendString(line, sizeof(line), " parked=");
            (void)appendU64Dec(line, sizeof(line), rep.parkedModules);
            ctx.writeLine(line);
            return true;
        }

        static bool cmdDepGraph(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            char id[48];
            QC::String::memset(id, 0, sizeof(id));
            const char *p = args;
            if (!readToken(p, id, sizeof(id)))
            {
                ctx.writeLine("usage: depgraph <module_id>");
                return true;
            }

            QK::Module::DependencyEdge edges[64];
            QC::usize n = QK::Module::Loader::instance().buildDependencyGraph(id, edges, 64);
            if (n == 0)
            {
                ctx.writeLine("depgraph: no dependencies or module not found");
                return true;
            }

            ctx.writeLine("depgraph: mermaid");
            ctx.writeLine("graph TD");
            for (QC::usize i = 0; i < n; ++i)
            {
                char line[128];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "  ");
                (void)appendString(line, sizeof(line), edges[i].from);
                (void)appendString(line, sizeof(line), " --> ");
                (void)appendString(line, sizeof(line), edges[i].to);
                ctx.writeLine(line);
            }
            return true;
        }

        static bool cmdModule(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            char sub[16];
            QC::String::memset(sub, 0, sizeof(sub));
            const char *p = args;
            if (!readToken(p, sub, sizeof(sub)))
            {
                ctx.writeLine("usage: module <list|load|unload> [module_id]");
                return true;
            }

            auto &loader = QK::Module::Loader::instance();

            if (streqIgnoreCase(sub, "list"))
            {
                QK::Module::LoadedModule mods[32] = {};
                const QC::usize total = loader.listLoaded(mods, sizeof(mods) / sizeof(mods[0]));
                if (total == 0)
                {
                    ctx.writeLine("module: no loaded modules");
                    return true;
                }

                const QC::usize shown = (total < (sizeof(mods) / sizeof(mods[0]))) ? total : (sizeof(mods) / sizeof(mods[0]));
                for (QC::usize i = 0; i < shown; ++i)
                {
                    char line[256];
                    QC::String::memset(line, 0, sizeof(line));
                    (void)appendString(line, sizeof(line), "module id=");
                    (void)appendString(line, sizeof(line), mods[i].id);
                    (void)appendString(line, sizeof(line), " bytes=");
                    (void)appendU64Dec(line, sizeof(line), mods[i].bytes);
                    (void)appendString(line, sizeof(line), " deps=");
                    (void)appendU64Dec(line, sizeof(line), mods[i].depCount);
                    ctx.writeLine(line);
                }
                if (shown < total)
                    ctx.writeLine("module: list truncated");
                return true;
            }

            char id[48];
            QC::String::memset(id, 0, sizeof(id));
            if (!readToken(p, id, sizeof(id)))
            {
                ctx.writeLine("usage: module <load|unload> <module_id>");
                return true;
            }

            if (streqIgnoreCase(sub, "load"))
            {
                bool sandboxLoad = false;
                char modeTok[16];
                QC::String::memset(modeTok, 0, sizeof(modeTok));
                if (readToken(p, modeTok, sizeof(modeTok)))
                {
                    if (streqIgnoreCase(modeTok, "sandbox") || streqIgnoreCase(modeTok, "first"))
                        sandboxLoad = true;
                    else
                    {
                        ctx.writeLine("module: optional mode must be 'sandbox'");
                        return true;
                    }
                }

                char execPayload[128];
                QC::String::memset(execPayload, 0, sizeof(execPayload));
                (void)appendString(execPayload, sizeof(execPayload), "module load id=");
                (void)appendString(execPayload, sizeof(execPayload), id);
                (void)appendString(execPayload, sizeof(execPayload), sandboxLoad ? " sandbox=1" : " sandbox=0");

                QK::SecurityCenter::DispatchRequest req{};
                req.op = QK::SecurityCenter::DispatchOp::ExecRequest;
                req.payload = execPayload;
                QK::SecurityCenter::DispatchResult res{};
                const QC::Status execSt = QK::SecurityCenter::instance().dispatch(req, &res);
                if (execSt != QC::Status::Success)
                {
                    char deny[192];
                    QC::String::memset(deny, 0, sizeof(deny));
                    (void)appendString(deny, sizeof(deny), "module: denied by SC");
                    if (res.detail[0])
                    {
                        (void)appendString(deny, sizeof(deny), " (");
                        (void)appendString(deny, sizeof(deny), res.detail);
                        (void)appendString(deny, sizeof(deny), ")");
                    }
                    ctx.writeLine(deny);
                    return true;
                }

                QK::Module::FetchReport rep{};
                const QC::Status st = sandboxLoad ? loader.loadSandboxed(id, &rep) : loader.load(id, &rep);
                if (st != QC::Status::Success)
                {
                    QK::Module::InspectionState insp{};
                    (void)loader.lastInspectionState(insp);
                    if (insp.detail[0])
                    {
                        char fail[256];
                        QC::String::memset(fail, 0, sizeof(fail));
                        (void)appendString(fail, sizeof(fail), "module: load failed (");
                        (void)appendString(fail, sizeof(fail), insp.detail);
                        if (insp.quarantinePath[0])
                        {
                            (void)appendString(fail, sizeof(fail), "; quarantine=");
                            (void)appendString(fail, sizeof(fail), insp.quarantinePath);
                        }
                        (void)appendString(fail, sizeof(fail), ")");
                        ctx.writeLine(fail);
                    }
                    else
                    {
                        ctx.writeLine("module: load failed");
                    }
                    return true;
                }

                char line[128];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "module: loaded modules=");
                (void)appendU64Dec(line, sizeof(line), rep.loadedModules);
                (void)appendString(line, sizeof(line), " bytes=");
                (void)appendU64Dec(line, sizeof(line), rep.loadedBytes);
                (void)appendString(line, sizeof(line), " parked=");
                (void)appendU64Dec(line, sizeof(line), rep.parkedModules);
                (void)appendString(line, sizeof(line), sandboxLoad ? " mode=sandbox" : " mode=direct");
                ctx.writeLine(line);
                return true;
            }

            if (streqIgnoreCase(sub, "unload"))
            {
                const QC::Status st = loader.unload(id);
                if (st == QC::Status::Busy)
                {
                    ctx.writeLine("module: unload blocked by loaded dependents");
                    return true;
                }
                if (st != QC::Status::Success)
                {
                    ctx.writeLine("module: unload failed");
                    return true;
                }
                ctx.writeLine("module: unloaded");
                return true;
            }

            ctx.writeLine("usage: module <list|load|unload> [module_id] [sandbox]");
            return true;
        }

        static bool dispatchSysOp(QK::SecurityCenter::DispatchOp op,
                                  const char *payload,
                                  QC::u32 flags,
                                  const QC::Cmd::Context &ctx,
                                  const char *prefix)
        {
            QK::SecurityCenter::DispatchRequest req{};
            req.op = op;
            req.flags = flags;
            req.payload = payload;

            QK::SecurityCenter::DispatchResult res{};
            const QC::Status st = QK::SecurityCenter::instance().dispatch(req, &res);

            char line[192];
            QC::String::memset(line, 0, sizeof(line));
            (void)appendString(line, sizeof(line), prefix);
            (void)appendString(line, sizeof(line), ": ");
            (void)appendString(line, sizeof(line), (st == QC::Status::Success) ? "ok" : "failed");
            if (res.detail[0])
            {
                (void)appendString(line, sizeof(line), " (");
                (void)appendString(line, sizeof(line), res.detail);
                (void)appendString(line, sizeof(line), ")");
            }
            ctx.writeLine(line);
            return st == QC::Status::Success;
        }

        static bool parsePrefixedU32(const char *token, const char *prefix, QC::u32 &out)
        {
            if (!token || !prefix)
                return false;
            const QC::usize prefixLen = QC::String::strlen(prefix);
            if (QC::String::memcmp(token, prefix, prefixLen) != 0)
                return false;
            return parseU32(token + prefixLen, out);
        }

        static void parseAuditWindowArgs(const char *args, QC::usize defaultPageSize, QC::usize &outPage, QC::usize &outPageSize)
        {
            outPage = 0;
            outPageSize = defaultPageSize;

            char tok[32];
            QC::String::memset(tok, 0, sizeof(tok));
            const char *p = args;
            bool plainSizeConsumed = false;
            while (readToken(p, tok, sizeof(tok)))
            {
                QC::u32 v = 0;
                if (parsePrefixedU32(tok, "page=", v))
                {
                    outPage = v;
                    continue;
                }
                if (parsePrefixedU32(tok, "size=", v) && v > 0)
                {
                    outPageSize = v;
                    continue;
                }
                if (!plainSizeConsumed && parseU32(tok, v) && v > 0)
                {
                    outPageSize = v;
                    plainSizeConsumed = true;
                }
            }

            if (outPageSize == 0)
                outPageSize = defaultPageSize;
            if (outPageSize > 256)
                outPageSize = 256;
        }

        static bool dumpOwnerEvents(const QC::Cmd::Context &ctx, QC::usize page, QC::usize pageSize)
        {
            const QC::usize total = QK::Boot::Events::Count();
            if (total == 0)
            {
                ctx.writeLine("sys_audit_view: no events");
                return true;
            }

            if (pageSize == 0)
                pageSize = 64;
            const QC::usize pageOffset = page * pageSize;
            if (pageOffset >= total)
            {
                ctx.writeLine("sys_audit_view: page out of range");
                return true;
            }

            const QC::usize end = total - pageOffset;
            const QC::usize start = (end > pageSize) ? (end - pageSize) : 0;
            const QC::usize pageCount = (total + pageSize - 1) / pageSize;
            char header[96];
            QC::String::memset(header, 0, sizeof(header));
            (void)appendString(header, sizeof(header), "audit page=");
            (void)appendU64Dec(header, sizeof(header), static_cast<QC::u64>(page));
            (void)appendString(header, sizeof(header), " pages=");
            (void)appendU64Dec(header, sizeof(header), static_cast<QC::u64>(pageCount));
            ctx.writeLine(header);

            QK::Boot::Events::Record recs[8] = {};
            QC::usize offset = start;
            auto &sc = QK::SecurityCenter::instance();
            while (offset < end)
            {
                const QC::usize remaining = end - offset;
                const QC::usize n = QK::Boot::Events::CopyOut(offset, recs, remaining < 8 ? remaining : 8);
                if (n == 0)
                    break;
                offset += n;

                for (QC::usize i = 0; i < n; ++i)
                {
                    char line[320];
                    char detail[160];
                    QC::String::memset(line, 0, sizeof(line));
                    QC::String::memset(detail, 0, sizeof(detail));
                    (void)appendString(line, sizeof(line), "EV seq=");
                    (void)appendU64Dec(line, sizeof(line), recs[i].seq);
                    (void)appendString(line, sizeof(line), " stage=");
                    (void)appendString(line, sizeof(line), recs[i].stage[0] ? recs[i].stage : "(none)");
                    (void)appendString(line, sizeof(line), " type=");
                    (void)appendString(line, sizeof(line), recs[i].type[0] ? recs[i].type : "(none)");
                    if (recs[i].details[0])
                    {
                        sc.redactAuditText(recs[i].details, detail, sizeof(detail));
                        if (detail[0])
                        {
                            (void)appendString(line, sizeof(line), " ");
                            (void)appendString(line, sizeof(line), detail);
                        }
                    }
                    ctx.writeLine(line);
                }
            }

            return true;
        }

        static bool ownerLogPolicySatisfied(const char *args, const QC::Cmd::Context &ctx)
        {
            auto &sc = QK::SecurityCenter::instance();
            if (!sc.ownerUnlocked())
            {
                ctx.writeLine("ownerlogs: owner unlock required");
                return false;
            }

            if (sc.ownerLockedOut())
            {
                ctx.writeLine("ownerlogs: owner lockout active");
                return false;
            }

            const auto mode = QK::Boot::Config::GetStartupMode();
            if (!(mode == QK::Boot::Config::StartupMode::Terminal || mode == QK::Boot::Config::StartupMode::Recovery))
            {
                ctx.writeLine("ownerlogs: physical presence required (console session)");
                return false;
            }

            char tok[32];
            QC::String::memset(tok, 0, sizeof(tok));
            const char *p = args;
            bool hasPresence = false;
            while (readToken(p, tok, sizeof(tok)))
            {
                if (streqIgnoreCase(tok, "present") || streqIgnoreCase(tok, "physical"))
                {
                    hasPresence = true;
                    break;
                }
            }

            if (!hasPresence)
            {
                ctx.writeLine("ownerlogs: add 'present' to confirm physical presence");
                return false;
            }
            return true;
        }

        static bool cmdOwnerLogs(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            if (!ownerLogPolicySatisfied(args, ctx))
                return true;

            if (QK::SecurityCenter::instance().allowAuditLogAccess(false) == QC::Status::Busy)
            {
                ctx.writeLine("ownerlogs: rate limited");
                return true;
            }

            QC::usize page = 0;
            QC::usize pageSize = 64;
            parseAuditWindowArgs(args, 64, page, pageSize);
            return dumpOwnerEvents(ctx, page, pageSize);
        }

        static bool cmdSysAuditView(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            if (!ownerLogPolicySatisfied(args, ctx))
                return true;

            if (!dispatchSysOp(QK::SecurityCenter::DispatchOp::AuditView, args, 0, ctx, "sys_audit_view"))
                return true;

            QC::usize page = 0;
            QC::usize pageSize = 64;
            parseAuditWindowArgs(args, 64, page, pageSize);
            return dumpOwnerEvents(ctx, page, pageSize);
        }

        static bool cmdSysExecRequest(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            const char *payload = args ? skipSpaces(args) : nullptr;
            if (!payload || *payload == '\0')
            {
                ctx.writeLine("usage: sys_exec_request <request_text>");
                return true;
            }
            (void)dispatchSysOp(QK::SecurityCenter::DispatchOp::ExecRequest, payload, 0, ctx, "sys_exec_request");
            return true;
        }

        static bool cmdSysRotateSst(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            if (!ownerLogPolicySatisfied(args, ctx))
                return true;
            QC::u32 flags = 1;
            char tok[32];
            QC::String::memset(tok, 0, sizeof(tok));
            const char *p = args;
            while (readToken(p, tok, sizeof(tok)))
            {
                if (QC::String::strcmp(tok, "policy") == 0)
                    flags = 0;
            }
            (void)dispatchSysOp(QK::SecurityCenter::DispatchOp::RotateSst, args, flags, ctx, "sys_rotate_sst");
            return true;
        }

        static bool cmdSysTrustCheck(const char *, const QC::Cmd::Context &ctx, void *)
        {
            (void)dispatchSysOp(QK::SecurityCenter::DispatchOp::TrustCheck, nullptr, 0, ctx, "sys_trust_check");
            return true;
        }

        static bool cmdSysUpdateVerify(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            const char *payload = args ? skipSpaces(args) : nullptr;
            (void)dispatchSysOp(QK::SecurityCenter::DispatchOp::UpdateVerify, payload, 0, ctx, "sys_update_verify");
            return true;
        }

        static bool cmdSysVaultRequest(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            const char *payload = args ? skipSpaces(args) : nullptr;
            if (!payload || *payload == '\0')
            {
                ctx.writeLine("usage: sys_vault_request <request_text>");
                return true;
            }
            (void)dispatchSysOp(QK::SecurityCenter::DispatchOp::VaultRequest, payload, 0, ctx, "sys_vault_request");
            return true;
        }

        static bool cmdClear(const char *, const QC::Cmd::Context &ctx, void *)
        {
            ctx.writeLine("\x1b[2J\x1b[H");
            // Fallback for terminals that ignore ANSI clear sequences.
            for (QC::usize i = 0; i < 24; ++i)
                ctx.writeLine("");
            return true;
        }

        static bool cmdShowMode(const char *, const QC::Cmd::Context &ctx, void *)
        {
            const auto mode = QK::Boot::Config::GetStartupMode();
            char line[96];
            QC::String::memset(line, 0, sizeof(line));
            (void)appendString(line, sizeof(line), "startup mode: ");
            (void)appendString(line, sizeof(line), QK::Boot::Config::StartupModeName(mode));
            ctx.writeLine(line);

            const bool tpmEnabled = QK::SecureStore::tpm_present();
            const bool hasTpmAnchor = QK::SecureStore::exists("WRAPKEY.TPM");
            const bool hasRecoveryAnchor = QK::SecureStore::exists("WRAPKEY.KDF");
            const bool hasLegacyPlainAnchor = QK::SecureStore::exists("WRAPKEY.BIN");

            char anchor[192];
            QC::String::memset(anchor, 0, sizeof(anchor));
            (void)appendString(anchor, sizeof(anchor), "securestore anchor: ");

            if (tpmEnabled && hasTpmAnchor)
            {
                (void)appendString(anchor, sizeof(anchor), "TPM-backed (active)");
            }
            else if (!tpmEnabled && hasRecoveryAnchor)
            {
                (void)appendString(anchor, sizeof(anchor), "recovery-backed (active)");
            }
            else if (!tpmEnabled && hasLegacyPlainAnchor)
            {
                (void)appendString(anchor, sizeof(anchor), "legacy-plain (upgrade pending)");
            }
            else if (!tpmEnabled && hasTpmAnchor)
            {
                (void)appendString(anchor, sizeof(anchor), "TPM-provisioned but TPM path unavailable");
            }
            else if (tpmEnabled)
            {
                (void)appendString(anchor, sizeof(anchor), "TPM-capable (anchor not provisioned yet)");
            }
            else
            {
                (void)appendString(anchor, sizeof(anchor), "recovery-bootstrap required");
            }

            ctx.writeLine(anchor);

            char detail[192];
            QC::String::memset(detail, 0, sizeof(detail));
            (void)appendString(detail, sizeof(detail), "securestore anchor artifacts: tpm=");
            (void)appendString(detail, sizeof(detail), hasTpmAnchor ? "yes" : "no");
            (void)appendString(detail, sizeof(detail), " kdf=");
            (void)appendString(detail, sizeof(detail), hasRecoveryAnchor ? "yes" : "no");
            (void)appendString(detail, sizeof(detail), " plain=");
            (void)appendString(detail, sizeof(detail), hasLegacyPlainAnchor ? "yes" : "no");
            ctx.writeLine(detail);

            const auto scMode = QK::SecurityCenter::instance().mode();
            char fallback[224];
            QC::String::memset(fallback, 0, sizeof(fallback));
            (void)appendString(fallback, sizeof(fallback), "fallback paths: startup_mode=");
            (void)appendString(fallback, sizeof(fallback), QK::Boot::Config::StartupModeName(mode));
            (void)appendString(fallback, sizeof(fallback), " flow_mode=");
            (void)appendString(fallback, sizeof(fallback), QK::SecurityCenter::modeName(scMode));
            (void)appendString(fallback, sizeof(fallback), " owner_bypass=");
            (void)appendString(fallback, sizeof(fallback), QK::SecurityCenter::instance().bypassEnabled() ? "on" : "off");
            ctx.writeLine(fallback);

            if (mode == QK::Boot::Config::StartupMode::Recovery)
                ctx.writeLine("fallback state: recovery startup mode is active");
            else if (mode == QK::Boot::Config::StartupMode::Terminal)
                ctx.writeLine("fallback state: terminal startup mode is active");
            else if (mode == QK::Boot::Config::StartupMode::Safe)
                ctx.writeLine("fallback state: safe startup mode is active");
            else if (mode == QK::Boot::Config::StartupMode::Installer)
                ctx.writeLine("fallback state: installer startup mode is active");

            ctx.writeLine("verify fallback path: run bevdump and check stages/types (securestore/*, owner_gate/fallback_terminal, boottrust/*, shutdown/acpi_*|fallback_halt)");

            char tuningMouse[224];
            QC::String::memset(tuningMouse, 0, sizeof(tuningMouse));
            (void)appendString(tuningMouse, sizeof(tuningMouse), "hardware tuning (mouse): sens=");
            (void)appendU64Dec(tuningMouse, sizeof(tuningMouse), QK::Boot::Config::GetMouseSensitivityPercent());
            (void)appendString(tuningMouse, sizeof(tuningMouse), "% usb=");
            (void)appendU64Dec(tuningMouse, sizeof(tuningMouse), QK::Boot::Config::GetMouseUsbRelativePercent());
            (void)appendString(tuningMouse, sizeof(tuningMouse), "% ps2=");
            (void)appendU64Dec(tuningMouse, sizeof(tuningMouse), QK::Boot::Config::GetMousePs2RelativePercent());
            (void)appendString(tuningMouse, sizeof(tuningMouse), "% wheel_lines=");
            (void)appendU64Dec(tuningMouse, sizeof(tuningMouse), QK::Boot::Config::GetMouseWheelLines());
            (void)appendString(tuningMouse, sizeof(tuningMouse), " invert_wheel=");
            (void)appendString(tuningMouse, sizeof(tuningMouse), QK::Boot::Config::GetMouseInvertWheel() ? "on" : "off");
            ctx.writeLine(tuningMouse);

            char tuningKeyboard[128];
            QC::String::memset(tuningKeyboard, 0, sizeof(tuningKeyboard));
            (void)appendString(tuningKeyboard, sizeof(tuningKeyboard), "hardware tuning (keyboard): repeat_delay_ms=");
            (void)appendU64Dec(tuningKeyboard, sizeof(tuningKeyboard), QK::Boot::Config::GetKeyboardRepeatDelayMs());
            (void)appendString(tuningKeyboard, sizeof(tuningKeyboard), " repeat_interval_ms=");
            (void)appendU64Dec(tuningKeyboard, sizeof(tuningKeyboard), QK::Boot::Config::GetKeyboardRepeatIntervalMs());
            ctx.writeLine(tuningKeyboard);
            return true;
        }

        static bool cmdMouseSpeed(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            auto writeStatus = [&]()
            {
                char line[128];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "mousespeed: ");
                (void)appendU64Dec(line, sizeof(line), QK::Boot::Config::GetMouseSensitivityPercent());
                (void)appendString(line, sizeof(line), "% (default=100, range=10-400)");
                ctx.writeLine(line);
            };

            char tok0[32];
            QC::String::memset(tok0, 0, sizeof(tok0));
            const char *p = args;
            if (!readToken(p, tok0, sizeof(tok0)))
            {
                writeStatus();
                return true;
            }

            if (streqIgnoreCase(tok0, "show") || streqIgnoreCase(tok0, "status"))
            {
                writeStatus();
                return true;
            }

            bool persist = false;
            const char *valueText = tok0;
            char tok1[32];
            QC::String::memset(tok1, 0, sizeof(tok1));
            if (streqIgnoreCase(tok0, "persist") || streqIgnoreCase(tok0, "save"))
            {
                persist = true;
                if (!readToken(p, tok1, sizeof(tok1)))
                {
                    ctx.writeLine("usage: mousespeed [show|<percent>|persist <percent>]");
                    return true;
                }
                valueText = tok1;
            }

            QC::u32 percent = 0;
            if (!parseU32(valueText, percent) || percent < 10 || percent > 400)
            {
                ctx.writeLine("mousespeed: percent must be 10..400");
                return true;
            }

            if (persist)
            {
                if (static_cast<QC::u8>(ctx.callerAccess) < static_cast<QC::u8>(QC::Cmd::AccessLevel::Admin))
                {
                    ctx.writeLine("mousespeed: persist requires admin");
                    return true;
                }

                const QC::Status st = QK::Boot::Config::PersistMouseSensitivityPercent(percent, nullptr);
                if (st != QC::Status::Success)
                {
                    ctx.writeLine("mousespeed: failed to persist startup.cfg");
                    return true;
                }
                ctx.writeLine("mousespeed: saved to startup.cfg");
                return true;
            }

            QK::Boot::Config::SetMouseSensitivityPercent(percent);
            ctx.writeLine("mousespeed: updated for current session");
            return true;
        }

        static bool cmdKeyRepeat(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            auto writeStatus = [&]()
            {
                char line[192];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "keyrepeat: delay_ms=");
                (void)appendU64Dec(line, sizeof(line), QK::Boot::Config::GetKeyboardRepeatDelayMs());
                (void)appendString(line, sizeof(line), " interval_ms=");
                (void)appendU64Dec(line, sizeof(line), QK::Boot::Config::GetKeyboardRepeatIntervalMs());
                (void)appendString(line, sizeof(line), " (delay=100-2000 interval=10-250)");
                ctx.writeLine(line);
            };

            char tok0[32];
            QC::String::memset(tok0, 0, sizeof(tok0));
            const char *p = args;
            if (!readToken(p, tok0, sizeof(tok0)))
            {
                writeStatus();
                return true;
            }

            if (streqIgnoreCase(tok0, "show") || streqIgnoreCase(tok0, "status"))
            {
                writeStatus();
                return true;
            }

            bool persist = false;
            const char *delayText = tok0;

            char tok1[32];
            QC::String::memset(tok1, 0, sizeof(tok1));
            if (streqIgnoreCase(tok0, "persist") || streqIgnoreCase(tok0, "save"))
            {
                persist = true;
                if (!readToken(p, tok1, sizeof(tok1)))
                {
                    ctx.writeLine("usage: keyrepeat [show|<delay_ms> <interval_ms>|persist <delay_ms> <interval_ms>]");
                    return true;
                }
                delayText = tok1;
            }

            char tokInterval[32];
            QC::String::memset(tokInterval, 0, sizeof(tokInterval));
            if (!readToken(p, tokInterval, sizeof(tokInterval)))
            {
                ctx.writeLine("usage: keyrepeat [show|<delay_ms> <interval_ms>|persist <delay_ms> <interval_ms>]");
                return true;
            }

            QC::u32 delayMs = 0;
            QC::u32 intervalMs = 0;
            if (!parseU32(delayText, delayMs) || !parseU32(tokInterval, intervalMs) ||
                delayMs < 100 || delayMs > 2000 || intervalMs < 10 || intervalMs > 250)
            {
                ctx.writeLine("keyrepeat: delay must be 100..2000 ms and interval must be 10..250 ms");
                return true;
            }

            if (persist)
            {
                if (static_cast<QC::u8>(ctx.callerAccess) < static_cast<QC::u8>(QC::Cmd::AccessLevel::Admin))
                {
                    ctx.writeLine("keyrepeat: persist requires admin");
                    return true;
                }

                const QC::Status st = QK::Boot::Config::PersistKeyboardRepeatTiming(delayMs, intervalMs, nullptr);
                if (st != QC::Status::Success)
                {
                    ctx.writeLine("keyrepeat: failed to persist startup.cfg");
                    return true;
                }
                ctx.writeLine("keyrepeat: saved to startup.cfg");
                return true;
            }

            QK::Boot::Config::SetKeyboardRepeatTiming(delayMs, intervalMs);
            ctx.writeLine("keyrepeat: updated for current session");
            return true;
        }

        static bool cmdMouseCfg(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            auto writeStatus = [&]()
            {
                char line[192];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "mousecfg: usb_relative=");
                (void)appendU64Dec(line, sizeof(line), QK::Boot::Config::GetMouseUsbRelativePercent());
                (void)appendString(line, sizeof(line), "% ps2_relative=");
                (void)appendU64Dec(line, sizeof(line), QK::Boot::Config::GetMousePs2RelativePercent());
                (void)appendString(line, sizeof(line), "% wheel_lines=");
                (void)appendU64Dec(line, sizeof(line), QK::Boot::Config::GetMouseWheelLines());
                (void)appendString(line, sizeof(line), " invert_wheel=");
                (void)appendString(line, sizeof(line), QK::Boot::Config::GetMouseInvertWheel() ? "on" : "off");
                ctx.writeLine(line);
                ctx.writeLine("mousecfg ranges: usb/ps2=25..400 wheel_lines=1..16 invert_wheel=on|off");
            };

            char tok0[32];
            QC::String::memset(tok0, 0, sizeof(tok0));
            const char *p = args;
            if (!readToken(p, tok0, sizeof(tok0)))
            {
                writeStatus();
                return true;
            }

            bool persist = false;
            if (streqIgnoreCase(tok0, "show") || streqIgnoreCase(tok0, "status"))
            {
                writeStatus();
                return true;
            }
            if (streqIgnoreCase(tok0, "persist") || streqIgnoreCase(tok0, "save"))
            {
                persist = true;
                if (!readToken(p, tok0, sizeof(tok0)))
                {
                    ctx.writeLine("usage: mousecfg [show|<usb|ps2|wheel|invertwheel> <value>|persist <usb|ps2|wheel|invertwheel> <value>]");
                    return true;
                }
            }

            char tok1[32];
            QC::String::memset(tok1, 0, sizeof(tok1));
            if (!readToken(p, tok1, sizeof(tok1)))
            {
                ctx.writeLine("usage: mousecfg [show|<usb|ps2|wheel|invertwheel> <value>|persist <usb|ps2|wheel|invertwheel> <value>]");
                return true;
            }

            QC::u32 usb = QK::Boot::Config::GetMouseUsbRelativePercent();
            QC::u32 ps2 = QK::Boot::Config::GetMousePs2RelativePercent();
            QC::u32 wheel = QK::Boot::Config::GetMouseWheelLines();
            bool invertWheel = QK::Boot::Config::GetMouseInvertWheel();

            if (streqIgnoreCase(tok0, "usb"))
            {
                QC::u32 value = 0;
                if (!parseU32(tok1, value) || value < 25 || value > 400)
                {
                    ctx.writeLine("mousecfg: usb must be 25..400");
                    return true;
                }
                usb = value;
            }
            else if (streqIgnoreCase(tok0, "ps2"))
            {
                QC::u32 value = 0;
                if (!parseU32(tok1, value) || value < 25 || value > 400)
                {
                    ctx.writeLine("mousecfg: ps2 must be 25..400");
                    return true;
                }
                ps2 = value;
            }
            else if (streqIgnoreCase(tok0, "wheel") || streqIgnoreCase(tok0, "wheel_lines"))
            {
                QC::u32 value = 0;
                if (!parseU32(tok1, value) || value < 1 || value > 16)
                {
                    ctx.writeLine("mousecfg: wheel lines must be 1..16");
                    return true;
                }
                wheel = value;
            }
            else if (streqIgnoreCase(tok0, "invertwheel") || streqIgnoreCase(tok0, "invert_wheel"))
            {
                if (streqIgnoreCase(tok1, "1") || streqIgnoreCase(tok1, "on") || streqIgnoreCase(tok1, "yes") || streqIgnoreCase(tok1, "true"))
                    invertWheel = true;
                else if (streqIgnoreCase(tok1, "0") || streqIgnoreCase(tok1, "off") || streqIgnoreCase(tok1, "no") || streqIgnoreCase(tok1, "false"))
                    invertWheel = false;
                else
                {
                    ctx.writeLine("mousecfg: invertwheel must be on|off");
                    return true;
                }
            }
            else
            {
                ctx.writeLine("mousecfg: unknown field (use usb|ps2|wheel|invertwheel)");
                return true;
            }

            if (persist)
            {
                if (static_cast<QC::u8>(ctx.callerAccess) < static_cast<QC::u8>(QC::Cmd::AccessLevel::Admin))
                {
                    ctx.writeLine("mousecfg: persist requires admin");
                    return true;
                }

                const QC::Status st = QK::Boot::Config::PersistMouseBehaviorConfig(usb, ps2, wheel, invertWheel, nullptr);
                if (st != QC::Status::Success)
                {
                    ctx.writeLine("mousecfg: failed to persist startup.cfg");
                    return true;
                }

                ctx.writeLine("mousecfg: saved to startup.cfg");
                return true;
            }

            QK::Boot::Config::SetMouseUsbRelativePercent(usb);
            QK::Boot::Config::SetMousePs2RelativePercent(ps2);
            QK::Boot::Config::SetMouseWheelLines(wheel);
            QK::Boot::Config::SetMouseInvertWheel(invertWheel);
            ctx.writeLine("mousecfg: updated for current session");
            return true;
        }

        static bool cmdSetMode(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            char tok[32];
            QC::String::memset(tok, 0, sizeof(tok));
            const char *p = args;
            if (!readToken(p, tok, sizeof(tok)))
            {
                ctx.writeLine("usage: setmode <DESKTOP|TERMINAL|SAFE>");
                return true;
            }

            QK::Boot::Config::StartupMode mode = QK::Boot::Config::StartupMode::Desktop;
            if (streqIgnoreCase(tok, "DESKTOP"))
                mode = QK::Boot::Config::StartupMode::Desktop;
            else if (streqIgnoreCase(tok, "TERMINAL"))
                mode = QK::Boot::Config::StartupMode::Terminal;
            else if (streqIgnoreCase(tok, "SAFE"))
                mode = QK::Boot::Config::StartupMode::Safe;
            else
            {
                ctx.writeLine("setmode: allowed values DESKTOP|TERMINAL|SAFE");
                return true;
            }

            const QC::Status st = QK::Boot::Config::PersistStartupMode(mode, nullptr);
            if (st != QC::Status::Success)
            {
                ctx.writeLine("setmode: failed to persist startup.cfg");
                return true;
            }

            ctx.writeLine("setmode: persisted (takes effect next boot)");
            return true;
        }

        static bool cmdStartx(const char *, const QC::Cmd::Context &ctx, void *)
        {
            const auto mode = QK::Boot::Config::GetStartupMode();
            if (mode == QK::Boot::Config::StartupMode::Desktop)
            {
                ctx.writeLine("startx: desktop mode already configured");
                return true;
            }

            const QC::Status st = QK::Boot::Config::PersistStartupMode(QK::Boot::Config::StartupMode::Desktop, nullptr);
            if (st != QC::Status::Success)
            {
                ctx.writeLine("startx: failed to persist desktop mode");
                return true;
            }

            ctx.writeLine("startx: desktop mode enabled (reboot to enter desktop)");
            return true;
        }

        static bool cmdStopx(const char *, const QC::Cmd::Context &ctx, void *)
        {
            if (QK::Boot::Desktop::RequestStopDesktop())
                ctx.writeLine("stopx: desktop stop requested");
            else
                ctx.writeLine("stopx: desktop is not active");
            return true;
        }

        static bool cmdSysAuditExport(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            if (!ownerLogPolicySatisfied(args, ctx))
                return true;

            if (!dispatchSysOp(QK::SecurityCenter::DispatchOp::AuditExport, args, 0, ctx, "sys_audit_export"))
                return true;

            Session *s = sessionFrom();
            char pathTok[256];
            QC::String::memset(pathTok, 0, sizeof(pathTok));
            char targetLabel[24];
            QC::String::memset(targetLabel, 0, sizeof(targetLabel));
            const char *p = args;
            if (!readToken(p, pathTok, sizeof(pathTok)))
            {
                ctx.writeLine("usage: sys_audit_export <path|auto|system|shared|usb> present [ephemeral-ok]");
                return true;
            }

            char path[256];
            QC::String::memset(path, 0, sizeof(path));

            ExportTarget target = ExportTarget::Auto;
            if (parseExportTarget(pathTok, target))
            {
                QC::String::strncpy(targetLabel, pathTok, sizeof(targetLabel) - 1);
                char refusal[160];
                QC::String::memset(refusal, 0, sizeof(refusal));
                if (!buildExportPathForTarget(target,
                                              "audit_events.log",
                                              path,
                                              sizeof(path),
                                              refusal,
                                              sizeof(refusal)))
                {
                    char line[224];
                    QC::String::memset(line, 0, sizeof(line));
                    (void)appendString(line, sizeof(line), "sys_audit_export: ");
                    (void)appendString(line, sizeof(line), refusal[0] ? refusal : "target unavailable");
                    ctx.writeLine(line);
                    return true;
                }
            }
            else
            {
                QC::String::strncpy(targetLabel, "path", sizeof(targetLabel) - 1);
                if (!resolvePath(s, pathTok, path, sizeof(path)))
                {
                    ctx.writeLine("sys_audit_export: invalid path");
                    return true;
                }
            }

            if (!allowWriteToPath(path, ctx, "sys_audit_export"))
                return true;

            if (!enforceEphemeralWriteGuard(path, targetLabel, args, ctx, "sys_audit_export"))
                return true;

            if (!preflightExportPath(path, estimateAuditExportBytes(), ctx, "sys_audit_export"))
                return true;

            QFS::File *f = QFS::VFS::instance().open(path,
                                                     QFS::OpenMode::Write | QFS::OpenMode::Create | QFS::OpenMode::Truncate);
            if (!f)
            {
                ctx.writeLine("sys_audit_export: cannot open output");
                return true;
            }

            const QC::usize total = QK::Boot::Events::Count();
            QK::Boot::Events::Record recs[8] = {};
            QC::usize offset = 0;
            while (offset < total)
            {
                const QC::usize n = QK::Boot::Events::CopyOut(offset, recs, sizeof(recs) / sizeof(recs[0]));
                if (n == 0)
                    break;
                offset += n;

                for (QC::usize i = 0; i < n; ++i)
                {
                    char line[384];
                    char detail[160];
                    QC::String::memset(line, 0, sizeof(line));
                    QC::String::memset(detail, 0, sizeof(detail));
                    (void)appendString(line, sizeof(line), "EV seq=");
                    (void)appendU64Dec(line, sizeof(line), recs[i].seq);
                    (void)appendString(line, sizeof(line), " t_ms=");
                    (void)appendU64Dec(line, sizeof(line), recs[i].t_ms);
                    (void)appendString(line, sizeof(line), " stage=");
                    (void)appendString(line, sizeof(line), recs[i].stage[0] ? recs[i].stage : "(none)");
                    (void)appendString(line, sizeof(line), " type=");
                    (void)appendString(line, sizeof(line), recs[i].type[0] ? recs[i].type : "(none)");
                    if (recs[i].details[0])
                    {
                        QK::SecurityCenter::instance().redactAuditText(recs[i].details, detail, sizeof(detail));
                        (void)appendString(line, sizeof(line), " ");
                        (void)appendString(line, sizeof(line), detail);
                    }
                    (void)appendString(line, sizeof(line), "\n");

                    const QC::usize need = QC::String::strlen(line);
                    QC::usize off = 0;
                    while (off < need)
                    {
                        const QC::isize w = f->write(line + off, need - off);
                        if (w <= 0)
                        {
                            QFS::VFS::instance().close(f);
                            ctx.writeLine("sys_audit_export: write failed");
                            return true;
                        }
                        off += static_cast<QC::usize>(w);
                    }
                }
            }

            QFS::VFS::instance().close(f);
            (void)writeExportMetadataSidecar(path, "audit_events", targetLabel, ctx, "sys_audit_export");
            {
                char line[320];
                char resolvedTarget[24];
                char persistenceClass[24];
                QC::String::memset(resolvedTarget, 0, sizeof(resolvedTarget));
                QC::String::memset(persistenceClass, 0, sizeof(persistenceClass));
                inferExportPathMetadata(path,
                                        targetLabel,
                                        resolvedTarget,
                                        sizeof(resolvedTarget),
                                        persistenceClass,
                                        sizeof(persistenceClass));
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "sys_audit_export: ok path=");
                (void)appendString(line, sizeof(line), path);
                (void)appendString(line, sizeof(line), " target=");
                (void)appendString(line, sizeof(line), resolvedTarget[0] ? resolvedTarget : "unknown");
                (void)appendString(line, sizeof(line), " persistence=");
                (void)appendString(line, sizeof(line), persistenceClass[0] ? persistenceClass : "unknown");
                ctx.writeLine(line);
            }
            return true;
        }

        static bool cmdDb(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            char sub[16];
            QC::String::memset(sub, 0, sizeof(sub));
            const char *p = args;
            if (!readToken(p, sub, sizeof(sub)))
            {
                ctx.writeLine("usage: db <status|list|get|set|del|save|reload> ...");
                return true;
            }

            auto &db = QK::Db::Store::instance();
            if (streqIgnoreCase(sub, "reload"))
            {
                const QC::Status st = db.load();
                ctx.writeLine(st == QC::Status::Success ? "db: reloaded" : "db: reload failed");
                return true;
            }

            if (streqIgnoreCase(sub, "save"))
            {
                const QC::Status st = db.save();
                ctx.writeLine(st == QC::Status::Success ? "db: saved" : "db: save failed");
                return true;
            }

            if (streqIgnoreCase(sub, "status"))
            {
                char line[160];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "db path=");
                (void)appendString(line, sizeof(line), db.path());
                (void)appendString(line, sizeof(line), " entries=");
                (void)appendU64Dec(line, sizeof(line), db.list(nullptr, 0));
                ctx.writeLine(line);
                return true;
            }

            if (streqIgnoreCase(sub, "list"))
            {
                QK::Db::Entry entries[32] = {};
                const QC::usize total = db.list(entries, sizeof(entries) / sizeof(entries[0]));
                if (total == 0)
                {
                    ctx.writeLine("db: empty");
                    return true;
                }

                const QC::usize shown = (total < (sizeof(entries) / sizeof(entries[0]))) ? total : (sizeof(entries) / sizeof(entries[0]));
                for (QC::usize i = 0; i < shown; ++i)
                {
                    char line[272];
                    QC::String::memset(line, 0, sizeof(line));
                    (void)appendString(line, sizeof(line), entries[i].key);
                    (void)appendString(line, sizeof(line), "=");
                    (void)appendString(line, sizeof(line), entries[i].value);
                    ctx.writeLine(line);
                }
                if (shown < total)
                    ctx.writeLine("db: list truncated");
                return true;
            }

            char key[48];
            QC::String::memset(key, 0, sizeof(key));
            if (!readToken(p, key, sizeof(key)))
            {
                ctx.writeLine("db: missing key");
                return true;
            }

            if (streqIgnoreCase(sub, "get"))
            {
                char value[192];
                QC::String::memset(value, 0, sizeof(value));
                const QC::Status st = db.get(key, value, sizeof(value));
                if (st != QC::Status::Success)
                {
                    ctx.writeLine("db: key not found");
                    return true;
                }
                char line[256];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), key);
                (void)appendString(line, sizeof(line), "=");
                (void)appendString(line, sizeof(line), value);
                ctx.writeLine(line);
                return true;
            }

            if (streqIgnoreCase(sub, "del"))
            {
                const QC::Status st = db.erase(key);
                if (st != QC::Status::Success)
                {
                    ctx.writeLine("db: key not found");
                    return true;
                }
                ctx.writeLine("db: deleted");
                return true;
            }

            if (streqIgnoreCase(sub, "set"))
            {
                const char *value = p ? skipSpaces(p) : nullptr;
                if (!value || *value == '\0')
                {
                    ctx.writeLine("usage: db set <key> <value>");
                    return true;
                }
                const QC::Status st = db.set(key, value);
                if (st != QC::Status::Success)
                {
                    ctx.writeLine("db: set failed");
                    return true;
                }
                ctx.writeLine("db: set");
                return true;
            }

            ctx.writeLine("usage: db <status|list|get|set|del|save|reload> ...");
            return true;
        }

        struct CsqlSession
        {
            bool open = false;
            QCQL::Database database = {};
            char path[192] = {};
        };

        static CsqlSession g_csqlSession;

        static const char *qcqlStatusName(QCQL::Status st)
        {
            switch (st)
            {
            case QCQL::Status::Success:
                return "Success";
            case QCQL::Status::Error:
                return "Error";
            case QCQL::Status::InvalidParam:
                return "InvalidParam";
            case QCQL::Status::NotFound:
                return "NotFound";
            case QCQL::Status::PermissionDenied:
                return "PermissionDenied";
            case QCQL::Status::AlreadyExists:
                return "AlreadyExists";
            case QCQL::Status::OutOfMemory:
                return "OutOfMemory";
            case QCQL::Status::NotSupported:
                return "NotSupported";
            case QCQL::Status::Corrupt:
                return "Corrupt";
            }
            return "Unknown";
        }

        static bool csqlListTables(const QC::Cmd::Context &ctx)
        {
            if (!g_csqlSession.open)
            {
                ctx.writeLine("csql: no database is open");
                return true;
            }

            char head[224];
            QC::String::memset(head, 0, sizeof(head));
            (void)appendString(head, sizeof(head), "csql: tables_loaded=");
            (void)appendU64Dec(head, sizeof(head), static_cast<QC::u64>(g_csqlSession.database.tables.size()));
            (void)appendString(head, sizeof(head), " path=");
            (void)appendString(head, sizeof(head), g_csqlSession.path);
            ctx.writeLine(head);

            if (g_csqlSession.database.tables.empty())
            {
                ctx.writeLine("csql: no tables found");
                return true;
            }

            for (QC::usize i = 0; i < g_csqlSession.database.tables.size(); ++i)
            {
                const QCQL::Table &t = g_csqlSession.database.tables[i];
                char line[192];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendU64Dec(line, sizeof(line), static_cast<QC::u64>(i + 1));
                (void)appendString(line, sizeof(line), ") ");
                (void)appendString(line, sizeof(line), t.name[0] ? t.name : "(unnamed)");
                (void)appendString(line, sizeof(line), " id=");
                (void)appendU64Dec(line, sizeof(line), static_cast<QC::u64>(t.tableId));
                (void)appendString(line, sizeof(line), " columns=");
                (void)appendU64Dec(line, sizeof(line), static_cast<QC::u64>(t.schema.columns.size()));
                ctx.writeLine(line);
            }

            return true;
        }

        static const char *csqlColumnTypeName(QCQL::ColumnType type)
        {
            switch (type)
            {
            case QCQL::ColumnType::Text:
                return "text";
            case QCQL::ColumnType::Int:
                return "int";
            case QCQL::ColumnType::Bool:
                return "bool";
            case QCQL::ColumnType::DateTime:
                return "datetime";
            }
            return "unknown";
        }

        static const QCQL::Table *csqlFindTableByName(const char *name)
        {
            if (!g_csqlSession.open || !name || *name == '\0')
                return nullptr;

            for (QC::usize i = 0; i < g_csqlSession.database.tables.size(); ++i)
            {
                const QCQL::Table &t = g_csqlSession.database.tables[i];
                if (streqIgnoreCase(t.name, name))
                    return &t;
            }
            return nullptr;
        }

        static bool csqlDescribeTable(const char *tableName, const QC::Cmd::Context &ctx)
        {
            if (!g_csqlSession.open)
            {
                ctx.writeLine("csql: no database is open");
                return true;
            }

            const QCQL::Table *table = csqlFindTableByName(tableName);
            if (!table)
            {
                ctx.writeLine("csql: table not found");
                return true;
            }

            char head[224];
            QC::String::memset(head, 0, sizeof(head));
            (void)appendString(head, sizeof(head), "csql: schema ");
            (void)appendString(head, sizeof(head), table->name);
            (void)appendString(head, sizeof(head), " columns=");
            (void)appendU64Dec(head, sizeof(head), static_cast<QC::u64>(table->schema.columns.size()));
            (void)appendString(head, sizeof(head), " pages=");
            (void)appendU64Dec(head, sizeof(head), static_cast<QC::u64>(table->pages.size()));
            ctx.writeLine(head);

            for (QC::usize i = 0; i < table->schema.columns.size(); ++i)
            {
                const QCQL::Column &col = table->schema.columns[i];
                char line[224];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendU64Dec(line, sizeof(line), static_cast<QC::u64>(i + 1));
                (void)appendString(line, sizeof(line), ") ");
                (void)appendString(line, sizeof(line), col.name[0] ? col.name : "(unnamed)");
                (void)appendString(line, sizeof(line), " type=");
                (void)appendString(line, sizeof(line), csqlColumnTypeName(col.type));
                if (col.isPrimaryKey)
                    (void)appendString(line, sizeof(line), " pk=1");
                ctx.writeLine(line);
            }

            return true;
        }

        static void csqlAppendCell(char *line, QC::usize cap, QC::u64 &len, const QCQL::Cell &cell)
        {
            auto appendText = [&](const char *text) {
                if (!text)
                    return;
                for (const char *p = text; *p && len + 1 < cap; ++p)
                    line[len++] = *p;
                line[len] = '\0';
            };

            auto appendU64 = [&](QC::u64 value) {
                char tmp[32];
                QC::String::memset(tmp, 0, sizeof(tmp));
                QC::usize digits = 0;
                if (value == 0)
                {
                    appendText("0");
                    return;
                }
                while (value > 0 && digits < sizeof(tmp))
                {
                    tmp[digits++] = static_cast<char>('0' + (value % 10));
                    value /= 10;
                }
                while (digits > 0)
                {
                    if (len + 1 >= cap)
                        break;
                    line[len++] = tmp[--digits];
                }
                line[len] = '\0';
            };

            if (cell.type == QCQL::ColumnType::Text)
            {
                for (QC::usize i = 0; i < cell.bytes.size(); ++i)
                {
                    const char ch = static_cast<char>(cell.bytes[i]);
                    if (ch == '\0')
                        continue;
                    if (ch >= 32 && ch <= 126)
                    {
                        if (len + 1 >= cap)
                            break;
                        line[len++] = ch;
                    }
                    else
                    {
                        if (len + 1 >= cap)
                            break;
                        line[len++] = ' ';
                    }
                }
                line[len] = '\0';
                return;
            }

            if (cell.type == QCQL::ColumnType::Bool)
            {
                appendText((!cell.bytes.empty() && cell.bytes[0] != 0) ? "true" : "false");
                return;
            }

            if (cell.type == QCQL::ColumnType::Int || cell.type == QCQL::ColumnType::DateTime)
            {
                QC::u64 value = 0;
                const QC::usize maxBytes = cell.bytes.size() < 8 ? cell.bytes.size() : static_cast<QC::usize>(8);
                for (QC::usize i = 0; i < maxBytes; ++i)
                    value |= (static_cast<QC::u64>(cell.bytes[i]) << (8 * i));
                if (cell.type == QCQL::ColumnType::DateTime)
                    appendText("ts=");
                appendU64(value);
                return;
            }

            appendText("<bin>");
        }

        static bool csqlParseU64(const char *text, QC::u64 &out)
        {
            if (!text)
                return false;
            while (*text == ' ' || *text == '\t')
                ++text;
            if (*text < '0' || *text > '9')
                return false;

            QC::u64 value = 0;
            while (*text >= '0' && *text <= '9')
            {
                value = (value * 10) + static_cast<QC::u64>(*text - '0');
                ++text;
            }
            out = value;
            return true;
        }

        static bool csqlDumpRows(const char *tableName, QC::u64 limit, const QC::Cmd::Context &ctx)
        {
            if (!g_csqlSession.open)
            {
                ctx.writeLine("csql: no database is open");
                return true;
            }

            const QCQL::Table *table = csqlFindTableByName(tableName);
            if (!table)
            {
                ctx.writeLine("csql: table not found");
                return true;
            }

            if (limit == 0)
                limit = 25;

            char head[224];
            QC::String::memset(head, 0, sizeof(head));
            (void)appendString(head, sizeof(head), "csql: rows ");
            (void)appendString(head, sizeof(head), table->name);
            (void)appendString(head, sizeof(head), " limit=");
            (void)appendU64Dec(head, sizeof(head), limit);
            (void)appendString(head, sizeof(head), " pages=");
            (void)appendU64Dec(head, sizeof(head), static_cast<QC::u64>(table->pages.size()));
            ctx.writeLine(head);

            QC::u64 emitted = 0;
            for (QC::usize p = 0; p < table->pages.size() && emitted < limit; ++p)
            {
                QCQL::Page page{};
                const QCQL::Status loadSt = QCQL::Engine::instance().loadPage(g_csqlSession.database, table->pages[p], page);
                if (loadSt != QCQL::Status::Success)
                    continue;

                for (QC::usize r = 0; r < page.rowOffsets.size() && emitted < limit; ++r)
                {
                    QCQL::Row row{};
                    const QCQL::Status rowSt = QCQL::Engine::instance().readRow(g_csqlSession.database, page.header.pageId, page.rowOffsets[r], row);
                    if (rowSt != QCQL::Status::Success || row.tombstone)
                        continue;

                    char line[320];
                    QC::String::memset(line, 0, sizeof(line));
                    QC::u64 len = 0;

                    for (QC::usize c = 0; c < row.cells.size(); ++c)
                    {
                        if (c > 0)
                        {
                            if (len + 3 < sizeof(line))
                            {
                                line[len++] = ' ';
                                line[len++] = '|';
                                line[len++] = ' ';
                                line[len] = '\0';
                            }
                        }
                        csqlAppendCell(line, sizeof(line), len, row.cells[c]);
                    }

                    ctx.writeLine(line[0] ? line : "(empty row)");
                    ++emitted;
                }
            }

            if (emitted == 0)
                ctx.writeLine("csql: no rows found");
            return true;
        }

        static bool csqlStartsWithIgnoreCase(const char *text, const char *prefix)
        {
            if (!text || !prefix)
                return false;
            while (*prefix)
            {
                if (*text == '\0')
                    return false;
                char a = *text;
                char b = *prefix;
                if (a >= 'A' && a <= 'Z')
                    a = static_cast<char>(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z')
                    b = static_cast<char>(b - 'A' + 'a');
                if (a != b)
                    return false;
                ++text;
                ++prefix;
            }
            return true;
        }

        static bool csqlIsIdentStart(char ch)
        {
            return (ch >= 'A' && ch <= 'Z') ||
                   (ch >= 'a' && ch <= 'z') ||
                   ch == '_';
        }

        static bool csqlIsIdentPart(char ch)
        {
            return csqlIsIdentStart(ch) ||
                   (ch >= '0' && ch <= '9');
        }

        static bool csqlReadIdentifier(const char *&p, char *out, QC::usize outSize)
        {
            if (!out || outSize == 0)
                return false;

            QC::String::memset(out, 0, outSize);
            p = p ? skipSpaces(p) : nullptr;
            if (!p || !csqlIsIdentStart(*p))
                return false;

            QC::usize i = 0;
            while (*p && csqlIsIdentPart(*p) && i + 1 < outSize)
                out[i++] = *p++;
            out[i] = '\0';
            return i > 0;
        }

        static bool csqlConsumeKeyword(const char *&p, const char *keyword)
        {
            p = p ? skipSpaces(p) : nullptr;
            if (!p || !keyword)
                return false;

            const char *cursor = p;
            while (*keyword)
            {
                if (*cursor == '\0')
                    return false;
                char a = *cursor;
                char b = *keyword;
                if (a >= 'A' && a <= 'Z')
                    a = static_cast<char>(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z')
                    b = static_cast<char>(b - 'A' + 'a');
                if (a != b)
                    return false;
                ++cursor;
                ++keyword;
            }

            if (*cursor != '\0' && !isSpace(*cursor) && *cursor != '(' && *cursor != ')' && *cursor != ',')
                return false;

            p = cursor;
            return true;
        }

        static bool csqlConsumeChar(const char *&p, char token)
        {
            p = p ? skipSpaces(p) : nullptr;
            if (!p || *p != token)
                return false;
            ++p;
            return true;
        }

        static bool csqlParseColumnType(const char *&p, QCQL::ColumnType &outType)
        {
            char typeName[32];
            if (!csqlReadIdentifier(p, typeName, sizeof(typeName)))
                return false;

            if (streqIgnoreCase(typeName, "text") ||
                streqIgnoreCase(typeName, "varchar") ||
                streqIgnoreCase(typeName, "char") ||
                streqIgnoreCase(typeName, "string"))
            {
                outType = QCQL::ColumnType::Text;
            }
            else if (streqIgnoreCase(typeName, "int") ||
                     streqIgnoreCase(typeName, "integer") ||
                     streqIgnoreCase(typeName, "tinyint") ||
                     streqIgnoreCase(typeName, "smallint") ||
                     streqIgnoreCase(typeName, "bigint"))
            {
                outType = QCQL::ColumnType::Int;
            }
            else if (streqIgnoreCase(typeName, "bool") ||
                     streqIgnoreCase(typeName, "boolean"))
            {
                outType = QCQL::ColumnType::Bool;
            }
            else if (streqIgnoreCase(typeName, "datetime") ||
                     streqIgnoreCase(typeName, "timestamp"))
            {
                outType = QCQL::ColumnType::DateTime;
            }
            else
            {
                return false;
            }

            p = p ? skipSpaces(p) : nullptr;
            if (p && *p == '(')
            {
                ++p;
                while (*p && *p != ')')
                    ++p;
                if (*p != ')')
                    return false;
                ++p;
            }

            return true;
        }

        static bool csqlParseCreateTableDefinition(const char *definition, QCQL::TableSchema &outSchema)
        {
            const char *p = definition ? skipSpaces(definition) : nullptr;
            if (!p)
                return false;

            outSchema = QCQL::TableSchema{};
            if (!csqlReadIdentifier(p, outSchema.tableName, sizeof(outSchema.tableName)))
                return false;
            if (!csqlConsumeChar(p, '('))
                return false;

            QC::u32 pkCount = 0;
            while (true)
            {
                QCQL::Column column{};
                if (!csqlReadIdentifier(p, column.name, sizeof(column.name)))
                    return false;
                if (!csqlParseColumnType(p, column.type))
                    return false;

                const char *pkCursor = p;
                if (csqlConsumeKeyword(pkCursor, "PRIMARY"))
                {
                    if (!csqlConsumeKeyword(pkCursor, "KEY"))
                        return false;
                    column.isPrimaryKey = true;
                    p = pkCursor;
                    outSchema.primaryKeyIndex = static_cast<QC::u32>(outSchema.columns.size());
                    ++pkCount;
                }

                outSchema.columns.push_back(static_cast<QCQL::Column &&>(column));
                if (outSchema.columns.size() > QCQL::kMaxColumnsPerTable)
                    return false;

                p = p ? skipSpaces(p) : nullptr;
                if (!p)
                    return false;
                if (*p == ',')
                {
                    ++p;
                    continue;
                }
                if (*p == ')')
                {
                    ++p;
                    break;
                }
                return false;
            }

            p = p ? skipSpaces(p) : nullptr;
            if (!p || *p != '\0')
                return false;

            return !outSchema.columns.empty() && pkCount == 1;
        }

        static bool csqlCreateTableFromDefinition(const char *definition, const QC::Cmd::Context &ctx)
        {
            if (!g_csqlSession.open)
            {
                ctx.writeLine("csql: no database is open");
                return true;
            }

            QCQL::TableSchema schema{};
            if (!csqlParseCreateTableDefinition(definition, schema))
            {
                ctx.writeLine("usage: csql create table <name>(<col> <type> [PRIMARY KEY], ...)");
                return true;
            }

            const QCQL::Status st = QCQL::Engine::instance().createTable(g_csqlSession.database, schema);
            if (st != QCQL::Status::Success)
            {
                char line[224];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "csql: create table failed (");
                (void)appendString(line, sizeof(line), qcqlStatusName(st));
                (void)appendString(line, sizeof(line), ")");
                ctx.writeLine(line);
                return true;
            }

            char line[224];
            QC::String::memset(line, 0, sizeof(line));
            (void)appendString(line, sizeof(line), "csql: created table ");
            (void)appendString(line, sizeof(line), schema.tableName);
            (void)appendString(line, sizeof(line), " columns=");
            (void)appendU64Dec(line, sizeof(line), static_cast<QC::u64>(schema.columns.size()));
            ctx.writeLine(line);
            return true;
        }

        static bool csqlCloseIfOpen(const QC::Cmd::Context &ctx)
        {
            if (!g_csqlSession.open)
                return true;

            const QCQL::Status st = QCQL::Engine::instance().closeDatabase(g_csqlSession.database);
            g_csqlSession.open = false;
            g_csqlSession.path[0] = '\0';
            if (st != QCQL::Status::Success)
            {
                ctx.writeLine("csql: close warning");
            }
            return true;
        }

        static bool cmdCsql(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            char sub[16];
            QC::String::memset(sub, 0, sizeof(sub));
            const char *p = args;
            if (!readToken(p, sub, sizeof(sub)))
            {
                ctx.writeLine("usage: csql <status|open|create|close|show|exec> ...");
                return true;
            }

            QCQL::Engine::instance().initialize();

            if (streqIgnoreCase(sub, "status"))
            {
                char line[224];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "csql: open=");
                (void)appendString(line, sizeof(line), g_csqlSession.open ? "1" : "0");
                (void)appendString(line, sizeof(line), " path=");
                (void)appendString(line, sizeof(line), g_csqlSession.open ? g_csqlSession.path : "(none)");
                (void)appendString(line, sizeof(line), " tables_loaded=");
                (void)appendU64Dec(line, sizeof(line), g_csqlSession.open ? static_cast<QC::u64>(g_csqlSession.database.tables.size()) : 0);
                ctx.writeLine(line);
                return true;
            }

            if (streqIgnoreCase(sub, "close"))
            {
                (void)csqlCloseIfOpen(ctx);
                ctx.writeLine("csql: closed");
                return true;
            }

            if (streqIgnoreCase(sub, "open") || streqIgnoreCase(sub, "create"))
            {
                Session *s = sessionFrom();
                char pathArg[192];
                QC::String::memset(pathArg, 0, sizeof(pathArg));
                if (!readToken(p, pathArg, sizeof(pathArg)))
                {
                    ctx.writeLine(streqIgnoreCase(sub, "open") ? "usage: csql open <path>" : "usage: csql create <path>");
                    return true;
                }

                if (streqIgnoreCase(sub, "create") && streqIgnoreCase(pathArg, "table"))
                    return csqlCreateTableFromDefinition(p ? skipSpaces(p) : nullptr, ctx);

                // Guard common SQL-looking input that would otherwise be treated as a file path.
                if (streqIgnoreCase(pathArg, "database"))
                {
                    ctx.writeLine("csql: expected database path, not SQL text");
                    ctx.writeLine("hint: use 'csql create <path>' for DB files or 'csql create table ...' for schemas");
                    return true;
                }

                char absPath[192];
                QC::String::memset(absPath, 0, sizeof(absPath));
                if (!resolvePath(s, pathArg, absPath, sizeof(absPath)))
                {
                    ctx.writeLine("csql: invalid path");
                    return true;
                }

                if (!allowWriteToPath(absPath, ctx, "csql"))
                    return true;

                if (streqIgnoreCase(sub, "create"))
                {
                    (void)QFS::VFS::instance().createDir("/system");
                    (void)QFS::VFS::instance().createDir("/system/db");
                }

                (void)csqlCloseIfOpen(ctx);

                QCQL::Database db;
                const QCQL::Status st = streqIgnoreCase(sub, "open")
                                            ? QCQL::Engine::instance().openDatabase(absPath, db)
                                            : QCQL::Engine::instance().createDatabase(absPath, db);
                if (st != QCQL::Status::Success)
                {
                    char line[160];
                    QC::String::memset(line, 0, sizeof(line));
                    (void)appendString(line, sizeof(line), "csql: ");
                    (void)appendString(line, sizeof(line), streqIgnoreCase(sub, "open") ? "open" : "create");
                    (void)appendString(line, sizeof(line), " failed (");
                    (void)appendString(line, sizeof(line), qcqlStatusName(st));
                    (void)appendString(line, sizeof(line), ")");
                    ctx.writeLine(line);
                    return true;
                }

                g_csqlSession.database = db;
                g_csqlSession.open = true;
                QC::String::strncpy(g_csqlSession.path, absPath, sizeof(g_csqlSession.path) - 1);

                char line[224];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "csql: ");
                (void)appendString(line, sizeof(line), streqIgnoreCase(sub, "open") ? "opened " : "created ");
                (void)appendString(line, sizeof(line), g_csqlSession.path);
                (void)appendString(line, sizeof(line), " tables_loaded=");
                (void)appendU64Dec(line, sizeof(line), static_cast<QC::u64>(g_csqlSession.database.tables.size()));
                ctx.writeLine(line);
                return true;
            }

            if (streqIgnoreCase(sub, "show"))
            {
                char noun[16];
                QC::String::memset(noun, 0, sizeof(noun));
                if (!readToken(p, noun, sizeof(noun)))
                {
                    ctx.writeLine("usage: csql show <tables|schema|rows> ...");
                    return true;
                }

                if (streqIgnoreCase(noun, "tables"))
                    return csqlListTables(ctx);

                if (streqIgnoreCase(noun, "schema"))
                {
                    char table[64];
                    QC::String::memset(table, 0, sizeof(table));
                    if (!readToken(p, table, sizeof(table)))
                    {
                        ctx.writeLine("usage: csql show schema <table>");
                        return true;
                    }
                    return csqlDescribeTable(table, ctx);
                }

                if (streqIgnoreCase(noun, "rows"))
                {
                    char table[64];
                    QC::String::memset(table, 0, sizeof(table));
                    if (!readToken(p, table, sizeof(table)))
                    {
                        ctx.writeLine("usage: csql show rows <table> [limit]");
                        return true;
                    }

                    QC::u64 limit = 25;
                    const char *tail = p ? skipSpaces(p) : nullptr;
                    if (tail && *tail)
                        (void)csqlParseU64(tail, limit);
                    return csqlDumpRows(table, limit, ctx);
                }

                ctx.writeLine("usage: csql show <tables|schema|rows> ...");
                return true;
            }

            if (streqIgnoreCase(sub, "exec"))
            {
                const char *query = p ? skipSpaces(p) : nullptr;
                if (!query || *query == '\0')
                {
                    ctx.writeLine("usage: csql exec \"SHOW TABLES\" | \"DESCRIBE <table>\" | \"SELECT * FROM <table> [LIMIT N]\"");
                    return true;
                }

                if (streqIgnoreCase(query, "SHOW TABLES") || streqIgnoreCase(query, "DUMP TABLES_LOADED"))
                    return csqlListTables(ctx);

                if (csqlStartsWithIgnoreCase(query, "CREATE TABLE "))
                    return csqlCreateTableFromDefinition(query + 13, ctx);

                if (csqlStartsWithIgnoreCase(query, "DESCRIBE "))
                {
                    const char *name = skipSpaces(query + 9);
                    if (!name || *name == '\0')
                    {
                        ctx.writeLine("usage: csql exec \"DESCRIBE <table>\"");
                        return true;
                    }

                    char table[64];
                    QC::String::memset(table, 0, sizeof(table));
                    const char *cursor = name;
                    QC::usize i = 0;
                    while (*cursor && !isSpace(*cursor) && i + 1 < sizeof(table))
                        table[i++] = *cursor++;
                    table[i] = '\0';
                    return csqlDescribeTable(table, ctx);
                }

                if (csqlStartsWithIgnoreCase(query, "SELECT * FROM "))
                {
                    const char *name = skipSpaces(query + 14);
                    if (!name || *name == '\0')
                    {
                        ctx.writeLine("usage: csql exec \"SELECT * FROM <table> [LIMIT N]\"");
                        return true;
                    }

                    char table[64];
                    QC::String::memset(table, 0, sizeof(table));
                    const char *cursor = name;
                    QC::usize i = 0;
                    while (*cursor && !isSpace(*cursor) && i + 1 < sizeof(table))
                        table[i++] = *cursor++;
                    table[i] = '\0';

                    QC::u64 limit = 25;
                    const char *tail = skipSpaces(cursor);
                    if (tail && *tail)
                    {
                        if (csqlStartsWithIgnoreCase(tail, "LIMIT "))
                        {
                            const char *n = skipSpaces(tail + 6);
                            (void)csqlParseU64(n, limit);
                        }
                    }

                    return csqlDumpRows(table, limit, ctx);
                }

                if (csqlStartsWithIgnoreCase(query, "SHOW COLUMNS FROM "))
                {
                    const char *name = skipSpaces(query + 18);
                    if (!name || *name == '\0')
                    {
                        ctx.writeLine("usage: csql exec \"SHOW COLUMNS FROM <table>\"");
                        return true;
                    }

                    char table[64];
                    QC::String::memset(table, 0, sizeof(table));
                    const char *cursor = name;
                    QC::usize i = 0;
                    while (*cursor && !isSpace(*cursor) && i + 1 < sizeof(table))
                        table[i++] = *cursor++;
                    table[i] = '\0';
                    return csqlDescribeTable(table, ctx);
                }

                ctx.writeLine("csql: supported exec forms are CREATE TABLE <name>(...), SHOW TABLES, DESCRIBE <table>, SHOW COLUMNS FROM <table>, SELECT * FROM <table> [LIMIT N]");
                return true;
            }

            ctx.writeLine("usage: csql <status|open|create|close|show|exec> ...");
            return true;
        }

        static bool cmdTouch(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            Session *s = sessionFrom();
            const char *p = args ? skipSpaces(args) : nullptr;
            if (!p || *p == '\0')
            {
                ctx.writeLine("touch: missing file operand");
                return true;
            }

            // Extract first token as path.
            char fileArg[256];
            QC::String::memset(fileArg, 0, sizeof(fileArg));
            QC::usize fi = 0;
            while (*p && !isSpace(*p) && fi + 1 < sizeof(fileArg))
            {
                fileArg[fi++] = *p++;
            }
            fileArg[fi] = '\0';

            char path[256];
            QC::String::memset(path, 0, sizeof(path));
            if (!resolvePath(s, fileArg, path, sizeof(path)))
            {
                ctx.writeLine("touch: invalid path");
                return true;
            }

            if (!allowWriteToPath(path, ctx, "touch"))
                return true;

            const QC::usize len = QC::String::strlen(path);
            if (len == 0 || path[len - 1] == '/')
            {
                ctx.writeLine("touch: invalid path");
                return true;
            }

            const QC::Status st = QK::SecurityCenter::instance().secureWriteFile(path, nullptr, 0, true);
            if (st != QC::Status::Success)
            {
                ctx.writeLine("touch: cannot create file");
                return true;
            }
            return true;
        }

        static bool cmdCp(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            Session *s = sessionFrom();
            const char *p = args ? skipSpaces(args) : nullptr;
            if (!p || *p == '\0')
            {
                ctx.writeLine("usage: cp <source> <dest>");
                return true;
            }

            char srcArg[256];
            QC::String::memset(srcArg, 0, sizeof(srcArg));
            if (!readToken(p, srcArg, sizeof(srcArg)))
            {
                ctx.writeLine("usage: cp <source> <dest>");
                return true;
            }

            char dstArg[256];
            QC::String::memset(dstArg, 0, sizeof(dstArg));
            if (!readToken(p, dstArg, sizeof(dstArg)))
            {
                ctx.writeLine("usage: cp <source> <dest>");
                return true;
            }

            char extra[8];
            QC::String::memset(extra, 0, sizeof(extra));
            if (readToken(p, extra, sizeof(extra)))
            {
                ctx.writeLine("usage: cp <source> <dest>");
                return true;
            }

            char srcPath[256];
            char dstPath[256];
            QC::String::memset(srcPath, 0, sizeof(srcPath));
            QC::String::memset(dstPath, 0, sizeof(dstPath));
            if (!resolvePath(s, srcArg, srcPath, sizeof(srcPath)))
            {
                ctx.writeLine("cp: invalid source path");
                return true;
            }
            if (!resolvePath(s, dstArg, dstPath, sizeof(dstPath)))
            {
                ctx.writeLine("cp: invalid destination path");
                return true;
            }

            if (QC::String::strcmp(srcPath, dstPath) == 0)
            {
                ctx.writeLine("cp: source and destination are the same");
                return true;
            }

            if (!allowWriteToPath(dstPath, ctx, "cp"))
                return true;

            const QC::usize dstLen = QC::String::strlen(dstPath);
            if (dstLen == 0 || dstPath[dstLen - 1] == '/')
            {
                ctx.writeLine("cp: invalid destination path");
                return true;
            }

            QFS::FileInfo srcInfo;
            QC::String::memset(&srcInfo, 0, sizeof(srcInfo));
            const QC::Status srcSt = QFS::VFS::instance().stat(srcPath, &srcInfo);
            if (srcSt != QC::Status::Success || srcInfo.type != QFS::FileType::Regular)
            {
                ctx.writeLine("cp: source must be a regular file");
                return true;
            }

            if (srcInfo.size > 1024ULL * 1024ULL)
            {
                ctx.writeLine("cp: source too large (limit 1 MiB for now)");
                return true;
            }

            QC::Vector<QC::u8> buf;
            const QC::usize srcSize = static_cast<QC::usize>(srcInfo.size);
            buf.resize(srcSize);
            if (buf.size() != srcSize)
            {
                ctx.writeLine("cp: out of memory");
                return true;
            }

            QC::usize bytesRead = 0;
            const QC::Status readSt = QK::SecurityCenter::instance().secureReadFile(srcPath,
                                                                                     srcSize ? buf.data() : nullptr,
                                                                                     srcSize,
                                                                                     &bytesRead);
            if (readSt != QC::Status::Success || bytesRead != srcSize)
            {
                ctx.writeLine("cp: cannot read source file");
                return true;
            }

            const QC::Status writeSt = QK::SecurityCenter::instance().secureWriteFile(dstPath,
                                                                                       srcSize ? buf.data() : nullptr,
                                                                                       srcSize,
                                                                                       false);
            if (writeSt != QC::Status::Success)
            {
                ctx.writeLine("cp: cannot write destination file");
                return true;
            }

            char line[320];
            QC::String::memset(line, 0, sizeof(line));
            (void)appendString(line, sizeof(line), "cp: copied ");
            (void)appendString(line, sizeof(line), srcPath);
            (void)appendString(line, sizeof(line), " -> ");
            (void)appendString(line, sizeof(line), dstPath);
            ctx.writeLine(line);
            return true;
        }

        static bool cmdMkdir(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            Session *s = sessionFrom();
            const char *p = args ? skipSpaces(args) : nullptr;
            if (!p || *p == '\0')
            {
                ctx.writeLine("mkdir: missing directory operand");
                return true;
            }

            // Extract first token as path.
            char dirArg[256];
            QC::String::memset(dirArg, 0, sizeof(dirArg));
            QC::usize di = 0;
            while (*p && !isSpace(*p) && di + 1 < sizeof(dirArg))
                dirArg[di++] = *p++;
            dirArg[di] = '\0';

            char path[256];
            QC::String::memset(path, 0, sizeof(path));
            if (!resolvePath(s, dirArg, path, sizeof(path)))
            {
                ctx.writeLine("mkdir: invalid path");
                return true;
            }

            if (!allowWriteToPath(path, ctx, "mkdir"))
                return true;

            if (QC::String::strcmp(path, "/") == 0)
            {
                ctx.writeLine("mkdir: invalid path");
                return true;
            }

            QFS::FileInfo info;
            QC::String::memset(&info, 0, sizeof(info));
            const QC::Status st = QFS::VFS::instance().stat(path, &info);
            if (st == QC::Status::Success)
            {
                if (info.type == QFS::FileType::Directory)
                    ctx.writeLine("mkdir: already exists");
                else
                    ctx.writeLine("mkdir: path exists and is not a directory");
                return true;
            }

            const QC::Status mk = QFS::VFS::instance().createDir(path);
            if (mk != QC::Status::Success)
            {
                ctx.writeLine("mkdir: failed");
                return true;
            }

            return true;
        }

        static bool cmdRm(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            Session *s = sessionFrom();
            const char *p = args ? skipSpaces(args) : nullptr;
            if (!p || *p == '\0')
            {
                ctx.writeLine("rm: missing operand");
                return true;
            }

            bool recursive = false;
            char tok[256];
            QC::String::memset(tok, 0, sizeof(tok));
            if (!readToken(p, tok, sizeof(tok)))
            {
                ctx.writeLine("rm: missing operand");
                return true;
            }

            if (streqIgnoreCase(tok, "-r") || streqIgnoreCase(tok, "-R"))
            {
                recursive = true;
                if (!readToken(p, tok, sizeof(tok)))
                {
                    ctx.writeLine("rm: missing operand");
                    return true;
                }
            }
            else if (tok[0] == '-')
            {
                ctx.writeLine("rm: unknown option");
                ctx.writeLine("usage: rm [-r] <path>");
                return true;
            }

            char path[256];
            QC::String::memset(path, 0, sizeof(path));
            if (!resolvePath(s, tok, path, sizeof(path)))
            {
                ctx.writeLine("rm: invalid path");
                return true;
            }

            if (!allowWriteToPath(path, ctx, "rm"))
                return true;

            if (QC::String::strcmp(path, "/") == 0)
            {
                ctx.writeLine("rm: refusing to remove '/'");
                return true;
            }

            QFS::FileInfo info;
            QC::String::memset(&info, 0, sizeof(info));
            const QC::Status st = QFS::VFS::instance().stat(path, &info);
            if (st != QC::Status::Success)
            {
                ctx.writeLine("rm: no such file or directory");
                return true;
            }


            auto dirIsEmpty = [&](const char *dirPath) -> bool {
                QFS::Directory *dir = QFS::VFS::instance().openDir(dirPath);
                if (!dir)
                    return false;
                QFS::DirEntry entry;
                const bool any = dir->read(&entry);
                QFS::VFS::instance().closeDir(dir);
                return !any;
            };

            auto joinPath = [&](const char *base, const char *name, char *out, QC::usize outSize) -> bool {
                if (!base || !name || !out || outSize == 0)
                    return false;
                QC::String::memset(out, 0, outSize);
                if (QC::String::strcmp(base, "/") == 0)
                {
                    if (!appendString(out, outSize, "/"))
                        return false;
                    return appendString(out, outSize, name);
                }

                if (!appendString(out, outSize, base))
                    return false;
                if (!appendString(out, outSize, "/"))
                    return false;
                return appendString(out, outSize, name);
            };

            struct RemoveTree
            {
                static bool run(const char *rootPath,
                                const QC::Cmd::Context &ctx,
                                const decltype(joinPath) &joinPathFn)
                {
                    QFS::Directory *dir = QFS::VFS::instance().openDir(rootPath);
                    if (!dir)
                    {
                        ctx.writeLine("rm: cannot open directory");
                        return false;
                    }

                    QFS::DirEntry entry;
                    while (dir->read(&entry))
                    {
                        char child[256];
                        if (!joinPathFn(rootPath, entry.name, child, sizeof(child)))
                        {
                            QFS::VFS::instance().closeDir(dir);
                            ctx.writeLine("rm: path too long");
                            return false;
                        }

                        if (entry.type == QFS::FileType::Directory)
                        {
                            if (!run(child, ctx, joinPathFn))
                            {
                                QFS::VFS::instance().closeDir(dir);
                                return false;
                            }

                            const QC::Status st = QFS::VFS::instance().removeDir(child);
                            if (st != QC::Status::Success)
                            {
                                QFS::VFS::instance().closeDir(dir);
                                ctx.writeLine("rm: failed to remove directory");
                                return false;
                            }
                        }
                        else
                        {
                            const QC::Status st = QFS::VFS::instance().remove(child);
                            if (st != QC::Status::Success)
                            {
                                QFS::VFS::instance().closeDir(dir);
                                ctx.writeLine("rm: failed to remove file");
                                return false;
                            }
                        }
                    }

                    QFS::VFS::instance().closeDir(dir);
                    return true;
                }
            };

            if (info.type == QFS::FileType::Directory)
            {
                const bool empty = dirIsEmpty(path);
                if (!empty && !recursive)
                {
                    ctx.writeLine("rm: directory not empty (use -r)");
                    return true;
                }

                if (recursive)
                {
                    if (!RemoveTree::run(path, ctx, joinPath))
                        return true;
                }

                const QC::Status rmst = QFS::VFS::instance().removeDir(path);
                if (rmst != QC::Status::Success)
                    ctx.writeLine("rm: failed to remove directory");
                return true;
            }

            const QC::Status rmst = QFS::VFS::instance().remove(path);
            if (rmst != QC::Status::Success)
                ctx.writeLine("rm: failed");
            return true;
        }

        static bool globMatchStarOnly(const char *pattern, const char *text)
        {
            if (!pattern || !text)
                return false;

            // DOS-ish convenience: treat "*.*" as "*".
            if (QC::String::strcmp(pattern, "*.*") == 0)
                pattern = "*";

            // Another DOS-ish convenience: "name.*" matches "name" too.
            const QC::usize plen = QC::String::strlen(pattern);
            if (plen >= 2 && pattern[plen - 2] == '.' && pattern[plen - 1] == '*')
            {
                char base[256];
                QC::String::memset(base, 0, sizeof(base));
                const QC::usize blen = plen - 2;
                if (blen + 1 < sizeof(base))
                {
                    QC::String::memcpy(base, pattern, blen);
                    base[blen] = '\0';
                    if (QC::String::strcmp(text, base) == 0)
                        return true;
                }
            }

            // Simple '*' glob.
            const char *p = pattern;
            const char *t = text;
            const char *star = nullptr;
            const char *starText = nullptr;

            while (*t)
            {
                if (*p == '*')
                {
                    star = p++;
                    starText = t;
                    continue;
                }
                if (*p == *t)
                {
                    ++p;
                    ++t;
                    continue;
                }
                if (star)
                {
                    p = star + 1;
                    t = ++starText;
                    continue;
                }
                return false;
            }

            while (*p == '*')
                ++p;
            return *p == '\0';
        }

        static bool cmdDel(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            Session *s = sessionFrom();
            const char *p = args ? skipSpaces(args) : nullptr;
            if (!p || *p == '\0')
            {
                ctx.writeLine("del: missing operand");
                ctx.writeLine("usage: del <pattern> [pattern2 ...]");
                return true;
            }

            QC::u32 removed = 0;
            QC::u32 matched = 0;

            char patTok[256];
            while (readToken(p, patTok, sizeof(patTok)))
            {
                // Split into directory + pattern on the last '/'.
                const char *lastSlash = nullptr;
                for (const char *q = patTok; *q; ++q)
                {
                    if (*q == '/')
                        lastSlash = q;
                }

                char dirArg[256];
                char namePat[256];
                QC::String::memset(dirArg, 0, sizeof(dirArg));
                QC::String::memset(namePat, 0, sizeof(namePat));

                if (lastSlash)
                {
                    const QC::usize dirLen = static_cast<QC::usize>(lastSlash - patTok);
                    if (dirLen == 0)
                    {
                        QC::String::strncpy(dirArg, "/", sizeof(dirArg) - 1);
                    }
                    else
                    {
                        if (dirLen + 1 >= sizeof(dirArg))
                        {
                            ctx.writeLine("del: path too long");
                            continue;
                        }
                        QC::String::memcpy(dirArg, patTok, dirLen);
                        dirArg[dirLen] = '\0';
                    }

                    QC::String::strncpy(namePat, lastSlash + 1, sizeof(namePat) - 1);
                }
                else
                {
                    QC::String::strncpy(dirArg, ".", sizeof(dirArg) - 1);
                    QC::String::strncpy(namePat, patTok, sizeof(namePat) - 1);
                }

                if (namePat[0] == '\0')
                {
                    ctx.writeLine("del: invalid pattern");
                    continue;
                }

                char dirPath[256];
                QC::String::memset(dirPath, 0, sizeof(dirPath));
                if (!resolvePath(s, dirArg, dirPath, sizeof(dirPath)))
                {
                    ctx.writeLine("del: invalid path");
                    continue;
                }

                if (!allowWriteToPath(dirPath, ctx, "del"))
                    continue;

                QFS::Directory *dir = QFS::VFS::instance().openDir(dirPath);
                if (!dir)
                {
                    ctx.writeLine("del: cannot open directory");
                    continue;
                }

                QFS::DirEntry entry;
                while (dir->read(&entry))
                {
                    if (!globMatchStarOnly(namePat, entry.name))
                        continue;
                    ++matched;

                    if (entry.type == QFS::FileType::Directory)
                        continue; // del only removes files

                    char full[256];
                    QC::String::memset(full, 0, sizeof(full));
                    if (QC::String::strcmp(dirPath, "/") == 0)
                    {
                        if (!appendString(full, sizeof(full), "/") || !appendString(full, sizeof(full), entry.name))
                            continue;
                    }
                    else
                    {
                        if (!appendString(full, sizeof(full), dirPath) || !appendString(full, sizeof(full), "/") ||
                            !appendString(full, sizeof(full), entry.name))
                            continue;
                    }

                    const QC::Status st = QFS::VFS::instance().remove(full);
                    if (st == QC::Status::Success)
                        ++removed;
                }

                QFS::VFS::instance().closeDir(dir);
            }

            if (matched == 0)
            {
                ctx.writeLine("del: no matches");
                return true;
            }

            char line[96];
            QC::String::memset(line, 0, sizeof(line));
            (void)appendString(line, sizeof(line), "del: removed ");
            (void)appendU64Dec(line, sizeof(line), removed);
            (void)appendString(line, sizeof(line), " file(s)");
            ctx.writeLine(line);
            return true;
        }

        static bool cmdHexdump(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            // usage: hexdump <path> [max_bytes]
            Session *s = sessionFrom();
            const char *p = args ? skipSpaces(args) : nullptr;
            if (!p || *p == '\0')
            {
                ctx.writeLine("hexdump: missing file operand");
                return true;
            }

            // Extract first token as path.
            char fileArg[256];
            QC::String::memset(fileArg, 0, sizeof(fileArg));
            QC::usize fi = 0;
            while (*p && !isSpace(*p) && fi + 1 < sizeof(fileArg))
                fileArg[fi++] = *p++;
            fileArg[fi] = '\0';
            p = skipSpaces(p);

            QC::u64 maxBytes = 4096;
            if (p && *p)
            {
                QC::u64 v = 0;
                bool any = false;
                while (*p >= '0' && *p <= '9')
                {
                    any = true;
                    v = (v * 10) + (QC::u64)(*p - '0');
                    ++p;
                }
                if (any)
                {
                    if (v < 64)
                        v = 64;
                    if (v > 65536)
                        v = 65536;
                    maxBytes = v;
                }
            }

            char path[256];
            QC::String::memset(path, 0, sizeof(path));
            if (!resolvePath(s, fileArg, path, sizeof(path)))
            {
                ctx.writeLine("hexdump: invalid path");
                return true;
            }

            QC::Vector<char> dump;
            if (!readFileToNullTerminatedBuffer(path, dump, static_cast<QC::usize>(maxBytes)))
            {
                ctx.writeLine("hexdump: cannot open file");
                return true;
            }

            static const char kHex[] = "0123456789abcdef";
            QC::u64 offset = 0;
            QC::usize inOff = 0;
            while (offset < maxBytes && inOff < dump.size())
            {
                QC::u8 buf[16];
                QC::isize n = 0;
                for (; n < 16 && inOff < dump.size() && dump[inOff] != '\0'; ++n, ++inOff)
                    buf[n] = static_cast<QC::u8>(dump[inOff]);
                if (n <= 0)
                    break;

                char line[128];
                QC::String::memset(line, 0, sizeof(line));
                QC::usize pos = 0;

                // offset (8 hex)
                for (int sh = 28; sh >= 0 && pos + 1 < sizeof(line); sh -= 4)
                    line[pos++] = kHex[(QC::u8)((offset >> sh) & 0xF)];
                if (pos + 2 < sizeof(line))
                {
                    line[pos++] = ' '; line[pos++] = ' ';
                }

                // hex bytes
                for (QC::isize i = 0; i < 16; ++i)
                {
                    if (i < n)
                    {
                        const QC::u8 b = buf[i];
                        line[pos++] = kHex[(b >> 4) & 0xF];
                        line[pos++] = kHex[b & 0xF];
                    }
                    else
                    {
                        line[pos++] = ' '; line[pos++] = ' ';
                    }
                    if (pos + 1 < sizeof(line))
                        line[pos++] = (i == 7) ? ' ' : ' ';
                }

                if (pos + 2 < sizeof(line))
                {
                    line[pos++] = ' '; line[pos++] = '|';
                }

                // ascii
                for (QC::isize i = 0; i < n && pos + 1 < sizeof(line); ++i)
                {
                    const QC::u8 b = buf[i];
                    const char c = (b >= 32 && b <= 126) ? (char)b : '.';
                    line[pos++] = c;
                }
                if (pos + 2 < sizeof(line))
                {
                    line[pos++] = '|';
                    line[pos] = '\0';
                }

                ctx.writeLine(line);
                offset += (QC::u64)n;
            }
            return true;
        }

        static bool cmdShutdown(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            auto phaseName = [](QK::Shutdown::Phase phase) -> const char * {
                switch (phase)
                {
                case QK::Shutdown::Phase::Idle:
                    return "idle";
                case QK::Shutdown::Phase::NotifyingSubsystems:
                    return "notifying-subsystems";
                case QK::Shutdown::Phase::AwaitingUserDecision:
                    return "awaiting-user-decision";
                case QK::Shutdown::Phase::PoweringOff:
                    return "powering-off";
                default:
                    return "unknown";
                }
            };

            auto reasonName = [](QK::Shutdown::Reason reason) -> const char * {
                switch (reason)
                {
                case QK::Shutdown::Reason::UserRequest:
                    return "user-request";
                case QK::Shutdown::Reason::ShellCommand:
                    return "shell-command";
                case QK::Shutdown::Reason::KeyboardShortcut:
                    return "keyboard-shortcut";
                case QK::Shutdown::Reason::SidebarPowerButton:
                    return "sidebar-power-button";
                case QK::Shutdown::Reason::SystemPolicy:
                    return "system-policy";
                default:
                    return "unknown";
                }
            };

            args = args ? skipSpaces(args) : nullptr;

            if (args && *args)
            {
                char op[16];
                QC::String::memset(op, 0, sizeof(op));
                if (!readToken(args, op, sizeof(op)))
                {
                    ctx.writeLine("shutdown: usage: shutdown [status]");
                    return true;
                }

                if (streqIgnoreCase(op, "status"))
                {
                    auto &controller = QK::Shutdown::Controller::instance();

                    char line[192];
                    QC::String::memset(line, 0, sizeof(line));
                    (void)appendString(line, sizeof(line), "shutdown: phase=");
                    (void)appendString(line, sizeof(line), phaseName(controller.phase()));
                    (void)appendString(line, sizeof(line), " reason=");
                    (void)appendString(line, sizeof(line), reasonName(controller.reason()));
                    ctx.writeLine(line);

                    ctx.writeLine("shutdown policy: acpi-first, then grace wait, then legacy firmware/hypervisor fallback ports");
                    return true;
                }

                ctx.writeLine("shutdown: usage: shutdown [status]");
                return true;
            }

            ctx.writeLine("Shutdown requested.");
            QK::Event::EventManager::instance().postShutdownEvent(
                QK::Event::Type::ShutdownRequest,
                static_cast<QC::u32>(QK::Shutdown::Reason::ShellCommand));
            return true;
        }

        static bool cmdIp(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            QNet::Stack::instance().initialize();

            const char *p = args ? skipSpaces(args) : nullptr;
            if (p && *p)
            {
                // Subcommand
                char sub[16];
                QC::String::memset(sub, 0, sizeof(sub));
                QC::usize si = 0;
                while (*p && !isSpace(*p) && si + 1 < sizeof(sub))
                    sub[si++] = *p++;
                sub[si] = '\0';
                p = skipSpaces(p);

                if (streqIgnoreCase(sub, "set"))
                {
                    QNet::IPv4Address addr{};
                    QNet::IPv4Address mask{};
                    QNet::IPv4Address gw{};

                    // addr
                    if (!parseIPv4(p, addr))
                    {
                        ctx.writeLine("ip: usage: ip set <a.b.c.d> [mask] [gw]");
                        return true;
                    }

                    // advance token
                    while (*p && !isSpace(*p))
                        ++p;
                    p = skipSpaces(p);

                    // optional mask
                    if (p && *p)
                    {
                        if (!parseIPv4(p, mask))
                        {
                            ctx.writeLine("ip: invalid mask");
                            return true;
                        }
                        while (*p && !isSpace(*p))
                            ++p;
                        p = skipSpaces(p);
                    }
                    else
                    {
                        mask.octets[0] = 255;
                        mask.octets[1] = 255;
                        mask.octets[2] = 255;
                        mask.octets[3] = 0;
                    }

                    // optional gateway
                    if (p && *p)
                    {
                        if (!parseIPv4(p, gw))
                        {
                            ctx.writeLine("ip: invalid gateway");
                            return true;
                        }
                    }
                    else
                    {
                        gw.value = 0;
                    }

                    QNet::Stack::instance().ip()->setAddress(addr);
                    QNet::Stack::instance().ip()->setSubnetMask(mask);
                    QNet::Stack::instance().ip()->setGateway(gw);

                    ctx.writeLine("ip: updated");
                    return true;
                }

                if (streqIgnoreCase(sub, "dhcp"))
                {
                    QC::u32 timeoutMs = 2500;
                    if (p && *p)
                    {
                        QC::u32 v = 0;
                        if (!parseU32(p, v) || v < 250 || v > 30000)
                        {
                            ctx.writeLine("ip: dhcp: usage: ip dhcp [timeout_ms 250..30000]");
                            return true;
                        }
                        timeoutMs = v;
                    }

                    if (!QK::Time::available() || !QK::System::pumpAvailable())
                    {
                        ctx.writeLine("ip: dhcp: unavailable (no time/pump)");
                        return true;
                    }

                    QNet::DHCPv4Client dhcp;
                    if (dhcp.begin() != QC::Status::Success)
                    {
                        ctx.writeLine("ip: dhcp: begin failed");
                        return true;
                    }

                    QNet::DHCPv4Lease lease{};
                    const QC::u64 deadlineMs = QK::Time::milliseconds() + static_cast<QC::u64>(timeoutMs);
                    while (QK::Time::milliseconds() < deadlineMs)
                    {
                        QK::System::pump();

                        if (dhcp.poll(&lease))
                        {
                            QNet::Stack::instance().ip()->setAddress(lease.address);
                            QNet::Stack::instance().ip()->setSubnetMask(lease.subnetMask);
                            QNet::Stack::instance().ip()->setGateway(lease.gateway);
                            QNet::Stack::instance().ip()->setDnsServer(lease.dnsServer);

                            char ipBuf[32];
                            char maskBuf[32];
                            char gwBuf[32];
                            char dnsBuf[32];
                            ipv4ToString(lease.address, ipBuf, sizeof(ipBuf));
                            ipv4ToString(lease.subnetMask, maskBuf, sizeof(maskBuf));
                            ipv4ToString(lease.gateway, gwBuf, sizeof(gwBuf));
                            ipv4ToString(lease.dnsServer, dnsBuf, sizeof(dnsBuf));

                            writeKeyValue(ctx, "ip", ipBuf);
                            writeKeyValue(ctx, "mask", maskBuf);
                            writeKeyValue(ctx, "gw", gwBuf);
                            writeKeyValue(ctx, "dns", dnsBuf);
                            ctx.writeLine("ip: dhcp: lease applied");
                            return true;
                        }

                        QK::Time::sleep(10);
                    }

                    ctx.writeLine("ip: dhcp: timeout");
                    return true;
                }
            }

            // show
            char addrBuf[32];
            char maskBuf[32];
            char gwBuf[32];
            char dnsBuf[32];
            char macBuf[32];
            ipv4ToString(QNet::Stack::instance().ip()->address(), addrBuf, sizeof(addrBuf));
            ipv4ToString(QNet::Stack::instance().ip()->subnetMask(), maskBuf, sizeof(maskBuf));
            ipv4ToString(QNet::Stack::instance().ip()->gateway(), gwBuf, sizeof(gwBuf));
            ipv4ToString(QNet::Stack::instance().ip()->dnsServer(), dnsBuf, sizeof(dnsBuf));
            macToString(QNet::Stack::instance().ethernet()->macAddress(), macBuf, sizeof(macBuf));

            writeKeyValue(ctx, "ip", addrBuf);
            writeKeyValue(ctx, "mask", maskBuf);
            writeKeyValue(ctx, "gw", gwBuf);
            writeKeyValue(ctx, "dns", dnsBuf);
            writeKeyValue(ctx, "mac", macBuf);
            return true;
        }

        static bool cmdPing(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            QNet::Stack::instance().initialize();
            const char *p = args;
            char host[256];
            if (!readToken(p, host, sizeof(host)))
            {
                ctx.writeLine("ping: usage: ping <a.b.c.d|host> [timeout_ms]");
                return true;
            }

            QC::u32 timeoutMs = 1000;
            if (p && *p)
            {
                QC::u32 v = 0;
                if (!parseU32(p, v) || v < 100 || v > 30000)
                {
                    ctx.writeLine("ping: usage: ping <a.b.c.d|host> [timeout_ms 100..30000]");
                    return true;
                }
                timeoutMs = v;
            }

            QNet::IPv4Address dest{};
            if (!resolveHostOrIPv4(host, dest, ctx, timeoutMs))
            {
                return true;
            }

            // If user provided a hostname, show what we resolved to.
            QNet::IPv4Address tmp{};
            if (!parseIPv4(host, tmp))
            {
                char ipBuf[32];
                ipv4ToString(dest, ipBuf, sizeof(ipBuf));
                char line[320];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "ping: resolved ");
                (void)appendString(line, sizeof(line), host);
                (void)appendString(line, sizeof(line), " -> ");
                (void)appendString(line, sizeof(line), ipBuf);
                ctx.writeLine(line);
            }

            auto *ip = QNet::Stack::instance().ip();
            ip->clearIcmpEchoReplies();

            const char payload[] = "qaios";
            ip->sendICMP(dest, 8, 0, payload, sizeof(payload) - 1);

            if (!QK::Time::available() || !QK::System::pumpAvailable())
            {
                ctx.writeLine("ping: sent (reply tracking unavailable)");
                return true;
            }

            const QC::u64 deadlineMs = QK::Time::milliseconds() + static_cast<QC::u64>(timeoutMs);
            while (QK::Time::milliseconds() < deadlineMs)
            {
                QK::System::pump();

                QNet::IP::IcmpEchoReply rep{};
                while (ip->popIcmpEchoReply(&rep))
                {
                    if (rep.source.value != dest.value)
                        continue;

                    char ipBuf[32];
                    ipv4ToString(rep.source, ipBuf, sizeof(ipBuf));
                    char line[96];
                    QC::String::memset(line, 0, sizeof(line));
                    (void)appendString(line, sizeof(line), "ping: reply from ");
                    (void)appendString(line, sizeof(line), ipBuf);
                    (void)appendString(line, sizeof(line), " bytes=");
                    (void)appendU64Dec(line, sizeof(line), static_cast<QC::u64>(rep.payloadLen));
                    ctx.writeLine(line);
                    return true;
                }

                QK::Time::sleep(10);
            }

            ctx.writeLine("ping: timeout");
            return true;
        }

        static bool cmdUdp(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            QNet::Stack::instance().initialize();
            const char *p = args;
            char host[256];
            if (!readToken(p, host, sizeof(host)))
            {
                ctx.writeLine("udp: usage: udp <a.b.c.d|host> <port> <text>");
                return true;
            }

            QNet::IPv4Address dest{};
            if (!resolveHostOrIPv4(host, dest, ctx, 1000))
                return true;

            if (!p || *p == '\0')
            {
                ctx.writeLine("udp: usage: udp <a.b.c.d|host> <port> <text>");
                return true;
            }

            QC::u32 port32 = 0;
            if (!parseU32(p, port32) || port32 == 0 || port32 > 65535)
            {
                ctx.writeLine("udp: invalid port");
                return true;
            }
            while (*p && !isSpace(*p))
                ++p;
            p = skipSpaces(p);

            const char *text = (p && *p) ? p : "";
            const QC::usize len = QC::String::strlen(text);
            QC::Status st = QNet::Stack::instance().udp()->send(dest, static_cast<QC::u16>(port32), 12345, text, len);
            if (st == QC::Status::Success)
            {
                ctx.writeLine("udp: sent");
            }
            else
            {
                ctx.writeLine("udp: send failed");
            }
            return true;
        }

        static bool cmdNslookup(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            QNet::Stack::instance().initialize();

            const char *p = args;
            char name[256];
            if (!readToken(p, name, sizeof(name)))
            {
                ctx.writeLine("nslookup: usage: nslookup <name> [dns_server_ip] [timeout_ms]");
                return true;
            }

            QNet::IPv4Address dns = QNet::Stack::instance().ip()->dnsServer();
            QC::u32 timeoutMs = 1000;

            // Optional: either dns_server_ip or timeout_ms
            if (p && *p)
            {
                char tok[64];
                if (!readToken(p, tok, sizeof(tok)))
                {
                    ctx.writeLine("nslookup: usage: nslookup <name> [dns_server_ip] [timeout_ms]");
                    return true;
                }

                QNet::IPv4Address server{};
                QC::u32 v = 0;
                if (parseIPv4(tok, server))
                {
                    dns = server;
                    // Optional timeout after server
                    if (p && *p)
                    {
                        if (!parseU32(p, v) || v < 100 || v > 30000)
                        {
                            ctx.writeLine("nslookup: usage: nslookup <name> [dns_server_ip] [timeout_ms 100..30000]");
                            return true;
                        }
                        timeoutMs = v;
                    }
                }
                else if (parseU32(tok, v))
                {
                    if (v < 100 || v > 30000)
                    {
                        ctx.writeLine("nslookup: usage: nslookup <name> [dns_server_ip] [timeout_ms 100..30000]");
                        return true;
                    }
                    timeoutMs = v;
                }
                else
                {
                    ctx.writeLine("nslookup: usage: nslookup <name> [dns_server_ip] [timeout_ms]");
                    return true;
                }
            }
            if (dns.value == 0)
            {
                ctx.writeLine("nslookup: no dns configured (DHCP?)");
                return true;
            }

            if (!QK::Time::available() || !QK::System::pumpAvailable())
            {
                ctx.writeLine("nslookup: unavailable (no time/pump)");
                return true;
            }

            QNet::DNSClient client;
            const QC::u16 txid = static_cast<QC::u16>((QK::Time::milliseconds() & 0xFFFF) ^ 0xA5A5);
            if (client.begin(dns, name, txid) != QC::Status::Success)
            {
                ctx.writeLine("nslookup: query failed");
                return true;
            }

            const QC::u64 deadlineMs = QK::Time::milliseconds() + static_cast<QC::u64>(timeoutMs);
            while (QK::Time::milliseconds() < deadlineMs)
            {
                QK::System::pump();

                QNet::IPv4Address out{};
                if (client.poll(&out))
                {
                    char ipBuf[32];
                    ipv4ToString(out, ipBuf, sizeof(ipBuf));

                    char line[320];
                    QC::String::memset(line, 0, sizeof(line));
                    (void)appendString(line, sizeof(line), "nslookup: ");
                    (void)appendString(line, sizeof(line), name);
                    (void)appendString(line, sizeof(line), " -> ");
                    (void)appendString(line, sizeof(line), ipBuf);
                    ctx.writeLine(line);
                    return true;
                }

                QK::Time::sleep(10);
            }

            ctx.writeLine("nslookup: timeout");
            return true;
        }

        static bool cmdTcpConnect(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            QNet::Stack::instance().initialize();

            const char *p = args;
            char host[256];
            if (!readToken(p, host, sizeof(host)))
            {
                ctx.writeLine("tcpconnect: usage: tcpconnect <ip|host> <port> [timeout_ms]");
                return true;
            }

            if (!p || *p == '\0')
            {
                ctx.writeLine("tcpconnect: usage: tcpconnect <ip|host> <port> [timeout_ms]");
                return true;
            }

            QC::u32 port32 = 0;
            if (!parseU32(p, port32) || port32 == 0 || port32 > 65535)
            {
                ctx.writeLine("tcpconnect: invalid port");
                return true;
            }
            while (*p && !isSpace(*p))
                ++p;
            p = skipSpaces(p);

            QC::u32 timeoutMs = 3000;
            if (p && *p)
            {
                QC::u32 v = 0;
                if (!parseU32(p, v) || v < 100 || v > 60000)
                {
                    ctx.writeLine("tcpconnect: usage: tcpconnect <ip|host> <port> [timeout_ms 100..60000]");
                    return true;
                }
                timeoutMs = v;
            }

            if (!QK::Time::available() || !QK::System::pumpAvailable())
            {
                ctx.writeLine("tcpconnect: unavailable (no time/pump)");
                return true;
            }

            QNet::IPv4Address dest{};
            if (!resolveHostOrIPv4(host, dest, ctx, timeoutMs))
                return true;

            auto *tcp = QNet::Stack::instance().tcp();
            if (!tcp)
            {
                ctx.writeLine("tcpconnect: tcp unavailable");
                return true;
            }

            QNet::TCPConnection *conn = tcp->connect(dest, static_cast<QC::u16>(port32));
            if (!conn)
            {
                ctx.writeLine("tcpconnect: connect failed");
                return true;
            }

            const QC::u64 deadlineMs = QK::Time::milliseconds() + static_cast<QC::u64>(timeoutMs);
            while (QK::Time::milliseconds() < deadlineMs)
            {
                QK::System::pump();
                if (conn->state == QNet::TCPState::Established)
                {
                    ctx.writeLine("tcpconnect: connected");
                    tcp->close(conn);
                    const QC::u64 closeDeadlineMs = QK::Time::milliseconds() + 250;
                    while (QK::Time::milliseconds() < closeDeadlineMs)
                    {
                        QK::System::pump();
                        if (conn->state == QNet::TCPState::Closed)
                            break;
                        QK::Time::sleep(10);
                    }
                    tcp->drop(conn);
                    return true;
                }
                if (conn->state == QNet::TCPState::Closed)
                {
                    ctx.writeLine("tcpconnect: closed");
                    tcp->close(conn);
                    return true;
                }
                QK::Time::sleep(10);
            }

            ctx.writeLine("tcpconnect: timeout");
            tcp->close(conn);
            return true;
        }

        static bool cmdHttpGet(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            QNet::Stack::instance().initialize();

            const char *p = args;
            char host[256];
            if (!readToken(p, host, sizeof(host)))
            {
                ctx.writeLine("httpget: usage: httpget <host> [path] [timeout_ms]");
                return true;
            }

            char path[256];
            QC::String::memset(path, 0, sizeof(path));
            QC::String::strncpy(path, "/", sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';

            // Optional path OR timeout.
            char tok[256];
            QC::String::memset(tok, 0, sizeof(tok));
            bool haveTok = readToken(p, tok, sizeof(tok));

            bool keep = false;

            QC::u32 timeoutMs = 7000;
            if (haveTok)
            {
                QC::u32 v = 0;
                if (parseU32(tok, v))
                {
                    if (v < 100 || v > 60000)
                    {
                        ctx.writeLine("httpget: usage: httpget <host> [path] [timeout_ms 100..60000]");
                        return true;
                    }
                    timeoutMs = v;

                    // Optional: keep
                    QC::String::memset(tok, 0, sizeof(tok));
                    if (readToken(p, tok, sizeof(tok)))
                    {
                        if (streqIgnoreCase(tok, "keep"))
                        {
                            keep = true;
                        }
                        else
                        {
                            ctx.writeLine("httpget: usage: httpget <host> [path] [timeout_ms 100..60000] [keep]");
                            return true;
                        }
                    }
                }
                else
                {
                    // It's a path; read optional timeout next.
                    QC::String::strncpy(path, tok, sizeof(path) - 1);
                    path[sizeof(path) - 1] = '\0';

                    // Optional: timeout
                    QC::String::memset(tok, 0, sizeof(tok));
                    if (readToken(p, tok, sizeof(tok)))
                    {
                        QC::u32 tv = 0;
                        if (parseU32(tok, tv))
                        {
                            if (tv < 100 || tv > 60000)
                            {
                                ctx.writeLine("httpget: usage: httpget <host> [path] [timeout_ms 100..60000] [keep]");
                                return true;
                            }
                            timeoutMs = tv;

                            // Optional: keep
                            QC::String::memset(tok, 0, sizeof(tok));
                            if (readToken(p, tok, sizeof(tok)))
                            {
                                if (streqIgnoreCase(tok, "keep"))
                                {
                                    keep = true;
                                }
                                else
                                {
                                    ctx.writeLine("httpget: usage: httpget <host> [path] [timeout_ms 100..60000] [keep]");
                                    return true;
                                }
                            }
                        }
                        else if (streqIgnoreCase(tok, "keep"))
                        {
                            keep = true;
                        }
                        else
                        {
                            ctx.writeLine("httpget: usage: httpget <host> [path] [timeout_ms 100..60000] [keep]");
                            return true;
                        }
                    }
                }
            }

            if (!QK::Time::available() || !QK::System::pumpAvailable())
            {
                ctx.writeLine("httpget: unavailable (no time/pump)");
                return true;
            }

            QNet::IPv4Address dest{};
            if (!resolveHostOrIPv4(host, dest, ctx, timeoutMs))
                return true;

            // If user provided a hostname, show what we resolved to.
            QNet::IPv4Address tmpHost{};
            if (!parseIPv4(host, tmpHost))
            {
                char ipBuf[32];
                ipv4ToString(dest, ipBuf, sizeof(ipBuf));
                char line[320];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "httpget: resolved ");
                (void)appendString(line, sizeof(line), host);
                (void)appendString(line, sizeof(line), " -> ");
                (void)appendString(line, sizeof(line), ipBuf);
                ctx.writeLine(line);
            }

            auto *tcp = QNet::Stack::instance().tcp();
            if (!tcp)
            {
                ctx.writeLine("httpget: tcp unavailable");
                return true;
            }

            const QC::u64 deadlineMs = QK::Time::milliseconds() + static_cast<QC::u64>(timeoutMs);
            QNet::TCPConnection *conn = nullptr;
            bool connected = false;

            // Within the overall timeout, keep trying fresh connects.
            // This helps when SYN/SYN-ACK is intermittently dropped (SLIRP/NAT/Internet).
            constexpr QC::u32 kAttemptBudgetMs = 4000;
            int attempt = 0;
            while (QK::Time::milliseconds() < deadlineMs)
            {
                ++attempt;
                conn = tcp->connect(dest, 80);
                if (!conn)
                {
                    ctx.writeLine("httpget: connect failed");
                    return true;
                }

                const QC::u64 nowMs = QK::Time::milliseconds();
                const QC::u64 attemptMaxMs = nowMs + static_cast<QC::u64>(kAttemptBudgetMs);
                const QC::u64 attemptDeadlineMs = (attemptMaxMs < deadlineMs) ? attemptMaxMs : deadlineMs;

                while (QK::Time::milliseconds() < attemptDeadlineMs)
                {
                    QK::System::pump();
                    if (conn->state == QNet::TCPState::Established)
                    {
                        connected = true;
                        break;
                    }
                    if (conn->state == QNet::TCPState::Closed)
                        break;
                    QK::Time::sleep(10);
                }

                if (connected)
                    break;

                // Connect attempt did not establish; close() is silent for SynSent.
                tcp->close(conn);
                conn = nullptr;

                // Small delay before re-attempting to avoid tight looping.
                QK::Time::sleep(50);
            }

            if (!connected || !conn)
            {
                ctx.writeLine("httpget: timeout (connect)");
                return true;
            }

            // Build a minimal HTTP/1.1 request while still forcing close (avoid keep-alive complexity).
            // Some servers/WAFs are more tolerant when a User-Agent/Accept is present.
            char req[768];
            QC::String::memset(req, 0, sizeof(req));
            (void)appendString(req, sizeof(req), "GET ");
            if (path[0] != '/')
                (void)appendString(req, sizeof(req), "/");
            (void)appendString(req, sizeof(req), path);
            (void)appendString(req, sizeof(req), " HTTP/1.1\r\nHost: ");
            (void)appendString(req, sizeof(req), host);
            (void)appendString(req, sizeof(req), "\r\nUser-Agent: QAIOS+ httpget/1.0\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n");

            (void)tcp->send(conn, req, QC::String::strlen(req));

            // Capture a bounded amount of response for terminal display.
            QC::u8 capture[4096];
            QC::String::memset(capture, 0, sizeof(capture));
            QC::usize capLen = 0;
            QC::u64 totalRx = 0;

            auto findHeaderEnd = [&](const QC::u8 *buf, QC::usize len) -> QC::usize
            {
                if (!buf || len < 4)
                    return static_cast<QC::usize>(-1);
                for (QC::usize i = 0; i + 3 < len; ++i)
                {
                    if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n')
                        return i;
                }
                return static_cast<QC::usize>(-1);
            };

            QC::usize headerEnd = static_cast<QC::usize>(-1);
            while (QK::Time::milliseconds() < deadlineMs && capLen < sizeof(capture))
            {
                QK::System::pump();

                QC::u8 tmp[256];
                const QC::isize n = tcp->receive(conn, tmp, sizeof(tmp));
                if (n > 0)
                {
                    totalRx += static_cast<QC::u64>(n);
                    const QC::usize nn = static_cast<QC::usize>(n);
                    const QC::usize room = sizeof(capture) - capLen;
                    const QC::usize toCopy = (nn < room) ? nn : room;
                    QC::String::memcpy(capture + capLen, tmp, toCopy);
                    capLen += toCopy;

                    if (headerEnd == static_cast<QC::usize>(-1))
                        headerEnd = findHeaderEnd(capture, capLen);

                    if (headerEnd != static_cast<QC::usize>(-1))
                    {
                        const QC::usize want = headerEnd + 4 + 512;
                        if (capLen >= want)
                            break;
                    }
                }
                else
                {
                    if (headerEnd != static_cast<QC::usize>(-1) && conn->state == QNet::TCPState::CloseWait && conn->recvCount == 0)
                        break;
                    QK::Time::sleep(10);
                }
            }

            if (capLen == 0)
            {
                ctx.writeLine("httpget: no data");
                tcp->close(conn);
                const QC::u64 closeDeadlineMs = QK::Time::milliseconds() + 250;
                while (QK::Time::milliseconds() < closeDeadlineMs)
                {
                    QK::System::pump();
                    if (conn->state == QNet::TCPState::Closed)
                        break;
                    QK::Time::sleep(10);
                }
                tcp->drop(conn);
                return true;
            }

            if (headerEnd == static_cast<QC::usize>(-1))
                headerEnd = findHeaderEnd(capture, capLen);

            // Print status line + a bounded number of header lines.
            if (headerEnd != static_cast<QC::usize>(-1))
            {
                // Status line is first line.
                char status[192];
                QC::String::memset(status, 0, sizeof(status));
                QC::usize si = 0;
                QC::usize firstLineEnd = 0;
                for (QC::usize i = 0; i + 1 < capLen && si + 1 < sizeof(status); ++i)
                {
                    if (capture[i] == '\r' && capture[i + 1] == '\n')
                    {
                        firstLineEnd = i + 2;
                        break;
                    }
                    const QC::u8 c = capture[i];
                    status[si++] = (c >= 32 && c <= 126) ? static_cast<char>(c) : '.';
                }
                status[si] = '\0';

                char line[256];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "httpget: ");
                (void)appendString(line, sizeof(line), status);
                ctx.writeLine(line);

                // Headers (cap lines to avoid flooding).
                QC::usize linesPrinted = 0;
                QC::usize pos = firstLineEnd;
                while (pos < headerEnd && linesPrinted < 20)
                {
                    char hline[256];
                    QC::String::memset(hline, 0, sizeof(hline));
                    QC::usize hi = 0;
                    while (pos < headerEnd && capture[pos] != '\n' && hi + 1 < sizeof(hline))
                    {
                        const QC::u8 c = capture[pos++];
                        if (c == '\r')
                            continue;
                        hline[hi++] = (c >= 32 && c <= 126) ? static_cast<char>(c) : '.';
                    }
                    if (pos < headerEnd && capture[pos] == '\n')
                        ++pos;
                    hline[hi] = '\0';

                    if (hline[0] != '\0')
                    {
                        ctx.writeLine(hline);
                        ++linesPrinted;
                    }
                }

                // Body snippet
                const QC::usize bodyStart = headerEnd + 4;
                if (bodyStart < capLen)
                {
                    const QC::usize bodyAvail = capLen - bodyStart;
                    const QC::usize bodyDump = (bodyAvail < 512) ? bodyAvail : 512;

                    ctx.writeLine("httpget: body (first bytes)");
                    char bline[129];
                    QC::String::memset(bline, 0, sizeof(bline));
                    QC::usize bi = 0;
                    for (QC::usize i = 0; i < bodyDump; ++i)
                    {
                        const QC::u8 c = capture[bodyStart + i];
                        if (c == '\r')
                            continue;
                        if (c == '\n')
                        {
                            bline[bi] = '\0';
                            if (bline[0] != '\0')
                                ctx.writeLine(bline);
                            QC::String::memset(bline, 0, sizeof(bline));
                            bi = 0;
                            continue;
                        }
                        bline[bi++] = (c >= 32 && c <= 126) ? static_cast<char>(c) : '.';
                        if (bi + 1 >= sizeof(bline))
                        {
                            bline[bi] = '\0';
                            ctx.writeLine(bline);
                            QC::String::memset(bline, 0, sizeof(bline));
                            bi = 0;
                        }
                    }
                    if (bi > 0)
                    {
                        bline[bi] = '\0';
                        ctx.writeLine(bline);
                    }
                }
            }
            else
            {
                ctx.writeLine("httpget: response (no headers found)");
                const QC::usize dump = (capLen < 512) ? capLen : 512;
                char bline[129];
                QC::String::memset(bline, 0, sizeof(bline));
                QC::usize bi = 0;
                for (QC::usize i = 0; i < dump; ++i)
                {
                    const QC::u8 c = capture[i];
                    if (c == '\r')
                        continue;
                    if (c == '\n')
                    {
                        bline[bi] = '\0';
                        if (bline[0] != '\0')
                            ctx.writeLine(bline);
                        QC::String::memset(bline, 0, sizeof(bline));
                        bi = 0;
                        continue;
                    }
                    bline[bi++] = (c >= 32 && c <= 126) ? static_cast<char>(c) : '.';
                    if (bi + 1 >= sizeof(bline))
                    {
                        bline[bi] = '\0';
                        ctx.writeLine(bline);
                        QC::String::memset(bline, 0, sizeof(bline));
                        bi = 0;
                    }
                }
                if (bi > 0)
                {
                    bline[bi] = '\0';
                    ctx.writeLine(bline);
                }
            }

            char done[128];
            QC::String::memset(done, 0, sizeof(done));
            (void)appendString(done, sizeof(done), "httpget: rx_bytes=");
            (void)appendU64Dec(done, sizeof(done), totalRx);
            ctx.writeLine(done);

            if (keep)
            {
                char keepLine[128];
                QC::String::memset(keepLine, 0, sizeof(keepLine));
                (void)appendString(keepLine, sizeof(keepLine), "httpget: kept connection (lp=");
                (void)appendU64Dec(keepLine, sizeof(keepLine), conn->localPort);
                (void)appendString(keepLine, sizeof(keepLine), ")");
                ctx.writeLine(keepLine);
                ctx.writeLine("httpget: use tcpdrop <local_port> to free it");
                return true;
            }

            // Graceful close: send FIN and give the stack a moment to exchange final ACKs.
            if (conn->state == QNet::TCPState::Established || conn->state == QNet::TCPState::CloseWait)
            {
                tcp->close(conn);
                const QC::u64 closeDeadlineMs = QK::Time::milliseconds() + 250;
                while (QK::Time::milliseconds() < closeDeadlineMs)
                {
                    QK::System::pump();
                    if (conn->state == QNet::TCPState::Closed)
                        break;
                    QK::Time::sleep(10);
                }
            }

            tcp->drop(conn);
            return true;
        }

        static bool cmdTcpDrop(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            QNet::Stack::instance().initialize();

            auto *tcp = QNet::Stack::instance().tcp();
            if (!tcp)
            {
                ctx.writeLine("tcpdrop: tcp unavailable");
                return true;
            }

            const char *p = args ? skipSpaces(args) : nullptr;
            QC::u32 port32 = 0;
            if (!p || !*p || !parseU32(p, port32) || port32 > 65535)
            {
                ctx.writeLine("tcpdrop: usage: tcpdrop <local_port>");
                return true;
            }

            if (tcp->dropByLocalPort(static_cast<QC::u16>(port32)))
                ctx.writeLine("tcpdrop: dropped");
            else
                ctx.writeLine("tcpdrop: not found");
            return true;
        }

        static bool cmdPorts(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            auto startsWithTokenIgnoreCase = [](const char *s, const char *token, const char **outTail) -> bool {
                if (!s || !token)
                    return false;

                QC::usize i = 0;
                for (;; ++i)
                {
                    char tc = token[i];
                    if (tc == '\0')
                        break;

                    char sc = s[i];
                    if (sc == '\0')
                        return false;
                    if (sc >= 'A' && sc <= 'Z')
                        sc = static_cast<char>(sc - 'A' + 'a');
                    if (tc >= 'A' && tc <= 'Z')
                        tc = static_cast<char>(tc - 'A' + 'a');
                    if (sc != tc)
                        return false;
                }

                if (s[i] != '\0' && !isSpace(s[i]))
                    return false;
                if (outTail)
                    *outTail = skipSpaces(s + i);
                return true;
            };

            const char *p = args ? skipSpaces(args) : nullptr;
            if (!p || *p == '\0' || streqIgnoreCase(p, "status"))
            {
                ctx.writeLine("ports: usage: ports <list|audit|ratelimit|close-unused>");
                return true;
            }

            if (streqIgnoreCase(p, "list"))
            {
                QK::Runtime::PortRecord recs[64] = {};
                const QC::usize n = QK::Runtime::Registries::instance().copyPortRecords(recs, sizeof(recs) / sizeof(recs[0]));
                if (n == 0)
                {
                    ctx.writeLine("ports: none open");
                    return true;
                }

                auto protoName = [](QK::Runtime::PortProtocol proto) -> const char * {
                    switch (proto)
                    {
                    case QK::Runtime::PortProtocol::TCP:
                        return "TCP";
                    case QK::Runtime::PortProtocol::UDP:
                        return "UDP";
                    default:
                        return "UNK";
                    }
                };

                auto stateName = [](QK::Runtime::PortState st) -> const char * {
                    switch (st)
                    {
                    case QK::Runtime::PortState::Closed:
                        return "Closed";
                    case QK::Runtime::PortState::Opening:
                        return "Opening";
                    case QK::Runtime::PortState::Open:
                        return "Open";
                    case QK::Runtime::PortState::Closing:
                        return "Closing";
                    default:
                        return "Unknown";
                    }
                };

                for (QC::usize i = 0; i < n; ++i)
                {
                    char line[200];
                    QC::String::memset(line, 0, sizeof(line));
                    (void)appendString(line, sizeof(line), protoName(recs[i].protocol));
                    (void)appendString(line, sizeof(line), " port=");
                    (void)appendU64Dec(line, sizeof(line), recs[i].port);
                    (void)appendString(line, sizeof(line), " owner=");
                    (void)appendU64Dec(line, sizeof(line), recs[i].ownerPid);
                    (void)appendString(line, sizeof(line), " state=");
                    (void)appendString(line, sizeof(line), stateName(recs[i].state));
                    ctx.writeLine(line);
                }
                return true;
            }

            if (streqIgnoreCase(p, "audit"))
            {
                QNet::Stack::instance().initialize();
                QNet::PortAuditEvent ev[64] = {};
                const QC::usize n = QNet::Stack::instance().copyPortAuditEvents(ev, sizeof(ev) / sizeof(ev[0]));
                if (n == 0)
                {
                    ctx.writeLine("ports: audit empty");
                    return true;
                }

                auto codeName = [](QC::u64 c) -> const char * {
                    switch (c)
                    {
                    case 0x504F504EULL:
                        return "OPEN";
                    case 0x504F434CULL:
                        return "CLOSE";
                    case 0x504F524AULL:
                        return "REJECT";
                    case 0x504F4155ULL:
                        return "AUTOCLOSE";
                    default:
                        return "EVENT";
                    }
                };

                auto protoName = [](QNet::Protocol proto) -> const char * {
                    switch (proto)
                    {
                    case QNet::Protocol::TCP:
                        return "TCP";
                    case QNet::Protocol::UDP:
                        return "UDP";
                    default:
                        return "SYS";
                    }
                };

                for (QC::usize i = 0; i < n; ++i)
                {
                    char line[220];
                    QC::String::memset(line, 0, sizeof(line));
                    (void)appendU64Dec(line, sizeof(line), ev[i].t_ms);
                    (void)appendString(line, sizeof(line), " ");
                    (void)appendString(line, sizeof(line), codeName(ev[i].code));
                    (void)appendString(line, sizeof(line), " proto=");
                    (void)appendString(line, sizeof(line), protoName(ev[i].protocol));
                    (void)appendString(line, sizeof(line), " port=");
                    (void)appendU64Dec(line, sizeof(line), ev[i].port);
                    (void)appendString(line, sizeof(line), " owner=");
                    (void)appendU64Dec(line, sizeof(line), ev[i].ownerPid);
                    ctx.writeLine(line);
                }
                return true;
            }

            const char *ratelimitArg = nullptr;
            if (startsWithTokenIgnoreCase(p, "ratelimit", &ratelimitArg))
            {
                QNet::Stack::instance().initialize();
                auto *ip = QNet::Stack::instance().ip();
                if (!ip)
                {
                    ctx.writeLine("ports: ip unavailable");
                    return true;
                }

                if (ratelimitArg && *ratelimitArg)
                {
                    if (!streqIgnoreCase(ratelimitArg, "reset"))
                    {
                        ctx.writeLine("ports: usage: ports ratelimit [reset]");
                        return true;
                    }

                    ip->resetIngressGuardStats();
                    ctx.writeLine("ports: ratelimit counters reset");
                    return true;
                }

                const QNet::IP::IngressGuardStats st = ip->ingressGuardStats();
                char line[220];

                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "ratelimit tcp accepted=");
                (void)appendU64Dec(line, sizeof(line), st.tcpAccepted);
                (void)appendString(line, sizeof(line), " boundary_drop=");
                (void)appendU64Dec(line, sizeof(line), st.tcpBoundaryDrops);
                (void)appendString(line, sizeof(line), " malformed_drop=");
                (void)appendU64Dec(line, sizeof(line), st.tcpMalformedDrops);
                (void)appendString(line, sizeof(line), " rate_drop=");
                (void)appendU64Dec(line, sizeof(line), st.tcpRateDrops);
                ctx.writeLine(line);

                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "ratelimit udp accepted=");
                (void)appendU64Dec(line, sizeof(line), st.udpAccepted);
                (void)appendString(line, sizeof(line), " boundary_drop=");
                (void)appendU64Dec(line, sizeof(line), st.udpBoundaryDrops);
                (void)appendString(line, sizeof(line), " malformed_drop=");
                (void)appendU64Dec(line, sizeof(line), st.udpMalformedDrops);
                (void)appendString(line, sizeof(line), " rate_drop=");
                (void)appendU64Dec(line, sizeof(line), st.udpRateDrops);
                ctx.writeLine(line);
                return true;
            }

            if (!streqIgnoreCase(p, "close-unused"))
            {
                ctx.writeLine("ports: unknown arg (use list|audit|ratelimit|close-unused)");
                return true;
            }

            QNet::Stack::instance().initialize();
            const QC::usize n = QNet::Stack::instance().closeUnusedPorts();

            char line[96];
            QC::String::memset(line, 0, sizeof(line));
            (void)appendString(line, sizeof(line), "ports: closed=");
            (void)appendU64Dec(line, sizeof(line), static_cast<QC::u64>(n));
            ctx.writeLine(line);
            return true;
        }

        // Forward declarations (cmdNetLog convenience wrapper).
        static bool cmdTcpLog(const char *args, const QC::Cmd::Context &ctx, void *);
        static bool cmdArp(const char *args, const QC::Cmd::Context &ctx, void *);

        static bool cmdNetLog(const char *, const QC::Cmd::Context &ctx, void *)
        {
            ctx.writeLine("netlog:");
            (void)cmdIp(nullptr, ctx, nullptr);
            (void)cmdArp(nullptr, ctx, nullptr);
            (void)cmdTcpLog(nullptr, ctx, nullptr);
            return true;
        }

        static const char *tcpStateName(QNet::TCPState st)
        {
            switch (st)
            {
            case QNet::TCPState::Closed:
                return "Closed";
            case QNet::TCPState::Listen:
                return "Listen";
            case QNet::TCPState::SynSent:
                return "SynSent";
            case QNet::TCPState::SynReceived:
                return "SynRecv";
            case QNet::TCPState::Established:
                return "Estab";
            case QNet::TCPState::FinWait1:
                return "FinW1";
            case QNet::TCPState::FinWait2:
                return "FinW2";
            case QNet::TCPState::CloseWait:
                return "CloseW";
            case QNet::TCPState::Closing:
                return "Closing";
            case QNet::TCPState::LastAck:
                return "LastAck";
            case QNet::TCPState::TimeWait:
                return "TimeW";
            default:
                return "?";
            }
        }

        static bool cmdTcpState(const char *, const QC::Cmd::Context &ctx, void *)
        {
            QNet::Stack::instance().initialize();

            auto *tcp = QNet::Stack::instance().tcp();
            if (!tcp)
            {
                ctx.writeLine("tcpstate: tcp unavailable");
                return true;
            }

            QNet::TCPConnectionView views[32];
            QC::String::memset(views, 0, sizeof(views));
            const QC::usize n = tcp->copyConnections(views, sizeof(views) / sizeof(views[0]));

            char head[64];
            QC::String::memset(head, 0, sizeof(head));
            (void)appendString(head, sizeof(head), "tcpstate: count=");
            (void)appendU64Dec(head, sizeof(head), static_cast<QC::u64>(n));
            ctx.writeLine(head);

            for (QC::usize i = 0; i < n; ++i)
            {
                char rip[32];
                ipv4ToString(views[i].remoteAddr, rip, sizeof(rip));

                char line[256];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), tcpStateName(views[i].state));
                (void)appendString(line, sizeof(line), " lp=");
                (void)appendU64Dec(line, sizeof(line), views[i].localPort);
                (void)appendString(line, sizeof(line), " rp=");
                (void)appendString(line, sizeof(line), rip);
                (void)appendString(line, sizeof(line), ":");
                (void)appendU64Dec(line, sizeof(line), views[i].remotePort);
                (void)appendString(line, sizeof(line), " snduna=");
                (void)appendU64Dec(line, sizeof(line), views[i].sendUnacked);
                (void)appendString(line, sizeof(line), " sndnxt=");
                (void)appendU64Dec(line, sizeof(line), views[i].sendNext);
                (void)appendString(line, sizeof(line), " rcvnxt=");
                (void)appendU64Dec(line, sizeof(line), views[i].recvNext);

                if (views[i].synRetries)
                {
                    (void)appendString(line, sizeof(line), " synret=");
                    (void)appendU64Dec(line, sizeof(line), views[i].synRetries);
                }
                if (views[i].txInFlightLen)
                {
                    (void)appendString(line, sizeof(line), " inflight=");
                    (void)appendU64Dec(line, sizeof(line), views[i].txInFlightLen);
                    (void)appendString(line, sizeof(line), " retr=");
                    (void)appendU64Dec(line, sizeof(line), views[i].txInFlightRetries);
                }

                ctx.writeLine(line);
            }

            return true;
        }

        // Forward declarations (cmdNetStat convenience wrapper).
        static bool cmdTcpState(const char *args, const QC::Cmd::Context &ctx, void *);

        static bool cmdNetStat(const char *, const QC::Cmd::Context &ctx, void *)
        {
            ctx.writeLine("netstat:");
            (void)cmdIp(nullptr, ctx, nullptr);
            (void)cmdArp(nullptr, ctx, nullptr);
            (void)cmdTcpState(nullptr, ctx, nullptr);
            return true;
        }

        static bool cmdTcpLog(const char *, const QC::Cmd::Context &ctx, void *)
        {
            QNet::Stack::instance().initialize();

            auto *tcp = QNet::Stack::instance().tcp();
            if (!tcp)
            {
                ctx.writeLine("tcplog: tcp unavailable");
                return true;
            }

            QNet::TCPEvent ev[32];
            QC::String::memset(ev, 0, sizeof(ev));
            const QC::usize n = tcp->copyEventLog(ev, sizeof(ev) / sizeof(ev[0]));

            char head[64];
            QC::String::memset(head, 0, sizeof(head));
            (void)appendString(head, sizeof(head), "tcplog: count=");
            (void)appendU64Dec(head, sizeof(head), static_cast<QC::u64>(n));
            ctx.writeLine(head);

            auto appendFlags = [&](char *out, QC::usize outSize, QC::u8 flags)
            {
                bool any = false;
                auto add = [&](const char *s)
                {
                    if (any)
                        (void)appendString(out, outSize, "|");
                    (void)appendString(out, outSize, s);
                    any = true;
                };

                if (flags & QNet::TCPFlags::SYN)
                    add("SYN");
                if (flags & QNet::TCPFlags::ACK)
                    add("ACK");
                if (flags & QNet::TCPFlags::FIN)
                    add("FIN");
                if (flags & QNet::TCPFlags::RST)
                    add("RST");
                if (flags & QNet::TCPFlags::PSH)
                    add("PSH");

                if (!any)
                    (void)appendString(out, outSize, "0");
            };

            for (QC::usize i = 0; i < n; ++i)
            {
                char ipBuf[32];
                ipv4ToString(ev[i].addr, ipBuf, sizeof(ipBuf));

                char flagsBuf[48];
                QC::String::memset(flagsBuf, 0, sizeof(flagsBuf));
                appendFlags(flagsBuf, sizeof(flagsBuf), ev[i].flags);

                char line[256];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), (ev[i].dir == QNet::TCPEvent::Dir::Tx) ? "TX" : "RX");
                (void)appendString(line, sizeof(line), " t=");
                (void)appendU64Dec(line, sizeof(line), ev[i].t_ms);
                (void)appendString(line, sizeof(line), " ip=");
                (void)appendString(line, sizeof(line), ipBuf);
                (void)appendString(line, sizeof(line), " sp=");
                (void)appendU64Dec(line, sizeof(line), ev[i].srcPort);
                (void)appendString(line, sizeof(line), " dp=");
                (void)appendU64Dec(line, sizeof(line), ev[i].dstPort);
                (void)appendString(line, sizeof(line), " f=");
                (void)appendString(line, sizeof(line), flagsBuf);
                (void)appendString(line, sizeof(line), " seq=");
                (void)appendU64Dec(line, sizeof(line), ev[i].seq);
                (void)appendString(line, sizeof(line), " ack=");
                (void)appendU64Dec(line, sizeof(line), ev[i].ack);
                (void)appendString(line, sizeof(line), " len=");
                (void)appendU64Dec(line, sizeof(line), ev[i].payloadLen);
                ctx.writeLine(line);
            }

            return true;
        }

        static bool cmdArp(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            QNet::Stack::instance().initialize();

            const char *p = args ? skipSpaces(args) : nullptr;
            if (p && *p)
            {
                QNet::IPv4Address target{};
                if (!parseIPv4(p, target))
                {
                    ctx.writeLine("arp: usage: arp [a.b.c.d]");
                    return true;
                }

                QNet::MACAddress mac{};
                const bool resolved = QNet::Stack::instance().ethernet()->resolveMAC(target.value, &mac);
                if (!resolved && QK::Time::available() && QK::System::pumpAvailable())
                {
                    const QC::u64 deadlineMs = QK::Time::milliseconds() + 250;
                    while (QK::Time::milliseconds() < deadlineMs)
                    {
                        QK::System::pump();

                        QNet::Ethernet::ARPCacheEntryView tmp[8] = {};
                        const QC::usize nTmp = QNet::Stack::instance().ethernet()->copyARPCache(tmp, sizeof(tmp) / sizeof(tmp[0]));
                        for (QC::usize i = 0; i < nTmp; ++i)
                        {
                            if (tmp[i].ip == target.value)
                            {
                                mac = tmp[i].mac;
                                goto arp_resolved;
                            }
                        }

                        QK::Time::sleep(10);
                    }
                }

            arp_resolved:
                if (mac.bytes[0] || mac.bytes[1] || mac.bytes[2] || mac.bytes[3] || mac.bytes[4] || mac.bytes[5])
                {
                    char ipBuf[32];
                    char macBuf[32];
                    ipv4ToString(target, ipBuf, sizeof(ipBuf));
                    macToString(mac, macBuf, sizeof(macBuf));

                    char line[96];
                    QC::String::memset(line, 0, sizeof(line));
                    (void)appendString(line, sizeof(line), "arp: resolved ");
                    (void)appendString(line, sizeof(line), ipBuf);
                    (void)appendString(line, sizeof(line), " -> ");
                    (void)appendString(line, sizeof(line), macBuf);
                    ctx.writeLine(line);
                }
                else
                {
                    ctx.writeLine("arp: requested (pending)");
                }
            }

            QNet::Ethernet::ARPCacheEntryView entries[64] = {};
            const QC::usize n = QNet::Stack::instance().ethernet()->copyARPCache(entries, sizeof(entries) / sizeof(entries[0]));
            if (n == 0)
            {
                ctx.writeLine("arp: (empty)");
                return true;
            }

            char header[64];
            QC::String::memset(header, 0, sizeof(header));
            (void)appendString(header, sizeof(header), "arp: count=");
            (void)appendU64Dec(header, sizeof(header), static_cast<QC::u64>(n));
            ctx.writeLine(header);

            for (QC::usize i = 0; i < n; ++i)
            {
                QNet::IPv4Address ip{};
                ip.value = entries[i].ip;

                char ipBuf[32];
                char macBuf[32];
                ipv4ToString(ip, ipBuf, sizeof(ipBuf));
                macToString(entries[i].mac, macBuf, sizeof(macBuf));

                char line[96];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), ipBuf);
                (void)appendString(line, sizeof(line), " -> ");
                (void)appendString(line, sizeof(line), macBuf);
                ctx.writeLine(line);
            }

            return true;
        }

        static bool cmdTier(const char *, const QC::Cmd::Context &ctx, void *)
        {
            char line[384];
            QC::String::memset(line, 0, sizeof(line));
            (void)appendString(line, sizeof(line), "Active tier: '");
            (void)appendString(line, sizeof(line), QK::Boot::Config::GetActiveConfigTierName());
            (void)appendString(line, sizeof(line), "' root='");
            (void)appendString(line, sizeof(line), QK::Boot::Config::GetActiveConfigTierRoot());
            (void)appendString(line, sizeof(line), "'");
            ctx.writeLine(line);

            const auto *stage = QK::Boot::Config::GetCommittedEarlyConfig();
            if (!stage)
            {
                ctx.writeLine("Committed early config: (none)");
                return true;
            }

            char header[256];
            QC::String::memset(header, 0, sizeof(header));
            (void)appendString(header, sizeof(header), "Committed early config: root='");
            (void)appendString(header, sizeof(header), stage->root[0] ? stage->root : "(none)");
            (void)appendString(header, sizeof(header), "' modules=");

            // moduleCount (decimal)
            char num[32];
            QC::String::memset(num, 0, sizeof(num));
            QC::u64 v = static_cast<QC::u64>(stage->moduleCount);
            int numIdx = 0;
            if (v == 0)
            {
                num[numIdx++] = '0';
            }
            else
            {
                char tmp[32];
                int tmpIdx = 0;
                while (v > 0 && tmpIdx < 31)
                {
                    tmp[tmpIdx++] = static_cast<char>('0' + (v % 10));
                    v /= 10;
                }
                for (int i = tmpIdx - 1; i >= 0; --i)
                    num[numIdx++] = tmp[i];
            }
            num[numIdx] = '\0';
            (void)appendString(header, sizeof(header), num);
            ctx.writeLine(header);

            for (QC::u32 i = 0; i < stage->moduleCount && i < 16; ++i)
            {
                const auto &m = stage->modules[i];
                if (m.id[0] == 0)
                    continue;

                char mline[512];
                QC::String::memset(mline, 0, sizeof(mline));
                (void)appendString(mline, sizeof(mline), "- id='");
                (void)appendString(mline, sizeof(mline), m.id);
                (void)appendString(mline, sizeof(mline), "' type='");
                (void)appendString(mline, sizeof(mline), m.type);
                (void)appendString(mline, sizeof(mline), "' required=");
                (void)appendString(mline, sizeof(mline), m.required ? "true" : "false");
                (void)appendString(mline, sizeof(mline), " json=");
                (void)appendString(mline, sizeof(mline), m.hasJson ? "true" : "false");
                (void)appendString(mline, sizeof(mline), " path='");
                (void)appendString(mline, sizeof(mline), m.resolvedPath);
                (void)appendString(mline, sizeof(mline), "'");
                ctx.writeLine(mline);
            }

            return true;
        }

        static bool cmdRegdump(const char *, const QC::Cmd::Context &ctx, void *)
        {
            auto &regs = QK::Runtime::Registries::instance();
            const auto &seed = regs.bootSeed();

            char header[512];
            QC::String::memset(header, 0, sizeof(header));
            (void)appendString(header, sizeof(header), "Runtime registries: tier='");
            (void)appendString(header, sizeof(header), seed.tierName[0] ? seed.tierName : "(unknown)");
            (void)appendString(header, sizeof(header), "' root='");
            (void)appendString(header, sizeof(header), seed.tierRoot[0] ? seed.tierRoot : "(none)");
            (void)appendString(header, sizeof(header), "' modules=");
            (void)appendU64Dec(header, sizeof(header), static_cast<QC::u64>(seed.moduleCount));
            ctx.writeLine(header);

            char counts[256];
            QC::String::memset(counts, 0, sizeof(counts));
            (void)appendString(counts, sizeof(counts), "Counts: processes=");
            (void)appendU64Dec(counts, sizeof(counts), static_cast<QC::u64>(regs.processCount()));
            (void)appendString(counts, sizeof(counts), " services=");
            (void)appendU64Dec(counts, sizeof(counts), static_cast<QC::u64>(regs.serviceCount()));
            (void)appendString(counts, sizeof(counts), " windows=");
            (void)appendU64Dec(counts, sizeof(counts), static_cast<QC::u64>(regs.windowCount()));
            (void)appendString(counts, sizeof(counts), " resources=");
            (void)appendU64Dec(counts, sizeof(counts), static_cast<QC::u64>(regs.resourceCount()));
            ctx.writeLine(counts);

            {
                auto &cr = QC::Cmd::Registry::instance();
                char cmdLine[160];
                QC::String::memset(cmdLine, 0, sizeof(cmdLine));
                (void)appendString(cmdLine, sizeof(cmdLine), "CommandRuntime: exec=");
                (void)appendU64Dec(cmdLine, sizeof(cmdLine), cr.executionCount());
                (void)appendString(cmdLine, sizeof(cmdLine), " parse_err=");
                (void)appendU64Dec(cmdLine, sizeof(cmdLine), cr.parseErrorCount());
                ctx.writeLine(cmdLine);
            }

            const auto &sec = regs.securityState();
            char secLine[256];
            QC::String::memset(secLine, 0, sizeof(secLine));
            (void)appendString(secLine, sizeof(secLine), "Security: tpm=");
            (void)appendString(secLine, sizeof(secLine), sec.tpmAvailable ? "true" : "false");
            (void)appendString(secLine, sizeof(secLine), " enforce=");
            (void)appendString(secLine, sizeof(secLine), sec.enforcementEnabled ? "true" : "false");
            (void)appendString(secLine, sizeof(secLine), " measured=");
            (void)appendU64Dec(secLine, sizeof(secLine), static_cast<QC::u64>(sec.measuredArtifactCount));
            (void)appendString(secLine, sizeof(secLine), " sc_mem(ns/nd/me)=");
            (void)appendString(secLine, sizeof(secLine), sec.scNoSwap ? "1" : "0");
            (void)appendString(secLine, sizeof(secLine), "/");
            (void)appendString(secLine, sizeof(secLine), sec.scNoDump ? "1" : "0");
            (void)appendString(secLine, sizeof(secLine), "/");
            (void)appendString(secLine, sizeof(secLine), sec.scMinimalExposure ? "1" : "0");
            (void)appendString(secLine, sizeof(secLine), " exec_guard=");
            (void)appendString(secLine, sizeof(secLine), sec.guardedExecutionEnabled ? "1" : "0");
            (void)appendString(secLine, sizeof(secLine), " app_space=");
            (void)appendString(secLine, sizeof(secLine), sec.protectedAppExecutionSpace ? "1" : "0");
            (void)appendString(secLine, sizeof(secLine), " sc_hidden=");
            (void)appendString(secLine, sizeof(secLine), sec.hiddenEncryptedScStorage ? "1" : "0");
            ctx.writeLine(secLine);

            const auto tf = QSC::SecurityCenter::instance().taskFlowMetrics();
            char tfLine[256];
            QC::String::memset(tfLine, 0, sizeof(tfLine));
            (void)appendString(tfLine, sizeof(tfLine), "TaskFlow: pending=");
            (void)appendU64Dec(tfLine, sizeof(tfLine), static_cast<QC::u64>(tf.pending));
            (void)appendString(tfLine, sizeof(tfLine), " running=");
            (void)appendU64Dec(tfLine, sizeof(tfLine), static_cast<QC::u64>(tf.running));
            (void)appendString(tfLine, sizeof(tfLine), " completed=");
            (void)appendU64Dec(tfLine, sizeof(tfLine), static_cast<QC::u64>(tf.completed));
            (void)appendString(tfLine, sizeof(tfLine), " executed=");
            (void)appendU64Dec(tfLine, sizeof(tfLine), tf.totalExecuted);
            (void)appendString(tfLine, sizeof(tfLine), " cached=");
            (void)appendU64Dec(tfLine, sizeof(tfLine), tf.cachedCompletions);
            (void)appendString(tfLine, sizeof(tfLine), " memo(h/m/r)=");
            (void)appendU64Dec(tfLine, sizeof(tfLine), tf.memoHits);
            (void)appendString(tfLine, sizeof(tfLine), "/");
            (void)appendU64Dec(tfLine, sizeof(tfLine), tf.memoMisses);
            (void)appendString(tfLine, sizeof(tfLine), "/");
            (void)appendU64Dec(tfLine, sizeof(tfLine), tf.memoRefused);
            ctx.writeLine(tfLine);

            char tfTimeLine[256];
            QC::String::memset(tfTimeLine, 0, sizeof(tfTimeLine));
            (void)appendString(tfTimeLine, sizeof(tfTimeLine), "TaskFlowTiming: build(total/avg)=");
            (void)appendU64Dec(tfTimeLine, sizeof(tfTimeLine), tf.totalBuildMs);
            (void)appendString(tfTimeLine, sizeof(tfTimeLine), "/");
            (void)appendU64Dec(tfTimeLine, sizeof(tfTimeLine), tf.averageBuildMs);
            (void)appendString(tfTimeLine, sizeof(tfTimeLine), " exec(total/avg)=");
            (void)appendU64Dec(tfTimeLine, sizeof(tfTimeLine), tf.totalExecutionMs);
            (void)appendString(tfTimeLine, sizeof(tfTimeLine), "/");
            (void)appendU64Dec(tfTimeLine, sizeof(tfTimeLine), tf.averageExecutionMs);
            (void)appendString(tfTimeLine, sizeof(tfTimeLine), " qwait(total/avg)=");
            (void)appendU64Dec(tfTimeLine, sizeof(tfTimeLine), tf.totalQueueDelayMs);
            (void)appendString(tfTimeLine, sizeof(tfTimeLine), "/");
            (void)appendU64Dec(tfTimeLine, sizeof(tfTimeLine), tf.averageQueueDelayMs);
            (void)appendString(tfTimeLine, sizeof(tfTimeLine), " sched(+/-)=");
            (void)appendU64Dec(tfTimeLine, sizeof(tfTimeLine), tf.schedulerPromotions);
            (void)appendString(tfTimeLine, sizeof(tfTimeLine), "/");
            (void)appendU64Dec(tfTimeLine, sizeof(tfTimeLine), tf.schedulerDemotions);
            ctx.writeLine(tfTimeLine);

            char tfPerf[256];
            QC::String::memset(tfPerf, 0, sizeof(tfPerf));
            (void)appendString(tfPerf, sizeof(tfPerf), "TaskFlowPerf: cross(+/-)=");
            (void)appendU64Dec(tfPerf, sizeof(tfPerf), tf.crossFlowPromotions);
            (void)appendString(tfPerf, sizeof(tfPerf), "/");
            (void)appendU64Dec(tfPerf, sizeof(tfPerf), tf.crossFlowDemotions);
            (void)appendString(tfPerf, sizeof(tfPerf), " policy(a/t/s/c)=");
            (void)appendU64Dec(tfPerf, sizeof(tfPerf), tf.policyAllow);
            (void)appendString(tfPerf, sizeof(tfPerf), "/");
            (void)appendU64Dec(tfPerf, sizeof(tfPerf), tf.policyThrottle);
            (void)appendString(tfPerf, sizeof(tfPerf), "/");
            (void)appendU64Dec(tfPerf, sizeof(tfPerf), tf.policySuspend);
            (void)appendString(tfPerf, sizeof(tfPerf), "/");
            (void)appendU64Dec(tfPerf, sizeof(tfPerf), tf.policyCancel);
            (void)appendString(tfPerf, sizeof(tfPerf), " redundant=");
            (void)appendU64Dec(tfPerf, sizeof(tfPerf), tf.redundantSubmissions);
            ctx.writeLine(tfPerf);

            if (seed.moduleCount > 0)
            {
                ctx.writeLine("BootSeed modules:");
                for (QC::u32 i = 0; i < seed.moduleCount && i < 16; ++i)
                {
                    const auto &m = seed.modules[i];
                    if (m.id[0] == 0)
                        continue;

                    char mline[640];
                    QC::String::memset(mline, 0, sizeof(mline));
                    (void)appendString(mline, sizeof(mline), "- id='");
                    (void)appendString(mline, sizeof(mline), m.id);
                    (void)appendString(mline, sizeof(mline), "' type='");
                    (void)appendString(mline, sizeof(mline), m.type[0] ? m.type : "(none)");
                    (void)appendString(mline, sizeof(mline), "' required=");
                    (void)appendString(mline, sizeof(mline), m.required ? "true" : "false");
                    (void)appendString(mline, sizeof(mline), " json=");
                    (void)appendString(mline, sizeof(mline), m.hasJson ? "true" : "false");
                    (void)appendString(mline, sizeof(mline), " path='");
                    (void)appendString(mline, sizeof(mline), m.resolvedPath[0] ? m.resolvedPath : "(none)");
                    (void)appendString(mline, sizeof(mline), "'");
                    ctx.writeLine(mline);
                }
            }

            QK::Runtime::WindowSnapshot snaps[QK::Runtime::Registries::MaxWindows] = {};
            const QC::usize n = regs.copyWindowSnapshots(snaps, QK::Runtime::Registries::MaxWindows);
            if (n == 0)
            {
                ctx.writeLine("Windows: (none)");
                return true;
            }

            ctx.writeLine("Windows:");
            for (QC::usize i = 0; i < n; ++i)
            {
                const auto &w = snaps[i];

                char wline[512];
                QC::String::memset(wline, 0, sizeof(wline));
                (void)appendString(wline, sizeof(wline), "- id=");
                (void)appendU64Dec(wline, sizeof(wline), static_cast<QC::u64>(w.windowId));
                (void)appendString(wline, sizeof(wline), " z=");
                (void)appendU64Dec(wline, sizeof(wline), static_cast<QC::u64>(w.zIndex));
                (void)appendString(wline, sizeof(wline), " focus=");
                (void)appendString(wline, sizeof(wline), w.focused ? "true" : "false");
                (void)appendString(wline, sizeof(wline), " bounds=");
                (void)appendI64Dec(wline, sizeof(wline), static_cast<QC::i64>(w.x));
                (void)appendString(wline, sizeof(wline), ",");
                (void)appendI64Dec(wline, sizeof(wline), static_cast<QC::i64>(w.y));
                (void)appendString(wline, sizeof(wline), "+");
                (void)appendU64Dec(wline, sizeof(wline), static_cast<QC::u64>(w.width));
                (void)appendString(wline, sizeof(wline), "x");
                (void)appendU64Dec(wline, sizeof(wline), static_cast<QC::u64>(w.height));
                (void)appendString(wline, sizeof(wline), " flags=0x");

                // Minimal hex (always 8 digits)
                char hex[9];
                hex[8] = '\0';
                const char *digits = "0123456789ABCDEF";
                QC::u32 fv = w.flags;
                for (int k = 7; k >= 0; --k)
                {
                    hex[k] = digits[fv & 0xF];
                    fv >>= 4;
                }
                (void)appendString(wline, sizeof(wline), hex);

                ctx.writeLine(wline);
            }

            return true;
        }

        static bool cmdBootLog(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            const QC::usize total = QK::Boot::Log::Size();

            QC::usize startOffset = 0;
            const char *p = args ? skipSpaces(args) : nullptr;
            if (p && *p)
            {
                char mode[16];
                QC::String::memset(mode, 0, sizeof(mode));
                if (!readToken(p, mode, sizeof(mode)))
                {
                    ctx.writeLine("usage: bootlog [tail [lines]|export <auto|system|shared|usb>]");
                    return true;
                }

                if (streqIgnoreCase(mode, "export"))
                {
                    char targetTok[16];
                    QC::String::memset(targetTok, 0, sizeof(targetTok));
                    if (!readToken(p, targetTok, sizeof(targetTok)))
                    {
                        ctx.writeLine("usage: bootlog export <auto|system|shared|usb> [ephemeral-ok]");
                        return true;
                    }

                    ExportTarget target = ExportTarget::Auto;
                    if (!parseExportTarget(targetTok, target))
                    {
                        ctx.writeLine("bootlog: invalid export target (use auto|system|shared|usb)");
                        return true;
                    }

                    char outPath[256];
                    char refusal[160];
                    QC::String::memset(outPath, 0, sizeof(outPath));
                    QC::String::memset(refusal, 0, sizeof(refusal));
                    if (!buildExportPathForTarget(target,
                                                  "bootlog.txt",
                                                  outPath,
                                                  sizeof(outPath),
                                                  refusal,
                                                  sizeof(refusal)))
                    {
                        char line[224];
                        QC::String::memset(line, 0, sizeof(line));
                        (void)appendString(line, sizeof(line), "bootlog: ");
                        (void)appendString(line, sizeof(line), refusal[0] ? refusal : "target unavailable");
                        ctx.writeLine(line);
                        return true;
                    }

                    if (!allowWriteToPath(outPath, ctx, "bootlog"))
                        return true;

                    if (!enforceEphemeralWriteGuard(outPath, targetTok, args, ctx, "bootlog"))
                        return true;

                    if (!preflightExportPath(outPath, total, ctx, "bootlog"))
                        return true;

                    QFS::File *f = QFS::VFS::instance().open(outPath,
                                                             QFS::OpenMode::Write | QFS::OpenMode::Create | QFS::OpenMode::Truncate);
                    if (!f)
                    {
                        ctx.writeLine("bootlog: cannot open export output");
                        return true;
                    }

                    QC::usize offset = 0;
                    char chunk[256];
                    while (offset < total)
                    {
                        const QC::usize n = QK::Boot::Log::CopyOut(offset, chunk, sizeof(chunk));
                        if (n == 0)
                            break;
                        QC::usize off = 0;
                        while (off < n)
                        {
                            const QC::isize w = f->write(chunk + off, n - off);
                            if (w <= 0)
                            {
                                QFS::VFS::instance().close(f);
                                ctx.writeLine("bootlog: export write failed");
                                return true;
                            }
                            off += static_cast<QC::usize>(w);
                        }
                        offset += n;
                    }

                    QFS::VFS::instance().close(f);
                    (void)writeExportMetadataSidecar(outPath, "bootlog", targetTok, ctx, "bootlog");
                    char line[320];
                    char resolvedTarget[24];
                    char persistenceClass[24];
                    QC::String::memset(resolvedTarget, 0, sizeof(resolvedTarget));
                    QC::String::memset(persistenceClass, 0, sizeof(persistenceClass));
                    inferExportPathMetadata(outPath,
                                            targetTok,
                                            resolvedTarget,
                                            sizeof(resolvedTarget),
                                            persistenceClass,
                                            sizeof(persistenceClass));
                    QC::String::memset(line, 0, sizeof(line));
                    (void)appendString(line, sizeof(line), "bootlog: export ok path=");
                    (void)appendString(line, sizeof(line), outPath);
                    (void)appendString(line, sizeof(line), " target=");
                    (void)appendString(line, sizeof(line), resolvedTarget[0] ? resolvedTarget : "unknown");
                    (void)appendString(line, sizeof(line), " persistence=");
                    (void)appendString(line, sizeof(line), persistenceClass[0] ? persistenceClass : "unknown");
                    ctx.writeLine(line);
                    return true;
                }

                if (!streqIgnoreCase(mode, "tail"))
                {
                    ctx.writeLine("usage: bootlog [tail [lines]|export <auto|system|shared|usb>]");
                    return true;
                }

                constexpr QC::u32 kDefaultTailLines = 120;
                constexpr QC::u32 kMaxTailLines = 512;

                QC::u32 tailLines = kDefaultTailLines;
                char linesTok[16];
                QC::String::memset(linesTok, 0, sizeof(linesTok));
                if (readToken(p, linesTok, sizeof(linesTok)))
                {
                    QC::u32 parsed = 0;
                    if (!parseU32(linesTok, parsed) || parsed == 0)
                    {
                        ctx.writeLine("bootlog: invalid line count");
                        return true;
                    }
                    tailLines = parsed;
                    if (tailLines > kMaxTailLines)
                        tailLines = kMaxTailLines;
                }

                // Find the byte offset for the last N lines.
                // We track line starts in a bounded ring to stay freestanding-friendly.
                constexpr QC::usize kTrack = static_cast<QC::usize>(kMaxTailLines + 1);
                QC::usize starts[kTrack];
                QC::usize head = 0;
                QC::usize count = 1;
                starts[0] = 0;

                auto pushStart = [&](QC::usize off)
                {
                    if (count < kTrack)
                    {
                        starts[(head + count) % kTrack] = off;
                        ++count;
                        return;
                    }

                    starts[head] = off;
                    head = (head + 1) % kTrack;
                };

                char scan[256];
                QC::usize off = 0;
                while (off < total)
                {
                    const QC::usize n = QK::Boot::Log::CopyOut(off, scan, sizeof(scan));
                    if (n == 0)
                        break;

                    for (QC::usize i = 0; i < n; ++i)
                    {
                        if (scan[i] == '\n')
                        {
                            const QC::usize next = off + i + 1;
                            if (next < total)
                                pushStart(next);
                        }
                    }

                    off += n;
                }

                if (count > static_cast<QC::usize>(tailLines))
                {
                    const QC::usize idx = (head + (count - static_cast<QC::usize>(tailLines))) % kTrack;
                    startOffset = starts[idx];
                }
            }

            if (total == 0)
            {
                ctx.writeLine("bootlog: (empty)");
                return true;
            }

            char chunk[256];
            char line[512];
            QC::usize lineLen = 0;
            QC::usize offset = startOffset;

            while (offset < total)
            {
                const QC::usize n = QK::Boot::Log::CopyOut(offset, chunk, sizeof(chunk));
                if (n == 0)
                    break;
                offset += n;

                for (QC::usize i = 0; i < n; ++i)
                {
                    const char c = chunk[i];
                    if (c == '\r')
                        continue;

                    if (c == '\n')
                    {
                        line[lineLen] = '\0';
                        ctx.writeLine(line);
                        lineLen = 0;
                        continue;
                    }

                    if (lineLen + 1 < sizeof(line))
                    {
                        line[lineLen++] = c;
                    }
                    else
                    {
                        line[lineLen] = '\0';
                        ctx.writeLine(line);
                        lineLen = 0;
                    }
                }
            }

            if (lineLen > 0)
            {
                line[lineLen] = '\0';
                ctx.writeLine(line);
            }

            return true;
        }

        static bool cmdBootModules(const char *, const QC::Cmd::Context &ctx, void *)
        {
            const auto &seed = QK::Runtime::Registries::instance().bootSeed();
            ctx.writeLine("BootSeed: early artifacts (from registries)");

            ctx.writeLine("--- Config Artifacts (.JSN) ---");

            char line[640];
            for (QC::u32 i = 0; i < seed.moduleCount && i < 16; ++i)
            {
                const auto &m = seed.modules[i];
                if (!m.id[0] && !m.resolvedPath[0])
                    continue;

                const QC::usize pathLen = static_cast<QC::usize>(QC::String::strlen(m.resolvedPath));
                const bool isJsn = (pathLen >= 4) && (m.resolvedPath[pathLen - 4] == '.') &&
                                   ((m.resolvedPath[pathLen - 3] == 'J') || (m.resolvedPath[pathLen - 3] == 'j')) &&
                                   ((m.resolvedPath[pathLen - 2] == 'S') || (m.resolvedPath[pathLen - 2] == 's')) &&
                                   ((m.resolvedPath[pathLen - 1] == 'N') || (m.resolvedPath[pathLen - 1] == 'n'));
                if (!isJsn)
                    continue;

                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "[");
                (void)appendU64Dec(line, sizeof(line), static_cast<QC::u64>(i));
                (void)appendString(line, sizeof(line), "] id='");
                (void)appendString(line, sizeof(line), m.id[0] ? m.id : "(none)");
                (void)appendString(line, sizeof(line), "' type='");
                (void)appendString(line, sizeof(line), m.type[0] ? m.type : "(none)");
                (void)appendString(line, sizeof(line), "' role='");
                (void)appendString(line, sizeof(line), m.role[0] ? m.role : "(none)");
                (void)appendString(line, sizeof(line), "' status='");
                (void)appendString(line, sizeof(line), m.status[0] ? m.status : "(none)");
                (void)appendString(line, sizeof(line), "' req_hash=");
                (void)appendString(line, sizeof(line), m.hashRequired ? "1" : "0");
                (void)appendString(line, sizeof(line), " req_sig=");
                (void)appendString(line, sizeof(line), m.signatureRequired ? "1" : "0");
                (void)appendString(line, sizeof(line), " required=");
                (void)appendString(line, sizeof(line), m.required ? "1" : "0");
                ctx.writeLine(line);

                // Print path on its own line to avoid horizontal wrapping issues in the fixed-size terminal.
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "    path='");
                (void)appendString(line, sizeof(line), m.resolvedPath[0] ? m.resolvedPath : "(none)");
                (void)appendString(line, sizeof(line), "'");
                ctx.writeLine(line);
            }

            ctx.writeLine("--- Loadable Modules (Layer 2 trust) ---");

            for (QC::u32 i = 0; i < seed.moduleCount && i < 16; ++i)
            {
                const auto &m = seed.modules[i];
                if (!m.id[0] && !m.resolvedPath[0])
                    continue;

                const QC::usize pathLen = static_cast<QC::usize>(QC::String::strlen(m.resolvedPath));
                const bool isJsn = (pathLen >= 4) && (m.resolvedPath[pathLen - 4] == '.') &&
                                   ((m.resolvedPath[pathLen - 3] == 'J') || (m.resolvedPath[pathLen - 3] == 'j')) &&
                                   ((m.resolvedPath[pathLen - 2] == 'S') || (m.resolvedPath[pathLen - 2] == 's')) &&
                                   ((m.resolvedPath[pathLen - 1] == 'N') || (m.resolvedPath[pathLen - 1] == 'n'));
                if (isJsn)
                    continue;

                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "[");
                (void)appendU64Dec(line, sizeof(line), static_cast<QC::u64>(i));
                (void)appendString(line, sizeof(line), "] id='");
                (void)appendString(line, sizeof(line), m.id[0] ? m.id : "(none)");
                (void)appendString(line, sizeof(line), "' type='");
                (void)appendString(line, sizeof(line), m.type[0] ? m.type : "(none)");
                (void)appendString(line, sizeof(line), "' role='");
                (void)appendString(line, sizeof(line), m.role[0] ? m.role : "(none)");
                (void)appendString(line, sizeof(line), "' status='");
                (void)appendString(line, sizeof(line), m.status[0] ? m.status : "(none)");
                (void)appendString(line, sizeof(line), "' req_hash=");
                (void)appendString(line, sizeof(line), m.hashRequired ? "1" : "0");
                (void)appendString(line, sizeof(line), " req_sig=");
                (void)appendString(line, sizeof(line), m.signatureRequired ? "1" : "0");
                (void)appendString(line, sizeof(line), " required=");
                (void)appendString(line, sizeof(line), m.required ? "1" : "0");
                ctx.writeLine(line);

                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "    path='");
                (void)appendString(line, sizeof(line), m.resolvedPath[0] ? m.resolvedPath : "(none)");
                (void)appendString(line, sizeof(line), "'");
                ctx.writeLine(line);
            }

            return true;
        }

        static QQ::TaskResult flowTestTaskFn(void *context, void *arg)
        {
            (void)context;
            (void)arg;
            return QQ::TaskResult{true, 1, nullptr, 0};
        }

        static const char *decisionName(QQ::FlowDecisionType t)
        {
            switch (t)
            {
            case QQ::FlowDecisionType::Allow:
                return "ALLOW";
            case QQ::FlowDecisionType::ThrottleDelay:
                return "DELAY";
            case QQ::FlowDecisionType::IsolateSuspend:
                return "SUSPEND";
            case QQ::FlowDecisionType::IsolateCancel:
                return "CANCEL";
            default:
                return "UNKNOWN";
            }
        }

        static const char *stateName(QQ::TaskState s)
        {
            switch (s)
            {
            case QQ::TaskState::Pending:
                return "Pending";
            case QQ::TaskState::Queued:
                return "Queued";
            case QQ::TaskState::Blocked:
                return "Blocked";
            case QQ::TaskState::Running:
                return "Running";
            case QQ::TaskState::Suspended:
                return "Suspended";
            case QQ::TaskState::Completed:
                return "Completed";
            case QQ::TaskState::Failed:
                return "Failed";
            case QQ::TaskState::Cancelled:
                return "Cancelled";
            default:
                return "Unknown";
            }
        }

        static const QC::Cmd::Context *g_flowTestCtx = nullptr;
        static QQ::FlowPolicyFn g_flowTestPrevPolicy = nullptr;

        static QQ::FlowDecision flowTestLoggingPolicy(const QQ::TaskDescriptor &td)
        {
            // Wrap the currently-installed policy (if any), so flowcontrol bypass/enforce is respected.
            const char *name = td.name;
            const char *origin = td.origin[0] ? td.origin : nullptr;
            const char *moduleId = td.moduleId[0] ? td.moduleId : nullptr;

            QQ::FlowDecision d{};
            if (g_flowTestPrevPolicy)
                d = g_flowTestPrevPolicy(td);

            if (g_flowTestCtx)
            {
                char line[256];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "policy: name=");
                (void)appendString(line, sizeof(line), name ? name : "(null)");
                if (origin)
                {
                    (void)appendString(line, sizeof(line), " origin=");
                    (void)appendString(line, sizeof(line), origin);
                }
                if (moduleId)
                {
                    (void)appendString(line, sizeof(line), " module=");
                    (void)appendString(line, sizeof(line), moduleId);
                }
                (void)appendString(line, sizeof(line), " decision=");
                (void)appendString(line, sizeof(line), decisionName(d.type));
                if (d.type == QQ::FlowDecisionType::ThrottleDelay)
                {
                    (void)appendString(line, sizeof(line), " ms=");
                    (void)appendU64Dec(line, sizeof(line), d.throttleDelayMs);
                }
                g_flowTestCtx->writeLine(line);
            }

            return d;
        }

        static bool cmdFlowTest(const char *, const QC::Cmd::Context &ctx, void *)
        {
            ctx.writeLine("flowtest: submitting 4 tasks (allow/delay/suspend/cancel)");

            auto &ex = QQ::Executor::instance();

            // Temporarily install a logging policy wrapper so we can print the decision.
            const QQ::FlowPolicyFn prev = ex.flowPolicy();
            g_flowTestCtx = &ctx;
            g_flowTestPrevPolicy = prev;
            ex.setFlowPolicy(&flowTestLoggingPolicy);

            const QC::u64 t0 = QK::Time::milliseconds();

            const QQ::TaskId allowId = ex.submitWithOrigin("flow_allow", "ui", "desktop", &flowTestTaskFn, nullptr, nullptr);
            const QQ::TaskId delayId = ex.submitWithOrigin("flow_delay", "ui", "delay", &flowTestTaskFn, nullptr, nullptr);
            const QQ::TaskId suspId = ex.submitWithOrigin("flow_suspend", "ui", "suspend", &flowTestTaskFn, nullptr, nullptr);
            const QQ::TaskId cancId = ex.submitWithOrigin("flow_cancel", "ui", "cancel", &flowTestTaskFn, nullptr, nullptr);

            char line[256];
            QC::String::memset(line, 0, sizeof(line));
            (void)appendString(line, sizeof(line), "t0_ms=");
            (void)appendU64Dec(line, sizeof(line), t0);
            ctx.writeLine(line);

            auto dumpOne = [&](const char *name, QQ::TaskId id) {
                const QQ::TaskState st = ex.state(id);
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "- name=");
                (void)appendString(line, sizeof(line), name);
                (void)appendString(line, sizeof(line), " id=");
                (void)appendU64Dec(line, sizeof(line), static_cast<QC::u64>(id));
                (void)appendString(line, sizeof(line), " state=");
                (void)appendString(line, sizeof(line), stateName(st));
                ctx.writeLine(line);
            };

            dumpOne("flow_allow", allowId);
            dumpOne("flow_delay", delayId);
            dumpOne("flow_suspend", suspId);
            dumpOne("flow_cancel", cancId);

            // Resume suspended and wait a little to observe completion.
            ctx.writeLine("flowtest: resuming suspended task");
            ex.resume(suspId);
            QK::Time::sleep(50);

            dumpOne("flow_suspend", suspId);
            dumpOne("flow_delay", delayId);
            dumpOne("flow_allow", allowId);
            dumpOne("flow_cancel", cancId);

            const QC::u64 t1 = QK::Time::milliseconds();
            QC::String::memset(line, 0, sizeof(line));
            (void)appendString(line, sizeof(line), "t1_ms=");
            (void)appendU64Dec(line, sizeof(line), t1);
            (void)appendString(line, sizeof(line), " dt_ms=");
            (void)appendU64Dec(line, sizeof(line), (t1 >= t0) ? (t1 - t0) : 0);
            ctx.writeLine(line);

            ctx.writeLine("flowtest: NOTE state enum values are numeric (Pending/Queued/Running/Suspended/Completed/Failed/Cancelled)");

            // Restore previous policy.
            ex.setFlowPolicy(prev);
            g_flowTestPrevPolicy = nullptr;
            g_flowTestCtx = nullptr;
            return true;
        }

        static bool cmdFlowControl(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            // MVP surface:
            // - flowcontrol status
            // - flowcontrol bypass
            // - flowcontrol enforce
            const char *a = args;
            while (a && (*a == ' ' || *a == '\t'))
                ++a;

            if (!a || *a == 0 || (a[0] == 's' && a[1] == 't'))
            {
                char line[128];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "flowcontrol: mode=");
                (void)appendString(line, sizeof(line), QK::SecurityCenter::modeName(QK::SecurityCenter::instance().mode()));
                ctx.writeLine(line);
                ctx.writeLine("usage: flowcontrol status|bypass|enforce");
                return true;
            }

            auto equalsIgnoreCaseToken = [](const char *s, const char *token) -> bool {
                if (!s || !token)
                    return false;
                auto lower = [](char c) -> char {
                    if (c >= 'A' && c <= 'Z')
                        return static_cast<char>(c - 'A' + 'a');
                    return c;
                };
                while (*s && (*s == ' ' || *s == '\t'))
                    ++s;
                const char *t = token;
                while (*s && *t && lower(*s) == lower(*t))
                {
                    ++s;
                    ++t;
                }
                if (*t != 0)
                    return false;
                // token must end at whitespace or string end
                return (*s == 0) || (*s == ' ') || (*s == '\t');
            };

            if (equalsIgnoreCaseToken(a, "bypass"))
            {
                QK::SecurityCenter::instance().setFlowEnforcementEnabled(false);
                ctx.writeLine("flowcontrol: mode=BYPASS");
                return true;
            }
            if (equalsIgnoreCaseToken(a, "enforce"))
            {
                QK::SecurityCenter::instance().setFlowEnforcementEnabled(true);
                ctx.writeLine("flowcontrol: mode=ENFORCE");
                return true;
            }

            ctx.writeLine("flowcontrol: unknown arg (use status|bypass|enforce)");
            return true;
        }

        static bool cmdCanonicalArgTest(const char *, const QC::Cmd::Context &ctx, void *)
        {
            ctx.writeLine("canonargtest: submitting 2 tasks with identical canonical args");

            // Build a canonical payload in a stable buffer.
            static QC::u8 argBuf[128];
            QC::usize wrote = 0;
            const char payload[] = "hello";

            if (!QC::CanonicalArgs::build(argBuf, sizeof(argBuf),
                                         /*schemaId=*/1,
                                         /*version=*/1,
                                         payload,
                                         sizeof(payload) - 1,
                                         wrote))
            {
                ctx.writeLine("canonargtest: failed to build canonical args");
                return true;
            }

            auto &ex = QQ::Executor::instance();

            const QQ::TaskId a = ex.submitWithOriginAndArgSize("canon_a", "ui", "desktop", &flowTestTaskFn, nullptr, argBuf, wrote);
            const QQ::TaskId b = ex.submitWithOriginAndArgSize("canon_b", "ui", "desktop", &flowTestTaskFn, nullptr, argBuf, wrote);

            const QQ::TaskDescriptor *ta = ex.taskDescriptor(a);
            const QQ::TaskDescriptor *tb = ex.taskDescriptor(b);

            if (!ta || !tb)
            {
                ctx.writeLine("canonargtest: failed to resolve task descriptors");
                return true;
            }

            char sigA[65], inA[65], sigB[65], inB[65];
            (void)QC::Sha256DigestToLowerHex(ta->signatureHash, sigA, sizeof(sigA));
            (void)QC::Sha256DigestToLowerHex(ta->inputHash, inA, sizeof(inA));
            (void)QC::Sha256DigestToLowerHex(tb->signatureHash, sigB, sizeof(sigB));
            (void)QC::Sha256DigestToLowerHex(tb->inputHash, inB, sizeof(inB));

            char line[256];
            QC::String::memset(line, 0, sizeof(line));
            (void)appendString(line, sizeof(line), "A sig=");
            (void)appendString(line, sizeof(line), sigA);
            (void)appendString(line, sizeof(line), " in=");
            (void)appendString(line, sizeof(line), inA);
            ctx.writeLine(line);

            QC::String::memset(line, 0, sizeof(line));
            (void)appendString(line, sizeof(line), "B sig=");
            (void)appendString(line, sizeof(line), sigB);
            (void)appendString(line, sizeof(line), " in=");
            (void)appendString(line, sizeof(line), inB);
            ctx.writeLine(line);

            ctx.writeLine("canonargtest: expected: input hashes match; signature hashes match (same function + schema/version)");
            return true;
        }

        static bool cmdMemoCache(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            const char *a = args;
            while (a && (*a == ' ' || *a == '\t'))
                ++a;

            auto &ex = QQ::Executor::instance();

            if (!a || *a == 0 || (a[0] == 's' && a[1] == 't'))
            {
                char line[192];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "memocache: enabled=");
                (void)appendString(line, sizeof(line), ex.memoizationEnabled() ? "1" : "0");
                (void)appendString(line, sizeof(line), " hits=");
                (void)appendU64Dec(line, sizeof(line), ex.memoizationHits());
                (void)appendString(line, sizeof(line), " misses=");
                (void)appendU64Dec(line, sizeof(line), ex.memoizationMisses());
                (void)appendString(line, sizeof(line), " refused=");
                (void)appendU64Dec(line, sizeof(line), ex.memoizationRefused());
                (void)appendString(line, sizeof(line), " evict=");
                (void)appendU64Dec(line, sizeof(line), ex.memoizationEvictions());
                (void)appendString(line, sizeof(line), " safetyrej=");
                (void)appendU64Dec(line, sizeof(line), ex.memoizationSafetyRejected());
                (void)appendString(line, sizeof(line), " cache=");
                (void)appendU64Dec(line, sizeof(line), ex.memoizationCacheEntries());
                (void)appendString(line, sizeof(line), "/");
                (void)appendU64Dec(line, sizeof(line), ex.memoizationCacheCapacity());
                (void)appendString(line, sizeof(line), " allowlist=");
                (void)appendString(line, sizeof(line), ex.memoizationAllowlistEnabled() ? "1" : "0");
                (void)appendString(line, sizeof(line), " allowcnt=");
                (void)appendU64Dec(line, sizeof(line), ex.memoizationAllowlistCount());
                ctx.writeLine(line);
                ctx.writeLine("usage: memocache status|on|off|clear");
                return true;
            }

            auto equalsIgnoreCaseToken = [](const char *s, const char *token) -> bool {
                if (!s || !token)
                    return false;
                auto lower = [](char c) -> char {
                    if (c >= 'A' && c <= 'Z')
                        return static_cast<char>(c - 'A' + 'a');
                    return c;
                };
                while (*s && (*s == ' ' || *s == '\t'))
                    ++s;
                const char *t = token;
                while (*s && *t && lower(*s) == lower(*t))
                {
                    ++s;
                    ++t;
                }
                if (*t != 0)
                    return false;
                return (*s == 0) || (*s == ' ') || (*s == '\t');
            };

            if (equalsIgnoreCaseToken(a, "on"))
            {
                ex.setMemoizationEnabled(true);
                ctx.writeLine("memocache: enabled=1");
                return true;
            }
            if (equalsIgnoreCaseToken(a, "off"))
            {
                ex.setMemoizationEnabled(false);
                ctx.writeLine("memocache: enabled=0");
                return true;
            }
            if (equalsIgnoreCaseToken(a, "clear"))
            {
                ex.clearMemoizationCache();
                ctx.writeLine("memocache: cleared");
                return true;
            }

            ctx.writeLine("memocache: unknown arg (use status|on|off|clear)");
            return true;
        }

        static bool cmdMemoAllow(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            const char *a = args;
            while (a && (*a == ' ' || *a == '\t'))
                ++a;

            auto &ex = QQ::Executor::instance();

            auto equalsIgnoreCaseToken = [](const char *s, const char *token) -> bool {
                if (!s || !token)
                    return false;
                auto lower = [](char c) -> char {
                    if (c >= 'A' && c <= 'Z')
                        return static_cast<char>(c - 'A' + 'a');
                    return c;
                };
                while (*s && (*s == ' ' || *s == '\t'))
                    ++s;
                const char *t = token;
                while (*s && *t && lower(*s) == lower(*t))
                {
                    ++s;
                    ++t;
                }
                if (*t != 0)
                    return false;
                return (*s == 0) || (*s == ' ') || (*s == '\t');
            };

            if (!a || *a == 0 || equalsIgnoreCaseToken(a, "status"))
            {
                char line[192];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "memoallow: enabled=");
                (void)appendString(line, sizeof(line), ex.memoizationAllowlistEnabled() ? "1" : "0");
                (void)appendString(line, sizeof(line), " count=");
                (void)appendU64Dec(line, sizeof(line), ex.memoizationAllowlistCount());
                ctx.writeLine(line);
                ctx.writeLine("usage: memoallow status|on|off|clear|add <taskId>|del <taskId>");
                return true;
            }

            if (equalsIgnoreCaseToken(a, "on"))
            {
                ex.setMemoizationAllowlistEnabled(true);
                ctx.writeLine("memoallow: enabled=1");
                return true;
            }
            if (equalsIgnoreCaseToken(a, "off"))
            {
                ex.setMemoizationAllowlistEnabled(false);
                ctx.writeLine("memoallow: enabled=0");
                return true;
            }
            if (equalsIgnoreCaseToken(a, "clear"))
            {
                ex.clearMemoizationAllowlist();
                ctx.writeLine("memoallow: cleared");
                return true;
            }

            auto isCommandWithArg = [&](const char *cmd, const char *&outArg) -> bool {
                const char *s = a;
                while (*s && (*s == ' ' || *s == '\t'))
                    ++s;

                auto lower = [](char c) -> char {
                    if (c >= 'A' && c <= 'Z')
                        return static_cast<char>(c - 'A' + 'a');
                    return c;
                };

                const char *t = cmd;
                while (*s && *t && lower(*s) == lower(*t))
                {
                    ++s;
                    ++t;
                }
                if (*t != 0)
                    return false;
                if (!(*s == 0 || *s == ' ' || *s == '\t'))
                    return false;
                while (*s == ' ' || *s == '\t')
                    ++s;
                outArg = s;
                return true;
            };

            const char *arg = nullptr;
            const bool isAdd = isCommandWithArg("add", arg);
            const bool isDel = isCommandWithArg("del", arg);
            if (isAdd || isDel)
            {
                const char *p = arg;
                if (!p || !*p)
                {
                    ctx.writeLine("memoallow: missing taskId");
                    return true;
                }

                auto parseU64 = [](const char *s, QC::u64 &out) -> bool {
                    if (!s)
                        return false;
                    while (*s == ' ' || *s == '\t')
                        ++s;
                    if (*s < '0' || *s > '9')
                        return false;
                    QC::u64 v = 0;
                    while (*s >= '0' && *s <= '9')
                    {
                        v = (v * 10) + (QC::u64)(*s - '0');
                        ++s;
                    }
                    out = v;
                    return true;
                };

                QC::u64 parsed = 0;
                if (!parseU64(p, parsed))
                {
                    ctx.writeLine("memoallow: invalid taskId");
                    return true;
                }
                const QQ::TaskId id = (QQ::TaskId)parsed;
                const QQ::TaskDescriptor *td = ex.taskDescriptor(id);
                if (!td)
                {
                    ctx.writeLine("memoallow: unknown taskId");
                    return true;
                }

                const bool ok = isAdd ? ex.memoizationAllowlistAdd(td->signatureHash) : ex.memoizationAllowlistRemove(td->signatureHash);
                if (!ok)
                {
                    ctx.writeLine(isAdd ? "memoallow: add failed" : "memoallow: del failed");
                    return true;
                }
                ctx.writeLine(isAdd ? "memoallow: added" : "memoallow: removed");
                return true;
            }

            ctx.writeLine("memoallow: unknown arg");
            ctx.writeLine("usage: memoallow status|on|off|clear|add <taskId>|del <taskId>");
            return true;
        }

        static bool cmdAiruntime(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            const char *a = args;
            while (a && (*a == ' ' || *a == '\t'))
                ++a;

            auto equalsIgnoreCaseToken = [](const char *s, const char *token) -> bool {
                if (!s || !token)
                    return false;
                auto lower = [](char c) -> char {
                    if (c >= 'A' && c <= 'Z')
                        return static_cast<char>(c - 'A' + 'a');
                    return c;
                };
                while (*s && (*s == ' ' || *s == '\t'))
                    ++s;
                const char *t = token;
                while (*s && *t && lower(*s) == lower(*t))
                {
                    ++s;
                    ++t;
                }
                if (*t != 0)
                    return false;
                return (*s == 0) || (*s == ' ') || (*s == '\t');
            };

            auto statusName = [](QC::Status st) -> const char * {
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
                }
                return "?";
            };

            auto &ex = QQ::Executor::instance();

            if (!a || *a == 0 || equalsIgnoreCaseToken(a, "status"))
            {
                char line[192];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "airuntime: persisted=");
                (void)appendString(line, sizeof(line), QK::AIRuntime::hasPersistentState() ? "1" : "0");
                (void)appendString(line, sizeof(line), " memo=");
                (void)appendString(line, sizeof(line), ex.memoizationEnabled() ? "1" : "0");
                (void)appendString(line, sizeof(line), " allowlist=");
                (void)appendString(line, sizeof(line), ex.memoizationAllowlistEnabled() ? "1" : "0");
                (void)appendString(line, sizeof(line), " allowcnt=");
                (void)appendU64Dec(line, sizeof(line), ex.memoizationAllowlistCount());
                ctx.writeLine(line);
                ctx.writeLine("usage: airuntime status|load|save|clear");
                return true;
            }

            if (equalsIgnoreCaseToken(a, "load"))
            {
                const QC::Status st = QK::AIRuntime::loadPersistentState();
                char line[96];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "airuntime: load=");
                (void)appendString(line, sizeof(line), statusName(st));
                ctx.writeLine(line);
                return true;
            }

            if (equalsIgnoreCaseToken(a, "save"))
            {
                const QC::Status st = QK::AIRuntime::savePersistentState();
                char line[96];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "airuntime: save=");
                (void)appendString(line, sizeof(line), statusName(st));
                ctx.writeLine(line);
                return true;
            }

            if (equalsIgnoreCaseToken(a, "clear"))
            {
                const QC::Status st = QK::AIRuntime::clearPersistentState();
                char line[96];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "airuntime: clear=");
                (void)appendString(line, sizeof(line), statusName(st));
                ctx.writeLine(line);
                return true;
            }

            ctx.writeLine("airuntime: unknown arg (use status|load|save|clear)");
            return true;
        }

        static bool cmdTranscriptTest(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            Session *s = sessionFrom();
            const char *p = args ? skipSpaces(args) : nullptr;
            if (!p || *p == '\0')
            {
                ctx.writeLine("usage: transcripttest <path> [unsafe]");
                return true;
            }

            char fileArg[256];
            QC::String::memset(fileArg, 0, sizeof(fileArg));
            QC::usize fi = 0;
            while (p[fi] && !isSpace(p[fi]) && fi + 1 < sizeof(fileArg))
            {
                fileArg[fi] = p[fi];
                ++fi;
            }
            fileArg[fi] = '\0';

            if (fi == 0)
            {
                ctx.writeLine("transcripttest: invalid path argument");
                return true;
            }

            p = skipSpaces(p + fi);
            const bool unsafe = (p && *p) ? (QC::String::strcmp(p, "unsafe") == 0) : false;

            char absPath[256];
            QC::String::memset(absPath, 0, sizeof(absPath));
            if (!resolvePath(s, fileArg, absPath, sizeof(absPath)))
            {
                ctx.writeLine("transcripttest: invalid path");
                return true;
            }

            QC::Vector<char> buf;
            if (!readFileToNullTerminatedBuffer(absPath, buf, 1024 * 1024))
            {
                ctx.writeLine("transcripttest: cannot read transcript");
                return true;
            }

            QK::CmdCenter::registerMvpCommands();
            auto &reg = QC::Cmd::Registry::instance();

            auto isBlocked = [&](const char *line) -> bool {
                if (!line || !*line)
                    return false;
                auto tokenEq = [](const char *a, const char *b) -> bool {
                    if (!a || !b)
                        return false;
                    while (*a && *b && !isSpace(*a) && !isSpace(*b))
                    {
                        char ca = *a;
                        char cb = *b;
                        if (ca >= 'A' && ca <= 'Z')
                            ca = static_cast<char>(ca - 'A' + 'a');
                        if (cb >= 'A' && cb <= 'Z')
                            cb = static_cast<char>(cb - 'A' + 'a');
                        if (ca != cb)
                            return false;
                        ++a;
                        ++b;
                    }
                    const bool aEnd = (*a == '\0' || isSpace(*a));
                    const bool bEnd = (*b == '\0' || isSpace(*b));
                    return aEnd && bEnd;
                };

                static const char *kBlocked[] = {
                    "shutdown", "reboot", "rm", "del", "touch", "mkdir", "sysformat", "recover"
                };
                for (QC::usize i = 0; i < (sizeof(kBlocked) / sizeof(kBlocked[0])); ++i)
                {
                    if (tokenEq(line, kBlocked[i]))
                        return true;
                }
                return false;
            };

            auto lineContains = [](const char *hay, const char *needleBegin, const char *needleEnd) -> bool {
                if (!hay || !needleBegin || !needleEnd || needleEnd <= needleBegin)
                    return false;
                const QC::usize nlen = static_cast<QC::usize>(needleEnd - needleBegin);
                for (const char *h = hay; *h; ++h)
                {
                    QC::usize j = 0;
                    while (j < nlen && h[j] && h[j] == needleBegin[j])
                        ++j;
                    if (j == nlen)
                        return true;
                }
                return false;
            };

            struct MatchState
            {
                const QC::Vector<const char *> *begs = nullptr;
                const QC::Vector<const char *> *ends = nullptr;
                QC::usize nextExpected = 0;
            };

            auto matchOut = [&](const char *line, void *userData) {
                MatchState *st = reinterpret_cast<MatchState *>(userData);
                if (!st || !st->begs || !st->ends)
                    return;
                if (st->nextExpected >= st->begs->size())
                    return;
                const char *eb = (*(st->begs))[st->nextExpected];
                const char *ee = (*(st->ends))[st->nextExpected];
                if (lineContains(line, eb, ee))
                    ++st->nextExpected;
            };

            QC::Cmd::AccessLevel role = ctx.callerAccess;
            QC::u64 commands = 0;
            QC::u64 passed = 0;
            QC::u64 failed = 0;
            QC::u64 skipped = 0;

            QC::Vector<const char *> expBeg;
            QC::Vector<const char *> expEnd;

            const char *cur = buf.data();
            while (cur && *cur)
            {
                const char *lineBegin = cur;
                while (*cur && *cur != '\n' && *cur != '\r')
                    ++cur;
                const char *lineEnd = cur;
                while (lineEnd > lineBegin && isSpace(*(lineEnd - 1)))
                    --lineEnd;
                while (*cur == '\r' || *cur == '\n')
                    ++cur;

                const char *t = lineBegin;
                while (t < lineEnd && isSpace(*t))
                    ++t;
                if (!(t < lineEnd && *t == '>'))
                    continue;

                ++t;
                while (t < lineEnd && isSpace(*t))
                    ++t;

                char cmd[256];
                QC::String::memset(cmd, 0, sizeof(cmd));
                QC::usize ci = 0;
                for (const char *q = t; q < lineEnd && ci + 1 < sizeof(cmd); ++q)
                    cmd[ci++] = *q;
                cmd[ci] = '\0';

                // Collect expected output lines until next prompt.
                expBeg.clear();
                expEnd.clear();
                const char *scan = cur;
                while (scan && *scan)
                {
                    const char *lb = scan;
                    while (*scan && *scan != '\n' && *scan != '\r')
                        ++scan;
                    const char *le = scan;
                    while (le > lb && isSpace(*(le - 1)))
                        --le;

                    const char *tt = lb;
                    while (tt < le && isSpace(*tt))
                        ++tt;
                    if (tt < le && *tt == '>')
                        break;

                    if (tt < le)
                    {
                        expBeg.push_back(tt);
                        expEnd.push_back(le);
                    }

                    while (*scan == '\r' || *scan == '\n')
                        ++scan;
                }
                cur = scan;

                if (cmd[0] == '\0')
                    continue;

                ++commands;

                auto setRoleAndEcho = [&](QC::Cmd::AccessLevel r, const char *line) {
                    role = r;
                    MatchState ms{&expBeg, &expEnd, 0};
                    matchOut(line, &ms);
                    if (ms.nextExpected == expBeg.size())
                        ++passed;
                    else
                        ++failed;
                };

                if (QC::String::strcmp(cmd, "admin") == 0)
                {
                    setRoleAndEcho(QC::Cmd::AccessLevel::Admin, "chmode: now Admin");
                    continue;
                }
                if (QC::String::strcmp(cmd, "su") == 0)
                {
                    setRoleAndEcho(QC::Cmd::AccessLevel::SysAdmin, "chmode: now SysAdmin");
                    continue;
                }
                if (QC::String::strcmp(cmd, "system") == 0)
                {
                    setRoleAndEcho(QC::Cmd::AccessLevel::System, "chmode: now System");
                    continue;
                }
                if (QC::String::strcmp(cmd, "user") == 0)
                {
                    setRoleAndEcho(QC::Cmd::AccessLevel::User, "chmode: now User");
                    continue;
                }

                if (!unsafe && isBlocked(cmd))
                {
                    ++skipped;
                    continue;
                }

                MatchState ms{&expBeg, &expEnd, 0};
                QC::Cmd::Context runCtx;
                runCtx.out = +[](const char *line, void *userData) {
                    MatchState *state = reinterpret_cast<MatchState *>(userData);
                    if (!state)
                        return;
                    const QC::Vector<const char *> *begs = state->begs;
                    const QC::Vector<const char *> *ends = state->ends;
                    if (!begs || !ends || state->nextExpected >= begs->size())
                        return;
                    const char *eb = (*begs)[state->nextExpected];
                    const char *ee = (*ends)[state->nextExpected];
                    // Inline contains check (dup intentionally keeps this callback freestanding).
                    const QC::usize nlen = static_cast<QC::usize>(ee - eb);
                    for (const char *h = line; *h; ++h)
                    {
                        QC::usize j = 0;
                        while (j < nlen && h[j] && h[j] == eb[j])
                            ++j;
                        if (j == nlen)
                        {
                            ++state->nextExpected;
                            break;
                        }
                    }
                };
                runCtx.userData = &ms;
                runCtx.callerAccess = role;

                const bool handled = reg.execute(cmd, runCtx);
                if (!handled)
                    runCtx.writeLine("Unknown command. Type 'help'.");

                if (ms.nextExpected == expBeg.size())
                    ++passed;
                else
                    ++failed;
            }

            char summary[224];
            QC::String::memset(summary, 0, sizeof(summary));
            (void)appendString(summary, sizeof(summary), "transcripttest: commands=");
            (void)appendU64Dec(summary, sizeof(summary), commands);
            (void)appendString(summary, sizeof(summary), " pass=");
            (void)appendU64Dec(summary, sizeof(summary), passed);
            (void)appendString(summary, sizeof(summary), " fail=");
            (void)appendU64Dec(summary, sizeof(summary), failed);
            (void)appendString(summary, sizeof(summary), " skip=");
            (void)appendU64Dec(summary, sizeof(summary), skipped);
            ctx.writeLine(summary);

            if (failed == 0)
                ctx.writeLine("transcripttest: PASS");
            else
                ctx.writeLine("transcripttest: FAIL");
            return true;
        }

        static QQ::TaskResult memoTestFnSmall(void *, void *)
        {
            QQ::TaskResult r{};
            r.success = true;
            r.value = 1234;
            r.data = nullptr;
            r.dataSize = 0;
            return r;
        }
        
        static QC::u8 g_memoTestBigBlob[512];
        
        static QQ::TaskResult memoTestFnBig(void *, void *)
        {
            for (QC::usize i = 0; i < sizeof(g_memoTestBigBlob); ++i)
                g_memoTestBigBlob[i] = (QC::u8)(i & 0xFF);
            
            QQ::TaskResult r{};
            r.success = true;
            r.value = 0xBEEF;
            r.data = g_memoTestBigBlob;
            r.dataSize = sizeof(g_memoTestBigBlob);
            return r;
        }

        static bool cmdMemoTest(const char *, const QC::Cmd::Context &ctx, void *)
        {
            ctx.writeLine("memotest: enabling memoization + submitting same cached task twice");
            auto &ex = QQ::Executor::instance();
            ex.setMemoizationEnabled(true);
            ex.setMemoizationAllowlistEnabled(false);

            static QC::u8 argBuf[128];
            QC::usize wrote = 0;
            const char payload[] = "hello";
            if (!QC::CanonicalArgs::build(argBuf, sizeof(argBuf), 1, 1, payload, sizeof(payload) - 1, wrote))
            {
                ctx.writeLine("memotest: failed to build canonical args");
                return true;
            }

            const QC::u64 h0 = ex.memoizationHits();
            const QC::u64 m0 = ex.memoizationMisses();
            const QQ::TaskId idA = ex.submitWithOriginAndArgSizeCached("memo_small_a", "ui", "desktop", &memoTestFnSmall, nullptr, argBuf, wrote);
            const QQ::TaskId idB = ex.submitWithOriginAndArgSizeCached("memo_small_b", "ui", "desktop", &memoTestFnSmall, nullptr, argBuf, wrote);
            {
                char ids[128];
                QC::String::memset(ids, 0, sizeof(ids));
                (void)appendString(ids, sizeof(ids), "memotest: small ids=");
                (void)appendU64Dec(ids, sizeof(ids), (QC::u64)idA);
                (void)appendString(ids, sizeof(ids), ",");
                (void)appendU64Dec(ids, sizeof(ids), (QC::u64)idB);
                ctx.writeLine(ids);
            }

            char deltaA[192];
            QC::String::memset(deltaA, 0, sizeof(deltaA));
            (void)appendString(deltaA, sizeof(deltaA), "memotest: small delta hits=");
            (void)appendU64Dec(deltaA, sizeof(deltaA), ex.memoizationHits() - h0);
            (void)appendString(deltaA, sizeof(deltaA), " misses=");
            (void)appendU64Dec(deltaA, sizeof(deltaA), ex.memoizationMisses() - m0);
            (void)appendString(deltaA, sizeof(deltaA), " refused=");
            (void)appendU64Dec(deltaA, sizeof(deltaA), ex.memoizationRefused());
            ctx.writeLine(deltaA);

            // Case B: oversized blob should be refused (no caching)

            QC::usize wrote2 = 0;
            if (!QC::CanonicalArgs::build(argBuf, sizeof(argBuf), 2, 1, payload, sizeof(payload) - 1, wrote2))
            {
                ctx.writeLine("memotest: failed to build canonical args (big)");
                return true;
            }

            const QC::u64 h1 = ex.memoizationHits();
            const QC::u64 m1 = ex.memoizationMisses();
            const QQ::TaskId idC = ex.submitWithOriginAndArgSizeCached("memo_big_a", "ui", "desktop", &memoTestFnBig, nullptr, argBuf, wrote2);
            const QQ::TaskId idD = ex.submitWithOriginAndArgSizeCached("memo_big_b", "ui", "desktop", &memoTestFnBig, nullptr, argBuf, wrote2);
            {
                char ids[128];
                QC::String::memset(ids, 0, sizeof(ids));
                (void)appendString(ids, sizeof(ids), "memotest: big ids=");
                (void)appendU64Dec(ids, sizeof(ids), (QC::u64)idC);
                (void)appendString(ids, sizeof(ids), ",");
                (void)appendU64Dec(ids, sizeof(ids), (QC::u64)idD);
                ctx.writeLine(ids);
            }

            char deltaB[192];
            QC::String::memset(deltaB, 0, sizeof(deltaB));
            (void)appendString(deltaB, sizeof(deltaB), "memotest: big delta hits=");
            (void)appendU64Dec(deltaB, sizeof(deltaB), ex.memoizationHits() - h1);
            (void)appendString(deltaB, sizeof(deltaB), " misses=");
            (void)appendU64Dec(deltaB, sizeof(deltaB), ex.memoizationMisses() - m1);
            (void)appendString(deltaB, sizeof(deltaB), " refused=");
            (void)appendU64Dec(deltaB, sizeof(deltaB), ex.memoizationRefused());
            (void)appendString(deltaB, sizeof(deltaB), " (expect hits=0)");
            ctx.writeLine(deltaB);

            char line[192];
            QC::String::memset(line, 0, sizeof(line));
            (void)appendString(line, sizeof(line), "memotest: hits=");
            (void)appendU64Dec(line, sizeof(line), ex.memoizationHits());
            (void)appendString(line, sizeof(line), " misses=");
            (void)appendU64Dec(line, sizeof(line), ex.memoizationMisses());
            (void)appendString(line, sizeof(line), " refused=");
            (void)appendU64Dec(line, sizeof(line), ex.memoizationRefused());
            ctx.writeLine(line);
            return true;
        }

        static bool cmdTaskLs(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            QC::u64 n = 20;
            const char *a = args;
            while (a && (*a == ' ' || *a == '\t'))
                ++a;
            if (a && *a)
            {
                auto parseU64 = [](const char *s, QC::u64 &out) -> bool {
                    if (!s)
                        return false;
                    while (*s == ' ' || *s == '\t')
                        ++s;
                    if (*s < '0' || *s > '9')
                        return false;
                    QC::u64 v = 0;
                    while (*s >= '0' && *s <= '9')
                    {
                        v = (v * 10) + (QC::u64)(*s - '0');
                        ++s;
                    }
                    out = v;
                    return true;
                };
                (void)parseU64(a, n);
                if (n == 0)
                    n = 1;
                if (n > 100)
                    n = 100;
            }

            auto &ex = QQ::Executor::instance();
            {
                char head[192];
                QC::String::memset(head, 0, sizeof(head));
                (void)appendString(head, sizeof(head), "taskls: recent tasks (count=");
                (void)appendU64Dec(head, sizeof(head), ex.recentTaskIdCount());
                (void)appendString(head, sizeof(head), " nextId=");
                (void)appendU64Dec(head, sizeof(head), (QC::u64)ex.nextTaskIdForDebug());
                (void)appendString(head, sizeof(head), ")");
                ctx.writeLine(head);
            }

            const QC::usize avail = ex.recentTaskIdCount();
            QC::u64 printed = 0;
            for (QC::usize i = 0; i < avail && printed < n; ++i)
            {
                const QQ::TaskId id = ex.recentTaskIdAt(i);
                if (id == QQ::INVALID_TASK)
                    continue;
                const QQ::TaskDescriptor *td = ex.taskDescriptor(id);
                if (!td)
                    continue;

                char sigHex[72];
                char inHex[72];
                QC::String::memset(sigHex, 0, sizeof(sigHex));
                QC::String::memset(inHex, 0, sizeof(inHex));
                (void)QC::Sha256DigestToLowerHex(td->signatureHash, sigHex, sizeof(sigHex));
                (void)QC::Sha256DigestToLowerHex(td->inputHash, inHex, sizeof(inHex));

                char line[256];
                QC::String::memset(line, 0, sizeof(line));
                (void)appendString(line, sizeof(line), "id=");
                (void)appendU64Dec(line, sizeof(line), (QC::u64)id);
                (void)appendString(line, sizeof(line), " name=");
                (void)appendString(line, sizeof(line), td->name);
                (void)appendString(line, sizeof(line), " sig=");
                (void)appendString(line, sizeof(line), sigHex);
                (void)appendString(line, sizeof(line), " in=");
                (void)appendString(line, sizeof(line), inHex);
                (void)appendString(line, sizeof(line), " prio=");
                (void)appendU64Dec(line, sizeof(line), static_cast<QC::u64>(td->priority));
                (void)appendString(line, sizeof(line), " state=");
                (void)appendString(line, sizeof(line), stateName(td->state));
                (void)appendString(line, sizeof(line), " build=");
                (void)appendU64Dec(line, sizeof(line), td->buildDurationMs);
                (void)appendString(line, sizeof(line), " qwait=");
                (void)appendU64Dec(line, sizeof(line), td->queueDelayMs);
                (void)appendString(line, sizeof(line), " weight=");
                (void)appendU64Dec(line, sizeof(line), td->weightCost);
                (void)appendString(line, sizeof(line), " exec=");
                (void)appendU64Dec(line, sizeof(line), td->executionDurationMs);
                ctx.writeLine(line);
                ++printed;
            }

            if (printed == 0)
                ctx.writeLine("taskls: (no tasks found)");

            return true;
        }

        static bool cmdTaskFlowViz(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            QC::u64 n = 20;
            const char *a = args;
            while (a && (*a == ' ' || *a == '\t'))
                ++a;
            if (a && *a)
            {
                auto parseU64 = [](const char *s, QC::u64 &out) -> bool {
                    if (!s)
                        return false;
                    while (*s == ' ' || *s == '\t')
                        ++s;
                    if (*s < '0' || *s > '9')
                        return false;
                    QC::u64 v = 0;
                    while (*s >= '0' && *s <= '9')
                    {
                        v = (v * 10) + (QC::u64)(*s - '0');
                        ++s;
                    }
                    out = v;
                    return true;
                };
                (void)parseU64(a, n);
                if (n == 0)
                    n = 1;
                if (n > 100)
                    n = 100;
            }

            auto &ex = QQ::Executor::instance();
            const QC::usize avail = ex.recentTaskIdCount();
            if (avail == 0)
            {
                ctx.writeLine("taskflowviz: (no tasks found)");
                return true;
            }

            QC::Vector<QQ::TaskId> ids;
            for (QC::usize i = 0; i < avail && ids.size() < n; ++i)
            {
                const QQ::TaskId id = ex.recentTaskIdAt(i);
                if (id != QQ::INVALID_TASK)
                    ids.push_back(id);
            }

            if (ids.empty())
            {
                ctx.writeLine("taskflowviz: (no tasks found)");
                return true;
            }

            ctx.writeLine("taskflowviz: mermaid");
            ctx.writeLine("graph LR");

            for (QC::usize i = 0; i < ids.size(); ++i)
            {
                const QQ::TaskDescriptor *td = ex.taskDescriptor(ids[i]);
                if (!td)
                    continue;

                char node[256];
                QC::String::memset(node, 0, sizeof(node));
                (void)appendString(node, sizeof(node), "  T");
                (void)appendU64Dec(node, sizeof(node), static_cast<QC::u64>(ids[i]));
                (void)appendString(node, sizeof(node), "[\"");
                (void)appendU64Dec(node, sizeof(node), static_cast<QC::u64>(ids[i]));
                (void)appendString(node, sizeof(node), ":");
                (void)appendString(node, sizeof(node), td->name[0] ? td->name : "task");
                (void)appendString(node, sizeof(node), " | p=");
                (void)appendU64Dec(node, sizeof(node), static_cast<QC::u64>(td->priority));
                (void)appendString(node, sizeof(node), "\"]");
                ctx.writeLine(node);
            }

            auto inSet = [&](QQ::TaskId id) -> bool {
                for (QC::usize i = 0; i < ids.size(); ++i)
                    if (ids[i] == id)
                        return true;
                return false;
            };

            for (QC::usize i = 0; i < ids.size(); ++i)
            {
                const QQ::TaskDescriptor *td = ex.taskDescriptor(ids[i]);
                if (!td)
                    continue;
                for (QC::usize d = 0; d < td->dependencies.size(); ++d)
                {
                    const QQ::TaskId depId = td->dependencies[d].taskId;
                    if (depId == QQ::INVALID_TASK || !inSet(depId))
                        continue;

                    char edge[128];
                    QC::String::memset(edge, 0, sizeof(edge));
                    (void)appendString(edge, sizeof(edge), "  T");
                    (void)appendU64Dec(edge, sizeof(edge), static_cast<QC::u64>(depId));
                    (void)appendString(edge, sizeof(edge), " --> T");
                    (void)appendU64Dec(edge, sizeof(edge), static_cast<QC::u64>(ids[i]));
                    ctx.writeLine(edge);
                }
            }

            return true;
        }

        static bool cmdBevDump(const char *, const QC::Cmd::Context &ctx, void *)
        {
            const QC::usize total = QK::Boot::Events::Count();
            if (total == 0)
            {
                ctx.writeLine("bevdump: (empty)");
                return true;
            }

            char header[128];
            QC::String::memset(header, 0, sizeof(header));
            (void)appendString(header, sizeof(header), "BootEventLog: count=");
            (void)appendU64Dec(header, sizeof(header), static_cast<QC::u64>(total));
            ctx.writeLine(header);

            QK::Boot::Events::Record recs[8] = {};
            QC::usize offset = 0;
            while (offset < total)
            {
                const QC::usize n = QK::Boot::Events::CopyOut(offset, recs, sizeof(recs) / sizeof(recs[0]));
                if (n == 0)
                    break;
                offset += n;

                for (QC::usize i = 0; i < n; ++i)
                {
                    const auto &r = recs[i];

                    char line[512];
                    QC::String::memset(line, 0, sizeof(line));
                    (void)appendString(line, sizeof(line), "EV seq=");
                    (void)appendU64Dec(line, sizeof(line), static_cast<QC::u64>(r.seq));
                    (void)appendString(line, sizeof(line), " t_ms=");
                    (void)appendU64Dec(line, sizeof(line), r.t_ms);
                    (void)appendString(line, sizeof(line), " stage=");
                    (void)appendString(line, sizeof(line), r.stage[0] ? r.stage : "(none)");
                    (void)appendString(line, sizeof(line), " type=");
                    (void)appendString(line, sizeof(line), r.type[0] ? r.type : "(none)");
                    if (r.details[0])
                    {
                        (void)appendString(line, sizeof(line), " ");
                        (void)appendString(line, sizeof(line), r.details);
                    }
                    ctx.writeLine(line);
                }
            }

            return true;
        }

    } // namespace

    void initSession(Session &session)
    {
        QC::String::memset(session.cwd, 0, sizeof(session.cwd));
        QC::String::strncpy(session.cwd, "/", sizeof(session.cwd) - 1);
        session.cwd[sizeof(session.cwd) - 1] = '\0';
    }

    void registerMvpCommands()
    {
        static bool registered = false;
        if (registered)
            return;

        // Ensure the MVP has a working directory even if callers never create a per-terminal session.
        (void)sessionFrom();

        // Split scaffolds keep this registrar as the central wiring point.
        QK::CmdCenter::Auth::touch();
        QK::CmdCenter::Parse::touch();
        QK::CmdCenter::PathFs::touch();
        QK::CmdCenter::Builtins::touch();
        QK::CmdCenter::DebugTest::touch();
        QK::CmdCenter::Net::touch();

        auto &reg = QC::Cmd::Registry::instance();
        (void)reg.registerCommandExAccess("help", QC::Cmd::AccessLevel::Everyone, &cmdHelp, nullptr, "Show available commands (help [cmd])");
        (void)reg.registerCommandExAccess("video", QC::Cmd::AccessLevel::User, &cmdVideo, nullptr, "Show or reset video/compositor stats (video <stats|reset>)");
        (void)reg.registerCommandExAccess("whoami", QC::Cmd::AccessLevel::Everyone, &cmdWhoami, nullptr, "Show current access role");
        (void)reg.registerCommandExAccess("echo", QC::Cmd::AccessLevel::User, &cmdEcho, nullptr, "Echo text (redirection requires admin)");
        (void)reg.registerCommandExAccess("clear", QC::Cmd::AccessLevel::User, &cmdClear, nullptr, "Clear terminal output");
        (void)reg.registerCommandExAccess("pwd", QC::Cmd::AccessLevel::User, &cmdPwd, nullptr, "Print working directory");
        (void)reg.registerCommandExAccess("cd", QC::Cmd::AccessLevel::User, &cmdCd, nullptr, "Change working directory (cd <path>)");
        (void)reg.registerCommandExAccess("ls", QC::Cmd::AccessLevel::User, &cmdLs, nullptr, "List directory contents (ls [path])");
        (void)reg.registerCommandExAccess("cat", QC::Cmd::AccessLevel::User, &cmdCat, nullptr, "Print file contents (cat <path>)");
        (void)reg.registerCommandExAccess("stat", QC::Cmd::AccessLevel::User, &cmdStat, nullptr, "Show file metadata via VFS stat API (stat <path>)");
        (void)reg.registerCommandExAccess("sync", QC::Cmd::AccessLevel::Admin, &cmdSync, nullptr, "Flush mounted filesystems (sync)");
        (void)reg.registerCommandExAccess("mount", QC::Cmd::AccessLevel::Admin, &cmdMount, nullptr, "Mount registered volumes (mount [list|all|<volume>])");
        (void)reg.registerCommandExAccess("umount", QC::Cmd::AccessLevel::Admin, &cmdUmount, nullptr, "Unmount volume or mount path (umount <volume|mount_path>)");
        (void)reg.registerCommandExAccess("fstab", QC::Cmd::AccessLevel::Admin, &cmdFstab, nullptr, "Persist auto-mount policy (fstab list|apply|add|del <volume>)");
        (void)reg.registerCommandExAccess("todoadd", QC::Cmd::AccessLevel::Admin, &cmdTodoAdd, nullptr, "Append a catch-all task note (todoadd <text>)");
        (void)reg.registerCommandExAccess("touch", QC::Cmd::AccessLevel::Admin, &cmdTouch, nullptr, "Create empty file (touch <path>)");
        (void)reg.registerCommandExAccess("cp", QC::Cmd::AccessLevel::Admin, &cmdCp, nullptr, "Copy a file (cp <source> <dest>)");
        (void)reg.registerCommandExAccess("mkdir", QC::Cmd::AccessLevel::Admin, &cmdMkdir, nullptr, "Create directory (mkdir <path>)");
        (void)reg.registerCommandExAccess("rm", QC::Cmd::AccessLevel::Admin, &cmdRm, nullptr, "Remove file or directory (rm [-r] <path>)");
        (void)reg.registerCommandExAccess("del", QC::Cmd::AccessLevel::Admin, &cmdDel, nullptr, "Delete files by pattern (del *.ext | name.* | *.*)");
        (void)reg.registerCommandExAccess("source", QC::Cmd::AccessLevel::Admin, &cmdSource, nullptr, "Run script commands from file (source <file.cmd>)");
        (void)reg.registerCommandExAccess("alias", QC::Cmd::AccessLevel::Admin, &cmdAlias, nullptr, "Manage persisted command aliases (alias [name expansion])");
        (void)reg.registerCommandExAccess("unalias", QC::Cmd::AccessLevel::Admin, &cmdUnalias, nullptr, "Remove persisted alias (unalias <name>)");
        (void)reg.registerCommandExAccess("aliasreload", QC::Cmd::AccessLevel::Admin, &cmdAliasReload, nullptr, "Reload command aliases from disk");
        (void)reg.registerCommandExAccess("imgpreview", QC::Cmd::AccessLevel::User, &cmdImgPreview, nullptr, "Decode image and dump surface summary (imgpreview <path>)");
        (void)reg.registerCommandExAccess("modfetch", QC::Cmd::AccessLevel::Admin, &cmdModFetch, nullptr, "Fetch, scan, and park module binary + dependencies from catalog (modfetch <module_id>)");
        (void)reg.registerCommandExAccess("module", QC::Cmd::AccessLevel::Admin, &cmdModule, nullptr, "Manage loadable modules (module list|load [id] [sandbox]|unload)");
        (void)reg.registerCommandExAccess("depgraph", QC::Cmd::AccessLevel::User, &cmdDepGraph, nullptr, "Emit module dependency graph from catalog (depgraph <module_id>)");
        (void)reg.registerCommandExAccess("hexdump", QC::Cmd::AccessLevel::User, &cmdHexdump, nullptr, "Hex dump a file (hexdump <path> [max_bytes])");
        (void)reg.registerCommandExAccess("shutdown", QC::Cmd::AccessLevel::Admin, &cmdShutdown, nullptr, "Request shutdown or show status (shutdown [status])");

        // Boot/config helpers.
        (void)reg.registerCommandExAccess("tier", QC::Cmd::AccessLevel::User, &cmdTier, nullptr, "Show active config tier + staged early modules");
        (void)reg.registerCommandExAccess("recover", QC::Cmd::AccessLevel::System, &cmdRecover, nullptr, "Guided restore from GOLDEN to PROD (/system or /PROD) (recover config|desktop|services)");
        (void)reg.registerCommandExAccess("validate", QC::Cmd::AccessLevel::User, &cmdValidate, nullptr, "Validate key configs (validate [all|config|desktop|services])");
        (void)reg.registerCommandExAccess("reboot", QC::Cmd::AccessLevel::System, &cmdReboot, nullptr, "Reboot immediately (reboot now)");
        (void)reg.registerCommandExAccess("showmode", QC::Cmd::AccessLevel::User, &cmdShowMode, nullptr, "Show active startup mode (showmode)");
        (void)reg.registerCommandExAccess("mousespeed", QC::Cmd::AccessLevel::User, &cmdMouseSpeed, nullptr, "Show/set mouse sensitivity percent (mousespeed [show|<percent>|persist <percent>])");
        (void)reg.registerCommandExAccess("mousecfg", QC::Cmd::AccessLevel::User, &cmdMouseCfg, nullptr, "Show/set pointer behavior (mousecfg [show|<usb|ps2|wheel|invertwheel> <value>|persist <...>])");
        (void)reg.registerCommandExAccess("keyrepeat", QC::Cmd::AccessLevel::User, &cmdKeyRepeat, nullptr, "Show/set keyboard repeat timing (keyrepeat [show|<delay_ms> <interval_ms>|persist <delay_ms> <interval_ms>])");
        (void)reg.registerCommandExAccess("setmode", QC::Cmd::AccessLevel::Admin, &cmdSetMode, nullptr, "Persist startup mode to startup.cfg (setmode <DESKTOP|TERMINAL|SAFE>)");
        (void)reg.registerCommandExAccess("startx", QC::Cmd::AccessLevel::Admin, &cmdStartx, nullptr, "Set desktop startup mode for next boot (startx)");
        (void)reg.registerCommandExAccess("stopx", QC::Cmd::AccessLevel::Admin, &cmdStopx, nullptr, "Stop desktop and return to console-only mode");
        (void)reg.registerCommandExAccess("bootlog", QC::Cmd::AccessLevel::User, &cmdBootLog, nullptr, "Dump/export captured boot log output (bootlog [tail [lines]|export <auto|system|shared|usb> [ephemeral-ok]])");
        (void)reg.registerCommandExAccess("bootmodules", QC::Cmd::AccessLevel::Admin, &cmdBootModules, nullptr, "Dump early module trust metadata (role/status/hash/signature)");
        (void)reg.registerCommandExAccess("flowtest", QC::Cmd::AccessLevel::Admin, &cmdFlowTest, nullptr, "Smoke test Security Center flow policy (allow/delay/suspend/cancel)");
        (void)reg.registerCommandExAccess("flowcontrol", QC::Cmd::AccessLevel::SysAdmin, &cmdFlowControl, nullptr, "Control Security Center flow enforcement (status|bypass|enforce)");
        (void)reg.registerCommandExAccess("canonargtest", QC::Cmd::AccessLevel::Admin, &cmdCanonicalArgTest, nullptr, "Submit tasks with canonical args and print hashes");
        (void)reg.registerCommandExAccess("taskls", QC::Cmd::AccessLevel::User, &cmdTaskLs, nullptr, "List recent tasks (taskls [N])");
        (void)reg.registerCommandExAccess("taskflowviz", QC::Cmd::AccessLevel::User, &cmdTaskFlowViz, nullptr, "Emit Mermaid graph for recent task dependencies (taskflowviz [N])");
        (void)reg.registerCommandExAccess("memocache", QC::Cmd::AccessLevel::Admin, &cmdMemoCache, nullptr, "Control memoization cache (status|on|off|clear)");
        (void)reg.registerCommandExAccess("memoallow", QC::Cmd::AccessLevel::SysAdmin, &cmdMemoAllow, nullptr, "Control memoization allowlist (status|on|off|clear|add|del)");
        (void)reg.registerCommandExAccess("airuntime", QC::Cmd::AccessLevel::Admin, &cmdAiruntime, nullptr, "AI runtime persistence (status|load|save|clear)");
        (void)reg.registerCommandExAccess("transcripttest", QC::Cmd::AccessLevel::Admin, &cmdTranscriptTest, nullptr, "Replay transcript commands through handlers (transcripttest <path> [unsafe])");
        (void)reg.registerCommandExAccess("memotest", QC::Cmd::AccessLevel::Admin, &cmdMemoTest, nullptr, "Submit cached tasks twice to demonstrate a memoization hit");
        (void)reg.registerCommandExAccess("bevdump", QC::Cmd::AccessLevel::Admin, &cmdBevDump, nullptr, "Dump boot event log (structured events)");
        (void)reg.registerCommandExAccess("scdumpownercred", QC::Cmd::AccessLevel::SysAdmin, &cmdScDumpOwnerCred, nullptr, "Dump Owner credential blob as hex for ramdisk seeding (scdumpownercred [raw|plain])");
        (void)reg.registerCommandExAccess("ownerlogs", QC::Cmd::AccessLevel::Admin, &cmdOwnerLogs, nullptr, "Owner-gated audit view with paging/redaction (ownerlogs [N|size=N] [page=N] present)");
        (void)reg.registerCommandExAccess("sys_audit_view", QC::Cmd::AccessLevel::SysAdmin, &cmdSysAuditView, nullptr, "Owner-gated audit view via SC dispatch (sys_audit_view [N|size=N] [page=N] present)");
        (void)reg.registerCommandExAccess("sys_audit_export", QC::Cmd::AccessLevel::SysAdmin, &cmdSysAuditExport, nullptr, "Export audit events (sys_audit_export <path|auto|system|shared|usb> present [ephemeral-ok])");
        (void)reg.registerCommandExAccess("sys_exec_request", QC::Cmd::AccessLevel::SysAdmin, &cmdSysExecRequest, nullptr, "Submit SC execution request (sys_exec_request <request_text>)");
        (void)reg.registerCommandExAccess("sys_rotate_sst", QC::Cmd::AccessLevel::SysAdmin, &cmdSysRotateSst, nullptr, "Request SST rotation (sys_rotate_sst present)");
        (void)reg.registerCommandExAccess("sys_trust_check", QC::Cmd::AccessLevel::SysAdmin, &cmdSysTrustCheck, nullptr, "Run trust gate check through SC dispatch");
        (void)reg.registerCommandExAccess("sys_update_verify", QC::Cmd::AccessLevel::SysAdmin, &cmdSysUpdateVerify, nullptr, "Run update verify gate through SC dispatch");
        (void)reg.registerCommandExAccess("sys_user_enroll", QC::Cmd::AccessLevel::User, &cmdSysUserEnroll, nullptr, "Enroll Owner credentials (sys_user_enroll <user> <pass>)");
        (void)reg.registerCommandExAccess("sys_user_unlock", QC::Cmd::AccessLevel::User, &cmdSysUserUnlock, nullptr, "Unlock Owner session (sys_user_unlock <user> <pass>)");
        (void)reg.registerCommandExAccess("sys_user_lock", QC::Cmd::AccessLevel::User, &cmdSysUserLock, nullptr, "Lock Owner session (sys_user_lock)");
        (void)reg.registerCommandExAccess("sys_vault_request", QC::Cmd::AccessLevel::SysAdmin, &cmdSysVaultRequest, nullptr, "Submit SC vault request (sys_vault_request <request_text>)");
        (void)reg.registerCommandExAccess("db", QC::Cmd::AccessLevel::Admin, &cmdDb, nullptr, "Simple persistent key/value database (db <op> ...)");
        (void)reg.registerCommandExAccess("csql", QC::Cmd::AccessLevel::Admin, &cmdCsql, nullptr, "CQL database shell (tables/schema/rows introspection)");
        (void)reg.registerCommandEx("regdump", &cmdRegdump, nullptr, "Dump runtime registries snapshot (counts + windows + boot seed)");

        // Networking helpers (for subsystem testing).
        (void)reg.registerCommandExAccess("ip", QC::Cmd::AccessLevel::User, &cmdIp, nullptr, "Show/set IPv4 config (ip | ip set <ip> [mask] [gw] | ip dhcp [timeout_ms])");
        (void)reg.registerCommandExAccess("arp", QC::Cmd::AccessLevel::User, &cmdArp, nullptr, "List/resolve ARP (arp | arp <ip>)");
        (void)reg.registerCommandExAccess("ping", QC::Cmd::AccessLevel::User, &cmdPing, nullptr, "Send ICMP echo request (ping <ip|host> [timeout_ms])");
        (void)reg.registerCommandExAccess("udp", QC::Cmd::AccessLevel::User, &cmdUdp, nullptr, "Send UDP datagram (udp <ip|host> <port> <text>)");
        (void)reg.registerCommandExAccess("nslookup", QC::Cmd::AccessLevel::User, &cmdNslookup, nullptr, "Resolve DNS A record (nslookup <name> [timeout_ms])");
        (void)reg.registerCommandExAccess("tcpconnect", QC::Cmd::AccessLevel::User, &cmdTcpConnect, nullptr, "Test TCP connect (tcpconnect <ip|host> <port> [timeout_ms])");
        (void)reg.registerCommandExAccess("httpget", QC::Cmd::AccessLevel::User, &cmdHttpGet, nullptr, "Minimal HTTP GET over TCP (httpget <host> <path> [timeout_ms])");
        (void)reg.registerCommandExAccess("tcpdrop", QC::Cmd::AccessLevel::Admin, &cmdTcpDrop, nullptr, "Drop TCP connection by local port (tcpdrop <local_port>)");
        (void)reg.registerCommandExAccess("ports", QC::Cmd::AccessLevel::Admin, &cmdPorts, nullptr, "Port manager tools (ports list|audit|ratelimit|close-unused)");
        (void)reg.registerCommandExAccess("tcplog", QC::Cmd::AccessLevel::User, &cmdTcpLog, nullptr, "Dump recent TCP TX/RX events (tcplog)");
        (void)reg.registerCommandExAccess("netlog", QC::Cmd::AccessLevel::User, &cmdNetLog, nullptr, "Dump net state (ip + arp + tcplog)");
        (void)reg.registerCommandExAccess("tcpstate", QC::Cmd::AccessLevel::User, &cmdTcpState, nullptr, "Dump active TCP connections (tcpstate)");
        (void)reg.registerCommandExAccess("netstat", QC::Cmd::AccessLevel::User, &cmdNetStat, nullptr, "Dump net summary (ip + arp + tcpstate)");
        (void)reg.registerCommandExAccess("netstart", QC::Cmd::AccessLevel::User, &cmdNetStat, nullptr, "Dump net summary (alias of netstat)");

        // Usage/schema metadata for auto-help and argument validation.
        (void)reg.setCommandMetadata("module", "module <list|load|unload> [module_id] [sandbox]", "subcmd:string module_id?:string mode?:string", 1, 3, true);
        (void)reg.setCommandMetadata("sys_audit_view", "sys_audit_view [N|size=N] [page=N] present", "lines_or_size?:string page?:string presence:string", 1, 3, true);
        (void)reg.setCommandMetadata("sys_audit_export", "sys_audit_export <path|auto|system|shared|usb> present [ephemeral-ok]", "target_or_path:string presence:string override?:string", 2, 3, true);
        (void)reg.setCommandMetadata("bootlog", "bootlog [tail [lines]|export <auto|system|shared|usb> [ephemeral-ok]]", "mode?:string arg1?:string arg2?:string", 0, 4, true);
        (void)reg.setCommandMetadata("sys_exec_request", "sys_exec_request <request_text>", "request:string", 1, 16, true);
        (void)reg.setCommandMetadata("sys_rotate_sst", "sys_rotate_sst present", "presence:string", 1, 1, true);
        (void)reg.setCommandMetadata("sys_trust_check", "sys_trust_check", "none", 0, 0, true);
        (void)reg.setCommandMetadata("sys_update_verify", "sys_update_verify [payload]", "payload?:string", 0, 16, true);
        (void)reg.setCommandMetadata("sys_vault_request", "sys_vault_request <request_text>", "request:string", 1, 16, true);
        (void)reg.setCommandMetadata("db", "db <status|list|get|set|del|save|reload> ...", "op:string key?:string value?:string", 1, 16, true);
        (void)reg.setCommandMetadata("csql", "csql <status|open|create|close|show|exec> ...", "op:string arg1?:string arg2?:string arg3?:string", 1, 16, true);
        (void)reg.setCommandMetadata("video", "video <stats|reset>", "op:string", 1, 1, true);
        (void)reg.setCommandMetadata("ports", "ports <list|audit|ratelimit|close-unused>", "op:string", 1, 1, true);
        (void)reg.setCommandMetadata("sync", "sync", "none", 0, 0, true);
        (void)reg.setCommandMetadata("clear", "clear", "none", 0, 0, true);
        (void)reg.setCommandMetadata("shutdown", "shutdown [status]", "op?:string", 0, 1, true);
        (void)reg.setCommandMetadata("mount", "mount [list|all|volume]", "op?:string", 0, 1, true);
        (void)reg.setCommandMetadata("umount", "umount <volume|mount_path>", "target:string", 1, 1, true);
        (void)reg.setCommandMetadata("fstab", "fstab <list|apply|add|del> [volume]", "op:string volume?:string", 1, 2, true);
        (void)reg.setCommandMetadata("todoadd", "todoadd <note text>", "note:string", 1, 32, true);
        (void)reg.setCommandMetadata("cp", "cp <source> <dest>", "source:string dest:string", 2, 2, true);
        (void)reg.setCommandMetadata("mousespeed", "mousespeed [show|<percent>|persist <percent>]", "op?:string percent?:u32", 0, 2, true);
        (void)reg.setCommandMetadata("mousecfg", "mousecfg [show|<usb|ps2|wheel|invertwheel> <value>|persist <...>]", "op?:string field?:string value?:string", 0, 3, true);
        (void)reg.setCommandMetadata("keyrepeat", "keyrepeat [show|<delay_ms> <interval_ms>|persist <delay_ms> <interval_ms>]", "op?:string delay_ms?:u32 interval_ms?:u32", 0, 3, true);
        (void)reg.setCommandMetadata("startx", "startx", "none", 0, 0, true);

        // Best-effort alias map restore; empty/missing file is allowed.
        (void)loadAliasMap(nullptr, false);

        registered = true;
    }

    void setIpcHook(IpcHookFn hook, void *userData)
    {
        g_ipcHook = hook;
        g_ipcHookUser = userData;
    }

    bool executePacket(const CommandPacket &packet, const QC::Cmd::Context &ctx)
    {
        if (packet.line[0] == '\0')
            return false;

        auto startsWith = [](const char *s, const char *prefix) -> bool {
            if (!s || !prefix)
                return false;
            while (*prefix)
            {
                if (*s++ != *prefix++)
                    return false;
            }
            return true;
        };

        auto tokenEq = [](const char *a, const char *b) -> bool {
            if (!a || !b)
                return false;
            while (*a && *b && !isSpace(*a) && !isSpace(*b))
            {
                char ca = *a;
                char cb = *b;
                if (ca >= 'A' && ca <= 'Z')
                    ca = static_cast<char>(ca - 'A' + 'a');
                if (cb >= 'A' && cb <= 'Z')
                    cb = static_cast<char>(cb - 'A' + 'a');
                if (ca != cb)
                    return false;
                ++a;
                ++b;
            }
            return (*b == '\0') && (*a == '\0' || isSpace(*a));
        };

        const bool appOrigin = packet.origin && (streqIgnoreCase(packet.origin, "app") || streqIgnoreCase(packet.origin, "desktop-app"));
        if (appOrigin)
        {
            const char *line = skipSpaces(packet.line);
            if (startsWith(line, "sys_") ||
                tokenEq(line, "shutdown") ||
                tokenEq(line, "reboot") ||
                tokenEq(line, "recover") ||
                tokenEq(line, "setmode") ||
                tokenEq(line, "ports") ||
                tokenEq(line, "tcpdrop"))
            {
                QC::Cmd::Context denied = ctx;
                denied.callerAccess = packet.callerAccess;
                denied.writeLine("execution guard: command denied for app origin");
                return true;
            }
        }

        if (g_ipcHook)
            g_ipcHook(packet, g_ipcHookUser);

        QC::Cmd::Context effective = ctx;
        effective.callerAccess = packet.callerAccess;
        return QC::Cmd::Registry::instance().execute(packet.line, effective);
    }

}
