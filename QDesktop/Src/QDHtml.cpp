// QDesktop HTML engine implementation
// Namespace: QD::Html

#include "QDHtml.h"

#include "QCString.h"

#include "QFSVFS.h"
#include "QFSFile.h"

#include "QG/Image.h"
#include "QG/SVG.h"

#include "QKSystemPump.h"
#include "QKTime.h"

#include "QNetDNS.h"
#include "QNetStack.h"
#include "QNetTCP.h"

#include "QWWindow.h"
#include "QWControls/Containers/Panel.h"
#include "QWControls/Leaf/Button.h"
#include "QWControls/Leaf/ImageView.h"
#include "QWControls/Leaf/Label.h"
#include "QWControls/Leaf/TextBox.h"

namespace QD::Html
{
    struct ImageAsset
    {
        char *src = nullptr;
        QG::ImageSurface surface;
    };

    struct LinkTarget
    {
        char *href = nullptr;
        LinkClickHandler handler = nullptr;
        void *userData = nullptr;
    };

    namespace
    {
        static bool startsWithIgnoreCase(const char *s, const char *prefix);
        static const char *findIgnoreCase(const char *haystack, const char *needle);
        static bool parseAttrValue(const char *tagText, const char *key, char *out, QC::usize outSize);
        static bool parseHttpUrl(const char *url, char *outHost, QC::usize hostSize, char *outPath, QC::usize pathSize);

        static bool resolveHttpUrl(const char *baseUrl, const char *ref, char *out, QC::usize outSize)
        {
            if (!out || outSize == 0)
                return false;
            out[0] = '\0';

            if (!ref)
                ref = "";

            // Already absolute (or a fragment / mailto / etc): return as-is.
            if (startsWithIgnoreCase(ref, "http://") || startsWithIgnoreCase(ref, "https://") || ref[0] == '#')
            {
                QC::String::strncpy(out, ref, outSize - 1);
                out[outSize - 1] = '\0';
                return true;
            }

            // Absolute VFS path or other schemes: return as-is.
            if (ref[0] == '/')
            {
                // If base is http, interpret leading '/' as site-root path.
                if (baseUrl && startsWithIgnoreCase(baseUrl, "http://"))
                {
                    char host[256];
                    char path[256];
                    QC::String::memset(host, 0, sizeof(host));
                    QC::String::memset(path, 0, sizeof(path));
                    if (!parseHttpUrl(baseUrl, host, sizeof(host), path, sizeof(path)))
                        return false;

                    char prefix[384];
                    QC::String::memset(prefix, 0, sizeof(prefix));
                    QC::String::strncpy(prefix, "http://", sizeof(prefix) - 1);
                    QC::usize used = QC::String::strlen(prefix);
                    QC::String::strncpy(prefix + used, host, sizeof(prefix) - 1 - used);

                    QC::String::strncpy(out, prefix, outSize - 1);
                    out[outSize - 1] = '\0';
                    const QC::usize used2 = QC::String::strlen(out);
                    if (used2 + 1 < outSize)
                    {
                        QC::String::strncpy(out + used2, ref, outSize - 1 - used2);
                        out[outSize - 1] = '\0';
                    }
                    return true;
                }

                QC::String::strncpy(out, ref, outSize - 1);
                out[outSize - 1] = '\0';
                return true;
            }

            // Relative path: requires an http base URL to resolve.
            if (!baseUrl || !startsWithIgnoreCase(baseUrl, "http://"))
            {
                QC::String::strncpy(out, ref, outSize - 1);
                out[outSize - 1] = '\0';
                return true;
            }

            char host[256];
            char basePath[256];
            QC::String::memset(host, 0, sizeof(host));
            QC::String::memset(basePath, 0, sizeof(basePath));
            if (!parseHttpUrl(baseUrl, host, sizeof(host), basePath, sizeof(basePath)))
                return false;

            // Compute directory portion of basePath.
            QC::usize dirLen = QC::String::strlen(basePath);
            while (dirLen > 0 && basePath[dirLen - 1] != '/')
                --dirLen;
            if (dirLen == 0)
                dirLen = 1; // at least '/'

            char dir[256];
            QC::String::memset(dir, 0, sizeof(dir));
            QC::usize copyLen = dirLen;
            if (copyLen + 1 > sizeof(dir))
                copyLen = sizeof(dir) - 1;
            for (QC::usize i = 0; i < copyLen; ++i)
                dir[i] = basePath[i];
            dir[copyLen] = '\0';

            char prefix[384];
            QC::String::memset(prefix, 0, sizeof(prefix));
            QC::String::strncpy(prefix, "http://", sizeof(prefix) - 1);
            QC::usize used = QC::String::strlen(prefix);
            QC::String::strncpy(prefix + used, host, sizeof(prefix) - 1 - used);
            used = QC::String::strlen(prefix);
            QC::String::strncpy(prefix + used, dir, sizeof(prefix) - 1 - used);

            QC::String::strncpy(out, prefix, outSize - 1);
            out[outSize - 1] = '\0';
            const QC::usize used2 = QC::String::strlen(out);
            if (used2 + 1 < outSize)
            {
                QC::String::strncpy(out + used2, ref, outSize - 1 - used2);
                out[outSize - 1] = '\0';
            }
            return true;
        }

        static bool parseIPv4Text(const char *text, QNet::IPv4Address &out)
        {
            if (!text || !*text)
                return false;

            QC::u32 parts[4] = {0, 0, 0, 0};
            int pi = 0;
            const char *p = text;

            while (*p)
            {
                if (pi >= 4)
                    return false;

                if (*p < '0' || *p > '9')
                    return false;

                QC::u32 v = 0;
                int digits = 0;
                while (*p >= '0' && *p <= '9')
                {
                    v = v * 10u + static_cast<QC::u32>(*p - '0');
                    ++p;
                    ++digits;
                    if (digits > 3)
                        return false;
                }
                if (v > 255)
                    return false;

                parts[pi++] = v;

                if (*p == '.')
                {
                    ++p;
                    continue;
                }

                if (*p == '\0')
                    break;

                return false;
            }

            if (pi != 4)
                return false;

            out.octets[0] = static_cast<QC::u8>(parts[0]);
            out.octets[1] = static_cast<QC::u8>(parts[1]);
            out.octets[2] = static_cast<QC::u8>(parts[2]);
            out.octets[3] = static_cast<QC::u8>(parts[3]);
            return true;
        }

        static bool parseHttpUrl(const char *url, char *outHost, QC::usize hostSize, char *outPath, QC::usize pathSize)
        {
            if (!url || !outHost || !outPath || hostSize == 0 || pathSize == 0)
                return false;

            outHost[0] = '\0';
            outPath[0] = '\0';

            if (!startsWithIgnoreCase(url, "http://"))
                return false;

            const char *p = url + 7;
            if (*p == '\0')
                return false;

            // host[:port][/path]
            const char *hostStart = p;
            while (*p && *p != '/' && *p != ':' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
                ++p;

            const QC::usize hostLen = static_cast<QC::usize>(p - hostStart);
            if (hostLen == 0 || hostLen + 1 > hostSize)
                return false;

            for (QC::usize i = 0; i < hostLen; ++i)
                outHost[i] = hostStart[i];
            outHost[hostLen] = '\0';

            // Optional :port (ignored; only port 80 supported for now)
            if (*p == ':')
            {
                ++p;
                while (*p && *p != '/' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
                {
                    if (*p < '0' || *p > '9')
                        return false;
                    ++p;
                }
            }

            if (*p == '/')
            {
                const char *pathStart = p;
                QC::usize pathLen = 0;
                while (pathStart[pathLen] && pathStart[pathLen] != ' ' && pathStart[pathLen] != '\t' && pathStart[pathLen] != '\r' && pathStart[pathLen] != '\n')
                    ++pathLen;
                if (pathLen + 1 > pathSize)
                    pathLen = pathSize - 1;
                for (QC::usize i = 0; i < pathLen; ++i)
                    outPath[i] = pathStart[i];
                outPath[pathLen] = '\0';
            }
            else
            {
                QC::String::strncpy(outPath, "/", pathSize - 1);
                outPath[pathSize - 1] = '\0';
            }

            return true;
        }

        static bool decodeChunkedBody(const QC::Vector<QC::u8> &resp, QC::usize bodyStart, QC::Vector<QC::u8> &outBody)
        {
            outBody.clear();
            QC::usize p = bodyStart;

            auto hexVal = [](QC::u8 c) -> int {
                if (c >= '0' && c <= '9')
                    return static_cast<int>(c - '0');
                if (c >= 'a' && c <= 'f')
                    return static_cast<int>(c - 'a' + 10);
                if (c >= 'A' && c <= 'F')
                    return static_cast<int>(c - 'A' + 10);
                return -1;
            };

            while (p < resp.size())
            {
                // Parse chunk size in hex until CRLF.
                QC::u32 sz = 0;
                bool any = false;
                while (p < resp.size())
                {
                    const QC::u8 c = resp[p];
                    if (c == '\r')
                        break;
                    if (c == '\n')
                        break;
                    if (c == ';')
                    {
                        // Skip optional chunk extensions until CRLF.
                        while (p < resp.size() && resp[p] != '\n')
                            ++p;
                        break;
                    }
                    const int hv = hexVal(c);
                    if (hv < 0)
                        return false;
                    sz = (sz << 4) + static_cast<QC::u32>(hv);
                    any = true;
                    ++p;
                }
                if (!any)
                    return false;

                // Consume line ending
                if (p < resp.size() && resp[p] == '\r')
                    ++p;
                if (p < resp.size() && resp[p] == '\n')
                    ++p;

                if (sz == 0)
                    break;

                if (p + sz > resp.size())
                    return false;

                const QC::usize old = outBody.size();
                outBody.resize(old + static_cast<QC::usize>(sz));
                for (QC::usize i = 0; i < static_cast<QC::usize>(sz); ++i)
                    outBody[old + i] = resp[p + i];
                p += static_cast<QC::usize>(sz);

                // Consume trailing CRLF after chunk data
                if (p < resp.size() && resp[p] == '\r')
                    ++p;
                if (p < resp.size() && resp[p] == '\n')
                    ++p;

                if (outBody.size() > 1024 * 512)
                    return false;
            }

            return outBody.size() > 0;
        }

        static bool headerContainsIgnoreCase(const QC::Vector<QC::u8> &resp, QC::usize headerEnd, const char *needle)
        {
            if (!needle || !*needle)
                return false;
            if (headerEnd > resp.size())
                headerEnd = resp.size();

            // Naive scan: compare needle against resp as lowercase.
            for (QC::usize i = 0; i < headerEnd; ++i)
            {
                QC::usize j = 0;
                QC::usize k = i;
                while (needle[j] != '\0' && k < headerEnd)
                {
                    QC::u8 c = resp[k];
                    char n = needle[j];
                    if (c >= 'A' && c <= 'Z')
                        c = static_cast<QC::u8>(c - 'A' + 'a');
                    if (n >= 'A' && n <= 'Z')
                        n = static_cast<char>(n - 'A' + 'a');
                    if (c != static_cast<QC::u8>(n))
                        break;
                    ++j;
                    ++k;
                }
                if (needle[j] == '\0')
                    return true;
            }
            return false;
        }

        static QC::u32 parseHttpStatusCode(const QC::Vector<QC::u8> &resp)
        {
            // Expect: HTTP/1.1 200 ...\r\n
            if (resp.size() < 12)
                return 0;
            if (!(resp[0] == 'H' && resp[1] == 'T' && resp[2] == 'T' && resp[3] == 'P'))
                return 0;
            QC::usize p = 0;
            while (p < resp.size() && resp[p] != ' ' && resp[p] != '\r' && resp[p] != '\n')
                ++p;
            while (p < resp.size() && resp[p] == ' ')
                ++p;
            if (p + 2 >= resp.size())
                return 0;
            if (resp[p] < '0' || resp[p] > '9')
                return 0;
            if (resp[p + 1] < '0' || resp[p + 1] > '9')
                return 0;
            if (resp[p + 2] < '0' || resp[p + 2] > '9')
                return 0;
            return static_cast<QC::u32>((resp[p] - '0') * 100 + (resp[p + 1] - '0') * 10 + (resp[p + 2] - '0'));
        }

        static bool extractHeaderValueLocation(const QC::Vector<QC::u8> &resp, QC::usize headerEnd, char *out, QC::usize outSize)
        {
            if (!out || outSize == 0)
                return false;
            out[0] = '\0';
            if (headerEnd > resp.size())
                headerEnd = resp.size();

            const char *needle = "location:";
            for (QC::usize i = 0; i + 9 < headerEnd; ++i)
            {
                bool match = true;
                for (QC::usize j = 0; needle[j] != '\0'; ++j)
                {
                    QC::u8 c = resp[i + j];
                    char n = needle[j];
                    if (c >= 'A' && c <= 'Z')
                        c = static_cast<QC::u8>(c - 'A' + 'a');
                    if (c != static_cast<QC::u8>(n))
                    {
                        match = false;
                        break;
                    }
                }
                if (!match)
                    continue;

                QC::usize p = i + 9;
                while (p < headerEnd && (resp[p] == ' ' || resp[p] == '\t'))
                    ++p;

                QC::usize len = 0;
                while (p + len < headerEnd && resp[p + len] != '\r' && resp[p + len] != '\n')
                    ++len;
                if (len == 0)
                    return false;
                if (len + 1 > outSize)
                    len = outSize - 1;
                for (QC::usize k = 0; k < len; ++k)
                    out[k] = static_cast<char>(resp[p + k]);
                out[len] = '\0';
                return true;
            }
            return false;
        }

        static bool httpGetBodyToBuffer(const char *host, const char *path, QC::Vector<QC::u8> &outBody)
        {
            outBody.clear();

            if (!host || !*host)
                return false;
            if (!path || !*path)
                path = "/";

            if (!QK::Time::available() || !QK::System::pumpAvailable())
                return false;

            QNet::Stack::instance().initialize();

            QNet::IPv4Address dest{};
            if (!parseIPv4Text(host, dest))
            {
                auto *ip = QNet::Stack::instance().ip();
                if (!ip)
                    return false;

                const QNet::IPv4Address dnsServer = ip->dnsServer();
                if (dnsServer.value == 0)
                    return false;

                QNet::DNSClient dns;
                const QC::u16 txid = static_cast<QC::u16>(QK::Time::milliseconds() & 0xFFFFu);
                if (dns.begin(dnsServer, host, txid) != QC::Status::Success)
                    return false;

                const QC::u64 dnsDeadline = QK::Time::milliseconds() + 5000;
                bool resolved = false;
                while (QK::Time::milliseconds() < dnsDeadline)
                {
                    QK::System::pump();
                    if (dns.poll(&dest))
                    {
                        resolved = true;
                        break;
                    }
                    QK::Time::sleep(10);
                }
                if (!resolved)
                    return false;
            }

            auto *tcp = QNet::Stack::instance().tcp();
            if (!tcp)
                return false;

            const QC::u64 deadlineMs = QK::Time::milliseconds() + 7000;

            // Within the overall timeout, keep trying fresh connects.
            QNet::TCPConnection *conn = nullptr;
            while (QK::Time::milliseconds() < deadlineMs)
            {
                conn = tcp->connect(dest, 80);
                if (!conn)
                    return false;

                const QC::u64 attemptDeadline = QK::Time::milliseconds() + 3000;
                bool connected = false;
                while (QK::Time::milliseconds() < attemptDeadline)
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

                tcp->close(conn);
                conn = nullptr;
                QK::Time::sleep(50);
            }

            if (!conn || conn->state != QNet::TCPState::Established)
                return false;

            char req[768];
            QC::String::memset(req, 0, sizeof(req));
            QC::String::strncpy(req, "GET ", sizeof(req) - 1);
            QC::usize used = QC::String::strlen(req);
            if (path[0] != '/')
            {
                req[used++] = '/';
                req[used] = '\0';
            }
            QC::String::strncpy(req + used, path, sizeof(req) - 1 - used);
            used = QC::String::strlen(req);
            QC::String::strncpy(req + used, " HTTP/1.1\r\nHost: ", sizeof(req) - 1 - used);
            used = QC::String::strlen(req);
            QC::String::strncpy(req + used, host, sizeof(req) - 1 - used);
            used = QC::String::strlen(req);
            QC::String::strncpy(req + used, "\r\nUser-Agent: CitadelBrowser/0.1\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n", sizeof(req) - 1 - used);

            (void)tcp->send(conn, req, QC::String::strlen(req));

            QC::Vector<QC::u8> resp;
            resp.reserve(4096);

            QC::u8 tmp[1024];
            const QC::u64 rxDeadline = QK::Time::milliseconds() + 7000;
            while (QK::Time::milliseconds() < rxDeadline)
            {
                QK::System::pump();
                const QC::isize n = tcp->receive(conn, tmp, sizeof(tmp));
                if (n > 0)
                {
                    const QC::usize old = resp.size();
                    resp.resize(old + static_cast<QC::usize>(n));
                    for (QC::usize i = 0; i < static_cast<QC::usize>(n); ++i)
                        resp[old + i] = tmp[i];
                    if (resp.size() > 1024 * 512)
                        break;
                    continue;
                }

                if (conn->state == QNet::TCPState::Closed)
                {
                    if (conn->recvCount == 0)
                        break;
                }

                if (conn->state == QNet::TCPState::CloseWait && conn->recvCount == 0)
                    break;

                QK::Time::sleep(10);
            }

            tcp->close(conn);
            const QC::u64 closeDeadline = QK::Time::milliseconds() + 250;
            while (QK::Time::milliseconds() < closeDeadline)
            {
                QK::System::pump();
                if (conn->state == QNet::TCPState::Closed)
                    break;
                QK::Time::sleep(10);
            }
            tcp->drop(conn);

            if (resp.size() < 12)
                return false;

            // Find header end (\r\n\r\n)
            QC::usize headerEnd = static_cast<QC::usize>(-1);
            for (QC::usize i = 0; i + 3 < resp.size(); ++i)
            {
                if (resp[i] == '\r' && resp[i + 1] == '\n' && resp[i + 2] == '\r' && resp[i + 3] == '\n')
                {
                    headerEnd = i + 4;
                    break;
                }
            }
            if (headerEnd == static_cast<QC::usize>(-1) || headerEnd >= resp.size())
                return false;

            const QC::u32 status = parseHttpStatusCode(resp);
            if (status == 301 || status == 302 || status == 307 || status == 308)
            {
                char loc[384];
                QC::String::memset(loc, 0, sizeof(loc));
                if (!extractHeaderValueLocation(resp, headerEnd, loc, sizeof(loc)))
                    return false;
                if (!startsWithIgnoreCase(loc, "http://"))
                    return false;

                char host2[256];
                char path2[256];
                QC::String::memset(host2, 0, sizeof(host2));
                QC::String::memset(path2, 0, sizeof(path2));
                if (!parseHttpUrl(loc, host2, sizeof(host2), path2, sizeof(path2)))
                    return false;
                return httpGetBodyToBuffer(host2, path2, outBody);
            }

            if (status != 200)
                return false;

            if (headerContainsIgnoreCase(resp, headerEnd, "transfer-encoding: chunked"))
                return decodeChunkedBody(resp, headerEnd, outBody);

            const QC::usize bodyLen = resp.size() - headerEnd;
            outBody.resize(bodyLen);
            for (QC::usize i = 0; i < bodyLen; ++i)
                outBody[i] = resp[headerEnd + i];

            return outBody.size() > 0;
        }

        static const char *skipSpaces(const char *p)
        {
            while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
                ++p;
            return p;
        }

        static bool equalsIgnoreCase(const char *a, const char *b)
        {
            if (!a || !b)
                return false;
            while (*a && *b)
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
            return *a == '\0' && *b == '\0';
        }

        static bool parseHtmlColor(const char *s, QC::Color *out)
        {
            if (!out)
                return false;

            if (!s)
                return false;

            s = skipSpaces(s);
            if (!s || *s == '\0')
                return false;

            if (equalsIgnoreCase(s, "white"))
            {
                *out = QC::Color::white();
                return true;
            }
            if (equalsIgnoreCase(s, "black"))
            {
                *out = QC::Color::black();
                return true;
            }
            if (equalsIgnoreCase(s, "red"))
            {
                *out = QC::Color::red();
                return true;
            }
            if (equalsIgnoreCase(s, "green"))
            {
                *out = QC::Color::green();
                return true;
            }
            if (equalsIgnoreCase(s, "blue"))
            {
                *out = QC::Color::blue();
                return true;
            }
            if (equalsIgnoreCase(s, "yellow"))
            {
                *out = QC::Color::yellow();
                return true;
            }
            if (equalsIgnoreCase(s, "gray") || equalsIgnoreCase(s, "grey"))
            {
                *out = QC::Color::gray();
                return true;
            }

            // #RGB, #RRGGBB, #AARRGGBB
            if (s[0] == '#')
            {
                const QC::usize len = QC::String::strlen(s);
                auto hexNibble = [](char c) -> int {
                    if (c >= '0' && c <= '9')
                        return c - '0';
                    if (c >= 'a' && c <= 'f')
                        return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F')
                        return c - 'A' + 10;
                    return -1;
                };

                if (len == 4)
                {
                    const int r = hexNibble(s[1]);
                    const int g = hexNibble(s[2]);
                    const int b = hexNibble(s[3]);
                    if (r < 0 || g < 0 || b < 0)
                        return false;
                    *out = QC::Color(static_cast<QC::u8>((r << 4) | r),
                                     static_cast<QC::u8>((g << 4) | g),
                                     static_cast<QC::u8>((b << 4) | b),
                                     255);
                    return true;
                }

                if (len == 7 || len == 9)
                {
                    const int a0 = (len == 9) ? hexNibble(s[1]) : 15;
                    const int a1 = (len == 9) ? hexNibble(s[2]) : 15;
                    const int r0 = hexNibble(s[len == 9 ? 3 : 1]);
                    const int r1 = hexNibble(s[len == 9 ? 4 : 2]);
                    const int g0 = hexNibble(s[len == 9 ? 5 : 3]);
                    const int g1 = hexNibble(s[len == 9 ? 6 : 4]);
                    const int b0 = hexNibble(s[len == 9 ? 7 : 5]);
                    const int b1 = hexNibble(s[len == 9 ? 8 : 6]);
                    if (a0 < 0 || a1 < 0 || r0 < 0 || r1 < 0 || g0 < 0 || g1 < 0 || b0 < 0 || b1 < 0)
                        return false;
                    const QC::u8 a = static_cast<QC::u8>((a0 << 4) | a1);
                    const QC::u8 r = static_cast<QC::u8>((r0 << 4) | r1);
                    const QC::u8 g = static_cast<QC::u8>((g0 << 4) | g1);
                    const QC::u8 b = static_cast<QC::u8>((b0 << 4) | b1);
                    *out = QC::Color(r, g, b, a);
                    return true;
                }
            }

            return false;
        }

        static void preScanBodyColors(const char *htmlText, QC::Color &ioBg, QC::Color &ioText)
        {
            if (!htmlText)
                return;

            const char *body = findIgnoreCase(htmlText, "<body");
            if (!body)
                return;

            const char *end = body;
            while (*end && *end != '>')
                ++end;
            if (*end != '>')
                return;

            char tagBuf[512];
            QC::String::memset(tagBuf, 0, sizeof(tagBuf));
            const QC::usize tagLen = static_cast<QC::usize>((end - body) + 1);
            const QC::usize copyLen = (tagLen < sizeof(tagBuf) - 1) ? tagLen : (sizeof(tagBuf) - 1);
            for (QC::usize i = 0; i < copyLen; ++i)
                tagBuf[i] = body[i];
            tagBuf[copyLen] = '\0';

            char bg[64];
            char txt[64];
            QC::String::memset(bg, 0, sizeof(bg));
            QC::String::memset(txt, 0, sizeof(txt));

            (void)parseAttrValue(tagBuf, "background", bg, sizeof(bg));
            if (bg[0] == '\0')
                (void)parseAttrValue(tagBuf, "bgcolor", bg, sizeof(bg));
            (void)parseAttrValue(tagBuf, "text", txt, sizeof(txt));

            QC::Color c{};
            if (bg[0] != '\0' && parseHtmlColor(bg, &c))
                ioBg = c;
            if (txt[0] != '\0' && parseHtmlColor(txt, &c))
                ioText = c;
        }

        static bool startsWithIgnoreCase(const char *s, const char *prefix)
        {
            if (!s || !prefix)
                return false;
            while (*prefix)
            {
                char a = *s++;
                char b = *prefix++;
                if (a >= 'A' && a <= 'Z')
                    a = static_cast<char>(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z')
                    b = static_cast<char>(b - 'A' + 'a');
                if (a != b)
                    return false;
            }
            return true;
        }

        static const char *findIgnoreCase(const char *haystack, const char *needle)
        {
            if (!haystack || !needle || *needle == '\0')
                return haystack;

            for (const char *p = haystack; *p; ++p)
            {
                if (startsWithIgnoreCase(p, needle))
                    return p;
            }
            return nullptr;
        }

        static char *dupString(const char *s)
        {
            if (!s)
                s = "";
            const QC::usize len = QC::String::strlen(s);
            char *out = static_cast<char *>(operator new[](len + 1));
            for (QC::usize i = 0; i < len; ++i)
                out[i] = s[i];
            out[len] = '\0';
            return out;
        }

        static bool parseAttrValue(const char *tagText, const char *key, char *out, QC::usize outSize)
        {
            if (!tagText || !key || !out || outSize == 0)
                return false;
            out[0] = '\0';

            // Find key=
            const char *p = tagText;
            while (*p)
            {
                const char *k = findIgnoreCase(p, key);
                if (!k)
                    return false;

                // Ensure boundary: preceding char is whitespace or start.
                if (k != tagText)
                {
                    const char prev = *(k - 1);
                    if (!(prev == ' ' || prev == '\t' || prev == '\n' || prev == '\r'))
                    {
                        p = k + 1;
                        continue;
                    }
                }

                const char *eq = k + QC::String::strlen(key);
                eq = skipSpaces(eq);
                if (*eq != '=')
                {
                    p = k + 1;
                    continue;
                }
                ++eq;
                eq = skipSpaces(eq);

                char quote = 0;
                if (*eq == '"' || *eq == '\'')
                {
                    quote = *eq;
                    ++eq;
                }

                const char *vStart = eq;
                const char *vEnd = vStart;
                if (quote)
                {
                    while (*vEnd && *vEnd != quote)
                        ++vEnd;
                }
                else
                {
                    while (*vEnd && *vEnd != ' ' && *vEnd != '\t' && *vEnd != '\n' && *vEnd != '\r' && *vEnd != '>')
                        ++vEnd;
                }

                const QC::usize len = static_cast<QC::usize>(vEnd - vStart);
                const QC::usize copy = (len + 1 < outSize) ? len : (outSize - 1);
                for (QC::usize i = 0; i < copy; ++i)
                    out[i] = vStart[i];
                out[copy] = '\0';
                return out[0] != '\0';
            }

            return false;
        }

        static QC::u32 lineHeightPx(QG::IPainter *painter)
        {
            if (!painter)
                return 8;
            const QC::Size s = painter->measureText("A");
            return (s.height == 0) ? 8 : s.height;
        }

        enum class CssSelectorType : QC::u8
        {
            Element,
            Class,
            Id,
        };

        struct CssStyleProps
        {
            bool hasTextColor = false;
            QC::Color textColor{};

            bool hasBackground = false;
            QC::Color background{};

            bool hasBorderColor = false;
            QC::Color borderColor{};

            bool hasBorderWidth = false;
            QC::u32 borderWidth = 0;

            bool hasPadL = false;
            bool hasPadT = false;
            bool hasPadR = false;
            bool hasPadB = false;
            QC::u32 padL = 0, padT = 0, padR = 0, padB = 0;

            bool hasMarL = false;
            bool hasMarT = false;
            bool hasMarR = false;
            bool hasMarB = false;
            QC::u32 marL = 0, marT = 0, marR = 0, marB = 0;

            bool hasFontSizePx = false;
            QC::u32 fontSizePx = 0;
        };

        struct CssRule
        {
            CssSelectorType type = CssSelectorType::Element;
            char key[64]{};
            CssStyleProps props;
        };

        static void mergeCssProps(CssStyleProps &dst, const CssStyleProps &src)
        {
            if (src.hasTextColor)
                dst.hasTextColor = true, dst.textColor = src.textColor;
            if (src.hasBackground)
                dst.hasBackground = true, dst.background = src.background;
            if (src.hasBorderColor)
                dst.hasBorderColor = true, dst.borderColor = src.borderColor;
            if (src.hasBorderWidth)
                dst.hasBorderWidth = true, dst.borderWidth = src.borderWidth;

            if (src.hasPadL)
                dst.hasPadL = true, dst.padL = src.padL;
            if (src.hasPadT)
                dst.hasPadT = true, dst.padT = src.padT;
            if (src.hasPadR)
                dst.hasPadR = true, dst.padR = src.padR;
            if (src.hasPadB)
                dst.hasPadB = true, dst.padB = src.padB;

            if (src.hasMarL)
                dst.hasMarL = true, dst.marL = src.marL;
            if (src.hasMarT)
                dst.hasMarT = true, dst.marT = src.marT;
            if (src.hasMarR)
                dst.hasMarR = true, dst.marR = src.marR;
            if (src.hasMarB)
                dst.hasMarB = true, dst.marB = src.marB;

            if (src.hasFontSizePx)
                dst.hasFontSizePx = true, dst.fontSizePx = src.fontSizePx;
        }

        static bool classListContainsCss(const char *classList, const char *key)
        {
            if (!classList || !*classList || !key || !*key)
                return false;
            const QC::usize keyLen = QC::String::strlen(key);
            const char *s = classList;
            while (s && *s)
            {
                s = skipSpaces(s);
                if (!s || !*s)
                    break;
                const char *start = s;
                while (*s && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n')
                    ++s;
                const QC::usize len = static_cast<QC::usize>(s - start);
                if (len == keyLen && QC::String::memcmp(start, key, keyLen) == 0)
                    return true;
            }
            return false;
        }

        struct Tag
        {
            bool isClose = false;
            bool selfClose = false;
            char name[16];
            char raw[256];
        };

        class Parser
        {
        public:
            explicit Parser(const char *text)
                : m_text(text ? text : "")
            {
                m_len = QC::String::strlen(m_text);
            }

            bool eof() const { return m_pos >= m_len; }

            bool nextText(char *out, QC::usize outSize)
            {
                if (!out || outSize == 0)
                    return false;
                out[0] = '\0';

                if (eof())
                    return false;

                const char *p = m_text + m_pos;
                const char *start = p;
                while (*p && *p != '<')
                    ++p;

                m_pos = static_cast<QC::usize>(p - m_text);

                // Trim whitespace-only text.
                const char *a = start;
                const char *b = p;
                while (a < b && (*a == ' ' || *a == '\t' || *a == '\r' || *a == '\n'))
                    ++a;
                while (b > a && (*(b - 1) == ' ' || *(b - 1) == '\t' || *(b - 1) == '\r' || *(b - 1) == '\n'))
                    --b;

                if (b <= a)
                    return false;

                const QC::usize len = static_cast<QC::usize>(b - a);
                const QC::usize copy = (len + 1 < outSize) ? len : (outSize - 1);
                for (QC::usize i = 0; i < copy; ++i)
                    out[i] = a[i];
                out[copy] = '\0';
                return out[0] != '\0';
            }

            bool nextTag(Tag &out)
            {
                if (eof())
                    return false;

                // Seek '<'
                while (!eof() && m_text[m_pos] != '<')
                    ++m_pos;
                if (eof())
                    return false;

                // Read until '>'
                ++m_pos; // skip '<'
                const QC::usize start = m_pos;
                while (!eof() && m_text[m_pos] != '>')
                    ++m_pos;
                const QC::usize end = m_pos;
                if (!eof() && m_text[m_pos] == '>')
                    ++m_pos;

                QC::String::memset(out.name, 0, sizeof(out.name));
                QC::String::memset(out.raw, 0, sizeof(out.raw));
                out.isClose = false;
                out.selfClose = false;

                const char *a = m_text + start;
                const char *b = m_text + end;
                a = skipSpaces(a);
                while (b > a && (*(b - 1) == ' ' || *(b - 1) == '\t' || *(b - 1) == '\r' || *(b - 1) == '\n'))
                    --b;

                if (a < b && *a == '/')
                {
                    out.isClose = true;
                    ++a;
                    a = skipSpaces(a);
                }

                if (b > a && *(b - 1) == '/')
                {
                    out.selfClose = true;
                    --b;
                }

                // Copy raw (bounded)
                const QC::usize rawLen = static_cast<QC::usize>(b - a);
                const QC::usize rawCopy = (rawLen + 1 < sizeof(out.raw)) ? rawLen : (sizeof(out.raw) - 1);
                for (QC::usize i = 0; i < rawCopy; ++i)
                    out.raw[i] = a[i];
                out.raw[rawCopy] = '\0';

                // Extract name
                const char *p = a;
                QC::usize ni = 0;
                while (p < b && *p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && ni + 1 < sizeof(out.name))
                {
                    char c = *p++;
                    if (c >= 'A' && c <= 'Z')
                        c = static_cast<char>(c - 'A' + 'a');
                    out.name[ni++] = c;
                }
                out.name[ni] = '\0';

                return out.name[0] != '\0';
            }

            // Reads raw text until a matching close tag appears. Nested tags are stripped.
            // <br> is translated to '\n'.
            char *readInnerTextUntilClose(const char *tagName)
            {
                if (!tagName)
                    return dupString("");

                QC::Vector<char> buf;

                while (!eof())
                {
                    if (m_text[m_pos] == '<')
                    {
                        // If next token is a close tag matching tagName, consume it and stop.
                        const QC::usize save = m_pos;
                        Tag t{};
                        if (nextTag(t))
                        {
                            if (t.isClose && equalsIgnoreCase(t.name, tagName))
                                break;

                            if (!t.isClose && equalsIgnoreCase(t.name, "br"))
                            {
                                buf.push_back('\n');
                            }

                            continue;
                        }
                        m_pos = save;
                    }

                    const char *p = m_text + m_pos;
                    while (*p && *p != '<')
                    {
                        buf.push_back(*p);
                        ++p;
                    }
                    m_pos = static_cast<QC::usize>(p - m_text);
                }

                // Trim
                QC::usize start = 0;
                while (start < buf.size() && (buf[start] == ' ' || buf[start] == '\t' || buf[start] == '\r' || buf[start] == '\n'))
                    ++start;
                QC::usize end = buf.size();
                while (end > start && (buf[end - 1] == ' ' || buf[end - 1] == '\t' || buf[end - 1] == '\r' || buf[end - 1] == '\n'))
                    --end;

                const QC::usize len = (end > start) ? (end - start) : 0;
                char *out = static_cast<char *>(operator new[](len + 1));
                for (QC::usize i = 0; i < len; ++i)
                    out[i] = buf[start + i];
                out[len] = '\0';
                return out;
            }

        private:
            const char *m_text;
            QC::usize m_len = 0;
            QC::usize m_pos = 0;
        };

        static ImageAsset *tryLoadPng(const char *src)
        {
            if (!src)
                return nullptr;

            auto endsWithIgnoreCaseAscii = [](const char *text, const char *suffix) -> bool {
                if (!text || !suffix)
                    return false;
                const QC::usize tl = QC::String::strlen(text);
                const QC::usize sl = QC::String::strlen(suffix);
                if (sl > tl)
                    return false;
                const char *a = text + (tl - sl);
                for (QC::usize i = 0; i < sl; ++i)
                {
                    char c1 = a[i];
                    char c2 = suffix[i];
                    if (c1 >= 'A' && c1 <= 'Z')
                        c1 = static_cast<char>(c1 - 'A' + 'a');
                    if (c2 >= 'A' && c2 <= 'Z')
                        c2 = static_cast<char>(c2 - 'A' + 'a');
                    if (c1 != c2)
                        return false;
                }
                return true;
            };

            // Remote PNG via plain HTTP (no TLS).
            if (startsWithIgnoreCase(src, "http://"))
            {
                char host[256];
                char path[256];
                QC::String::memset(host, 0, sizeof(host));
                QC::String::memset(path, 0, sizeof(path));
                if (!parseHttpUrl(src, host, sizeof(host), path, sizeof(path)))
                    return nullptr;

                QC::Vector<QC::u8> body;
                if (!httpGetBodyToBuffer(host, path, body))
                    return nullptr;

                auto *asset = new ImageAsset();
                asset->src = dupString(src);
                if (!QG::decodePNG(body, asset->surface))
                {
                    operator delete[](asset->src);
                    delete asset;
                    return nullptr;
                }
                return asset;
            }

            // Local VFS PNG (absolute path)
            if (src[0] != '/')
                return nullptr;

            QFS::File *file = QFS::VFS::instance().open(src, QFS::OpenMode::Read);
            if (!file)
                return nullptr;

            const QC::u64 size64 = file->size();
            if (size64 == 0 || size64 > 1024 * 512)
            {
                QFS::VFS::instance().close(file);
                return nullptr;
            }

            QC::Vector<QC::u8> buffer;
            buffer.resize(static_cast<QC::usize>(size64));

            const QC::isize n = file->read(buffer.data(), buffer.size());
            QFS::VFS::instance().close(file);

            if (n <= 0)
                return nullptr;

            buffer.resize(static_cast<QC::usize>(n));

            auto *asset = new ImageAsset();
            asset->src = dupString(src);
            const bool ok = endsWithIgnoreCaseAscii(src, ".svg")
                                ? QG::decodeSVG(buffer, asset->surface)
                                : QG::decodePNG(buffer, asset->surface);
            if (!ok)
            {
                operator delete[](asset->src);
                delete asset;
                return nullptr;
            }

            return asset;
        }

        static void onLinkClicked(QW::Controls::Label *, void *userData)
        {
            auto *t = static_cast<LinkTarget *>(userData);
            if (!t || !t->handler)
                return;
            t->handler(t->href ? t->href : "", t->userData);
        }

        static void wrapTextToWidth(QG::IPainter *painter, const char *rawText, QC::u32 maxWidthPx, QC::Vector<char> &out)
        {
            out.clear();

            if (!rawText || !painter)
            {
                out.push_back('\0');
                return;
            }

            if (maxWidthPx == 0)
                maxWidthPx = 1;

            const QC::i32 glyphAdvance = painter->measureText("A").width;
            QC::u32 maxChars = 1;
            if (glyphAdvance > 0)
            {
                maxChars = (maxWidthPx / static_cast<QC::u32>(glyphAdvance));
                if (maxChars < 1)
                    maxChars = 1;
            }

            auto isWhitespace = [](char c) {
                return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
            };

            QC::u32 curLineLen = 0;
            bool pendingSpace = false;

            const char *p = rawText;
            while (*p)
            {
                while (*p && isWhitespace(*p))
                {
                    pendingSpace = true;
                    ++p;
                }

                if (!*p)
                    break;

                char wordBuf[512];
                QC::u32 wordLen = 0;
                while (*p && !isWhitespace(*p))
                {
                    if (wordLen + 1 < sizeof(wordBuf))
                        wordBuf[wordLen++] = *p;
                    ++p;
                }
                wordBuf[wordLen] = '\0';

                if (wordLen == 0)
                    continue;

                if (pendingSpace && curLineLen > 0)
                {
                    if (curLineLen + 1 > maxChars)
                    {
                        out.push_back('\n');
                        curLineLen = 0;
                    }
                    else
                    {
                        out.push_back(' ');
                        curLineLen += 1;
                    }
                }
                pendingSpace = false;

                // If the word doesn't fit on this line, start a new line.
                if (curLineLen > 0 && (curLineLen + wordLen) > maxChars)
                {
                    out.push_back('\n');
                    curLineLen = 0;
                }

                // If the word is longer than a whole line, hard-break it.
                if (wordLen > maxChars)
                {
                    const char *w = wordBuf;
                    QC::u32 remaining = wordLen;
                    while (remaining)
                    {
                        const QC::u32 take = (remaining > maxChars) ? maxChars : remaining;
                        if (curLineLen != 0)
                        {
                            out.push_back('\n');
                            curLineLen = 0;
                        }

                        for (QC::u32 i = 0; i < take; ++i)
                            out.push_back(w[i]);

                        w += take;
                        remaining -= take;
                        curLineLen += take;
                        if (remaining)
                        {
                            out.push_back('\n');
                            curLineLen = 0;
                        }
                    }
                }
                else
                {
                    for (QC::u32 i = 0; i < wordLen; ++i)
                        out.push_back(wordBuf[i]);
                    curLineLen += wordLen;
                }
            }

            out.push_back('\0');
        }

    } // namespace

    struct Document::Node
    {
        enum class Type : QC::u8
        {
            Root,
            Div,
            Paragraph,
            Heading1,
            Heading2,
            Heading3,
            Heading4,
            Heading5,
            Heading6,
            Link,
            Break,
            Image,
            InputText,
            Button
        };

        Type type = Type::Paragraph;
        QW::Controls::IControl *native = nullptr;
        QW::Controls::Label *textLabel = nullptr; // when non-null, `native` is a wrapper Panel and this is the inner label
        float labelScale = 0.0f; // only for labels
        QC::u32 fixedHeight = 0; // used by break/input/button fallbacks
        ImageAsset *image = nullptr;
        LinkTarget *link = nullptr;
        char *rawText = nullptr; // for newline-based reflow of labels
        QC::u32 lastWrapWidthPx = 0;
        float lastWrapScale = 0.0f;
        QC::u32 marL = 0, marT = 0, marR = 0, marB = 0;
        QC::Vector<Node *> children;

        ~Node()
        {
            for (QC::usize i = 0; i < children.size(); ++i)
                delete children[i];
            children.clear();

            if (link)
            {
                if (link->href)
                    operator delete[](link->href);
                delete link;
                link = nullptr;
            }

            if (rawText)
            {
                operator delete[](rawText);
                rawText = nullptr;
            }
        }

        QC::u32 layout(QW::Window *window, QG::IPainter *painter, const QC::Rect &rootClientRect, QC::i32 x, QC::i32 y, QC::u32 w)
        {
            (void)window;

            const QC::u32 lineH = lineHeightPx(painter);

            const QC::u32 ml = marL;
            const QC::u32 mt = marT;
            const QC::u32 mr = marR;
            const QC::u32 mb = marB;

            const QC::i32 contentX = x + static_cast<QC::i32>(ml);
            const QC::i32 contentY = y + static_cast<QC::i32>(mt);
            const QC::u32 contentW = (w > (ml + mr)) ? (w - ml - mr) : 0;

            switch (type)
            {
            case Type::Root:
            {
                QC::u32 used = 0;
                const QC::i32 originX = rootClientRect.x;
                const QC::i32 originY = rootClientRect.y;

                for (QC::usize i = 0; i < children.size(); ++i)
                {
                    Node *c = children[i];
                    if (!c)
                        continue;
                    const QC::u32 h = c->layout(window, painter, rootClientRect, originX, originY + static_cast<QC::i32>(used), w);
                    used += h;
                }
                return used;
            }

            case Type::Div:
            {
                QC::u32 usedInner = 0;

                const QW::Controls::Panel *panel = native ? native->asPanel() : nullptr;
                const QC::u32 bw = panel ? panel->borderWidth() : 0;
                const QC::u32 pl = panel ? panel->paddingLeft() : 0;
                const QC::u32 pt = panel ? panel->paddingTop() : 0;
                const QC::u32 pr = panel ? panel->paddingRight() : 0;
                const QC::u32 pb = panel ? panel->paddingBottom() : 0;

                const QC::u32 topInset = bw + pt;
                const QC::u32 bottomInset = bw + pb;
                const QC::u32 leftInset = bw + pl;
                const QC::u32 rightInset = bw + pr;

                const QC::i32 innerX = static_cast<QC::i32>(leftInset);
                const QC::i32 innerY = static_cast<QC::i32>(topInset);
                const QC::u32 innerW = (contentW > (leftInset + rightInset)) ? (contentW - leftInset - rightInset) : 0;

                for (QC::usize i = 0; i < children.size(); ++i)
                {
                    Node *c = children[i];
                    if (!c)
                        continue;
                    const QC::u32 h = c->layout(window, painter, rootClientRect, innerX, innerY + static_cast<QC::i32>(usedInner), innerW);
                    usedInner += h;
                }

                const QC::u32 totalH = topInset + usedInner + bottomInset;
                if (native)
                    native->setBounds({contentX, contentY, contentW, totalH});
                return mt + totalH + mb;
            }

            case Type::Break:
            {
                const QC::u32 h = fixedHeight ? fixedHeight : lineH;
                if (native)
                    native->setBounds({contentX, contentY, contentW, h});
                return mt + h + mb;
            }

            case Type::InputText:
            case Type::Button:
            {
                const QC::u32 h = fixedHeight ? fixedHeight : 22;
                if (native)
                    native->setBounds({contentX, contentY, contentW, h});
                return mt + h + mb;
            }

            case Type::Image:
            {
                QC::u32 h = fixedHeight ? fixedHeight : 120;
                if (image && image->surface.isValid())
                {
                    h = image->surface.height;
                    if (h > 240)
                        h = 240;
                    if (h < 24)
                        h = 24;
                }
                if (native)
                    native->setBounds({contentX, contentY, contentW, h});
                return mt + h + mb;
            }

            case Type::Heading1:
            case Type::Heading2:
            case Type::Heading3:
            case Type::Heading4:
            case Type::Heading5:
            case Type::Heading6:
            case Type::Link:
            case Type::Paragraph:
            default:
            {
                auto *label = textLabel ? textLabel : static_cast<QW::Controls::Label *>(native);
                if (!label)
                    return 0;

                const float old = painter ? painter->textScale() : 1.0f;
                if (painter && labelScale > 0.0f)
                    painter->setTextScale(labelScale);

                // Newline-based wrap to the available width.
                QC::u32 topInset = 0, bottomInset = 0, leftInset = 0, rightInset = 0;
                QC::u32 innerW = contentW;
                if (textLabel)
                {
                    auto *box = native ? native->asPanel() : nullptr;
                    if (!box)
                        return 0;

                    const QC::u32 bw = box->borderWidth();
                    const QC::u32 pl = box->paddingLeft();
                    const QC::u32 pt = box->paddingTop();
                    const QC::u32 pr = box->paddingRight();
                    const QC::u32 pb = box->paddingBottom();

                    topInset = bw + pt;
                    bottomInset = bw + pb;
                    leftInset = bw + pl;
                    rightInset = bw + pr;
                    innerW = (contentW > (leftInset + rightInset)) ? (contentW - leftInset - rightInset) : 0;
                }

                if (painter && rawText)
                {
                    const QC::u32 wrapW = innerW;
                    const float scaleNow = painter->textScale();
                    if (wrapW != lastWrapWidthPx || scaleNow != lastWrapScale)
                    {
                        QC::Vector<char> wrapped;
                        wrapTextToWidth(painter, rawText, wrapW, wrapped);
                        label->setText(wrapped.data());
                        lastWrapWidthPx = wrapW;
                        lastWrapScale = scaleNow;
                    }
                }

                const char *t = label->text();
                const QC::Size sz = painter ? painter->measureText(t ? t : "") : QC::Size(0, lineH);
                const QC::u32 h = (sz.height == 0) ? lineH : sz.height;

                if (painter && labelScale > 0.0f)
                    painter->setTextScale(old);

                if (textLabel)
                {
                    auto *box = native ? native->asPanel() : nullptr;
                    if (!box)
                        return 0;

                    const QC::u32 totalH = topInset + h + bottomInset;
                    box->setBounds({contentX, contentY, contentW, totalH});
                    label->setBounds({static_cast<QC::i32>(leftInset), static_cast<QC::i32>(topInset), innerW, h});
                    return mt + totalH + mb;
                }

                label->setBounds({contentX, contentY, contentW, h});
                return mt + h + mb;
            }
            }
        }
    };

    Document::Document() = default;

    Document::~Document()
    {
        clearInternal(true);
    }

    void Document::setLinkClickHandler(LinkClickHandler handler, void *userData)
    {
        m_linkHandler = handler;
        m_linkUserData = userData;
    }

    void Document::clearInternal(bool detachFromParent)
    {
        // Detach + destroy native controls (reverse: children before parents).
        for (QC::isize i = static_cast<QC::isize>(m_native.size()) - 1; i >= 0; --i)
        {
            QW::Controls::IControl *c = m_native[static_cast<QC::usize>(i)];
            if (!c)
                continue;

            if (detachFromParent)
            {
                if (QW::Controls::Panel *p = c->parent())
                {
                    p->removeChild(c);
                }
            }

            delete c;
        }
        m_native.clear();

        for (QC::usize i = 0; i < m_images.size(); ++i)
        {
            if (!m_images[i])
                continue;
            if (m_images[i]->src)
                operator delete[](m_images[i]->src);
            delete m_images[i];
        }
        m_images.clear();

        if (m_root)
        {
            delete m_root;
            m_root = nullptr;
        }

        m_contentHeight = 0;
    }

    void Document::clear()
    {
        clearInternal(true);
    }

    void Document::layout(QW::Window *window, QW::Controls::Panel *root)
    {
        if (!window || !root)
        {
            m_contentHeight = 0;
            return;
        }

        if (!m_root)
        {
            m_contentHeight = 0;
            return;
        }

        QG::IPainter *painter = window->painter();
        const QC::Rect cr = root->clientRect();

        const QC::u32 used = m_root->layout(window, painter, cr, 0, 0, cr.width);

        // Content height should include top/bottom insets so scroll feels right.
        const QC::Rect rb = root->bounds();
        const QC::u32 topInset = static_cast<QC::u32>(cr.y);
        const QC::u32 bottomInset = (rb.height > (cr.y + cr.height)) ? (rb.height - static_cast<QC::u32>(cr.y + cr.height)) : 0;
        m_contentHeight = topInset + used + bottomInset;
    }

    void Document::renderToInternal(QW::Window *window, QW::Controls::Panel *root, const char *htmlText)
    {
        if (!window || !root || !htmlText)
            return;

        auto skipCssWsComments = [&](const char *s) -> const char *
        {
            while (s && *s)
            {
                if (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
                {
                    ++s;
                    continue;
                }
                if (s[0] == '/' && s[1] == '*')
                {
                    const char *end = findIgnoreCase(s + 2, "*/");
                    s = end ? (end + 2) : (s + QC::String::strlen(s));
                    continue;
                }
                if (s[0] == '/' && s[1] == '/')
                {
                    while (*s && *s != '\n')
                        ++s;
                    continue;
                }
                break;
            }
            return s;
        };

        auto trimInPlace = [&](char *s)
        {
            if (!s)
                return;
            QC::usize len = QC::String::strlen(s);
            while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n'))
                s[--len] = '\0';
            QC::usize start = 0;
            while (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')
                ++start;
            if (start > 0)
            {
                const QC::usize newLen = (len > start) ? (len - start) : 0;
                for (QC::usize i = 0; i < newLen; ++i)
                    s[i] = s[start + i];
                s[newLen] = '\0';
            }
        };

        auto parseCssColor = [&](const char *s, QC::Color *out) -> bool
        {
            if (!out)
                return false;
            if (!s)
                return false;
            s = skipSpaces(s);
            if (!s || *s == '\0')
                return false;

            if (equalsIgnoreCase(s, "transparent"))
            {
                *out = QC::Color(0, 0, 0, 0);
                return true;
            }

            if (parseHtmlColor(s, out))
                return true;

            // rgb(r,g,b)
            if (startsWithIgnoreCase(s, "rgb("))
            {
                s += 4;
                auto readInt = [&](int &v) -> bool
                {
                    s = skipSpaces(s);
                    bool neg = false;
                    if (*s == '-')
                        neg = true, ++s;
                    if (*s < '0' || *s > '9')
                        return false;
                    int x = 0;
                    while (*s >= '0' && *s <= '9')
                        x = x * 10 + (*s++ - '0');
                    v = neg ? -x : x;
                    return true;
                };

                int r = 0, g = 0, b = 0;
                if (!readInt(r))
                    return false;
                s = skipSpaces(s);
                if (*s != ',')
                    return false;
                ++s;
                if (!readInt(g))
                    return false;
                s = skipSpaces(s);
                if (*s != ',')
                    return false;
                ++s;
                if (!readInt(b))
                    return false;
                *out = QC::Color(static_cast<QC::u8>(r < 0 ? 0 : (r > 255 ? 255 : r)),
                                 static_cast<QC::u8>(g < 0 ? 0 : (g > 255 ? 255 : g)),
                                 static_cast<QC::u8>(b < 0 ? 0 : (b > 255 ? 255 : b)),
                                 255);
                return true;
            }

            // rgba(r,g,b,a)
            if (startsWithIgnoreCase(s, "rgba("))
            {
                s += 5;
                auto readInt = [&](int &v) -> bool
                {
                    s = skipSpaces(s);
                    bool neg = false;
                    if (*s == '-')
                        neg = true, ++s;
                    if (*s < '0' || *s > '9')
                        return false;
                    int x = 0;
                    while (*s >= '0' && *s <= '9')
                        x = x * 10 + (*s++ - '0');
                    v = neg ? -x : x;
                    return true;
                };
                auto readAlpha = [&](QC::u8 &aOut) -> bool
                {
                    s = skipSpaces(s);
                    if ((*s < '0' || *s > '9') && *s != '.')
                        return false;
                    int intPart = 0;
                    while (*s >= '0' && *s <= '9')
                        intPart = intPart * 10 + (*s++ - '0');
                    int frac = 0;
                    int fracDiv = 1;
                    if (*s == '.')
                    {
                        ++s;
                        while (*s >= '0' && *s <= '9')
                        {
                            frac = frac * 10 + (*s++ - '0');
                            fracDiv *= 10;
                            if (fracDiv > 1000000)
                                break;
                        }
                    }

                    // If it looks like 0..1, treat as float; else treat as 0..255.
                    if (intPart <= 1 && fracDiv > 1)
                    {
                        const float f = static_cast<float>(intPart) + (static_cast<float>(frac) / static_cast<float>(fracDiv));
                        int ai = static_cast<int>(f * 255.0f + 0.5f);
                        if (ai < 0)
                            ai = 0;
                        if (ai > 255)
                            ai = 255;
                        aOut = static_cast<QC::u8>(ai);
                        return true;
                    }

                    int ai = intPart;
                    if (ai < 0)
                        ai = 0;
                    if (ai > 255)
                        ai = 255;
                    aOut = static_cast<QC::u8>(ai);
                    return true;
                };

                int r = 0, g = 0, b = 0;
                QC::u8 a = 255;
                if (!readInt(r))
                    return false;
                s = skipSpaces(s);
                if (*s != ',')
                    return false;
                ++s;
                if (!readInt(g))
                    return false;
                s = skipSpaces(s);
                if (*s != ',')
                    return false;
                ++s;
                if (!readInt(b))
                    return false;
                s = skipSpaces(s);
                if (*s != ',')
                    return false;
                ++s;
                if (!readAlpha(a))
                    return false;
                *out = QC::Color(static_cast<QC::u8>(r < 0 ? 0 : (r > 255 ? 255 : r)),
                                 static_cast<QC::u8>(g < 0 ? 0 : (g > 255 ? 255 : g)),
                                 static_cast<QC::u8>(b < 0 ? 0 : (b > 255 ? 255 : b)),
                                 a);
                return true;
            }

            return false;
        };

        auto parseCssIntPx = [&](const char *s, QC::u32 &outPx) -> bool
        {
            if (!s)
                return false;
            s = skipSpaces(s);
            if (!s || *s == '\0')
                return false;
            bool ok = false;
            int v = 0;
            while (*s == ' ' || *s == '\t')
                ++s;
            if (*s < '0' || *s > '9')
                return false;
            while (*s >= '0' && *s <= '9')
            {
                v = v * 10 + (*s++ - '0');
                ok = true;
                if (v > 100000)
                    break;
            }
            if (!ok)
                return false;
            if (v < 0)
                v = 0;
            outPx = static_cast<QC::u32>(v);
            return true;
        };

        auto parseFontSpecSizePx = [&](const char *s, QC::u32 &outPx) -> bool
        {
            if (!s)
                return false;
            const char *dash = nullptr;
            for (const char *p = s; *p; ++p)
                if (*p == '-')
                    dash = p;
            if (!dash || *(dash + 1) == '\0')
                return false;
            return parseCssIntPx(dash + 1, outPx);
        };

        auto parseCssDeclListIntoProps = [&](const char *declText, CssStyleProps &ioProps)
        {
            const char *s = declText ? declText : "";
            while (s && *s)
            {
                s = skipCssWsComments(s);
                if (!s || !*s)
                    break;

                if (*s == ';')
                {
                    ++s;
                    continue;
                }

                char prop[64];
                QC::String::memset(prop, 0, sizeof(prop));
                QC::usize pn = 0;
                while (*s && pn + 1 < sizeof(prop))
                {
                    char c = *s;
                    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
                    {
                        prop[pn++] = c;
                        ++s;
                        continue;
                    }
                    break;
                }
                prop[pn] = '\0';
                trimInPlace(prop);

                s = skipCssWsComments(s);
                if (!prop[0] || !s || *s != ':')
                {
                    while (*s && *s != ';')
                        ++s;
                    if (*s == ';')
                        ++s;
                    continue;
                }

                ++s; // ':'
                s = skipCssWsComments(s);

                char val[256];
                QC::String::memset(val, 0, sizeof(val));
                QC::usize vn = 0;
                while (*s && *s != ';' && vn + 1 < sizeof(val))
                    val[vn++] = *s++;
                val[vn] = '\0';
                trimInPlace(val);

                if (*s == ';')
                    ++s;

                // Properties we support (deterministic subset)
                if (equalsIgnoreCase(prop, "color"))
                {
                    QC::Color c{};
                    if (parseCssColor(val, &c))
                        ioProps.hasTextColor = true, ioProps.textColor = c;
                }
                else if (equalsIgnoreCase(prop, "background") || equalsIgnoreCase(prop, "background-color"))
                {
                    QC::Color c{};
                    if (parseCssColor(val, &c))
                        ioProps.hasBackground = true, ioProps.background = c;
                }
                else if (equalsIgnoreCase(prop, "border-color"))
                {
                    QC::Color c{};
                    if (parseCssColor(val, &c))
                        ioProps.hasBorderColor = true, ioProps.borderColor = c;
                }
                else if (equalsIgnoreCase(prop, "border-width"))
                {
                    QC::u32 px = 0;
                    if (parseCssIntPx(val, px))
                        ioProps.hasBorderWidth = true, ioProps.borderWidth = px;
                }
                else if (equalsIgnoreCase(prop, "border"))
                {
                    // Very small parser: try to find a width and a color token.
                    // Examples: "1px solid #fff", "#fff 1px".
                    const char *t = val;
                    QC::u32 px = 0;
                    QC::Color c{};

                    while (t && *t)
                    {
                        t = skipSpaces(t);
                        if (!t || !*t)
                            break;

                        char token[64];
                        QC::String::memset(token, 0, sizeof(token));
                        QC::usize ti = 0;
                        while (*t && *t != ' ' && *t != '\t' && *t != '\r' && *t != '\n' && ti + 1 < sizeof(token))
                            token[ti++] = *t++;
                        token[ti] = '\0';

                        if (!ioProps.hasBorderWidth && parseCssIntPx(token, px))
                            ioProps.hasBorderWidth = true, ioProps.borderWidth = px;
                        if (!ioProps.hasBorderColor && parseCssColor(token, &c))
                            ioProps.hasBorderColor = true, ioProps.borderColor = c;
                    }
                }
                else if (equalsIgnoreCase(prop, "padding"))
                {
                    QC::u32 px = 0;
                    if (parseCssIntPx(val, px))
                    {
                        ioProps.hasPadL = ioProps.hasPadT = ioProps.hasPadR = ioProps.hasPadB = true;
                        ioProps.padL = ioProps.padT = ioProps.padR = ioProps.padB = px;
                    }
                }
                else if (equalsIgnoreCase(prop, "padding-left"))
                {
                    QC::u32 px = 0;
                    if (parseCssIntPx(val, px))
                        ioProps.hasPadL = true, ioProps.padL = px;
                }
                else if (equalsIgnoreCase(prop, "padding-top"))
                {
                    QC::u32 px = 0;
                    if (parseCssIntPx(val, px))
                        ioProps.hasPadT = true, ioProps.padT = px;
                }
                else if (equalsIgnoreCase(prop, "padding-right"))
                {
                    QC::u32 px = 0;
                    if (parseCssIntPx(val, px))
                        ioProps.hasPadR = true, ioProps.padR = px;
                }
                else if (equalsIgnoreCase(prop, "padding-bottom"))
                {
                    QC::u32 px = 0;
                    if (parseCssIntPx(val, px))
                        ioProps.hasPadB = true, ioProps.padB = px;
                }
                else if (equalsIgnoreCase(prop, "margin"))
                {
                    QC::u32 px = 0;
                    if (parseCssIntPx(val, px))
                    {
                        ioProps.hasMarL = ioProps.hasMarT = ioProps.hasMarR = ioProps.hasMarB = true;
                        ioProps.marL = ioProps.marT = ioProps.marR = ioProps.marB = px;
                    }
                }
                else if (equalsIgnoreCase(prop, "margin-left"))
                {
                    QC::u32 px = 0;
                    if (parseCssIntPx(val, px))
                        ioProps.hasMarL = true, ioProps.marL = px;
                }
                else if (equalsIgnoreCase(prop, "margin-top"))
                {
                    QC::u32 px = 0;
                    if (parseCssIntPx(val, px))
                        ioProps.hasMarT = true, ioProps.marT = px;
                }
                else if (equalsIgnoreCase(prop, "margin-right"))
                {
                    QC::u32 px = 0;
                    if (parseCssIntPx(val, px))
                        ioProps.hasMarR = true, ioProps.marR = px;
                }
                else if (equalsIgnoreCase(prop, "margin-bottom"))
                {
                    QC::u32 px = 0;
                    if (parseCssIntPx(val, px))
                        ioProps.hasMarB = true, ioProps.marB = px;
                }
                else if (equalsIgnoreCase(prop, "font-size"))
                {
                    QC::u32 px = 0;
                    if (parseCssIntPx(val, px))
                        ioProps.hasFontSizePx = true, ioProps.fontSizePx = px;
                }
                else if (equalsIgnoreCase(prop, "font"))
                {
                    // Accept Citadel-style font specs inside CSS (e.g. "system-bold-18").
                    QC::u32 px = 0;
                    if (parseFontSpecSizePx(val, px))
                        ioProps.hasFontSizePx = true, ioProps.fontSizePx = px;
                }
            }
        };

        QC::Vector<CssRule> cssRules;

        auto parseCssTextAppendRules = [&](const char *cssText)
        {
            if (!cssText)
                return;

            auto isNameChar = [&](char c) -> bool
            {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ':';
            };

            const char *s = cssText;
            while (s && *s)
            {
                s = skipCssWsComments(s);
                if (!s || !*s)
                    break;

                CssRule rule{};
                if (*s == '#')
                    rule.type = CssSelectorType::Id, ++s;
                else if (*s == '.')
                    rule.type = CssSelectorType::Class, ++s;
                else
                    rule.type = CssSelectorType::Element;

                QC::usize k = 0;
                while (*s && isNameChar(*s) && k + 1 < sizeof(rule.key))
                    rule.key[k++] = *s++;
                rule.key[k] = '\0';
                trimInPlace(rule.key);

                s = skipCssWsComments(s);
                if (!rule.key[0] || !s || *s != '{')
                {
                    while (*s && *s != '{' && *s != '\n')
                        ++s;
                    if (*s == '{')
                        ++s;
                    continue;
                }
                ++s; // '{'

                // Parse declarations until '}'
                QC::Vector<char> decl;
                while (*s && *s != '}')
                {
                    decl.push_back(*s);
                    ++s;
                }
                if (*s == '}')
                    ++s;

                decl.push_back('\0');
                parseCssDeclListIntoProps(decl.data(), rule.props);

                cssRules.push_back(rule);
            }
        };

        auto readFileToOwnedCStringVfs = [&](const char *path) -> char *
        {
            if (!path || !*path)
                return nullptr;
            if (path[0] != '/')
                return nullptr;

            QFS::File *file = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
            if (!file)
                return nullptr;

            const QC::u64 size64 = file->size();
            if (size64 == 0 || size64 > 1024 * 512)
            {
                QFS::VFS::instance().close(file);
                return nullptr;
            }

            char *text = static_cast<char *>(operator new[](static_cast<QC::usize>(size64) + 1));
            const QC::isize n = file->read(text, static_cast<QC::usize>(size64));
            QFS::VFS::instance().close(file);

            if (n <= 0)
            {
                operator delete[](text);
                return nullptr;
            }

            const QC::usize used = static_cast<QC::usize>(n);
            text[used] = '\0';
            return text;
        };

        auto loadCssFromRef = [&](const char *href)
        {
            if (!href || !*href)
                return;

            char resolved[384];
            QC::String::memset(resolved, 0, sizeof(resolved));
            (void)resolveHttpUrl(m_baseUrl, href, resolved, sizeof(resolved));
            const char *src = resolved[0] ? resolved : href;

            // Remote CSS via plain HTTP only.
            if (startsWithIgnoreCase(src, "http://"))
            {
                char host[256];
                char path[256];
                QC::String::memset(host, 0, sizeof(host));
                QC::String::memset(path, 0, sizeof(path));
                if (!parseHttpUrl(src, host, sizeof(host), path, sizeof(path)))
                    return;

                QC::Vector<QC::u8> body;
                if (!httpGetBodyToBuffer(host, path, body))
                    return;

                QC::Vector<char> css;
                css.resize(body.size() + 1);
                for (QC::usize i = 0; i < body.size(); ++i)
                    css[i] = static_cast<char>(body[i]);
                css[body.size()] = '\0';
                parseCssTextAppendRules(css.data());
                return;
            }

            // Local VFS.
            if (src[0] == '/')
            {
                char *css = readFileToOwnedCStringVfs(src);
                if (!css)
                    return;
                parseCssTextAppendRules(css);
                operator delete[](css);
            }
        };

        // Pre-scan styles (CSS is not rendered as text).
        {
            Parser pre(htmlText);
            while (!pre.eof())
            {
                Tag t{};
                if (!pre.nextTag(t))
                    break;

                if (!t.isClose && equalsIgnoreCase(t.name, "style"))
                {
                    if (!t.selfClose)
                    {
                        char *css = pre.readInnerTextUntilClose("style");
                        if (css)
                        {
                            parseCssTextAppendRules(css);
                            operator delete[](css);
                        }
                    }
                    continue;
                }

                if (!t.isClose && equalsIgnoreCase(t.name, "link"))
                {
                    char rel[64];
                    char href[256];
                    QC::String::memset(rel, 0, sizeof(rel));
                    QC::String::memset(href, 0, sizeof(href));
                    (void)parseAttrValue(t.raw, "rel", rel, sizeof(rel));
                    (void)parseAttrValue(t.raw, "href", href, sizeof(href));
                    if (rel[0] && href[0] && equalsIgnoreCase(rel, "stylesheet"))
                        loadCssFromRef(href);
                    continue;
                }
            }
        }

        QC::Color docBg = QC::Color::white();
        QC::Color docText = QC::Color::black();
        preScanBodyColors(htmlText, docBg, docText);
        root->setBackgroundColor(docBg);

        Parser parser(htmlText);

        // Root node wraps the provided root panel.
        m_root = new Node();
        m_root->type = Node::Type::Root;
        m_root->native = root;

        // Stack of container nodes (<div>). Root is first.
        QC::Vector<Node *> stack;
        stack.push_back(m_root);

        QG::IPainter *painter = window->painter();
        const float baseScale = painter ? painter->textScale() : 1.0f;
        const QC::u32 baseLineH = lineHeightPx(painter);

        auto computeStyleProps = [&](const char *elementName, const char *idAttr, const char *classAttr, const CssStyleProps *inlineProps) -> CssStyleProps
        {
            CssStyleProps out{};

            // Deterministic application order (not full CSS): Element -> Class -> Id
            for (QC::usize i = 0; i < cssRules.size(); ++i)
            {
                const CssRule &r = cssRules[i];
                if (r.type == CssSelectorType::Element && equalsIgnoreCase(r.key, elementName))
                    mergeCssProps(out, r.props);
            }

            for (QC::usize i = 0; i < cssRules.size(); ++i)
            {
                const CssRule &r = cssRules[i];
                if (r.type == CssSelectorType::Class && classListContainsCss(classAttr, r.key))
                    mergeCssProps(out, r.props);
            }

            for (QC::usize i = 0; i < cssRules.size(); ++i)
            {
                const CssRule &r = cssRules[i];
                if (r.type == CssSelectorType::Id && idAttr && r.key[0] && QC::String::strcmp(idAttr, r.key) == 0)
                    mergeCssProps(out, r.props);
            }

            if (inlineProps)
                mergeCssProps(out, *inlineProps);

            return out;
        };

        float bodyScaleMult = 1.0f;
        {
            const CssStyleProps bodyProps = computeStyleProps("body", "", "", nullptr);
            if (bodyProps.hasBackground)
                docBg = bodyProps.background, root->setBackgroundColor(docBg);
            if (bodyProps.hasTextColor)
                docText = bodyProps.textColor;
            if (bodyProps.hasFontSizePx && baseLineH > 0)
                bodyScaleMult = static_cast<float>(bodyProps.fontSizePx) / static_cast<float>(baseLineH);
        }

        bool inHead = false;

        while (!parser.eof())
        {
            // Text outside tags: treat as a paragraph.
            char textBuf[256];
            QC::String::memset(textBuf, 0, sizeof(textBuf));
            if (parser.nextText(textBuf, sizeof(textBuf)))
            {
                if (inHead)
                    continue;

                CssStyleProps inlineProps{};
                const CssStyleProps p = computeStyleProps("p", "", "", &inlineProps);

                const bool anyPadding = p.hasPadL || p.hasPadT || p.hasPadR || p.hasPadB;
                const bool anyBorder = p.hasBorderColor || p.hasBorderWidth;

                float scale = baseScale * bodyScaleMult;
                if (p.hasFontSizePx && baseLineH > 0)
                    scale = baseScale * (static_cast<float>(p.fontSizePx) / static_cast<float>(baseLineH));

                auto *n = new Node();
                n->type = Node::Type::Paragraph;
                n->labelScale = scale;
                n->rawText = dupString(textBuf);

                if (anyPadding || anyBorder)
                {
                    auto *box = new QW::Controls::Panel(window, {0, 0, 0, 0});
                    box->setBorderStyle(QW::Controls::BorderStyle::None);
                    box->setFrameVisible(false);
                    box->setBorderWidth(0);

                    if (p.hasBackground)
                        box->setBackgroundColor(p.background);

                    if (anyBorder)
                    {
                        box->setBorderStyle(QW::Controls::BorderStyle::Flat);
                        box->setFrameVisible(true);
                        box->setBorderWidth(p.hasBorderWidth ? p.borderWidth : 1);
                        if (p.hasBorderColor)
                            box->setBorderColor(p.borderColor);
                    }

                    if (anyPadding)
                    {
                        box->setPadding(p.hasPadL ? p.padL : 0,
                                        p.hasPadT ? p.padT : 0,
                                        p.hasPadR ? p.padR : 0,
                                        p.hasPadB ? p.padB : 0);
                    }

                    auto *label = new QW::Controls::Label(window, textBuf, {0, 0, 0, 0});
                    label->setTransparent(true);
                    label->setTextColor(p.hasTextColor ? p.textColor : docText);
                    label->setTextScaleOverride(scale);

                    n->native = box;
                    n->textLabel = label;

                    auto *parentPanel = static_cast<QW::Controls::Panel *>(stack.back()->native);
                    if (parentPanel)
                        parentPanel->addChild(box);
                    box->addChild(label);

                    m_native.push_back(box);
                    m_native.push_back(label);
                }
                else
                {
                    auto *label = new QW::Controls::Label(window, textBuf, {0, 0, 0, 0});
                    label->setTextColor(p.hasTextColor ? p.textColor : docText);
                    if (p.hasBackground)
                    {
                        label->setTransparent(false);
                        label->setBackgroundColor(p.background);
                    }
                    else
                    {
                        label->setTransparent(true);
                    }
                    label->setTextScaleOverride(scale);

                    n->native = label;

                    auto *parentPanel = static_cast<QW::Controls::Panel *>(stack.back()->native);
                    if (parentPanel)
                        parentPanel->addChild(label);
                    m_native.push_back(label);
                }

                const bool anyMargin = p.hasMarL || p.hasMarT || p.hasMarR || p.hasMarB;
                if (anyMargin)
                {
                    n->marL = p.hasMarL ? p.marL : 0;
                    n->marT = p.hasMarT ? p.marT : 0;
                    n->marR = p.hasMarR ? p.marR : 0;
                    n->marB = p.hasMarB ? p.marB : 0;
                }
                else
                {
                    n->marB = 4;
                }

                stack.back()->children.push_back(n);
                continue;
            }

            Tag tag{};
            if (!parser.nextTag(tag))
                break;

            if (tag.isClose)
            {
                if (equalsIgnoreCase(tag.name, "head"))
                {
                    inHead = false;
                    continue;
                }

                if (equalsIgnoreCase(tag.name, "div"))
                {
                    if (stack.size() > 1)
                        stack.pop_back();
                }
                continue;
            }

            if (equalsIgnoreCase(tag.name, "head"))
            {
                inHead = true;
                continue;
            }

            // Ignore any tags encountered while inside <head>.
            if (inHead)
            {
                continue;
            }

            if (equalsIgnoreCase(tag.name, "html") || equalsIgnoreCase(tag.name, "body"))
            {
                if (equalsIgnoreCase(tag.name, "body"))
                {
                    // Honor legacy body colors even if a pre-scan missed it.
                    char bg[64];
                    char txt[64];
                    QC::String::memset(bg, 0, sizeof(bg));
                    QC::String::memset(txt, 0, sizeof(txt));

                    (void)parseAttrValue(tag.raw, "background", bg, sizeof(bg));
                    if (bg[0] == '\0')
                        (void)parseAttrValue(tag.raw, "bgcolor", bg, sizeof(bg));
                    (void)parseAttrValue(tag.raw, "text", txt, sizeof(txt));

                    QC::Color c{};
                    if (bg[0] != '\0' && parseHtmlColor(bg, &c))
                    {
                        docBg = c;
                        root->setBackgroundColor(docBg);
                    }
                    if (txt[0] != '\0' && parseHtmlColor(txt, &c))
                    {
                        docText = c;
                    }

                    // Inline body style can override colors and base font scale.
                    char idAttr[128];
                    char classAttr[256];
                    char styleAttr[512];
                    QC::String::memset(idAttr, 0, sizeof(idAttr));
                    QC::String::memset(classAttr, 0, sizeof(classAttr));
                    QC::String::memset(styleAttr, 0, sizeof(styleAttr));
                    (void)parseAttrValue(tag.raw, "id", idAttr, sizeof(idAttr));
                    (void)parseAttrValue(tag.raw, "class", classAttr, sizeof(classAttr));
                    (void)parseAttrValue(tag.raw, "style", styleAttr, sizeof(styleAttr));

                    CssStyleProps inlineBody{};
                    if (styleAttr[0])
                        parseCssDeclListIntoProps(styleAttr, inlineBody);

                    const CssStyleProps bodyProps = computeStyleProps("body", idAttr, classAttr, styleAttr[0] ? &inlineBody : nullptr);
                    if (bodyProps.hasBackground)
                        docBg = bodyProps.background, root->setBackgroundColor(docBg);
                    if (bodyProps.hasTextColor)
                        docText = bodyProps.textColor;
                    if (bodyProps.hasFontSizePx && baseLineH > 0)
                        bodyScaleMult = static_cast<float>(bodyProps.fontSizePx) / static_cast<float>(baseLineH);
                }
                continue;
            }

            if (equalsIgnoreCase(tag.name, "style"))
            {
                // MVP: CSS is not supported; discard contents instead of rendering it as text.
                if (!tag.selfClose)
                {
                    char *css = parser.readInnerTextUntilClose("style");
                    if (css)
                        operator delete[](css);
                }
                continue;
            }

            if (equalsIgnoreCase(tag.name, "link"))
            {
                // Styles are pre-scanned; ignore.
                continue;
            }

            if (equalsIgnoreCase(tag.name, "script"))
            {
                // MVP: scripting is not supported; discard contents.
                if (!tag.selfClose)
                {
                    char *js = parser.readInnerTextUntilClose("script");
                    if (js)
                        operator delete[](js);
                }
                continue;
            }

            if (equalsIgnoreCase(tag.name, "div"))
            {
                auto *panel = new QW::Controls::Panel(window, {0, 0, 0, 0});

                // HTML divs are their own boxed elements; default to a visible frame.
                // Allow a simple legacy background override.
                char bg[64];
                QC::String::memset(bg, 0, sizeof(bg));
                (void)parseAttrValue(tag.raw, "background", bg, sizeof(bg));
                if (bg[0] == '\0')
                    (void)parseAttrValue(tag.raw, "bgcolor", bg, sizeof(bg));
                QC::Color c{};
                if (bg[0] != '\0' && parseHtmlColor(bg, &c))
                    panel->setBackgroundColor(c);

                char idAttr[128];
                char classAttr[256];
                char styleAttr[512];
                QC::String::memset(idAttr, 0, sizeof(idAttr));
                QC::String::memset(classAttr, 0, sizeof(classAttr));
                QC::String::memset(styleAttr, 0, sizeof(styleAttr));
                (void)parseAttrValue(tag.raw, "id", idAttr, sizeof(idAttr));
                (void)parseAttrValue(tag.raw, "class", classAttr, sizeof(classAttr));
                (void)parseAttrValue(tag.raw, "style", styleAttr, sizeof(styleAttr));

                CssStyleProps inlineDiv{};
                if (styleAttr[0])
                    parseCssDeclListIntoProps(styleAttr, inlineDiv);

                const CssStyleProps p = computeStyleProps("div", idAttr, classAttr, styleAttr[0] ? &inlineDiv : nullptr);

                if (p.hasBackground)
                    panel->setBackgroundColor(p.background);
                if (p.hasBorderWidth)
                {
                    panel->setBorderStyle(QW::Controls::BorderStyle::Flat);
                    panel->setFrameVisible(true);
                    panel->setBorderWidth(p.borderWidth);
                }
                if (p.hasBorderColor)
                {
                    panel->setBorderStyle(QW::Controls::BorderStyle::Flat);
                    panel->setFrameVisible(true);
                    panel->setBorderColor(p.borderColor);
                }

                const bool anyPadding = p.hasPadL || p.hasPadT || p.hasPadR || p.hasPadB;
                if (anyPadding)
                {
                    panel->setPadding(p.hasPadL ? p.padL : 0,
                                      p.hasPadT ? p.padT : 0,
                                      p.hasPadR ? p.padR : 0,
                                      p.hasPadB ? p.padB : 0);
                }
                else
                {
                    panel->setPadding(4);
                }

                auto *n = new Node();
                n->type = Node::Type::Div;
                n->native = panel;
                {
                    const bool anyMargin = p.hasMarL || p.hasMarT || p.hasMarR || p.hasMarB;
                    if (anyMargin)
                    {
                        n->marL = p.hasMarL ? p.marL : 0;
                        n->marT = p.hasMarT ? p.marT : 0;
                        n->marR = p.hasMarR ? p.marR : 0;
                        n->marB = p.hasMarB ? p.marB : 0;
                    }
                    else
                    {
                        n->marB = 4;
                    }
                }

                stack.back()->children.push_back(n);

                // Add div panel to current container native panel.
                auto *parentPanel = static_cast<QW::Controls::Panel *>(stack.back()->native);
                if (parentPanel)
                    parentPanel->addChild(panel);

                m_native.push_back(panel);
                stack.push_back(n);
                continue;
            }

            if (equalsIgnoreCase(tag.name, "br"))
            {
                auto *sp = new QW::Controls::Panel(window, {0, 0, 0, 0});
                sp->setBorderStyle(QW::Controls::BorderStyle::None);
                sp->setFrameVisible(false);

                auto *n = new Node();
                n->type = Node::Type::Break;
                n->native = sp;
                n->fixedHeight = lineHeightPx(painter);
                n->marB = 0;

                stack.back()->children.push_back(n);

                auto *parentPanel = static_cast<QW::Controls::Panel *>(stack.back()->native);
                if (parentPanel)
                    parentPanel->addChild(sp);

                m_native.push_back(sp);
                continue;
            }

            if (equalsIgnoreCase(tag.name, "p"))
            {
                char *inner = parser.readInnerTextUntilClose("p");

                char idAttr[128];
                char classAttr[256];
                char styleAttr[512];
                QC::String::memset(idAttr, 0, sizeof(idAttr));
                QC::String::memset(classAttr, 0, sizeof(classAttr));
                QC::String::memset(styleAttr, 0, sizeof(styleAttr));
                (void)parseAttrValue(tag.raw, "id", idAttr, sizeof(idAttr));
                (void)parseAttrValue(tag.raw, "class", classAttr, sizeof(classAttr));
                (void)parseAttrValue(tag.raw, "style", styleAttr, sizeof(styleAttr));

                CssStyleProps inlineP{};
                if (styleAttr[0])
                    parseCssDeclListIntoProps(styleAttr, inlineP);
                const CssStyleProps p = computeStyleProps("p", idAttr, classAttr, styleAttr[0] ? &inlineP : nullptr);

                const bool anyPadding = p.hasPadL || p.hasPadT || p.hasPadR || p.hasPadB;
                const bool anyBorder = p.hasBorderColor || p.hasBorderWidth;

                float scale = baseScale * bodyScaleMult;
                if (p.hasFontSizePx && baseLineH > 0)
                    scale = baseScale * (static_cast<float>(p.fontSizePx) / static_cast<float>(baseLineH));

                auto *n = new Node();
                n->type = Node::Type::Paragraph;
                n->labelScale = scale;
                n->rawText = dupString(inner ? inner : "");

                if (anyPadding || anyBorder)
                {
                    auto *box = new QW::Controls::Panel(window, {0, 0, 0, 0});
                    box->setBorderStyle(QW::Controls::BorderStyle::None);
                    box->setFrameVisible(false);
                    box->setBorderWidth(0);

                    if (p.hasBackground)
                        box->setBackgroundColor(p.background);

                    if (anyBorder)
                    {
                        box->setBorderStyle(QW::Controls::BorderStyle::Flat);
                        box->setFrameVisible(true);
                        box->setBorderWidth(p.hasBorderWidth ? p.borderWidth : 1);
                        if (p.hasBorderColor)
                            box->setBorderColor(p.borderColor);
                    }

                    if (anyPadding)
                    {
                        box->setPadding(p.hasPadL ? p.padL : 0,
                                        p.hasPadT ? p.padT : 0,
                                        p.hasPadR ? p.padR : 0,
                                        p.hasPadB ? p.padB : 0);
                    }

                    auto *label = new QW::Controls::Label(window, inner ? inner : "", {0, 0, 0, 0});
                    label->setTransparent(true);
                    label->setTextColor(p.hasTextColor ? p.textColor : docText);
                    label->setTextScaleOverride(scale);

                    n->native = box;
                    n->textLabel = label;

                    stack.back()->children.push_back(n);

                    auto *parentPanel = static_cast<QW::Controls::Panel *>(stack.back()->native);
                    if (parentPanel)
                        parentPanel->addChild(box);
                    box->addChild(label);

                    m_native.push_back(box);
                    m_native.push_back(label);
                }
                else
                {
                    auto *label = new QW::Controls::Label(window, inner ? inner : "", {0, 0, 0, 0});
                    label->setTextColor(p.hasTextColor ? p.textColor : docText);
                    if (p.hasBackground)
                    {
                        label->setTransparent(false);
                        label->setBackgroundColor(p.background);
                    }
                    else
                    {
                        label->setTransparent(true);
                    }
                    label->setTextScaleOverride(scale);

                    n->native = label;

                    stack.back()->children.push_back(n);

                    auto *parentPanel = static_cast<QW::Controls::Panel *>(stack.back()->native);
                    if (parentPanel)
                        parentPanel->addChild(label);

                    m_native.push_back(label);
                }
                {
                    const bool anyMargin = p.hasMarL || p.hasMarT || p.hasMarR || p.hasMarB;
                    if (anyMargin)
                    {
                        n->marL = p.hasMarL ? p.marL : 0;
                        n->marT = p.hasMarT ? p.marT : 0;
                        n->marR = p.hasMarR ? p.marR : 0;
                        n->marB = p.hasMarB ? p.marB : 0;
                    }
                    else
                    {
                        n->marB = 4;
                    }
                }

                if (inner)
                    operator delete[](inner);
                continue;
            }

            auto handleHeading = [&](const char *tagName, Node::Type headingType, float defaultMult)
            {
                char *inner = parser.readInnerTextUntilClose(tagName);

                char idAttr[128];
                char classAttr[256];
                char styleAttr[512];
                QC::String::memset(idAttr, 0, sizeof(idAttr));
                QC::String::memset(classAttr, 0, sizeof(classAttr));
                QC::String::memset(styleAttr, 0, sizeof(styleAttr));
                (void)parseAttrValue(tag.raw, "id", idAttr, sizeof(idAttr));
                (void)parseAttrValue(tag.raw, "class", classAttr, sizeof(classAttr));
                (void)parseAttrValue(tag.raw, "style", styleAttr, sizeof(styleAttr));

                CssStyleProps inlineH{};
                if (styleAttr[0])
                    parseCssDeclListIntoProps(styleAttr, inlineH);
                const CssStyleProps p = computeStyleProps(tagName, idAttr, classAttr, styleAttr[0] ? &inlineH : nullptr);

                float scale = baseScale * bodyScaleMult * defaultMult;
                if (p.hasFontSizePx && baseLineH > 0)
                    scale = baseScale * (static_cast<float>(p.fontSizePx) / static_cast<float>(baseLineH));

                const bool anyPadding = p.hasPadL || p.hasPadT || p.hasPadR || p.hasPadB;
                const bool anyBorder = p.hasBorderColor || p.hasBorderWidth;

                auto *n = new Node();
                n->type = headingType;
                n->labelScale = scale;
                n->rawText = dupString(inner ? inner : "");
                {
                    const bool anyMargin = p.hasMarL || p.hasMarT || p.hasMarR || p.hasMarB;
                    if (anyMargin)
                    {
                        n->marL = p.hasMarL ? p.marL : 0;
                        n->marT = p.hasMarT ? p.marT : 0;
                        n->marR = p.hasMarR ? p.marR : 0;
                        n->marB = p.hasMarB ? p.marB : 0;
                    }
                    else
                    {
                        n->marT = 8;
                        n->marB = 4;
                    }
                }

                stack.back()->children.push_back(n);

                if (anyPadding || anyBorder)
                {
                    auto *box = new QW::Controls::Panel(window, {0, 0, 0, 0});
                    box->setBorderStyle(QW::Controls::BorderStyle::None);
                    box->setFrameVisible(false);
                    box->setBorderWidth(0);

                    if (p.hasBackground)
                        box->setBackgroundColor(p.background);

                    if (anyBorder)
                    {
                        box->setBorderStyle(QW::Controls::BorderStyle::Flat);
                        box->setFrameVisible(true);
                        box->setBorderWidth(p.hasBorderWidth ? p.borderWidth : 1);
                        if (p.hasBorderColor)
                            box->setBorderColor(p.borderColor);
                    }

                    if (anyPadding)
                    {
                        box->setPadding(p.hasPadL ? p.padL : 0,
                                        p.hasPadT ? p.padT : 0,
                                        p.hasPadR ? p.padR : 0,
                                        p.hasPadB ? p.padB : 0);
                    }

                    auto *label = new QW::Controls::Label(window, inner ? inner : "", {0, 0, 0, 0});
                    label->setTransparent(true);
                    label->setTextColor(p.hasTextColor ? p.textColor : docText);
                    label->setTextScaleOverride(scale);

                    n->native = box;
                    n->textLabel = label;

                    auto *parentPanel = static_cast<QW::Controls::Panel *>(stack.back()->native);
                    if (parentPanel)
                        parentPanel->addChild(box);
                    box->addChild(label);

                    m_native.push_back(box);
                    m_native.push_back(label);
                }
                else
                {
                    auto *label = new QW::Controls::Label(window, inner ? inner : "", {0, 0, 0, 0});
                    label->setTextColor(p.hasTextColor ? p.textColor : docText);
                    if (p.hasBackground)
                    {
                        label->setTransparent(false);
                        label->setBackgroundColor(p.background);
                    }
                    else
                    {
                        label->setTransparent(true);
                    }
                    label->setTextScaleOverride(scale);

                    n->native = label;

                    auto *parentPanel = static_cast<QW::Controls::Panel *>(stack.back()->native);
                    if (parentPanel)
                        parentPanel->addChild(label);

                    m_native.push_back(label);
                }

                if (inner)
                    operator delete[](inner);
            };

            if (equalsIgnoreCase(tag.name, "h1"))
            {
                handleHeading("h1", Node::Type::Heading1, 2.0f);
                continue;
            }
            if (equalsIgnoreCase(tag.name, "h2"))
            {
                handleHeading("h2", Node::Type::Heading2, 1.75f);
                continue;
            }
            if (equalsIgnoreCase(tag.name, "h3"))
            {
                handleHeading("h3", Node::Type::Heading3, 1.5f);
                continue;
            }
            if (equalsIgnoreCase(tag.name, "h4"))
            {
                handleHeading("h4", Node::Type::Heading4, 1.25f);
                continue;
            }
            if (equalsIgnoreCase(tag.name, "h5"))
            {
                handleHeading("h5", Node::Type::Heading5, 1.1f);
                continue;
            }
            if (equalsIgnoreCase(tag.name, "h6"))
            {
                handleHeading("h6", Node::Type::Heading6, 1.0f);
                continue;
            }

            if (equalsIgnoreCase(tag.name, "a"))
            {
                char href[256];
                QC::String::memset(href, 0, sizeof(href));
                (void)parseAttrValue(tag.raw, "href", href, sizeof(href));

                char resolvedHref[256];
                QC::String::memset(resolvedHref, 0, sizeof(resolvedHref));
                (void)resolveHttpUrl(m_baseUrl, href, resolvedHref, sizeof(resolvedHref));

                char *inner = parser.readInnerTextUntilClose("a");

                char idAttr[128];
                char classAttr[256];
                char styleAttr[512];
                QC::String::memset(idAttr, 0, sizeof(idAttr));
                QC::String::memset(classAttr, 0, sizeof(classAttr));
                QC::String::memset(styleAttr, 0, sizeof(styleAttr));
                (void)parseAttrValue(tag.raw, "id", idAttr, sizeof(idAttr));
                (void)parseAttrValue(tag.raw, "class", classAttr, sizeof(classAttr));
                (void)parseAttrValue(tag.raw, "style", styleAttr, sizeof(styleAttr));

                CssStyleProps inlineA{};
                if (styleAttr[0])
                    parseCssDeclListIntoProps(styleAttr, inlineA);
                const CssStyleProps p = computeStyleProps("a", idAttr, classAttr, styleAttr[0] ? &inlineA : nullptr);

                auto *label = new QW::Controls::Label(window, inner ? inner : "", {0, 0, 0, 0});
                if (p.hasBackground)
                {
                    label->setTransparent(false);
                    label->setBackgroundColor(p.background);
                }
                else
                {
                    label->setTransparent(true);
                }
                label->setUnderline(true);
                label->setTextColor(p.hasTextColor ? p.textColor : docText);

                float scale = baseScale * bodyScaleMult;
                if (p.hasFontSizePx && baseLineH > 0)
                    scale = baseScale * (static_cast<float>(p.fontSizePx) / static_cast<float>(baseLineH));
                label->setTextScaleOverride(scale);

                auto *t = new LinkTarget();
                t->href = dupString(resolvedHref[0] ? resolvedHref : (href[0] ? href : ""));
                t->handler = m_linkHandler;
                t->userData = m_linkUserData;

                label->setClickHandler(&onLinkClicked, t);

                auto *n = new Node();
                n->type = Node::Type::Link;
                n->native = label;
                n->link = t;
                n->labelScale = scale;
                n->rawText = dupString(inner ? inner : "");
                {
                    const bool anyMargin = p.hasMarL || p.hasMarT || p.hasMarR || p.hasMarB;
                    if (anyMargin)
                    {
                        n->marL = p.hasMarL ? p.marL : 0;
                        n->marT = p.hasMarT ? p.marT : 0;
                        n->marR = p.hasMarR ? p.marR : 0;
                        n->marB = p.hasMarB ? p.marB : 0;
                    }
                    else
                    {
                        n->marB = 4;
                    }
                }

                stack.back()->children.push_back(n);

                auto *parentPanel = static_cast<QW::Controls::Panel *>(stack.back()->native);
                if (parentPanel)
                    parentPanel->addChild(label);

                m_native.push_back(label);

                if (inner)
                    operator delete[](inner);
                continue;
            }

            if (equalsIgnoreCase(tag.name, "input"))
            {
                char type[32];
                QC::String::memset(type, 0, sizeof(type));
                (void)parseAttrValue(tag.raw, "type", type, sizeof(type));
                if (type[0] != '\0' && !equalsIgnoreCase(type, "text"))
                    continue;

                char idAttr[128];
                char classAttr[256];
                char styleAttr[512];
                QC::String::memset(idAttr, 0, sizeof(idAttr));
                QC::String::memset(classAttr, 0, sizeof(classAttr));
                QC::String::memset(styleAttr, 0, sizeof(styleAttr));
                (void)parseAttrValue(tag.raw, "id", idAttr, sizeof(idAttr));
                (void)parseAttrValue(tag.raw, "class", classAttr, sizeof(classAttr));
                (void)parseAttrValue(tag.raw, "style", styleAttr, sizeof(styleAttr));

                CssStyleProps inlineInput{};
                if (styleAttr[0])
                    parseCssDeclListIntoProps(styleAttr, inlineInput);
                const CssStyleProps p = computeStyleProps("input", idAttr, classAttr, styleAttr[0] ? &inlineInput : nullptr);

                auto *tb = new QW::Controls::TextBox(window, {0, 0, 0, 0});
                tb->setPlaceholder("text...");
                tb->setTextColor(p.hasTextColor ? p.textColor : docText);
                if (p.hasBackground)
                    tb->setBackgroundColor(p.background);
                if (p.hasBorderColor)
                    tb->setBorderColor(p.borderColor);

                auto *n = new Node();
                n->type = Node::Type::InputText;
                n->native = tb;
                n->fixedHeight = 22;

                {
                    const bool anyMargin = p.hasMarL || p.hasMarT || p.hasMarR || p.hasMarB;
                    if (anyMargin)
                    {
                        n->marL = p.hasMarL ? p.marL : 0;
                        n->marT = p.hasMarT ? p.marT : 0;
                        n->marR = p.hasMarR ? p.marR : 0;
                        n->marB = p.hasMarB ? p.marB : 0;
                    }
                    else
                    {
                        n->marB = 4;
                    }
                }

                stack.back()->children.push_back(n);

                auto *parentPanel = static_cast<QW::Controls::Panel *>(stack.back()->native);
                if (parentPanel)
                    parentPanel->addChild(tb);

                m_native.push_back(tb);
                continue;
            }

            if (equalsIgnoreCase(tag.name, "button"))
            {
                char *inner = parser.readInnerTextUntilClose("button");

                char idAttr[128];
                char classAttr[256];
                char styleAttr[512];
                QC::String::memset(idAttr, 0, sizeof(idAttr));
                QC::String::memset(classAttr, 0, sizeof(classAttr));
                QC::String::memset(styleAttr, 0, sizeof(styleAttr));
                (void)parseAttrValue(tag.raw, "id", idAttr, sizeof(idAttr));
                (void)parseAttrValue(tag.raw, "class", classAttr, sizeof(classAttr));
                (void)parseAttrValue(tag.raw, "style", styleAttr, sizeof(styleAttr));

                CssStyleProps inlineBtn{};
                if (styleAttr[0])
                    parseCssDeclListIntoProps(styleAttr, inlineBtn);
                const CssStyleProps p = computeStyleProps("button", idAttr, classAttr, styleAttr[0] ? &inlineBtn : nullptr);

                auto *btn = new QW::Controls::Button(window, inner ? inner : "Button", {0, 0, 0, 0});
                btn->setContentMode(QW::ButtonContentMode::Text);
                float scale = baseScale * bodyScaleMult;
                if (p.hasFontSizePx && baseLineH > 0)
                    scale = baseScale * (static_cast<float>(p.fontSizePx) / static_cast<float>(baseLineH));
                btn->setTextScaleOverride(scale);

                auto *n = new Node();
                n->type = Node::Type::Button;
                n->native = btn;
                n->fixedHeight = 22;

                {
                    const bool anyMargin = p.hasMarL || p.hasMarT || p.hasMarR || p.hasMarB;
                    if (anyMargin)
                    {
                        n->marL = p.hasMarL ? p.marL : 0;
                        n->marT = p.hasMarT ? p.marT : 0;
                        n->marR = p.hasMarR ? p.marR : 0;
                        n->marB = p.hasMarB ? p.marB : 0;
                    }
                    else
                    {
                        n->marB = 4;
                    }
                }

                stack.back()->children.push_back(n);

                auto *parentPanel = static_cast<QW::Controls::Panel *>(stack.back()->native);
                if (parentPanel)
                    parentPanel->addChild(btn);

                m_native.push_back(btn);

                if (inner)
                    operator delete[](inner);
                continue;
            }

            if (equalsIgnoreCase(tag.name, "img"))
            {
                char src[256];
                QC::String::memset(src, 0, sizeof(src));
                (void)parseAttrValue(tag.raw, "src", src, sizeof(src));

                char resolvedSrc[256];
                QC::String::memset(resolvedSrc, 0, sizeof(resolvedSrc));
                (void)resolveHttpUrl(m_baseUrl, src, resolvedSrc, sizeof(resolvedSrc));

                char alt[256];
                QC::String::memset(alt, 0, sizeof(alt));
                (void)parseAttrValue(tag.raw, "alt", alt, sizeof(alt));

                ImageAsset *asset = tryLoadPng(resolvedSrc[0] ? resolvedSrc : src);
                if (asset)
                    m_images.push_back(asset);

                char idAttr[128];
                char classAttr[256];
                char styleAttr[512];
                QC::String::memset(idAttr, 0, sizeof(idAttr));
                QC::String::memset(classAttr, 0, sizeof(classAttr));
                QC::String::memset(styleAttr, 0, sizeof(styleAttr));
                (void)parseAttrValue(tag.raw, "id", idAttr, sizeof(idAttr));
                (void)parseAttrValue(tag.raw, "class", classAttr, sizeof(classAttr));
                (void)parseAttrValue(tag.raw, "style", styleAttr, sizeof(styleAttr));

                CssStyleProps inlineImg{};
                if (styleAttr[0])
                    parseCssDeclListIntoProps(styleAttr, inlineImg);
                const CssStyleProps p = computeStyleProps("img", idAttr, classAttr, styleAttr[0] ? &inlineImg : nullptr);

                if (asset && asset->surface.isValid())
                {
                    auto *view = new QW::Controls::ImageView(window, {0, 0, 0, 0});
                    view->setScaleMode(QG::ImageScaleMode::Fit);
                    view->setImage(&asset->surface);

                    auto *n = new Node();
                    n->type = Node::Type::Image;
                    n->native = view;
                    n->image = asset;
                    {
                        const bool anyMargin = p.hasMarL || p.hasMarT || p.hasMarR || p.hasMarB;
                        if (anyMargin)
                        {
                            n->marL = p.hasMarL ? p.marL : 0;
                            n->marT = p.hasMarT ? p.marT : 0;
                            n->marR = p.hasMarR ? p.marR : 0;
                            n->marB = p.hasMarB ? p.marB : 0;
                        }
                        else
                        {
                            n->marB = 4;
                        }
                    }

                    stack.back()->children.push_back(n);

                    auto *parentPanel = static_cast<QW::Controls::Panel *>(stack.back()->native);
                    if (parentPanel)
                        parentPanel->addChild(view);

                    m_native.push_back(view);
                }
                else
                {
                    const char *fallback = (alt[0] != '\0') ? alt : (resolvedSrc[0] != '\0' ? resolvedSrc : (src[0] != '\0' ? src : "[image]"));
                    auto *label = new QW::Controls::Label(window, fallback, {0, 0, 0, 0});
                    label->setTransparent(true);
                    label->setTextColor(docText);

                    auto *n = new Node();
                    n->type = Node::Type::Paragraph;
                    n->native = label;
                    n->marB = 4;

                    stack.back()->children.push_back(n);

                    auto *parentPanel = static_cast<QW::Controls::Panel *>(stack.back()->native);
                    if (parentPanel)
                        parentPanel->addChild(label);

                    m_native.push_back(label);
                }
                continue;
            }

            // Unknown tag: ignore.
        }

        layout(window, root);
    }

    void Document::renderTo(QW::Window *window, QW::Controls::Panel *root, const char *htmlText)
    {
        clearInternal(true);

        QC::String::memset(m_baseUrl, 0, sizeof(m_baseUrl));
        renderToInternal(window, root, htmlText);
    }

    void Document::renderUrlTo(QW::Window *window, QW::Controls::Panel *root, const char *url)
    {
        clearInternal(true);

        if (!window || !root || !url || !*url)
            return;

        QC::String::memset(m_baseUrl, 0, sizeof(m_baseUrl));
        QC::String::strncpy(m_baseUrl, url, sizeof(m_baseUrl) - 1);

        if (!startsWithIgnoreCase(url, "http://"))
        {
            renderToInternal(window, root, "<p>browsefile: only http:// URLs are supported</p>");
            return;
        }

        char host[256];
        char path[256];
        QC::String::memset(host, 0, sizeof(host));
        QC::String::memset(path, 0, sizeof(path));
        if (!parseHttpUrl(url, host, sizeof(host), path, sizeof(path)))
        {
            renderToInternal(window, root, "<p>browsefile: invalid URL</p>");
            return;
        }

        QC::Vector<QC::u8> body;
        if (!httpGetBodyToBuffer(host, path, body))
        {
            renderToInternal(window, root, "<p>browsefile: HTTP fetch failed</p>");
            return;
        }

        // Ensure NUL-terminated text buffer.
        QC::Vector<char> html;
        html.resize(body.size() + 1);
        for (QC::usize i = 0; i < body.size(); ++i)
            html[i] = static_cast<char>(body[i]);
        html[body.size()] = '\0';
        renderToInternal(window, root, html.data());
    }

} // namespace QD::Html
