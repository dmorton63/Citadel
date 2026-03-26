// QDesktop CUI-ML Viewer (MVP)
// Namespace: QD

#include "QDCuiMLViewer.h"

#include "QDDesktop.h"
#include "QDColorUtils.h"

#include "QCLogger.h"
#include "QCString.h"
#include "QCVector.h"

#include "QFSVFS.h"
#include "QFSFile.h"

#include "QWWindowManager.h"
#include "QWWindow.h"
#include "QWControls/Containers/Panel.h"
#include "QWControls/Leaf/Label.h"
#include "QWControls/Leaf/Button.h"
#include "QWControls/Leaf/ImageView.h"

#include "QG/Image.h"
#include "QG/SVG.h"

namespace QD
{
    namespace
    {
        static bool endsWithIgnoreCaseAscii(const char *text, const char *suffix)
        {
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
        }

        static bool streqIgnoreCaseAscii(const char *a, const char *b)
        {
            if (!a || !b)
                return false;
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

        static bool startsWithIgnoreCaseAscii(const char *s, const char *prefix)
        {
            if (!s || !prefix)
                return false;
            while (*prefix)
            {
                char a = *s;
                char b = *prefix;
                if (!a)
                    return false;
                if (a >= 'A' && a <= 'Z')
                    a = static_cast<char>(a + 32);
                if (b >= 'A' && b <= 'Z')
                    b = static_cast<char>(b + 32);
                if (a != b)
                    return false;
                ++s;
                ++prefix;
            }
            return true;
        }

        static bool hasSlash(const char *s)
        {
            if (!s)
                return false;
            for (; *s; ++s)
            {
                if (*s == '/')
                    return true;
            }
            return false;
        }

        static char *readFileToOwnedCString(const char *path)
        {
            if (!path || !*path)
                return nullptr;

            QFS::File *file = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
            if (!file)
                return nullptr;

            const QC::u64 size64 = file->size();
            if (size64 == 0 || size64 > 1024 * 1024)
            {
                QFS::VFS::instance().close(file);
                return nullptr;
            }

            QC::Vector<char> buf;
            buf.resize(static_cast<QC::usize>(size64) + 1);

            const QC::isize n = file->read(buf.data(), static_cast<QC::usize>(size64));
            QFS::VFS::instance().close(file);

            if (n <= 0)
                return nullptr;

            buf[static_cast<QC::usize>(n)] = '\0';

            char *out = static_cast<char *>(operator new[](static_cast<QC::usize>(n) + 1));
            for (QC::usize i = 0; i < static_cast<QC::usize>(n) + 1; ++i)
                out[i] = buf[i];
            return out;
        }

        static bool readFileToBytes(const char *path, QC::Vector<QC::u8> &out)
        {
            out.clear();
            if (!path || !*path)
                return false;

            QFS::File *file = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
            if (!file)
                return false;

            const QC::u64 size64 = file->size();
            if (size64 == 0 || size64 > 8 * 1024 * 1024)
            {
                QFS::VFS::instance().close(file);
                return false;
            }

            out.resize(static_cast<QC::usize>(size64));
            const QC::isize n = file->read(out.data(), static_cast<QC::usize>(size64));
            QFS::VFS::instance().close(file);

            if (n <= 0)
            {
                out.clear();
                return false;
            }
            out.resize(static_cast<QC::usize>(n));
            return true;
        }

        static const char *skipWs(const char *p)
        {
            while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
                ++p;
            return p;
        }

        static const char *findStr(const char *haystack, const char *needle)
        {
            if (!haystack || !needle || !*needle)
                return nullptr;
            for (const char *p = haystack; *p; ++p)
            {
                const char *a = p;
                const char *b = needle;
                while (*a && *b && *a == *b)
                {
                    ++a;
                    ++b;
                }
                if (*b == '\0')
                    return p;
            }
            return nullptr;
        }

        static bool parseAttrValue(const char *tagRaw, const char *key, char *out, QC::usize outSize)
        {
            if (!tagRaw || !key || !out || outSize == 0)
                return false;
            out[0] = '\0';

            const char *p = tagRaw;
            const QC::usize keyLen = QC::String::strlen(key);

            while (*p)
            {
                // Find key
                if ((p[0] == ' ' || p[0] == '\t' || p[0] == '\n' || p[0] == '\r') &&
                    keyLen > 0)
                {
                    bool match = true;
                    for (QC::usize i = 0; i < keyLen; ++i)
                    {
                        if (p[1 + i] != key[i])
                        {
                            match = false;
                            break;
                        }
                    }
                    if (!match)
                    {
                        ++p;
                        continue;
                    }

                    const char *k = p + 1 + keyLen;
                    k = skipWs(k);
                    if (*k != '=')
                    {
                        ++p;
                        continue;
                    }
                    ++k;
                    k = skipWs(k);

                    QC::usize i = 0;
                    if (*k == '"' || *k == '\'')
                    {
                        const char quote = *k;
                        ++k;
                        while (*k && *k != quote && i + 1 < outSize)
                            out[i++] = *k++;
                        out[i] = '\0';
                        return true;
                    }

                    // Unquoted attribute values: read until whitespace or tag end.
                    while (*k && *k != ' ' && *k != '\t' && *k != '\r' && *k != '\n' && *k != '>' && *k != '/' && i + 1 < outSize)
                        out[i++] = *k++;
                    out[i] = '\0';
                    return true;
                }
                ++p;
            }

            return false;
        }

        static QC::i32 parseInt(const char *s, bool &ok)
        {
            ok = false;
            if (!s || !*s)
                return 0;
            bool neg = false;
            QC::usize i = 0;
            if (s[0] == '-')
            {
                neg = true;
                i = 1;
            }
            if (!s[i])
                return 0;
            QC::i32 v = 0;
            for (; s[i]; ++i)
            {
                if (s[i] < '0' || s[i] > '9')
                    return 0;
                v = v * 10 + (s[i] - '0');
            }
            ok = true;
            return neg ? -v : v;
        }

        static QC::i32 evalDim(const char *expr, QC::i32 base, const char *edgeKeyword)
        {
            if (!expr || !*expr)
                return 0;

            // right-XX / bottom-XX
            if (edgeKeyword && startsWithIgnoreCaseAscii(expr, edgeKeyword))
            {
                const char *p = expr + QC::String::strlen(edgeKeyword);
                if (*p == '-')
                    ++p;
                bool ok = false;
                const QC::i32 n = parseInt(p, ok);
                if (ok)
                    return base - n;
                return base;
            }

            // 100% or 100%-N
            const char *pct = findStr(expr, "%");
            if (pct)
            {
                // parse leading number
                QC::u32 percent = 0;
                const char *p = expr;
                while (*p >= '0' && *p <= '9')
                {
                    percent = percent * 10u + static_cast<QC::u32>(*p - '0');
                    ++p;
                }
                QC::i32 v = static_cast<QC::i32>((static_cast<QC::i64>(base) * percent) / 100);

                const char *after = pct + 1;
                after = skipWs(after);
                if (*after == '-')
                {
                    ++after;
                    after = skipWs(after);
                    bool ok = false;
                    const QC::i32 n = parseInt(after, ok);
                    if (ok)
                        v -= n;
                }
                else if (*after == '+')
                {
                    ++after;
                    after = skipWs(after);
                    bool ok = false;
                    const QC::i32 n = parseInt(after, ok);
                    if (ok)
                        v += n;
                }

                if (v < 0)
                    v = 0;
                return v;
            }

            bool ok = false;
            QC::i32 v = parseInt(expr, ok);
            if (!ok)
                return 0;
            return v;
        }

        static QC::u32 guessTextHeightPx(const char *text)
        {
            if (!text)
                return 16;
            QC::u32 lines = 1;
            for (const char *p = text; *p; ++p)
            {
                if (*p == '\n')
                    ++lines;
            }
            const QC::u32 lineH = 16;
            return lines * lineH;
        }

        struct HelpSpec
        {
            char *srcOrUrl = nullptr;
            char *inlineHtml = nullptr;
        };

        static void destroyHelpSpec(HelpSpec &hs)
        {
            if (hs.srcOrUrl)
                operator delete[](hs.srcOrUrl);
            if (hs.inlineHtml)
                operator delete[](hs.inlineHtml);
            hs.srcOrUrl = nullptr;
            hs.inlineHtml = nullptr;
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

        // Extract inner raw content until a matching close tag is found. Returns owned string (may be empty).
        static const char *extractRawUntilClose(const char *p, const char *closeTag, char *&outOwned)
        {
            outOwned = nullptr;
            if (!p || !closeTag)
                return p;

            const char *end = findStr(p, closeTag);
            if (!end)
                return p;

            const QC::usize len = static_cast<QC::usize>(end - p);
            outOwned = static_cast<char *>(operator new[](len + 1));
            for (QC::usize i = 0; i < len; ++i)
                outOwned[i] = p[i];
            outOwned[len] = '\0';

            return end + QC::String::strlen(closeTag);
        }

        static void stripCdataInPlace(char *text)
        {
            if (!text)
                return;
            const char *open = "<![CDATA[";
            const char *close = "]]>";
            const QC::usize openLen = QC::String::strlen(open);
            const QC::usize closeLen = QC::String::strlen(close);

            const QC::usize len = QC::String::strlen(text);
            if (len < openLen + closeLen)
                return;

            // Allow whitespace before CDATA.
            QC::usize startIdx = 0;
            while (text[startIdx] == ' ' || text[startIdx] == '\t' || text[startIdx] == '\r' || text[startIdx] == '\n')
                ++startIdx;

            if (QC::String::strlen(text + startIdx) < openLen + closeLen)
                return;

            if (QC::String::memcmp(text + startIdx, open, openLen) != 0)
                return;

            const char *end = findStr(text + startIdx + openLen, close);
            if (!end)
                return;

            const QC::usize innerLen = static_cast<QC::usize>(end - (text + startIdx + openLen));
            for (QC::usize i = 0; i < innerLen; ++i)
                text[i] = text[startIdx + openLen + i];
            text[innerLen] = '\0';
        }

        struct BuildFrame
        {
            QW::Controls::Panel *panel = nullptr;
            QC::i32 w = 0;
            QC::i32 h = 0;
        };

        static const char *parseAndBuild(QD::Desktop *desktop,
                                         QD::CuiMLViewer *viewer,
                                         QW::Window *window,
                                         QW::Controls::Panel *root,
                                         const char *text,
                                         QC::Vector<QW::Controls::IControl *> &outControls,
                                         HelpSpec &outHelp)
        {
            (void)desktop;
            if (!window || !root || !text)
                return text;

            struct CuiMLStyleProps
            {
                bool hasTextColor = false;
                QC::Color textColor{};

                bool hasBackground = false;
                QC::Color background{};

                bool hasBorderColor = false;
                QC::Color borderColor{};

                bool hasBorderWidth = false;
                QC::u32 borderWidth = 1;

                bool hasFont = false;
                char font[128]{};

                bool hasPadding = false;
                QC::u32 padL = 0, padT = 0, padR = 0, padB = 0;

                bool hasTextAlign = false;
                QW::Controls::TextAlign textAlign = QW::Controls::TextAlign::Left;

                bool hasEnabled = false;
                bool enabled = true;

                bool hasVisible = false;
                bool visible = true;

                bool hasOpacity = false;
                QC::u8 opacity = 255;

                bool hasRole = false;
                char role[32]{};
            };

            enum class CuiMLSelectorType : QC::u8
            {
                Element,
                Class,
                Id,
            };

            struct CuiMLStyleRule
            {
                CuiMLSelectorType type = CuiMLSelectorType::Element;
                char key[64]{};
                CuiMLStyleProps props;
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
                    QC::usize i = 0;
                    for (; s[start + i]; ++i)
                        s[i] = s[start + i];
                    s[i] = '\0';
                }
            };

            auto parseBool = [&](const char *v, bool &out) -> bool
            {
                if (!v || !*v)
                    return false;
                if (streqIgnoreCaseAscii(v, "true") || streqIgnoreCaseAscii(v, "1") || streqIgnoreCaseAscii(v, "yes") || streqIgnoreCaseAscii(v, "on"))
                {
                    out = true;
                    return true;
                }
                if (streqIgnoreCaseAscii(v, "false") || streqIgnoreCaseAscii(v, "0") || streqIgnoreCaseAscii(v, "no") || streqIgnoreCaseAscii(v, "off"))
                {
                    out = false;
                    return true;
                }
                return false;
            };

            auto parseOpacity = [&](const char *v, QC::u8 &outA) -> bool
            {
                if (!v || !*v)
                    return false;

                const char *pct = findStr(v, "%");
                if (pct)
                {
                    char buf[32];
                    QC::String::memset(buf, 0, sizeof(buf));
                    QC::usize n = static_cast<QC::usize>(pct - v);
                    if (n >= sizeof(buf))
                        n = sizeof(buf) - 1;
                    for (QC::usize i = 0; i < n; ++i)
                        buf[i] = v[i];
                    buf[n] = '\0';
                    trimInPlace(buf);
                    bool ok = false;
                    const QC::i32 p = parseInt(buf, ok);
                    if (!ok)
                        return false;
                    QC::i32 clamped = p;
                    if (clamped < 0)
                        clamped = 0;
                    if (clamped > 100)
                        clamped = 100;
                    outA = static_cast<QC::u8>((clamped * 255) / 100);
                    return true;
                }

                const char *dot = findStr(v, ".");
                if (dot)
                {
                    QC::i32 whole = 0;
                    bool okWhole = false;
                    {
                        char buf[16];
                        QC::String::memset(buf, 0, sizeof(buf));
                        QC::usize n = static_cast<QC::usize>(dot - v);
                        if (n >= sizeof(buf))
                            n = sizeof(buf) - 1;
                        for (QC::usize i = 0; i < n; ++i)
                            buf[i] = v[i];
                        buf[n] = '\0';
                        trimInPlace(buf);
                        whole = parseInt(buf, okWhole);
                    }
                    if (!okWhole)
                        return false;

                    const char *fracStart = dot + 1;
                    QC::i32 frac = 0;
                    QC::i32 denom = 1;
                    for (int i = 0; fracStart[i] && i < 6; ++i)
                    {
                        if (fracStart[i] < '0' || fracStart[i] > '9')
                            break;
                        frac = frac * 10 + (fracStart[i] - '0');
                        denom *= 10;
                    }

                    float f = static_cast<float>(whole) + (denom > 1 ? static_cast<float>(frac) / static_cast<float>(denom) : 0.0f);
                    if (f < 0.0f)
                        f = 0.0f;
                    if (f > 1.0f)
                        f = 1.0f;
                    outA = static_cast<QC::u8>(f * 255.0f + 0.5f);
                    return true;
                }

                bool ok = false;
                const QC::i32 n = parseInt(v, ok);
                if (!ok)
                    return false;
                QC::i32 clamped = n;
                if (clamped < 0)
                    clamped = 0;
                if (clamped > 255)
                    clamped = 255;
                outA = static_cast<QC::u8>(clamped);
                return true;
            };

            auto mergeProps = [&](CuiMLStyleProps &dst, const CuiMLStyleProps &src)
            {
                if (src.hasTextColor)
                    dst.hasTextColor = true, dst.textColor = src.textColor;
                if (src.hasBackground)
                    dst.hasBackground = true, dst.background = src.background;
                if (src.hasBorderColor)
                    dst.hasBorderColor = true, dst.borderColor = src.borderColor;
                if (src.hasBorderWidth)
                    dst.hasBorderWidth = true, dst.borderWidth = src.borderWidth;
                if (src.hasFont)
                {
                    dst.hasFont = true;
                    QC::String::strncpy(dst.font, src.font, sizeof(dst.font) - 1);
                    dst.font[sizeof(dst.font) - 1] = '\0';
                }
                if (src.hasPadding)
                    dst.hasPadding = true, dst.padL = src.padL, dst.padT = src.padT, dst.padR = src.padR, dst.padB = src.padB;
                if (src.hasTextAlign)
                    dst.hasTextAlign = true, dst.textAlign = src.textAlign;
                if (src.hasEnabled)
                    dst.hasEnabled = true, dst.enabled = src.enabled;
                if (src.hasVisible)
                    dst.hasVisible = true, dst.visible = src.visible;
                if (src.hasOpacity)
                    dst.hasOpacity = true, dst.opacity = src.opacity;
                if (src.hasRole)
                {
                    dst.hasRole = true;
                    QC::String::strncpy(dst.role, src.role, sizeof(dst.role) - 1);
                    dst.role[sizeof(dst.role) - 1] = '\0';
                }
            };

            auto applyOpacityToColor = [&](QC::Color &c, QC::u8 opacity)
            {
                const QC::u32 a = static_cast<QC::u32>(c.a);
                c.a = static_cast<QC::u8>((a * opacity) / 255u);
            };

            auto applyOpacityToProps = [&](CuiMLStyleProps &p)
            {
                if (!p.hasOpacity)
                    return;
                if (p.hasTextColor)
                    applyOpacityToColor(p.textColor, p.opacity);
                if (p.hasBackground)
                    applyOpacityToColor(p.background, p.opacity);
                if (p.hasBorderColor)
                    applyOpacityToColor(p.borderColor, p.opacity);
            };

            QC::Vector<CuiMLStyleRule> styleRules;

            auto parseCuimlssText = [&](const char *cssText)
            {
                if (!cssText)
                    return;

                auto isNameChar = [&](char c) -> bool
                {
                    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ':';
                };

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
                            const char *end = findStr(s + 2, "*/");
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

                const char *s = cssText;
                while (s && *s)
                {
                    s = skipCssWsComments(s);
                    if (!s || !*s)
                        break;

                    CuiMLStyleRule rule{};

                    if (*s == '#')
                        rule.type = CuiMLSelectorType::Id, ++s;
                    else if (*s == '.')
                        rule.type = CuiMLSelectorType::Class, ++s;
                    else
                        rule.type = CuiMLSelectorType::Element;

                    QC::usize k = 0;
                    while (*s && isNameChar(*s) && k + 1 < sizeof(rule.key))
                        rule.key[k++] = *s++;
                    rule.key[k] = '\0';

                    s = skipCssWsComments(s);
                    if (!rule.key[0] || !s || *s != '{')
                    {
                        while (*s && *s != '{' && *s != '\n')
                            ++s;
                        if (*s == '{')
                            ++s;
                        continue;
                    }
                    ++s;

                    while (s && *s)
                    {
                        s = skipCssWsComments(s);
                        if (!s || !*s)
                            break;
                        if (*s == '}')
                        {
                            ++s;
                            break;
                        }

                        char prop[64];
                        QC::String::memset(prop, 0, sizeof(prop));
                        QC::usize pn = 0;
                        while (*s && isNameChar(*s) && pn + 1 < sizeof(prop))
                            prop[pn++] = *s++;
                        prop[pn] = '\0';
                        trimInPlace(prop);

                        s = skipCssWsComments(s);
                        if (!prop[0] || !s || *s != ':')
                        {
                            while (*s && *s != ';' && *s != '}')
                                ++s;
                            if (*s == ';')
                                ++s;
                            continue;
                        }
                        ++s;

                        char val[256];
                        QC::String::memset(val, 0, sizeof(val));
                        QC::usize vn = 0;
                        while (*s && *s != ';' && *s != '}' && vn + 1 < sizeof(val))
                            val[vn++] = *s++;
                        val[vn] = '\0';
                        trimInPlace(val);

                        if (streqIgnoreCaseAscii(prop, "color"))
                        {
                            QC::Color c;
                            if (QD::parseColorString(val, c))
                                rule.props.hasTextColor = true, rule.props.textColor = c;
                        }
                        else if (streqIgnoreCaseAscii(prop, "background") || streqIgnoreCaseAscii(prop, "background-color"))
                        {
                            QC::Color c;
                            if (QD::parseColorString(val, c))
                                rule.props.hasBackground = true, rule.props.background = c;
                        }
                        else if (streqIgnoreCaseAscii(prop, "border") || streqIgnoreCaseAscii(prop, "border-color"))
                        {
                            QC::Color c;
                            if (QD::parseColorString(val, c))
                                rule.props.hasBorderColor = true, rule.props.borderColor = c;
                        }
                        else if (streqIgnoreCaseAscii(prop, "border-width"))
                        {
                            bool ok = false;
                            const QC::i32 n = parseInt(val, ok);
                            if (ok && n > 0)
                                rule.props.hasBorderWidth = true, rule.props.borderWidth = static_cast<QC::u32>(n);
                        }
                        else if (streqIgnoreCaseAscii(prop, "font"))
                        {
                            rule.props.hasFont = true;
                            QC::String::strncpy(rule.props.font, val, sizeof(rule.props.font) - 1);
                            rule.props.font[sizeof(rule.props.font) - 1] = '\0';
                        }
                        else if (streqIgnoreCaseAscii(prop, "padding"))
                        {
                            bool ok = false;
                            const QC::i32 n = parseInt(val, ok);
                            if (ok && n >= 0)
                            {
                                rule.props.hasPadding = true;
                                const QC::u32 u = static_cast<QC::u32>(n);
                                rule.props.padL = u;
                                rule.props.padT = u;
                                rule.props.padR = u;
                                rule.props.padB = u;
                            }
                        }
                        else if (streqIgnoreCaseAscii(prop, "padding-left") || streqIgnoreCaseAscii(prop, "padding-top") ||
                                 streqIgnoreCaseAscii(prop, "padding-right") || streqIgnoreCaseAscii(prop, "padding-bottom"))
                        {
                            bool ok = false;
                            const QC::i32 n = parseInt(val, ok);
                            if (ok && n >= 0)
                            {
                                if (!rule.props.hasPadding)
                                    rule.props.hasPadding = true;
                                const QC::u32 u = static_cast<QC::u32>(n);
                                if (streqIgnoreCaseAscii(prop, "padding-left"))
                                    rule.props.padL = u;
                                else if (streqIgnoreCaseAscii(prop, "padding-top"))
                                    rule.props.padT = u;
                                else if (streqIgnoreCaseAscii(prop, "padding-right"))
                                    rule.props.padR = u;
                                else
                                    rule.props.padB = u;
                            }
                        }
                        else if (streqIgnoreCaseAscii(prop, "text-align"))
                        {
                            if (streqIgnoreCaseAscii(val, "left"))
                                rule.props.hasTextAlign = true, rule.props.textAlign = QW::Controls::TextAlign::Left;
                            else if (streqIgnoreCaseAscii(val, "center"))
                                rule.props.hasTextAlign = true, rule.props.textAlign = QW::Controls::TextAlign::Center;
                            else if (streqIgnoreCaseAscii(val, "right"))
                                rule.props.hasTextAlign = true, rule.props.textAlign = QW::Controls::TextAlign::Right;
                        }
                        else if (streqIgnoreCaseAscii(prop, "enabled"))
                        {
                            bool b = true;
                            if (parseBool(val, b))
                                rule.props.hasEnabled = true, rule.props.enabled = b;
                        }
                        else if (streqIgnoreCaseAscii(prop, "visibility"))
                        {
                            if (streqIgnoreCaseAscii(val, "hidden"))
                                rule.props.hasVisible = true, rule.props.visible = false;
                            else if (streqIgnoreCaseAscii(val, "visible"))
                                rule.props.hasVisible = true, rule.props.visible = true;
                        }
                        else if (streqIgnoreCaseAscii(prop, "opacity"))
                        {
                            QC::u8 a = 255;
                            if (parseOpacity(val, a))
                                rule.props.hasOpacity = true, rule.props.opacity = a;
                        }
                        else if (streqIgnoreCaseAscii(prop, "role"))
                        {
                            rule.props.hasRole = true;
                            QC::String::strncpy(rule.props.role, val, sizeof(rule.props.role) - 1);
                            rule.props.role[sizeof(rule.props.role) - 1] = '\0';
                            trimInPlace(rule.props.role);
                        }

                        if (*s == ';')
                            ++s;
                    }

                    applyOpacityToProps(rule.props);
                    styleRules.push_back(rule);
                }
            };

            auto loadCuimlssFromPath = [&](const char *path)
            {
                if (!path || !*path)
                    return;
                if (startsWithIgnoreCaseAscii(path, "http://") || startsWithIgnoreCaseAscii(path, "https://"))
                    return;
                char *css = readFileToOwnedCString(path);
                if (!css)
                    return;
                parseCuimlssText(css);
                operator delete[](css);
            };

            auto scanStyleImports = [&](const char *srcText)
            {
                const char *sp = srcText;
                while (sp && *sp)
                {
                    if (*sp != '<')
                    {
                        ++sp;
                        continue;
                    }
                    if (startsWithIgnoreCaseAscii(sp, "<!--"))
                    {
                        const char *end = findStr(sp, "-->");
                        if (!end)
                            break;
                        sp = end + 3;
                        continue;
                    }

                    const char *gt = findStr(sp, ">");
                    if (!gt)
                        break;

                    const QC::usize len = static_cast<QC::usize>(gt - sp + 1);
                    if (len > 1023)
                    {
                        sp = gt + 1;
                        continue;
                    }

                    char tag[1024];
                    QC::String::memset(tag, 0, sizeof(tag));
                    for (QC::usize i = 0; i < len; ++i)
                        tag[i] = sp[i];
                    tag[len] = '\0';

                    bool isClose = false;
                    const char *ns = sp + 1;
                    ns = skipWs(ns);
                    if (*ns == '/')
                        isClose = true, ++ns, ns = skipWs(ns);

                    const char *ne = ns;
                    while (*ne && *ne != ' ' && *ne != '\t' && *ne != '\r' && *ne != '\n' && *ne != '>' && *ne != '/')
                        ++ne;

                    char name[64];
                    QC::String::memset(name, 0, sizeof(name));
                    QC::usize nlen = static_cast<QC::usize>(ne - ns);
                    if (nlen >= sizeof(name))
                        nlen = sizeof(name) - 1;
                    for (QC::usize i = 0; i < nlen; ++i)
                        name[i] = ns[i];
                    name[nlen] = '\0';

                    if (!isClose && name[0])
                    {
                        if (startsWithIgnoreCaseAscii(name, "ImportStyle"))
                        {
                            char src[256];
                            QC::String::memset(src, 0, sizeof(src));
                            (void)parseAttrValue(tag, "src", src, sizeof(src));
                            if (src[0])
                                loadCuimlssFromPath(src);
                        }
                        else if (startsWithIgnoreCaseAscii(name, "link"))
                        {
                            char rel[64];
                            char type[64];
                            char href[256];
                            QC::String::memset(rel, 0, sizeof(rel));
                            QC::String::memset(type, 0, sizeof(type));
                            QC::String::memset(href, 0, sizeof(href));
                            (void)parseAttrValue(tag, "rel", rel, sizeof(rel));
                            (void)parseAttrValue(tag, "type", type, sizeof(type));
                            (void)parseAttrValue(tag, "href", href, sizeof(href));
                            if (rel[0] && type[0] && href[0] && streqIgnoreCaseAscii(rel, "stylesheet") && streqIgnoreCaseAscii(type, "text/cuimlss"))
                                loadCuimlssFromPath(href);
                        }
                    }

                    sp = gt + 1;
                }
            };

            bool isHtmlCuiml = false;
            bool inBody = true;
            {
                const char *s = skipWs(text);
                if (startsWithIgnoreCaseAscii(s, "<html"))
                {
                    const char *gt = findStr(s, ">");
                    if (gt)
                    {
                        const QC::usize len = static_cast<QC::usize>(gt - s + 1);
                        if (len <= 1023)
                        {
                            char tag[1024];
                            QC::String::memset(tag, 0, sizeof(tag));
                            for (QC::usize i = 0; i < len; ++i)
                                tag[i] = s[i];
                            tag[len] = '\0';

                            char lang[64];
                            QC::String::memset(lang, 0, sizeof(lang));
                            (void)parseAttrValue(tag, "lang", lang, sizeof(lang));
                            if (lang[0] && streqIgnoreCaseAscii(lang, "cuiml"))
                            {
                                isHtmlCuiml = true;
                                inBody = false;
                            }
                        }
                    }
                }
            }

            scanStyleImports(text);

            auto classListContains = [&](const char *classList, const char *key) -> bool
            {
                if (!classList || !*classList || !key || !*key)
                    return false;
                const QC::usize keyLen = QC::String::strlen(key);
                const char *s = classList;
                while (s && *s)
                {
                    s = skipWs(s);
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
            };

            auto computeStyleProps = [&](const char *elementName, const char *id, const char *classList) -> CuiMLStyleProps
            {
                CuiMLStyleProps out{};

                for (QC::usize i = 0; i < styleRules.size(); ++i)
                {
                    const CuiMLStyleRule &r = styleRules[i];
                    if (r.type == CuiMLSelectorType::Element && streqIgnoreCaseAscii(r.key, elementName))
                        mergeProps(out, r.props);
                }

                for (QC::usize i = 0; i < styleRules.size(); ++i)
                {
                    const CuiMLStyleRule &r = styleRules[i];
                    if (r.type == CuiMLSelectorType::Class && classListContains(classList, r.key))
                        mergeProps(out, r.props);
                }

                if (id && *id)
                {
                    for (QC::usize i = 0; i < styleRules.size(); ++i)
                    {
                        const CuiMLStyleRule &r = styleRules[i];
                        if (r.type == CuiMLSelectorType::Id && QC::String::strcmp(r.key, id) == 0)
                            mergeProps(out, r.props);
                    }
                }

                return out;
            };

            // Initialize stack with root.
            QC::Vector<BuildFrame> stack;
            {
                BuildFrame f;
                f.panel = root;
                const QC::Rect rb = root->bounds();
                f.w = static_cast<QC::i32>(rb.width);
                f.h = static_cast<QC::i32>(rb.height);
                stack.push_back(f);
            }

            const char *p = text;
            while (*p)
            {
                // Skip whitespace
                if (*p != '<')
                {
                    ++p;
                    continue;
                }

                // Comment
                if (startsWithIgnoreCaseAscii(p, "<!--"))
                {
                    const char *end = findStr(p, "-->");
                    if (!end)
                        break;
                    p = end + 3;
                    continue;
                }

                // Find tag end
                const char *gt = findStr(p, ">");
                if (!gt)
                    break;

                const QC::usize tagLen = static_cast<QC::usize>(gt - p + 1);
                if (tagLen > 1023)
                {
                    p = gt + 1;
                    continue;
                }

                char tagRaw[1024];
                QC::String::memset(tagRaw, 0, sizeof(tagRaw));
                for (QC::usize i = 0; i < tagLen; ++i)
                    tagRaw[i] = p[i];
                tagRaw[tagLen] = '\0';

                bool isClose = false;
                bool selfClose = false;

                const char *nameStart = p + 1;
                nameStart = skipWs(nameStart);
                if (*nameStart == '/')
                {
                    isClose = true;
                    ++nameStart;
                    nameStart = skipWs(nameStart);
                }

                const char *nameEnd = nameStart;
                while (*nameEnd && *nameEnd != ' ' && *nameEnd != '\t' && *nameEnd != '\r' && *nameEnd != '\n' && *nameEnd != '>' && *nameEnd != '/')
                    ++nameEnd;

                char name[64];
                QC::String::memset(name, 0, sizeof(name));
                QC::usize nlen = static_cast<QC::usize>(nameEnd - nameStart);
                if (nlen >= sizeof(name))
                    nlen = sizeof(name) - 1;
                for (QC::usize i = 0; i < nlen; ++i)
                    name[i] = nameStart[i];
                name[nlen] = '\0';

                // detect self-close
                const char *scan = gt;
                while (scan > p && (*scan == '>' || *scan == ' ' || *scan == '\t' || *scan == '\r' || *scan == '\n'))
                    --scan;
                if (*scan == '/')
                    selfClose = true;

                p = gt + 1;

                if (name[0] == '\0')
                    continue;

                if (isClose)
                {
                    if (isHtmlCuiml && startsWithIgnoreCaseAscii(name, "body"))
                    {
                        // End-of-body terminates CUIML parsing for html-wrapped files.
                        break;
                    }
                    if (startsWithIgnoreCaseAscii(name, "Panel"))
                    {
                        if (stack.size() > 1)
                            stack.pop_back();
                    }
                    continue;
                }

                if (isHtmlCuiml)
                {
                    if (startsWithIgnoreCaseAscii(name, "html") || startsWithIgnoreCaseAscii(name, "head"))
                        continue;
                    if (startsWithIgnoreCaseAscii(name, "body"))
                    {
                        inBody = true;
                        continue;
                    }
                    if (!inBody)
                        continue;
                }

                // HelpWindow: does not create native controls.
                const bool isHelpWindow = (startsWithIgnoreCaseAscii(name, "HelpWindow") ||
                                           startsWithIgnoreCaseAscii(name, "helpwindow") ||
                                           startsWithIgnoreCaseAscii(name, "help-window"));

                if (isHelpWindow)
                {
                    char src[256];
                    QC::String::memset(src, 0, sizeof(src));
                    (void)parseAttrValue(tagRaw, "src", src, sizeof(src));

                    const char *close1 = "</HelpWindow>";
                    const char *close2 = "</help-window>";

                    if (src[0] != '\0')
                    {
                        destroyHelpSpec(outHelp);
                        outHelp.srcOrUrl = dupString(src);
                        if (!selfClose)
                        {
                            const char *end1 = findStr(p, close1);
                            const char *end2 = findStr(p, close2);
                            const char *end = end1;
                            const char *chosenClose = close1;
                            if (!end || (end2 && end2 < end))
                            {
                                end = end2;
                                chosenClose = close2;
                            }

                            if (end)
                                p = end + QC::String::strlen(chosenClose);
                        }
                    }
                    else if (!selfClose)
                    {
                        destroyHelpSpec(outHelp);
                        char *inner = nullptr;

                        const char *end1 = findStr(p, close1);
                        const char *end2 = findStr(p, close2);
                        const char *end = end1;
                        const char *chosenClose = close1;
                        if (!end || (end2 && end2 < end))
                        {
                            end = end2;
                            chosenClose = close2;
                        }

                        if (end)
                        {
                            const QC::usize rawLen = static_cast<QC::usize>(end - p);
                            inner = static_cast<char *>(operator new[](rawLen + 1));
                            for (QC::usize i = 0; i < rawLen; ++i)
                                inner[i] = p[i];
                            inner[rawLen] = '\0';
                            p = end + QC::String::strlen(chosenClose);
                        }
                        if (!inner)
                            inner = dupString("");
                        stripCdataInPlace(inner);
                        outHelp.inlineHtml = inner;
                    }

                    continue;
                }

                // Ignore non-control structural tags.
                if (startsWithIgnoreCaseAscii(name, "Desktop") || startsWithIgnoreCaseAscii(name, "Layout") || startsWithIgnoreCaseAscii(name, "Theme") ||
                    startsWithIgnoreCaseAscii(name, "Background") || startsWithIgnoreCaseAscii(name, "ImportStyle") || startsWithIgnoreCaseAscii(name, "link") ||
                    startsWithIgnoreCaseAscii(name, "cui-ml"))
                {
                    continue;
                }

                // Build controls
                BuildFrame &parent = stack.back();
                QW::Controls::Panel *parentPanel = parent.panel;
                const QC::i32 parentW = parent.w;
                const QC::i32 parentH = parent.h;

                char xBuf[64];
                char yBuf[64];
                char wBuf[64];
                char hBuf[64];
                QC::String::memset(xBuf, 0, sizeof(xBuf));
                QC::String::memset(yBuf, 0, sizeof(yBuf));
                QC::String::memset(wBuf, 0, sizeof(wBuf));
                QC::String::memset(hBuf, 0, sizeof(hBuf));

                (void)parseAttrValue(tagRaw, "x", xBuf, sizeof(xBuf));
                (void)parseAttrValue(tagRaw, "y", yBuf, sizeof(yBuf));
                (void)parseAttrValue(tagRaw, "width", wBuf, sizeof(wBuf));
                (void)parseAttrValue(tagRaw, "height", hBuf, sizeof(hBuf));

                const QC::i32 x = evalDim(xBuf, parentW, "right");
                const QC::i32 y = evalDim(yBuf, parentH, "bottom");

                QC::i32 w = 0;
                QC::i32 h = 0;

                if (wBuf[0] != '\0')
                    w = evalDim(wBuf, parentW, nullptr);
                if (hBuf[0] != '\0')
                    h = evalDim(hBuf, parentH, nullptr);

                char idAttr[128];
                char classAttr[256];
                QC::String::memset(idAttr, 0, sizeof(idAttr));
                QC::String::memset(classAttr, 0, sizeof(classAttr));
                (void)parseAttrValue(tagRaw, "id", idAttr, sizeof(idAttr));
                (void)parseAttrValue(tagRaw, "class", classAttr, sizeof(classAttr));

                const CuiMLStyleProps styleProps = computeStyleProps(name, idAttr, classAttr);

                if (startsWithIgnoreCaseAscii(name, "Panel"))
                {
                    if (w <= 0)
                        w = parentW;
                    if (h <= 0)
                        h = parentH;

                    auto *panel = new QW::Controls::Panel(window, {x, y, static_cast<QC::u32>(w), static_cast<QC::u32>(h)});
                    panel->setPadding(0);
                    panel->setBorderStyle(QW::Controls::BorderStyle::None);
                    panel->setFrameVisible(false);

                    if (styleProps.hasPadding)
                        panel->setPadding(styleProps.padL, styleProps.padT, styleProps.padR, styleProps.padB);
                    if (styleProps.hasBackground)
                        panel->setBackgroundColor(styleProps.background);
                    if (styleProps.hasBorderWidth)
                    {
                        panel->setFrameVisible(true);
                        panel->setBorderStyle(QW::Controls::BorderStyle::Flat);
                        panel->setBorderWidth(styleProps.borderWidth);
                    }
                    if (styleProps.hasBorderColor)
                    {
                        panel->setFrameVisible(true);
                        panel->setBorderStyle(QW::Controls::BorderStyle::Flat);
                        panel->setBorderColor(styleProps.borderColor);
                        if (styleProps.hasBorderWidth)
                            panel->setBorderWidth(styleProps.borderWidth);
                    }
                    if (styleProps.hasEnabled)
                        panel->setEnabled(styleProps.enabled);
                    if (styleProps.hasVisible)
                        panel->setVisible(styleProps.visible);

                    // background
                    char bg[128];
                    QC::String::memset(bg, 0, sizeof(bg));
                    (void)parseAttrValue(tagRaw, "background", bg, sizeof(bg));
                    if (bg[0] != '\0')
                    {
                        QC::Color c;
                        if (QD::parseColorString(bg, c))
                            panel->setBackgroundColor(c);
                    }

                    // border (best-effort: single color)
                    char border[128];
                    QC::String::memset(border, 0, sizeof(border));
                    (void)parseAttrValue(tagRaw, "border", border, sizeof(border));
                    if (border[0] == '\0')
                        (void)parseAttrValue(tagRaw, "borderTop", border, sizeof(border));
                    if (border[0] == '\0')
                        (void)parseAttrValue(tagRaw, "borderBottom", border, sizeof(border));
                    if (border[0] == '\0')
                        (void)parseAttrValue(tagRaw, "borderLeft", border, sizeof(border));
                    if (border[0] == '\0')
                        (void)parseAttrValue(tagRaw, "borderRight", border, sizeof(border));
                    if (border[0] != '\0')
                    {
                        QC::Color c;
                        if (QD::parseColorString(border, c))
                        {
                            panel->setFrameVisible(true);
                            panel->setBorderStyle(QW::Controls::BorderStyle::Flat);
                            panel->setBorderColor(c);
                            panel->setBorderWidth(styleProps.hasBorderWidth ? styleProps.borderWidth : 1);
                        }
                    }

                    if (parentPanel)
                        parentPanel->addChild(panel);
                    outControls.push_back(panel);

                    if (!selfClose)
                    {
                        BuildFrame f;
                        f.panel = panel;
                        f.w = w;
                        f.h = h;
                        stack.push_back(f);
                    }

                    continue;
                }

                if (startsWithIgnoreCaseAscii(name, "Image") || startsWithIgnoreCaseAscii(name, "ImageView"))
                {
                    char pathAttr[256];
                    char mode[64];
                    QC::String::memset(pathAttr, 0, sizeof(pathAttr));
                    QC::String::memset(mode, 0, sizeof(mode));

                    (void)parseAttrValue(tagRaw, "path", pathAttr, sizeof(pathAttr));
                    if (pathAttr[0] == '\0')
                        (void)parseAttrValue(tagRaw, "src", pathAttr, sizeof(pathAttr));
                    (void)parseAttrValue(tagRaw, "mode", mode, sizeof(mode));
                    if (mode[0] == '\0')
                        (void)parseAttrValue(tagRaw, "scale", mode, sizeof(mode));

                    if (w <= 0)
                        w = 24;
                    if (h <= 0)
                        h = 24;

                    auto *img = new QW::Controls::ImageView(window, {x, y, static_cast<QC::u32>(w), static_cast<QC::u32>(h)});

                    QG::ImageScaleMode scale = QG::ImageScaleMode::Fit;
                    if (mode[0])
                    {
                        if (streqIgnoreCaseAscii(mode, "stretch"))
                            scale = QG::ImageScaleMode::Stretch;
                        else if (streqIgnoreCaseAscii(mode, "center"))
                            scale = QG::ImageScaleMode::Center;
                        else if (streqIgnoreCaseAscii(mode, "tile"))
                            scale = QG::ImageScaleMode::Tile;
                        else if (streqIgnoreCaseAscii(mode, "fill"))
                            scale = QG::ImageScaleMode::Fill;
                        else if (streqIgnoreCaseAscii(mode, "original"))
                            scale = QG::ImageScaleMode::Original;
                        else if (streqIgnoreCaseAscii(mode, "fit"))
                            scale = QG::ImageScaleMode::Fit;
                    }
                    img->setScaleMode(scale);

                    if (styleProps.hasEnabled)
                        img->setEnabled(styleProps.enabled);
                    if (styleProps.hasVisible)
                        img->setVisible(styleProps.visible);

                    if (pathAttr[0])
                    {
                        QC::Vector<QC::u8> bytes;
                        if (readFileToBytes(pathAttr, bytes) && !bytes.empty())
                        {
                            auto *surface = new QG::ImageSurface();
                            const bool ok = endsWithIgnoreCaseAscii(pathAttr, ".svg")
                                                ? QG::decodeSVG(bytes.data(), bytes.size(), *surface)
                                                : QG::decodePNG(bytes.data(), bytes.size(), *surface);
                            if (ok)
                            {
                                if (viewer)
                                    viewer->registerImageSurface(surface);
                                img->setImage(surface);
                            }
                            else
                            {
                                delete surface;
                                img->setImage(nullptr);
                            }
                        }
                    }

                    if (parentPanel)
                        parentPanel->addChild(img);
                    outControls.push_back(img);
                    continue;
                }

                if (startsWithIgnoreCaseAscii(name, "Label"))
                {
                    char textAttr[512];
                    QC::String::memset(textAttr, 0, sizeof(textAttr));
                    (void)parseAttrValue(tagRaw, "text", textAttr, sizeof(textAttr));

                    char fontAttr[128];
                    QC::String::memset(fontAttr, 0, sizeof(fontAttr));
                    (void)parseAttrValue(tagRaw, "font", fontAttr, sizeof(fontAttr));

                    if (fontAttr[0] == '\0' && styleProps.hasFont)
                    {
                        QC::String::strncpy(fontAttr, styleProps.font, sizeof(fontAttr) - 1);
                        fontAttr[sizeof(fontAttr) - 1] = '\0';
                    }

                    auto parseFontSizePx = [&](const char *fontSpec, QC::u32 &outPx) -> bool
                    {
                        outPx = 0;
                        if (!fontSpec || !*fontSpec)
                            return false;

                        const char *lastDash = nullptr;
                        for (const char *q = fontSpec; *q; ++q)
                            if (*q == '-')
                                lastDash = q;
                        if (!lastDash || !lastDash[1])
                            return false;

                        bool ok = false;
                        const QC::i32 v = parseInt(lastDash + 1, ok);
                        if (!ok || v <= 0)
                            return false;
                        outPx = static_cast<QC::u32>(v);
                        return true;
                    };

                    auto textScaleFromFontPx = [&](QC::u32 px) -> float
                    {
                        if (px == 0)
                            return 0.0f;
                        return static_cast<float>(px) / 12.0f;
                    };

                    auto lineHeightForTextScale = [&](float scale) -> QC::u32
                    {
                        if (scale <= 0.0f)
                            return 16;
                        float clamped = scale;
                        if (clamped < 0.5f)
                            clamped = 0.5f;
                        if (clamped > 4.0f)
                            clamped = 4.0f;
                        return static_cast<QC::u32>(12.0f * clamped + 0.5f) + 4;
                    };

                    QC::u32 fontPx = 0;
                    const bool hasFontPx = parseFontSizePx(fontAttr, fontPx);
                    const float fontScale = hasFontPx ? textScaleFromFontPx(fontPx) : 0.0f;

                    // Default size guess
                    if (w <= 0)
                        w = parentW - x;
                    if (w < 0)
                        w = 0;
                    if (h <= 0)
                    {
                        const QC::u32 lineH = lineHeightForTextScale(fontScale);
                        QC::u32 lines = 1;
                        for (const char *t = textAttr; *t; ++t)
                            if (*t == '\n')
                                ++lines;
                        h = static_cast<QC::i32>(lines * lineH);
                    }

                    auto *label = new QW::Controls::Label(window, textAttr, {x, y, static_cast<QC::u32>(w), static_cast<QC::u32>(h)});
                    label->setTransparent(true);

                    if (styleProps.hasTextAlign)
                        label->setTextAlign(styleProps.textAlign);
                    if (styleProps.hasTextColor)
                        label->setTextColor(styleProps.textColor);
                    if (styleProps.hasBackground)
                    {
                        label->setTransparent(false);
                        label->setBackgroundColor(styleProps.background);
                    }
                    if (styleProps.hasEnabled)
                        label->setEnabled(styleProps.enabled);
                    if (styleProps.hasVisible)
                        label->setVisible(styleProps.visible);

                    if (fontScale > 0.0f)
                    {
                        label->setTextScaleOverride(fontScale);
                    }

                    char color[128];
                    QC::String::memset(color, 0, sizeof(color));
                    (void)parseAttrValue(tagRaw, "color", color, sizeof(color));
                    if (color[0] != '\0')
                    {
                        QC::Color c;
                        if (QD::parseColorString(color, c))
                            label->setTextColor(c);
                    }

                    if (parentPanel)
                        parentPanel->addChild(label);
                    outControls.push_back(label);
                    continue;
                }

                if (startsWithIgnoreCaseAscii(name, "Button"))
                {
                    char textAttr[256];
                    QC::String::memset(textAttr, 0, sizeof(textAttr));
                    (void)parseAttrValue(tagRaw, "text", textAttr, sizeof(textAttr));

                    char fontAttr[128];
                    QC::String::memset(fontAttr, 0, sizeof(fontAttr));
                    (void)parseAttrValue(tagRaw, "font", fontAttr, sizeof(fontAttr));

                    if (fontAttr[0] == '\0' && styleProps.hasFont)
                    {
                        QC::String::strncpy(fontAttr, styleProps.font, sizeof(fontAttr) - 1);
                        fontAttr[sizeof(fontAttr) - 1] = '\0';
                    }

                    char actionAttr[64];
                    QC::String::memset(actionAttr, 0, sizeof(actionAttr));
                    (void)parseAttrValue(tagRaw, "action", actionAttr, sizeof(actionAttr));

                    auto parseFontSizePx = [&](const char *fontSpec, QC::u32 &outPx) -> bool
                    {
                        outPx = 0;
                        if (!fontSpec || !*fontSpec)
                            return false;

                        const char *lastDash = nullptr;
                        for (const char *q = fontSpec; *q; ++q)
                            if (*q == '-')
                                lastDash = q;
                        if (!lastDash || !lastDash[1])
                            return false;

                        bool ok = false;
                        const QC::i32 v = parseInt(lastDash + 1, ok);
                        if (!ok || v <= 0)
                            return false;
                        outPx = static_cast<QC::u32>(v);
                        return true;
                    };

                    auto textScaleFromFontPx = [&](QC::u32 px) -> float
                    {
                        if (px == 0)
                            return 0.0f;
                        return static_cast<float>(px) / 12.0f;
                    };

                    auto textPixelScaleForTextScale = [&](float scale) -> QC::u32
                    {
                        if (scale <= 0.0f)
                            return 1;
                        float clamped = scale;
                        if (clamped < 0.5f)
                            clamped = 0.5f;
                        QC::i32 rounded = static_cast<QC::i32>(clamped + 0.5f);
                        if (rounded < 1)
                            rounded = 1;
                        return static_cast<QC::u32>(rounded);
                    };

                    auto measureTextMono5x7Px = [&](const char *text, QC::u32 pixelScale) -> QC::Size
                    {
                        if (!text || !*text)
                            return QC::Size(0, 0);

                        QC::u32 maxLineChars = 0;
                        QC::u32 lineChars = 0;
                        QC::u32 lines = 1;
                        for (const char *t = text; *t; ++t)
                        {
                            if (*t == '\n')
                            {
                                if (lineChars > maxLineChars)
                                    maxLineChars = lineChars;
                                lineChars = 0;
                                ++lines;
                            }
                            else
                            {
                                ++lineChars;
                            }
                        }
                        if (lineChars > maxLineChars)
                            maxLineChars = lineChars;

                        constexpr QC::u32 kGlyphW = 6;
                        constexpr QC::u32 kGlyphH = 8;
                        return QC::Size(maxLineChars * kGlyphW * pixelScale,
                                        lines * kGlyphH * pixelScale);
                    };

                    QC::u32 fontPx = 0;
                    const bool hasFontPx = parseFontSizePx(fontAttr, fontPx);
                    const float fontScale = hasFontPx ? textScaleFromFontPx(fontPx) : 0.0f;
                    const QC::u32 pixelScale = textPixelScaleForTextScale(fontScale);

                    const bool hasWidthAttr = (wBuf[0] != '\0');
                    const bool hasHeightAttr = (hBuf[0] != '\0');

                    if (w <= 0)
                        w = 120;
                    if (h <= 0)
                        h = 28;

                    if (fontScale > 0.0f || !hasWidthAttr || !hasHeightAttr)
                    {
                        const QC::Size textSize = measureTextMono5x7Px(textAttr, pixelScale);
                        const QC::u32 minW = textSize.width + 24;
                        const QC::u32 minH = textSize.height + 20;

                        if (static_cast<QC::u32>(w) < minW)
                            w = static_cast<QC::i32>(minW);
                        if (static_cast<QC::u32>(h) < minH)
                            h = static_cast<QC::i32>(minH);
                    }

                    auto *btn = new QW::Controls::Button(window, textAttr, {x, y, static_cast<QC::u32>(w), static_cast<QC::u32>(h)});

                    if (fontScale > 0.0f)
                    {
                        btn->setTextScaleOverride(fontScale);
                    }

                    if (styleProps.hasEnabled)
                        btn->setEnabled(styleProps.enabled);
                    if (styleProps.hasVisible)
                        btn->setVisible(styleProps.visible);

                    char role[64];
                    QC::String::memset(role, 0, sizeof(role));
                    (void)parseAttrValue(tagRaw, "role", role, sizeof(role));

                    if (role[0] == '\0' && styleProps.hasRole)
                    {
                        QC::String::strncpy(role, styleProps.role, sizeof(role) - 1);
                        role[sizeof(role) - 1] = '\0';
                    }

                    if (role[0] != '\0')
                    {
                        if (startsWithIgnoreCaseAscii(role, "destructive"))
                            btn->setRole(QW::ButtonRole::Destructive);
                        else
                            btn->setRole(QW::ButtonRole::Default);
                    }
                    else
                    {
                        btn->setRole(QW::ButtonRole::Default);
                    }

                    // Security/policy: child/preview windows are not permitted to request shutdown.
                    // If a shutdown action is expressed in markup, intercept and deny with a message.
                    const bool looksLikeShutdown =
                        (actionAttr[0] && streqIgnoreCaseAscii(actionAttr, "shutdown")) ||
                        (idAttr[0] && streqIgnoreCaseAscii(idAttr, "shutDownButton")) ||
                        (textAttr[0] && streqIgnoreCaseAscii(textAttr, "Shut Down"));

                    if (looksLikeShutdown && viewer)
                    {
                        btn->setClickHandler(&QD::CuiMLViewer::onShutdownDeniedRequested, viewer);
                    }

                    if (parentPanel)
                        parentPanel->addChild(btn);
                    outControls.push_back(btn);
                    continue;
                }

                // Unknown tag: ignore.
            }

            return p;
        }

    } // namespace

    CuiMLViewer::CuiMLViewer(Desktop *desktop)
        : m_desktop(desktop)
    {
    }

    CuiMLViewer::~CuiMLViewer()
    {
        close();
    }

    void CuiMLViewer::openShutdownDeniedDialog()
    {
        if (m_shutdownDeniedWindow)
        {
            QW::WindowManager::instance().bringToFront(m_shutdownDeniedWindow);
            QW::WindowManager::instance().setFocus(m_shutdownDeniedWindow);
            m_shutdownDeniedWindow->setVisible(true);
            QW::WindowManager::instance().render();
            return;
        }

        if (!m_desktop)
            return;

        static constexpr QC::i32 DIALOG_WIDTH = 520;
        static constexpr QC::i32 DIALOG_HEIGHT = 180;

        const QC::Rect work = m_desktop->workArea();
        QC::i32 x = work.x + static_cast<QC::i32>((work.width - DIALOG_WIDTH) / 2);
        QC::i32 y = work.y + static_cast<QC::i32>((work.height - DIALOG_HEIGHT) / 2);
        QW::Rect bounds = {x, y, static_cast<QC::u32>(DIALOG_WIDTH), static_cast<QC::u32>(DIALOG_HEIGHT)};

        m_shutdownDeniedWindow = QW::WindowManager::instance().createWindow("Denied", bounds);
        if (!m_shutdownDeniedWindow)
            return;

        m_shutdownDeniedWindowId = m_shutdownDeniedWindow->windowId();
        m_shutdownDeniedWindow->setFlags(QW::WindowFlags::Visible | QW::WindowFlags::Movable | QW::WindowFlags::HasTitle | QW::WindowFlags::HasBorder);

        m_shutdownDeniedRoot = m_shutdownDeniedWindow->root();
        if (!m_shutdownDeniedRoot)
        {
            closeShutdownDeniedDialog();
            return;
        }

        m_shutdownDeniedRoot->setPadding(14);
        m_shutdownDeniedRoot->setBorderStyle(QW::Controls::BorderStyle::None);

        QW::Rect msgBounds = {18, 24, static_cast<QC::u32>(DIALOG_WIDTH - 36), 70};
        auto *msg = new QW::Controls::Label(
            m_shutdownDeniedWindow,
            "Shutdown requests are not allowed from preview/child windows.",
            msgBounds);
        msg->setWordWrap(true);
        m_shutdownDeniedRoot->addChild(msg);

        const QC::i32 buttonWidth = 160;
        const QC::i32 buttonHeight = 32;
        const QC::i32 baseY = DIALOG_HEIGHT - buttonHeight - 20;
        const QC::i32 startX = (DIALOG_WIDTH - buttonWidth) / 2;
        QW::Rect okBounds = {startX, baseY, static_cast<QC::u32>(buttonWidth), static_cast<QC::u32>(buttonHeight)};
        m_shutdownDeniedOk = new QW::Controls::Button(m_shutdownDeniedWindow, "OK", okBounds);
        m_shutdownDeniedOk->setRole(QW::ButtonRole::Default);
        m_shutdownDeniedOk->setClickHandler(&CuiMLViewer::onShutdownDeniedOkClick, this);
        m_shutdownDeniedRoot->addChild(m_shutdownDeniedOk);

        QW::WindowManager::instance().bringToFront(m_shutdownDeniedWindow);
        QW::WindowManager::instance().setFocus(m_shutdownDeniedWindow);
        m_shutdownDeniedWindow->setVisible(true);
        QW::WindowManager::instance().render();
    }

    void CuiMLViewer::closeShutdownDeniedDialog()
    {
        if (!m_shutdownDeniedWindow)
            return;

        QW::WindowManager::instance().destroyWindow(m_shutdownDeniedWindow);
        m_shutdownDeniedWindow = nullptr;

        m_shutdownDeniedWindowId = 0;
        m_shutdownDeniedRoot = nullptr;
        m_shutdownDeniedOk = nullptr;
    }

    void CuiMLViewer::onShutdownDeniedRequested(QW::Controls::Button *button, void *userData)
    {
        (void)button;
        auto *self = static_cast<CuiMLViewer *>(userData);
        if (!self)
            return;
        self->openShutdownDeniedDialog();
    }

    void CuiMLViewer::onShutdownDeniedOkClick(QW::Controls::Button *button, void *userData)
    {
        (void)button;
        auto *self = static_cast<CuiMLViewer *>(userData);
        if (!self)
            return;

        self->closeShutdownDeniedDialog();
        if (self->m_window)
        {
            QW::WindowManager::instance().bringToFront(self->m_window);
            QW::WindowManager::instance().setFocus(self->m_window);
        }
    }

    bool CuiMLViewer::windowStillAlive() const
    {
        if (!m_windowId)
            return false;
        return QW::WindowManager::instance().windowById(m_windowId) != nullptr;
    }

    bool CuiMLViewer::isOpen() const
    {
        if (!m_window)
            return false;
        return windowStillAlive();
    }

    void CuiMLViewer::ensureWindow()
    {
        if (m_window)
        {
            if (!windowStillAlive())
            {
                m_window = nullptr;
                m_windowId = 0;
                m_root = nullptr;
                m_controls.clear();
            }
            else
            {
                QW::WindowManager::instance().bringToFront(m_window);
                QW::WindowManager::instance().setFocus(m_window);
                return;
            }
        }

        if (!m_desktop)
            return;

        const QC::Rect wa = m_desktop->workArea();
        const QC::u32 w = (wa.width > 920) ? 920 : wa.width;
        const QC::u32 h = (wa.height > 640) ? 640 : wa.height;
        const QC::i32 x = wa.x + static_cast<QC::i32>((wa.width > w) ? ((wa.width - w) / 2) : 0);
        const QC::i32 y = wa.y + 16;

        m_window = QW::WindowManager::instance().createWindow("CUI-ML Desktop", {x, y, w, h});
        if (!m_window)
            return;

        m_windowId = m_window->windowId();
        m_root = m_window->root();
        if (m_root)
        {
            m_root->setBorderStyle(QW::Controls::BorderStyle::None);
            m_root->setPadding(0);
            m_root->setBackgroundColor(QC::Color(255, 255, 255, 255));
        }

        m_window->setFlags(QW::WindowFlags::Visible | QW::WindowFlags::Resizable | QW::WindowFlags::Movable | QW::WindowFlags::HasTitle | QW::WindowFlags::HasBorder);

        // Title bar (and close button) like Terminal.
        // Note: WindowManager treats the top 24px as draggable title region.
        static constexpr QC::i32 kTitleBarHeight = 24;
        static constexpr QC::i32 kPad = 8;
        if (m_root)
        {
            auto *titleBar = new QW::Controls::Panel(m_window, {0, 0, w, static_cast<QC::u32>(kTitleBarHeight)});
            titleBar->setBorderStyle(QW::Controls::BorderStyle::None);
            titleBar->setPadding(0);
            titleBar->setBackgroundColor(QC::Color(20, 20, 20, 255));
            m_root->addChild(titleBar);
            m_controls.push_back(titleBar);

            auto *titleLabel = new QW::Controls::Label(m_window, "CUI-ML", {kPad, 4, w > 64 ? (w - 64) : w, 16});
            titleLabel->setTextColor(QC::Color(230, 230, 230, 255));
            titleBar->addChild(titleLabel);
            m_controls.push_back(titleLabel);

            auto *closeButton = new QW::Controls::Button(m_window, "X", {static_cast<QC::i32>(w - kPad - 20), 2, 20, 20});
            closeButton->setRole(QW::ButtonRole::Destructive);
            closeButton->setClickHandler([](QW::Controls::Button *, void *ud) {
                auto *self = static_cast<CuiMLViewer *>(ud);
                if (self)
                    self->close();
            }, this);
            titleBar->addChild(closeButton);
            m_controls.push_back(closeButton);
        }

        m_window->invalidate();
        QW::WindowManager::instance().render();

        if (m_desktop)
        {
            m_desktop->addTaskbarWindow(m_windowId, "CUI-ML");
            m_desktop->setActiveTaskbarWindow(m_windowId);
        }
    }

    void CuiMLViewer::clearControls()
    {
        // Delete created controls (children before parents).
        for (QC::isize i = static_cast<QC::isize>(m_controls.size()) - 1; i >= 0; --i)
        {
            QW::Controls::IControl *c = m_controls[static_cast<QC::usize>(i)];
            if (!c)
                continue;
            if (m_root)
            {
                if (QW::Controls::Panel *p = c->parent())
                {
                    p->removeChild(c);
                }
            }
            delete c;
        }
        m_controls.clear();

        for (QC::usize i = 0; i < m_imageSurfaces.size(); ++i)
            delete m_imageSurfaces[i];
        m_imageSurfaces.clear();
    }

    void CuiMLViewer::registerImageSurface(QG::ImageSurface *surface)
    {
        if (!surface)
            return;
        m_imageSurfaces.push_back(surface);
    }

    void CuiMLViewer::openFile(const char *path)
    {
        ensureWindow();
        if (!m_window || !m_root)
            return;

        clearControls();

        char absPath[256];
        QC::String::memset(absPath, 0, sizeof(absPath));

        if (!path || !*path)
            path = "";

        if (!hasSlash(path))
        {
            QC::String::strncpy(absPath, "/shared/", sizeof(absPath) - 1);
            const QC::usize used = QC::String::strlen(absPath);
            QC::String::strncpy(absPath + used, path, sizeof(absPath) - 1 - used);
        }
        else
        {
            QC::String::strncpy(absPath, path, sizeof(absPath) - 1);
        }

        char *cuiml = readFileToOwnedCString(absPath);
        if (!cuiml)
        {
            QC_LOG_WARN("QDCuiML", "Failed to read %s", absPath);
            return;
        }

        HelpSpec help{};
        parseAndBuild(m_desktop, this, m_window, m_root, cuiml, m_controls, help);

        // Auto-open help window (separate) if specified.
        if (m_desktop)
        {
            if (help.srcOrUrl && help.srcOrUrl[0])
            {
                if (startsWithIgnoreCaseAscii(help.srcOrUrl, "http://") || startsWithIgnoreCaseAscii(help.srcOrUrl, "https://"))
                    m_desktop->openBrowserUrl(help.srcOrUrl);
                else
                    m_desktop->openBrowserFile(help.srcOrUrl);
            }
            else if (help.inlineHtml)
            {
                m_desktop->openBrowserHtmlText(help.inlineHtml);
            }
        }

        destroyHelpSpec(help);

        operator delete[](cuiml);

        QW::WindowManager::instance().bringToFront(m_window);
        QW::WindowManager::instance().setFocus(m_window);
        m_window->invalidate();
        QW::WindowManager::instance().render();
    }

    void CuiMLViewer::close()
    {
        closeShutdownDeniedDialog();

        if (!m_window)
            return;

        const QC::u32 closingId = m_windowId ? m_windowId : m_window->windowId();
        if (m_desktop)
        {
            m_desktop->removeTaskbarWindow(closingId);
        }

        clearControls();

        m_windowId = 0;
        QW::WindowManager::instance().destroyWindow(m_window);
        m_window = nullptr;
        m_root = nullptr;
    }

} // namespace QD
