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
#include "QKBootEventLog.h"
#include "QKRuntimeRegistries.h"

#include "QKTime.h"
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
                }
                else
                {
                    // It's a path; read optional timeout next.
                    QC::String::strncpy(path, tok, sizeof(path) - 1);
                    path[sizeof(path) - 1] = '\0';

                    if (p && *p)
                    {
                        p = skipSpaces(p);
                        QC::u32 tv = 0;
                        if (!parseU32(p, tv) || tv < 100 || tv > 60000)
                        {
                            ctx.writeLine("httpget: usage: httpget <host> [path] [timeout_ms 100..60000]");
                            return true;
                        }
                        timeoutMs = tv;
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
        (void)reg.registerCommandEx("help", &cmdHelp, nullptr, "Show available commands (help [cmd])");
        (void)reg.registerCommandEx("echo", &cmdEcho, nullptr, "Echo text");
        (void)reg.registerCommandEx("pwd", &cmdPwd, nullptr, "Print working directory");
        (void)reg.registerCommandEx("cd", &cmdCd, nullptr, "Change working directory (cd <path>)");
        (void)reg.registerCommandEx("ls", &cmdLs, nullptr, "List directory contents (ls [path])");
        (void)reg.registerCommandEx("cat", &cmdCat, nullptr, "Print file contents (cat <path>)");
        (void)reg.registerCommandExAccess("shutdown", QC::Cmd::AccessLevel::Admin, &cmdShutdown, nullptr, "Request shutdown");

        // Boot/config helpers.
        (void)reg.registerCommandEx("tier", &cmdTier, nullptr, "Show active config tier + staged early modules");
        (void)reg.registerCommandEx("bootlog", &cmdBootLog, nullptr, "Dump captured boot log output");
        (void)reg.registerCommandExAccess("bevdump", QC::Cmd::AccessLevel::Admin, &cmdBevDump, nullptr, "Dump boot event log (structured events)");
        (void)reg.registerCommandEx("regdump", &cmdRegdump, nullptr, "Dump runtime registries snapshot (counts + windows + boot seed)");

        // Networking helpers (for subsystem testing).
        (void)reg.registerCommandEx("ip", &cmdIp, nullptr, "Show/set IPv4 config (ip | ip set <ip> [mask] [gw] | ip dhcp [timeout_ms])");
        (void)reg.registerCommandEx("arp", &cmdArp, nullptr, "List/resolve ARP (arp | arp <ip>)");
        (void)reg.registerCommandEx("ping", &cmdPing, nullptr, "Send ICMP echo request (ping <ip|host> [timeout_ms])");
        (void)reg.registerCommandEx("udp", &cmdUdp, nullptr, "Send UDP datagram (udp <ip|host> <port> <text>)");
        (void)reg.registerCommandEx("nslookup", &cmdNslookup, nullptr, "Resolve DNS A record (nslookup <name> [timeout_ms])");
        (void)reg.registerCommandEx("tcpconnect", &cmdTcpConnect, nullptr, "Test TCP connect (tcpconnect <ip|host> <port> [timeout_ms])");
        (void)reg.registerCommandEx("httpget", &cmdHttpGet, nullptr, "Minimal HTTP GET over TCP (httpget <host> <path> [timeout_ms])");
        (void)reg.registerCommandEx("tcplog", &cmdTcpLog, nullptr, "Dump recent TCP TX/RX events (tcplog)");

        registered = true;
    }

}
