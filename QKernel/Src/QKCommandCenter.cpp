// QKernel CommandCenter - shared command registration for terminals
// Namespace: QK::CmdCenter

#include "QKCommandCenter.h"

#include "QCCommandRegistry.h"

#include "QCString.h"

#include "QFSDirectory.h"
#include "QFSFile.h"
#include "QFSVFS.h"

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
#include "QKSystemPump.h"

#include "QNetStack.h"
#include "QNetIP.h"
#include "QNetEthernet.h"
#include "QNetUDP.h"
#include "QNetDHCP.h"
#include "QNetDNS.h"
#include "QNetTCP.h"

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
            while (*p && !isSpace(*p) && i + 1 < outSize)
                out[i++] = *p++;
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
                ctx.writeLine(line);
            }
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

            QFS::File *file = QFS::VFS::instance().open(path, mode);
            if (!file)
            {
                ctx.writeLine("echo: cannot open output file");
                return true;
            }

            if (appendMode)
                (void)file->seek(0, QFS::SeekOrigin::End);

            if (textEnd > textStart)
                (void)file->write(textStart, static_cast<QC::usize>(textEnd - textStart));
            (void)file->write("\r\n", 2);

            QFS::VFS::instance().close(file);
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

            QFS::File *file = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
            if (!file)
            {
                ctx.writeLine("cat: cannot open file");
                return true;
            }

            // Stream as lines.
            char inBuf[256];
            char lineBuf[512];
            QC::usize lineLen = 0;
            QC::String::memset(lineBuf, 0, sizeof(lineBuf));

            while (true)
            {
                QC::isize n = file->read(inBuf, sizeof(inBuf));
                if (n <= 0)
                    break;

                for (QC::isize i = 0; i < n; ++i)
                {
                    char c = inBuf[i];
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
            }

            if (lineLen > 0)
            {
                lineBuf[lineLen] = '\0';
                ctx.writeLine(lineBuf);
            }

            QFS::VFS::instance().close(file);
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

            // Create if missing; if it already exists, do not truncate.
            QFS::File *file = QFS::VFS::instance().open(path, QFS::OpenMode::Write | QFS::OpenMode::Create);
            if (!file)
            {
                ctx.writeLine("touch: cannot create file");
                return true;
            }

            QFS::VFS::instance().close(file);
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

            QFS::File *file = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
            if (!file)
            {
                ctx.writeLine("hexdump: cannot open file");
                return true;
            }

            static const char kHex[] = "0123456789abcdef";
            QC::u64 offset = 0;
            while (offset < maxBytes)
            {
                QC::u8 buf[16];
                QC::isize n = file->read(buf, sizeof(buf));
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

            QFS::VFS::instance().close(file);
            return true;
        }

        static bool cmdShutdown(const char *, const QC::Cmd::Context &ctx, void *)
        {
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
                char keepLine[96];
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

            const auto &sec = regs.securityState();
            char secLine[256];
            QC::String::memset(secLine, 0, sizeof(secLine));
            (void)appendString(secLine, sizeof(secLine), "Security: tpm=");
            (void)appendString(secLine, sizeof(secLine), sec.tpmAvailable ? "true" : "false");
            (void)appendString(secLine, sizeof(secLine), " enforce=");
            (void)appendString(secLine, sizeof(secLine), sec.enforcementEnabled ? "true" : "false");
            (void)appendString(secLine, sizeof(secLine), " measured=");
            (void)appendU64Dec(secLine, sizeof(secLine), static_cast<QC::u64>(sec.measuredArtifactCount));
            ctx.writeLine(secLine);

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

        static bool cmdBootLog(const char *, const QC::Cmd::Context &ctx, void *)
        {
            const QC::usize total = QK::Boot::Log::Size();
            if (total == 0)
            {
                ctx.writeLine("bootlog: (empty)");
                return true;
            }

            char chunk[256];
            char line[512];
            QC::usize lineLen = 0;
            QC::usize offset = 0;

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
                ctx.writeLine(line);
                ++printed;
            }

            if (printed == 0)
                ctx.writeLine("taskls: (no tasks found)");

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

        auto &reg = QC::Cmd::Registry::instance();
        (void)reg.registerCommandExAccess("help", QC::Cmd::AccessLevel::Everyone, &cmdHelp, nullptr, "Show available commands (help [cmd])");
        (void)reg.registerCommandExAccess("whoami", QC::Cmd::AccessLevel::Everyone, &cmdWhoami, nullptr, "Show current access role");
        (void)reg.registerCommandExAccess("echo", QC::Cmd::AccessLevel::User, &cmdEcho, nullptr, "Echo text (supports > and >> redirection)");
        (void)reg.registerCommandExAccess("pwd", QC::Cmd::AccessLevel::User, &cmdPwd, nullptr, "Print working directory");
        (void)reg.registerCommandExAccess("cd", QC::Cmd::AccessLevel::User, &cmdCd, nullptr, "Change working directory (cd <path>)");
        (void)reg.registerCommandExAccess("ls", QC::Cmd::AccessLevel::User, &cmdLs, nullptr, "List directory contents (ls [path])");
        (void)reg.registerCommandExAccess("cat", QC::Cmd::AccessLevel::User, &cmdCat, nullptr, "Print file contents (cat <path>)");
        (void)reg.registerCommandExAccess("touch", QC::Cmd::AccessLevel::User, &cmdTouch, nullptr, "Create empty file (touch <path>)");
        (void)reg.registerCommandExAccess("mkdir", QC::Cmd::AccessLevel::User, &cmdMkdir, nullptr, "Create directory (mkdir <path>)");
        (void)reg.registerCommandExAccess("rm", QC::Cmd::AccessLevel::User, &cmdRm, nullptr, "Remove file or directory (rm [-r] <path>)");
        (void)reg.registerCommandExAccess("del", QC::Cmd::AccessLevel::User, &cmdDel, nullptr, "Delete files by pattern (del *.ext | name.* | *.*)");
        (void)reg.registerCommandExAccess("hexdump", QC::Cmd::AccessLevel::User, &cmdHexdump, nullptr, "Hex dump a file (hexdump <path> [max_bytes])");
        (void)reg.registerCommandExAccess("shutdown", QC::Cmd::AccessLevel::Admin, &cmdShutdown, nullptr, "Request shutdown");

        // Boot/config helpers.
        (void)reg.registerCommandExAccess("tier", QC::Cmd::AccessLevel::User, &cmdTier, nullptr, "Show active config tier + staged early modules");
        (void)reg.registerCommandExAccess("bootlog", QC::Cmd::AccessLevel::Admin, &cmdBootLog, nullptr, "Dump captured boot log output");
        (void)reg.registerCommandExAccess("bootmodules", QC::Cmd::AccessLevel::Admin, &cmdBootModules, nullptr, "Dump early module trust metadata (role/status/hash/signature)");
        (void)reg.registerCommandExAccess("flowtest", QC::Cmd::AccessLevel::Admin, &cmdFlowTest, nullptr, "Smoke test Security Center flow policy (allow/delay/suspend/cancel)");
        (void)reg.registerCommandExAccess("flowcontrol", QC::Cmd::AccessLevel::SysAdmin, &cmdFlowControl, nullptr, "Control Security Center flow enforcement (status|bypass|enforce)");
        (void)reg.registerCommandExAccess("canonargtest", QC::Cmd::AccessLevel::Admin, &cmdCanonicalArgTest, nullptr, "Submit tasks with canonical args and print hashes");
        (void)reg.registerCommandExAccess("taskls", QC::Cmd::AccessLevel::User, &cmdTaskLs, nullptr, "List recent tasks (taskls [N])");
        (void)reg.registerCommandExAccess("memocache", QC::Cmd::AccessLevel::Admin, &cmdMemoCache, nullptr, "Control memoization cache (status|on|off|clear)");
        (void)reg.registerCommandExAccess("memoallow", QC::Cmd::AccessLevel::SysAdmin, &cmdMemoAllow, nullptr, "Control memoization allowlist (status|on|off|clear|add|del)");
        (void)reg.registerCommandExAccess("memotest", QC::Cmd::AccessLevel::Admin, &cmdMemoTest, nullptr, "Submit cached tasks twice to demonstrate a memoization hit");
        (void)reg.registerCommandExAccess("bevdump", QC::Cmd::AccessLevel::Admin, &cmdBevDump, nullptr, "Dump boot event log (structured events)");
        (void)reg.registerCommandExAccess("scdumpownercred", QC::Cmd::AccessLevel::SysAdmin, &cmdScDumpOwnerCred, nullptr, "Dump Owner credential blob as hex for ramdisk seeding (scdumpownercred [raw|plain])");
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
        (void)reg.registerCommandExAccess("tcplog", QC::Cmd::AccessLevel::User, &cmdTcpLog, nullptr, "Dump recent TCP TX/RX events (tcplog)");
        (void)reg.registerCommandExAccess("netlog", QC::Cmd::AccessLevel::User, &cmdNetLog, nullptr, "Dump net state (ip + arp + tcplog)");
        (void)reg.registerCommandExAccess("tcpstate", QC::Cmd::AccessLevel::User, &cmdTcpState, nullptr, "Dump active TCP connections (tcpstate)");
        (void)reg.registerCommandExAccess("netstat", QC::Cmd::AccessLevel::User, &cmdNetStat, nullptr, "Dump net summary (ip + arp + tcpstate)");
        (void)reg.registerCommandExAccess("netstart", QC::Cmd::AccessLevel::User, &cmdNetStat, nullptr, "Dump net summary (alias of netstat)");

        registered = true;
    }

}
