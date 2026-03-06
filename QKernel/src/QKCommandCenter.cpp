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
#include "QKBootLog.h"

#include "QNetStack.h"
#include "QNetIP.h"
#include "QNetEthernet.h"
#include "QNetUDP.h"

namespace QK::CmdCenter
{
    namespace
    {
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

        static void ipv4ToString(QNet::IPv4Address addr, char *out, QC::usize outSize)
        {
            if (!out || outSize == 0)
                return;
            QC::String::memset(out, 0, outSize);

            auto appendDec = [&](QC::u32 v)
            {
                char tmp[4];
                QC::String::memset(tmp, 0, sizeof(tmp));
                int ti = 0;
                if (v == 0)
                {
                    tmp[ti++] = '0';
                }
                else
                {
                    char rev[4];
                    int ri = 0;
                    while (v > 0 && ri < 3)
                    {
                        rev[ri++] = static_cast<char>('0' + (v % 10));
                        v /= 10;
                    }
                    while (ri > 0)
                        tmp[ti++] = rev[--ri];
                }
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

                const char *desc = QC::Cmd::Registry::instance().findDescription(name);
                if (!desc)
                {
                    ctx.writeLine("help: command not found");
                    return true;
                }

                writeKeyValue(ctx, name, desc);
                return true;
            }

            ctx.writeLine("Commands:");
            auto &reg = QC::Cmd::Registry::instance();
            for (QC::usize i = 0; i < reg.commandCount(); ++i)
            {
                const char *name = reg.commandNameAt(i);
                if (!name)
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
            const char *p = args ? skipSpaces(args) : nullptr;
            ctx.writeLine((p && *p) ? p : "");
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
            }

            // show
            char addrBuf[32];
            char maskBuf[32];
            char gwBuf[32];
            char macBuf[32];
            ipv4ToString(QNet::Stack::instance().ip()->address(), addrBuf, sizeof(addrBuf));
            ipv4ToString(QNet::Stack::instance().ip()->subnetMask(), maskBuf, sizeof(maskBuf));
            ipv4ToString(QNet::Stack::instance().ip()->gateway(), gwBuf, sizeof(gwBuf));
            macToString(QNet::Stack::instance().ethernet()->macAddress(), macBuf, sizeof(macBuf));

            writeKeyValue(ctx, "ip", addrBuf);
            writeKeyValue(ctx, "mask", maskBuf);
            writeKeyValue(ctx, "gw", gwBuf);
            writeKeyValue(ctx, "mac", macBuf);
            return true;
        }

        static bool cmdPing(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            QNet::Stack::instance().initialize();
            const char *p = args ? skipSpaces(args) : nullptr;
            if (!p || *p == '\0')
            {
                ctx.writeLine("ping: usage: ping <a.b.c.d>");
                return true;
            }

            QNet::IPv4Address dest{};
            if (!parseIPv4(p, dest))
            {
                ctx.writeLine("ping: invalid address");
                return true;
            }

            const char payload[] = "qaios";
            QNet::Stack::instance().ip()->sendICMP(dest, 8, 0, payload, sizeof(payload) - 1);
            ctx.writeLine("ping: sent (no reply tracking yet)");
            return true;
        }

        static bool cmdUdp(const char *args, const QC::Cmd::Context &ctx, void *)
        {
            QNet::Stack::instance().initialize();
            const char *p = args ? skipSpaces(args) : nullptr;
            if (!p || *p == '\0')
            {
                ctx.writeLine("udp: usage: udp <a.b.c.d> <port> <text>");
                return true;
            }

            QNet::IPv4Address dest{};
            if (!parseIPv4(p, dest))
            {
                ctx.writeLine("udp: invalid address");
                return true;
            }
            while (*p && !isSpace(*p))
                ++p;
            p = skipSpaces(p);

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
        (void)reg.registerCommandEx("help", &cmdHelp, nullptr, "Show available commands (help [cmd])");
        (void)reg.registerCommandEx("echo", &cmdEcho, nullptr, "Echo text");
        (void)reg.registerCommandEx("pwd", &cmdPwd, nullptr, "Print working directory");
        (void)reg.registerCommandEx("cd", &cmdCd, nullptr, "Change working directory (cd <path>)");
        (void)reg.registerCommandEx("ls", &cmdLs, nullptr, "List directory contents (ls [path])");
        (void)reg.registerCommandEx("cat", &cmdCat, nullptr, "Print file contents (cat <path>)");
        (void)reg.registerCommandEx("shutdown", &cmdShutdown, nullptr, "Request shutdown");

        // Boot/config helpers.
        (void)reg.registerCommandEx("tier", &cmdTier, nullptr, "Show active config tier + staged early modules");
        (void)reg.registerCommandEx("bootlog", &cmdBootLog, nullptr, "Dump captured boot log output");

        // Networking helpers (for subsystem testing).
        (void)reg.registerCommandEx("ip", &cmdIp, nullptr, "Show/set IPv4 config (ip | ip set <ip> [mask] [gw])");
        (void)reg.registerCommandEx("ping", &cmdPing, nullptr, "Send ICMP echo request (ping <ip>)");
        (void)reg.registerCommandEx("udp", &cmdUdp, nullptr, "Send UDP datagram (udp <ip> <port> <text>)");

        registered = true;
    }

}
