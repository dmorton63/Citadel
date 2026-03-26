#include "QG/SVG.h"

#include "QG/Image.h"

#include "QCColor.h"
#include "QCLogger.h"
#include "QCString.h"

namespace QG
{
    namespace
    {
        constexpr const char *LOG_MODULE = "QGSVG";

        struct Vec2
        {
            float x = 0.0f;
            float y = 0.0f;
        };

        struct StrokeStyle
        {
            QC::Color color = QC::Color::white();
            float width = 1.0f;
            bool roundCap = false;
            bool roundJoin = false;
            bool enabled = true;
        };

        inline bool isWs(char c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',';
        }

        inline bool isAlpha(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == ':' || c == '_';
        }

        inline bool isDigit(char c)
        {
            return c >= '0' && c <= '9';
        }

        static void skipWs(const char *&p, const char *end)
        {
            while (p < end && isWs(*p))
                ++p;
        }

        static bool parseFloat(const char *&p, const char *end, float &out)
        {
            skipWs(p, end);
            if (p >= end)
                return false;

            const char *s = p;
            bool neg = false;
            if (*s == '+' || *s == '-')
            {
                neg = (*s == '-');
                ++s;
            }

            double value = 0.0;
            bool any = false;
            while (s < end && isDigit(*s))
            {
                any = true;
                value = value * 10.0 + static_cast<double>(*s - '0');
                ++s;
            }

            if (s < end && *s == '.')
            {
                ++s;
                double base = 0.1;
                while (s < end && isDigit(*s))
                {
                    any = true;
                    value += static_cast<double>(*s - '0') * base;
                    base *= 0.1;
                    ++s;
                }
            }

            if (!any)
                return false;

            // Exponent (optional)
            if (s < end && (*s == 'e' || *s == 'E'))
            {
                const char *e = s + 1;
                bool expNeg = false;
                if (e < end && (*e == '+' || *e == '-'))
                {
                    expNeg = (*e == '-');
                    ++e;
                }

                int exp = 0;
                bool expAny = false;
                while (e < end && isDigit(*e))
                {
                    expAny = true;
                    exp = exp * 10 + (*e - '0');
                    ++e;
                }

                if (expAny)
                {
                    double pow10 = 1.0;
                    int n = exp;
                    while (n-- > 0)
                        pow10 *= 10.0;
                    value = expNeg ? (value / pow10) : (value * pow10);
                    s = e;
                }
            }

            p = s;
            out = static_cast<float>(neg ? -value : value);
            return true;
        }

        static bool parseBoolFlag(const char *&p, const char *end, int &out)
        {
            skipWs(p, end);
            if (p >= end)
                return false;
            if (*p == '0')
            {
                out = 0;
                ++p;
                return true;
            }
            if (*p == '1')
            {
                out = 1;
                ++p;
                return true;
            }
            // Some SVGs use "true/false"; accept minimally.
            if ((end - p) >= 4 && (p[0] == 't' || p[0] == 'T'))
            {
                out = 1;
                p += 4;
                return true;
            }
            if ((end - p) >= 5 && (p[0] == 'f' || p[0] == 'F'))
            {
                out = 0;
                p += 5;
                return true;
            }
            return false;
        }

        static bool parseHexNibble(char c, QC::u8 &out)
        {
            if (c >= '0' && c <= '9')
            {
                out = static_cast<QC::u8>(c - '0');
                return true;
            }
            if (c >= 'a' && c <= 'f')
            {
                out = static_cast<QC::u8>(10 + (c - 'a'));
                return true;
            }
            if (c >= 'A' && c <= 'F')
            {
                out = static_cast<QC::u8>(10 + (c - 'A'));
                return true;
            }
            return false;
        }

        static bool parseColor(const char *text, QC::Color &out, const SVGDecodeOptions &options)
        {
            if (!text || !*text)
                return false;

            if (QC::String::strcmp(text, "none") == 0)
                return false;

            if (QC::String::strcmp(text, "currentColor") == 0)
            {
                out = QC::Color(options.currentColorARGB);
                return true;
            }

            if (text[0] == '#')
            {
                const QC::usize len = QC::String::strlen(text);
                if (len == 7) // #RRGGBB
                {
                    QC::u8 r1, r2, g1, g2, b1, b2;
                    if (!parseHexNibble(text[1], r1) || !parseHexNibble(text[2], r2) ||
                        !parseHexNibble(text[3], g1) || !parseHexNibble(text[4], g2) ||
                        !parseHexNibble(text[5], b1) || !parseHexNibble(text[6], b2))
                        return false;
                    out = QC::Color(static_cast<QC::u8>((r1 << 4) | r2),
                                   static_cast<QC::u8>((g1 << 4) | g2),
                                   static_cast<QC::u8>((b1 << 4) | b2),
                                   255);
                    return true;
                }
                if (len == 4) // #RGB
                {
                    QC::u8 r, g, b;
                    if (!parseHexNibble(text[1], r) || !parseHexNibble(text[2], g) || !parseHexNibble(text[3], b))
                        return false;
                    out = QC::Color(static_cast<QC::u8>((r << 4) | r),
                                   static_cast<QC::u8>((g << 4) | g),
                                   static_cast<QC::u8>((b << 4) | b),
                                   255);
                    return true;
                }
            }

            // Minimal named support
            if (QC::String::strcmp(text, "black") == 0)
            {
                out = QC::Color::black();
                return true;
            }
            if (QC::String::strcmp(text, "white") == 0)
            {
                out = QC::Color::white();
                return true;
            }

            return false;
        }

        static bool tagEquals(const char *a, QC::usize aLen, const char *b)
        {
            const QC::usize bLen = QC::String::strlen(b);
            if (aLen != bLen)
                return false;
            for (QC::usize i = 0; i < aLen; ++i)
            {
                char c1 = a[i];
                char c2 = b[i];
                if (c1 >= 'A' && c1 <= 'Z')
                    c1 = static_cast<char>(c1 - 'A' + 'a');
                if (c2 >= 'A' && c2 <= 'Z')
                    c2 = static_cast<char>(c2 - 'A' + 'a');
                if (c1 != c2)
                    return false;
            }
            return true;
        }

        static bool findAttr(const char *tagStart, const char *tagEnd, const char *key, const char *&outValStart, const char *&outValEnd)
        {
            const QC::usize keyLen = QC::String::strlen(key);
            const char *p = tagStart;
            while (p < tagEnd)
            {
                skipWs(p, tagEnd);
                if (p >= tagEnd)
                    break;

                // Parse name
                const char *nameStart = p;
                while (p < tagEnd && !isWs(*p) && *p != '=' && *p != '>' && *p != '/')
                    ++p;
                const char *nameEnd = p;
                skipWs(p, tagEnd);
                if (p >= tagEnd || *p != '=')
                {
                    // No value; skip token.
                    while (p < tagEnd && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
                        ++p;
                    continue;
                }
                ++p; // '='
                skipWs(p, tagEnd);
                if (p >= tagEnd)
                    break;

                char quote = 0;
                if (*p == '\'' || *p == '"')
                {
                    quote = *p;
                    ++p;
                }

                const char *valStart = p;
                if (quote)
                {
                    while (p < tagEnd && *p != quote)
                        ++p;
                    const char *valEnd = p;
                    if (p < tagEnd)
                        ++p;

                    const QC::usize nameLen = static_cast<QC::usize>(nameEnd - nameStart);
                    if (nameLen == keyLen && QC::String::memcmp(nameStart, key, keyLen) == 0)
                    {
                        outValStart = valStart;
                        outValEnd = valEnd;
                        return true;
                    }
                }
                else
                {
                    while (p < tagEnd && !isWs(*p) && *p != '>' && *p != '/')
                        ++p;
                    const char *valEnd = p;

                    const QC::usize nameLen = static_cast<QC::usize>(nameEnd - nameStart);
                    if (nameLen == keyLen && QC::String::memcmp(nameStart, key, keyLen) == 0)
                    {
                        outValStart = valStart;
                        outValEnd = valEnd;
                        return true;
                    }
                }
            }
            return false;
        }

        static bool copyAttr(const char *tagStart, const char *tagEnd, const char *key, char *out, QC::usize outCap)
        {
            if (!out || outCap == 0)
                return false;
            out[0] = '\0';
            const char *vs = nullptr;
            const char *ve = nullptr;
            if (!findAttr(tagStart, tagEnd, key, vs, ve) || !vs || !ve || ve < vs)
                return false;
            QC::usize len = static_cast<QC::usize>(ve - vs);
            if (len + 1 > outCap)
                len = outCap - 1;
            for (QC::usize i = 0; i < len; ++i)
                out[i] = vs[i];
            out[len] = '\0';
            return out[0] != '\0';
        }

        static void blendPixel(ImageSurface &s, QC::i32 x, QC::i32 y, QC::Color src)
        {
            if (x < 0 || y < 0)
                return;
            if (static_cast<QC::u32>(x) >= s.width || static_cast<QC::u32>(y) >= s.height)
                return;
            QC::u32 &dst = s.pixels[static_cast<QC::usize>(y) * s.width + static_cast<QC::usize>(x)];
            if (src.a == 255)
            {
                dst = src.value;
                return;
            }
            if (src.a == 0)
                return;
            QC::Color dstC(dst);
            dst = src.blend(dstC).value;
        }

        static void stampDisk(ImageSurface &s, QC::i32 cx, QC::i32 cy, QC::i32 radius, QC::Color color)
        {
            if (radius <= 0)
            {
                blendPixel(s, cx, cy, color);
                return;
            }

            const QC::i32 r2 = radius * radius;
            for (QC::i32 dy = -radius; dy <= radius; ++dy)
            {
                const QC::i32 yy = cy + dy;
                for (QC::i32 dx = -radius; dx <= radius; ++dx)
                {
                    if (dx * dx + dy * dy <= r2)
                        blendPixel(s, cx + dx, yy, color);
                }
            }
        }

        static void strokeLine(ImageSurface &s, QC::i32 x1, QC::i32 y1, QC::i32 x2, QC::i32 y2, QC::Color color, QC::u32 widthPx)
        {
            if (widthPx == 0)
                return;

            const QC::i32 radius = static_cast<QC::i32>(widthPx / 2);

            QC::i32 dx = x2 - x1;
            QC::i32 dy = y2 - y1;
            QC::i32 sx = (dx > 0) ? 1 : -1;
            QC::i32 sy = (dy > 0) ? 1 : -1;
            dx = (dx > 0) ? dx : -dx;
            dy = (dy > 0) ? dy : -dy;

            QC::i32 err = dx - dy;
            QC::i32 x = x1;
            QC::i32 y = y1;

            while (true)
            {
                stampDisk(s, x, y, radius, color);
                if (x == x2 && y == y2)
                    break;

                QC::i32 e2 = err << 1;
                if (e2 > -dy)
                {
                    err -= dy;
                    x += sx;
                }
                if (e2 < dx)
                {
                    err += dx;
                    y += sy;
                }
            }
        }

        static void strokePolyline(ImageSurface &s, const QC::Vector<Vec2> &pts, QC::Color color, QC::u32 widthPx, bool closed)
        {
            if (pts.size() < 2)
                return;
            for (QC::usize i = 1; i < pts.size(); ++i)
            {
                const QC::i32 x1 = static_cast<QC::i32>(pts[i - 1].x + 0.5f);
                const QC::i32 y1 = static_cast<QC::i32>(pts[i - 1].y + 0.5f);
                const QC::i32 x2 = static_cast<QC::i32>(pts[i].x + 0.5f);
                const QC::i32 y2 = static_cast<QC::i32>(pts[i].y + 0.5f);
                strokeLine(s, x1, y1, x2, y2, color, widthPx);
            }
            if (closed)
            {
                const QC::i32 x1 = static_cast<QC::i32>(pts[pts.size() - 1].x + 0.5f);
                const QC::i32 y1 = static_cast<QC::i32>(pts[pts.size() - 1].y + 0.5f);
                const QC::i32 x2 = static_cast<QC::i32>(pts[0].x + 0.5f);
                const QC::i32 y2 = static_cast<QC::i32>(pts[0].y + 0.5f);
                strokeLine(s, x1, y1, x2, y2, color, widthPx);
            }
        }


        // Tiny sin/cos approximations good enough for icon circles.
        static float wrapPi(float x)
        {
            const float twoPi = 6.28318530718f;
            while (x > 3.14159265359f)
                x -= twoPi;
            while (x < -3.14159265359f)
                x += twoPi;
            return x;
        }

        static float sinApprox(float x)
        {
            x = wrapPi(x);
            const float x2 = x * x;
            // 7th-order Taylor
            return x * (1.0f - x2 / 6.0f + (x2 * x2) / 120.0f - (x2 * x2 * x2) / 5040.0f);
        }

        static float cosApprox(float x)
        {
            x = wrapPi(x);
            const float x2 = x * x;
            // 6th-order Taylor
            return 1.0f - x2 / 2.0f + (x2 * x2) / 24.0f - (x2 * x2 * x2) / 720.0f;
        }
        
        static float atan2Approx(float y, float x)
        {
            // Fast atan2 approximation (good enough for icon arcs)
            // Reference form commonly used in realtime rendering.
            const float absY = (y < 0.0f ? -y : y) + 1e-10f;
            float angle = 0.0f;
            if (x < 0.0f)
            {
                const float r = (x + absY) / (absY - x);
                angle = 2.35619449019f; // 3*pi/4
                angle += (0.1963f * r * r - 0.9817f) * r;
            }
            else
            {
                const float r = (x - absY) / (x + absY);
                angle = 0.78539816339f; // pi/4
                angle += (0.1963f * r * r - 0.9817f) * r;
            }
            return (y < 0.0f) ? -angle : angle;
        }

        static void appendCircle(QC::Vector<Vec2> &out, float cx, float cy, float r)
        {
            int segments = static_cast<int>(r * 6.0f);
            if (segments < 24)
                segments = 24;
            if (segments > 128)
                segments = 128;

            const float step = 6.28318530718f / static_cast<float>(segments);
            for (int i = 0; i <= segments; ++i)
            {
                const float a = step * static_cast<float>(i);
                out.push_back({cx + cosApprox(a) * r, cy + sinApprox(a) * r});
            }
        }

        static void appendCubic(QC::Vector<Vec2> &out, Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, int segments)
        {
            if (segments < 4)
                segments = 4;
            for (int i = 1; i <= segments; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(segments);
                const float it = 1.0f - t;
                const float b0 = it * it * it;
                const float b1 = 3.0f * it * it * t;
                const float b2 = 3.0f * it * t * t;
                const float b3 = t * t * t;
                out.push_back({p0.x * b0 + p1.x * b1 + p2.x * b2 + p3.x * b3,
                               p0.y * b0 + p1.y * b1 + p2.y * b2 + p3.y * b3});
            }
        }

        static void appendQuadratic(QC::Vector<Vec2> &out, Vec2 p0, Vec2 p1, Vec2 p2, int segments)
        {
            if (segments < 4)
                segments = 4;
            for (int i = 1; i <= segments; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(segments);
                const float it = 1.0f - t;
                const float b0 = it * it;
                const float b1 = 2.0f * it * t;
                const float b2 = t * t;
                out.push_back({p0.x * b0 + p1.x * b1 + p2.x * b2,
                               p0.y * b0 + p1.y * b1 + p2.y * b2});
            }
        }

        static float absf(float v)
        {
            return v < 0.0f ? -v : v;
        }

        static float sqrtApprox(float x)
        {
            if (x <= 0.0f)
                return 0.0f;
            // Newton-Raphson
            float g = x;
            for (int i = 0; i < 6; ++i)
                g = 0.5f * (g + x / g);
            return g;
        }

        static void appendArc(QC::Vector<Vec2> &out, Vec2 p0, float rx, float ry, float xAxisRotationDeg, int largeArc, int sweep, Vec2 p1)
        {
            // Based on SVG arc implementation notes (center parameterization).
            // This is a best-effort implementation for icons; it assumes sane inputs.

            if (rx == 0.0f || ry == 0.0f)
            {
                out.push_back(p1);
                return;
            }

            // Convert to radians
            const float phi = xAxisRotationDeg * 0.01745329252f;
            const float cosPhi = cosApprox(phi);
            const float sinPhi = sinApprox(phi);

            // Step 1: compute (x1', y1')
            const float dx2 = (p0.x - p1.x) * 0.5f;
            const float dy2 = (p0.y - p1.y) * 0.5f;
            const float x1p = cosPhi * dx2 + sinPhi * dy2;
            const float y1p = -sinPhi * dx2 + cosPhi * dy2;

            rx = absf(rx);
            ry = absf(ry);

            // Ensure radii are large enough
            const float rx2 = rx * rx;
            const float ry2 = ry * ry;
            const float x1p2 = x1p * x1p;
            const float y1p2 = y1p * y1p;

            float lambda = x1p2 / rx2 + y1p2 / ry2;
            if (lambda > 1.0f)
            {
                const float s = sqrtApprox(lambda);
                rx *= s;
                ry *= s;
            }

            const float rx2b = rx * rx;
            const float ry2b = ry * ry;

            // Step 2: compute (cx', cy')
            const float num = (rx2b * ry2b) - (rx2b * y1p2) - (ry2b * x1p2);
            const float den = (rx2b * y1p2) + (ry2b * x1p2);
            float factor = 0.0f;
            if (den > 0.0f)
            {
                float f = num / den;
                if (f < 0.0f)
                    f = 0.0f;
                factor = sqrtApprox(f);
            }
            if (largeArc == sweep)
                factor = -factor;

            const float cxp = factor * ((rx * y1p) / ry);
            const float cyp = factor * (-(ry * x1p) / rx);

            // Step 3: compute (cx, cy)
            const float mx = (p0.x + p1.x) * 0.5f;
            const float my = (p0.y + p1.y) * 0.5f;
            const float cx = cosPhi * cxp - sinPhi * cyp + mx;
            const float cy = sinPhi * cxp + cosPhi * cyp + my;

            // Compute start angle and delta angle
            const float ux = (x1p - cxp) / rx;
            const float uy = (y1p - cyp) / ry;
            const float vx = (-x1p - cxp) / rx;
            const float vy = (-y1p - cyp) / ry;

            float theta1 = atan2Approx(uy, ux);
            float dtheta = atan2Approx((ux * vy) - (uy * vx), (ux * vx) + (uy * vy));

            // Normalize delta to match sweep
            if (!sweep && dtheta > 0.0f)
                dtheta -= 6.28318530718f;
            else if (sweep && dtheta < 0.0f)
                dtheta += 6.28318530718f;

            // Segment count based on arc length
            const float avgR = (absf(rx) + absf(ry)) * 0.5f;
            int segments = static_cast<int>(avgR * absf(dtheta) * 0.5f);
            if (segments < 6)
                segments = 6;
            if (segments > 64)
                segments = 64;

            for (int i = 1; i <= segments; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(segments);
                const float th = theta1 + dtheta * t;
                const float cosTh = cosApprox(th);
                const float sinTh = sinApprox(th);

                // Ellipse point (x', y')
                const float xep = rx * cosTh;
                const float yep = ry * sinTh;

                // Rotate back and translate
                const float x = cosPhi * xep - sinPhi * yep + cx;
                const float y = sinPhi * xep + cosPhi * yep + cy;
                out.push_back({x, y});
            }
        }

        struct Transform
        {
            float scale = 1.0f;
            float offX = 0.0f;
            float offY = 0.0f;
        };

        static Vec2 apply(const Transform &t, Vec2 v)
        {
            return {v.x * t.scale + t.offX, v.y * t.scale + t.offY};
        }

        static bool parseViewBox(const char *text, float &minX, float &minY, float &w, float &h)
        {
            if (!text)
                return false;
            const char *p = text;
            const char *end = text + QC::String::strlen(text);
            return parseFloat(p, end, minX) && parseFloat(p, end, minY) && parseFloat(p, end, w) && parseFloat(p, end, h);
        }

        static void strokePathD(ImageSurface &surface,
                                const char *dStart,
                                const char *dEnd,
                                const Transform &xform,
                                const StrokeStyle &stroke,
                                const SVGDecodeOptions &options)
        {
            if (!stroke.enabled)
                return;

            QC::Vector<Vec2> poly;
            poly.reserve(128);

            Vec2 cur{0.0f, 0.0f};
            Vec2 subStart{0.0f, 0.0f};

            auto flushOpen = [&](bool closed) {
                if (poly.size() >= 2)
                {
                    const QC::u32 w = stroke.width < 1.0f ? 1u : static_cast<QC::u32>(stroke.width + 0.5f);
                    strokePolyline(surface, poly, stroke.color, w, closed);
                }
                poly.clear();
            };

            char cmd = 0;
            const char *p = dStart;
            while (p < dEnd)
            {
                skipWs(p, dEnd);
                if (p >= dEnd)
                    break;

                if (isAlpha(*p))
                {
                    cmd = *p;
                    ++p;
                }

                if (!cmd)
                    break;

                const bool rel = (cmd >= 'a' && cmd <= 'z');
                const char uc = rel ? static_cast<char>(cmd - 'a' + 'A') : cmd;

                if (uc == 'M')
                {
                    float x = 0.0f, y = 0.0f;
                    if (!parseFloat(p, dEnd, x) || !parseFloat(p, dEnd, y))
                        break;
                    if (rel)
                    {
                        x += cur.x;
                        y += cur.y;
                    }
                    flushOpen(false);
                    cur = {x, y};
                    subStart = cur;
                    poly.push_back(apply(xform, cur));

                    // Subsequent coordinate pairs are treated as implicit "L".
                    cmd = rel ? 'l' : 'L';
                    continue;
                }

                if (uc == 'Z')
                {
                    flushOpen(true);
                    cur = subStart;
                    continue;
                }

                if (uc == 'L')
                {
                    float x = 0.0f, y = 0.0f;
                    if (!parseFloat(p, dEnd, x) || !parseFloat(p, dEnd, y))
                        break;
                    if (rel)
                    {
                        x += cur.x;
                        y += cur.y;
                    }
                    cur = {x, y};
                    poly.push_back(apply(xform, cur));
                    continue;
                }

                if (uc == 'H')
                {
                    float x = 0.0f;
                    if (!parseFloat(p, dEnd, x))
                        break;
                    if (rel)
                        x += cur.x;
                    cur = {x, cur.y};
                    poly.push_back(apply(xform, cur));
                    continue;
                }

                if (uc == 'V')
                {
                    float y = 0.0f;
                    if (!parseFloat(p, dEnd, y))
                        break;
                    if (rel)
                        y += cur.y;
                    cur = {cur.x, y};
                    poly.push_back(apply(xform, cur));
                    continue;
                }

                if (uc == 'C')
                {
                    float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f, x = 0.0f, y = 0.0f;
                    if (!parseFloat(p, dEnd, x1) || !parseFloat(p, dEnd, y1) ||
                        !parseFloat(p, dEnd, x2) || !parseFloat(p, dEnd, y2) ||
                        !parseFloat(p, dEnd, x) || !parseFloat(p, dEnd, y))
                        break;
                    if (rel)
                    {
                        x1 += cur.x;
                        y1 += cur.y;
                        x2 += cur.x;
                        y2 += cur.y;
                        x += cur.x;
                        y += cur.y;
                    }
                    Vec2 p0 = cur;
                    Vec2 p1 = {x1, y1};
                    Vec2 p2 = {x2, y2};
                    Vec2 p3 = {x, y};

                    QC::Vector<Vec2> temp;
                    temp.reserve(16);
                    temp.push_back(apply(xform, p0));
                    appendCubic(temp,
                               apply(xform, p0),
                               apply(xform, p1),
                               apply(xform, p2),
                               apply(xform, p3),
                               12);
                    for (QC::usize i = 1; i < temp.size(); ++i)
                        poly.push_back(temp[i]);

                    cur = p3;
                    continue;
                }

                if (uc == 'Q')
                {
                    float x1 = 0.0f, y1 = 0.0f, x = 0.0f, y = 0.0f;
                    if (!parseFloat(p, dEnd, x1) || !parseFloat(p, dEnd, y1) ||
                        !parseFloat(p, dEnd, x) || !parseFloat(p, dEnd, y))
                        break;
                    if (rel)
                    {
                        x1 += cur.x;
                        y1 += cur.y;
                        x += cur.x;
                        y += cur.y;
                    }
                    Vec2 p0 = cur;
                    Vec2 p1 = {x1, y1};
                    Vec2 p2 = {x, y};

                    QC::Vector<Vec2> temp;
                    temp.reserve(16);
                    temp.push_back(apply(xform, p0));
                    appendQuadratic(temp,
                                    apply(xform, p0),
                                    apply(xform, p1),
                                    apply(xform, p2),
                                    10);
                    for (QC::usize i = 1; i < temp.size(); ++i)
                        poly.push_back(temp[i]);

                    cur = p2;
                    continue;
                }

                if (uc == 'A')
                {
                    float rx = 0.0f, ry = 0.0f, xrot = 0.0f, x = 0.0f, y = 0.0f;
                    int largeArc = 0;
                    int sweep = 0;
                    if (!parseFloat(p, dEnd, rx) || !parseFloat(p, dEnd, ry) || !parseFloat(p, dEnd, xrot) ||
                        !parseBoolFlag(p, dEnd, largeArc) || !parseBoolFlag(p, dEnd, sweep) ||
                        !parseFloat(p, dEnd, x) || !parseFloat(p, dEnd, y))
                        break;
                    if (rel)
                    {
                        x += cur.x;
                        y += cur.y;
                    }

                    Vec2 p0 = cur;
                    Vec2 p1 = {x, y};
                    QC::Vector<Vec2> arc;
                    arc.reserve(32);
                    arc.push_back(p0);
                    appendArc(arc, p0, rx, ry, xrot, largeArc, sweep, p1);
                    for (QC::usize i = 1; i < arc.size(); ++i)
                        poly.push_back(apply(xform, arc[i]));

                    cur = p1;
                    continue;
                }

                // Unsupported command: bail out.
                break;
            }

            flushOpen(false);
            (void)options;
        }

        static void strokePrimitiveLine(ImageSurface &surface, float x1, float y1, float x2, float y2, const Transform &xform, const StrokeStyle &stroke)
        {
            if (!stroke.enabled)
                return;
            Vec2 p1 = apply(xform, {x1, y1});
            Vec2 p2 = apply(xform, {x2, y2});
            const QC::u32 w = stroke.width < 1.0f ? 1u : static_cast<QC::u32>(stroke.width + 0.5f);
            strokeLine(surface,
                       static_cast<QC::i32>(p1.x + 0.5f), static_cast<QC::i32>(p1.y + 0.5f),
                       static_cast<QC::i32>(p2.x + 0.5f), static_cast<QC::i32>(p2.y + 0.5f),
                       stroke.color,
                       w);
        }

        static bool parsePointsList(const char *text, QC::Vector<Vec2> &out)
        {
            out.clear();
            if (!text)
                return false;
            const char *p = text;
            const char *end = text + QC::String::strlen(text);
            while (p < end)
            {
                float x = 0.0f, y = 0.0f;
                if (!parseFloat(p, end, x))
                    break;
                if (!parseFloat(p, end, y))
                    break;
                out.push_back({x, y});
            }
            return out.size() >= 2;
        }

        static void strokePrimitivePolyline(ImageSurface &surface, const char *pointsText, bool closed, const Transform &xform, const StrokeStyle &stroke)
        {
            if (!stroke.enabled)
                return;
            QC::Vector<Vec2> pts;
            if (!parsePointsList(pointsText, pts))
                return;
            for (QC::usize i = 0; i < pts.size(); ++i)
                pts[i] = apply(xform, pts[i]);
            const QC::u32 w = stroke.width < 1.0f ? 1u : static_cast<QC::u32>(stroke.width + 0.5f);
            strokePolyline(surface, pts, stroke.color, w, closed);
        }

        static void strokePrimitiveCircle(ImageSurface &surface, float cx, float cy, float r, const Transform &xform, const StrokeStyle &stroke)
        {
            if (!stroke.enabled)
                return;

            // Approximate by polyline; handle uniform scaling only.
            Vec2 c = apply(xform, {cx, cy});
            const float rr = r * xform.scale;
            QC::Vector<Vec2> pts;
            pts.reserve(64);
            appendCircle(pts, c.x, c.y, rr);
            const QC::u32 w = stroke.width < 1.0f ? 1u : static_cast<QC::u32>(stroke.width + 0.5f);
            strokePolyline(surface, pts, stroke.color, w, true);
        }

        static void applyStrokeAttrs(const char *tagStart, const char *tagEnd, StrokeStyle &stroke, const SVGDecodeOptions &options)
        {
            char strokeAttr[64];
            char widthAttr[32];
            char capAttr[32];
            char joinAttr[32];

            QC::String::memset(strokeAttr, 0, sizeof(strokeAttr));
            QC::String::memset(widthAttr, 0, sizeof(widthAttr));
            QC::String::memset(capAttr, 0, sizeof(capAttr));
            QC::String::memset(joinAttr, 0, sizeof(joinAttr));

            (void)copyAttr(tagStart, tagEnd, "stroke", strokeAttr, sizeof(strokeAttr));
            (void)copyAttr(tagStart, tagEnd, "stroke-width", widthAttr, sizeof(widthAttr));
            (void)copyAttr(tagStart, tagEnd, "stroke-linecap", capAttr, sizeof(capAttr));
            (void)copyAttr(tagStart, tagEnd, "stroke-linejoin", joinAttr, sizeof(joinAttr));

            if (strokeAttr[0])
            {
                QC::Color c;
                if (parseColor(strokeAttr, c, options))
                {
                    stroke.color = c;
                    stroke.enabled = true;
                }
                else
                {
                    // "none" or unsupported -> disable stroke
                    if (QC::String::strcmp(strokeAttr, "none") == 0)
                        stroke.enabled = false;
                }
            }

            if (widthAttr[0])
            {
                const char *p = widthAttr;
                const char *end = widthAttr + QC::String::strlen(widthAttr);
                float w = 0.0f;
                if (parseFloat(p, end, w) && w > 0.0f)
                    stroke.width = w;
            }

            if (capAttr[0])
                stroke.roundCap = (QC::String::strcmp(capAttr, "round") == 0);
            if (joinAttr[0])
                stroke.roundJoin = (QC::String::strcmp(joinAttr, "round") == 0);
        }

        static bool parseNumberAttr(const char *tagStart, const char *tagEnd, const char *key, float &out)
        {
            char buf[64];
            QC::String::memset(buf, 0, sizeof(buf));
            if (!copyAttr(tagStart, tagEnd, key, buf, sizeof(buf)))
                return false;
            const char *p = buf;
            const char *end = buf + QC::String::strlen(buf);
            return parseFloat(p, end, out);
        }

        static bool parseU32Attr(const char *tagStart, const char *tagEnd, const char *key, QC::u32 &out)
        {
            float f = 0.0f;
            if (!parseNumberAttr(tagStart, tagEnd, key, f))
                return false;
            if (f <= 0.0f)
                return false;
            out = static_cast<QC::u32>(f + 0.5f);
            return true;
        }

        static void clearSurface(ImageSurface &s, QC::u32 w, QC::u32 h)
        {
            s.reset();
            s.width = w;
            s.height = h;
            s.pixels.resize(static_cast<QC::usize>(w) * static_cast<QC::usize>(h));
            for (QC::usize i = 0; i < s.pixels.size(); ++i)
                s.pixels[i] = QC::Color::transparent().value;
        }

    } // namespace

    bool decodeSVG(const QC::u8 *data, QC::usize size, ImageSurface &outSurface, const SVGDecodeOptions &options)
    {
        outSurface.reset();
        if (!data || size == 0)
            return false;

        const char *text = reinterpret_cast<const char *>(data);
        const char *end = text + size;

        float vbMinX = 0.0f, vbMinY = 0.0f, vbW = 0.0f, vbH = 0.0f;
        QC::u32 outW = 0, outH = 0;

        StrokeStyle defaultStroke;
        defaultStroke.color = QC::Color(options.currentColorARGB);
        defaultStroke.width = 2.0f;
        defaultStroke.roundCap = true;
        defaultStroke.roundJoin = true;

        bool haveRoot = false;

        // Scan tags
        const char *p = text;
        while (p < end)
        {
            // Find '<'
            while (p < end && *p != '<')
                ++p;
            if (p >= end)
                break;
            ++p;

            if (p < end && *p == '!')
            {
                // Skip comments/doctype
                while (p < end && *p != '>')
                    ++p;
                continue;
            }
            if (p < end && *p == '?')
            {
                while (p < end && *p != '>')
                    ++p;
                continue;
            }
            if (p < end && *p == '/')
            {
                while (p < end && *p != '>')
                    ++p;
                continue;
            }

            const char *tagNameStart = p;
            while (p < end && !isWs(*p) && *p != '>' && *p != '/')
                ++p;
            const char *tagNameEnd = p;

            const char *tagStart = p;
            while (p < end && *p != '>')
                ++p;
            const char *tagEnd = p;
            if (p < end)
                ++p;

            const QC::usize tagLen = static_cast<QC::usize>(tagNameEnd - tagNameStart);

            if (tagEquals(tagNameStart, tagLen, "svg"))
            {
                haveRoot = true;

                // width/height
                (void)parseU32Attr(tagStart, tagEnd, "width", outW);
                (void)parseU32Attr(tagStart, tagEnd, "height", outH);

                char vb[96];
                QC::String::memset(vb, 0, sizeof(vb));
                if (copyAttr(tagStart, tagEnd, "viewBox", vb, sizeof(vb)))
                {
                    (void)parseViewBox(vb, vbMinX, vbMinY, vbW, vbH);
                }

                applyStrokeAttrs(tagStart, tagEnd, defaultStroke, options);
                continue;
            }

            if (!haveRoot)
                continue;

            // If no explicit output size, infer from viewBox
            if (outW == 0 || outH == 0)
            {
                if (vbW > 0.0f && vbH > 0.0f)
                {
                    if (outW == 0)
                        outW = static_cast<QC::u32>(vbW + 0.5f);
                    if (outH == 0)
                        outH = static_cast<QC::u32>(vbH + 0.5f);
                }
                else
                {
                    if (outW == 0)
                        outW = 24;
                    if (outH == 0)
                        outH = 24;
                }
            }

            const QC::u64 pixelCount = static_cast<QC::u64>(outW) * static_cast<QC::u64>(outH);
            if (pixelCount == 0 || pixelCount > options.maxOutputPixels)
            {
                QC_LOG_WARN(LOG_MODULE, "SVG output size too large: %ux%u", (unsigned)outW, (unsigned)outH);
                return false;
            }

            if (!outSurface.isValid())
                clearSurface(outSurface, outW, outH);

            // Build transform once (assume square-ish)
            Transform xform;
            if (vbW > 0.0f && vbH > 0.0f)
            {
                const float sx0 = static_cast<float>(outW) / vbW;
                const float sy0 = static_cast<float>(outH) / vbH;
                const float baseScale = (sx0 < sy0) ? sx0 : sy0;

                // Inset by half the (root/default) stroke width in output pixels.
                float padPx = (defaultStroke.width * baseScale) * 0.5f;
                if (padPx < 0.0f)
                    padPx = 0.0f;
                const float minDim = static_cast<float>((outW < outH) ? outW : outH);
                if (padPx > minDim * 0.25f)
                    padPx = minDim * 0.25f;

                float availW = static_cast<float>(outW) - padPx * 2.0f;
                float availH = static_cast<float>(outH) - padPx * 2.0f;
                if (availW < 1.0f)
                    availW = 1.0f;
                if (availH < 1.0f)
                    availH = 1.0f;

                const float sx = availW / vbW;
                const float sy = availH / vbH;
                xform.scale = (sx < sy) ? sx : sy;
                xform.offX = (-vbMinX) * xform.scale + (static_cast<float>(outW) - vbW * xform.scale) * 0.5f;
                xform.offY = (-vbMinY) * xform.scale + (static_cast<float>(outH) - vbH * xform.scale) * 0.5f;
            }
            else
            {
                xform.scale = 1.0f;
                xform.offX = 0.0f;
                xform.offY = 0.0f;
            }

            StrokeStyle stroke = defaultStroke;
            applyStrokeAttrs(tagStart, tagEnd, stroke, options);
            stroke.width *= xform.scale;

            if (tagEquals(tagNameStart, tagLen, "line"))
            {
                float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
                if (parseNumberAttr(tagStart, tagEnd, "x1", x1) &&
                    parseNumberAttr(tagStart, tagEnd, "y1", y1) &&
                    parseNumberAttr(tagStart, tagEnd, "x2", x2) &&
                    parseNumberAttr(tagStart, tagEnd, "y2", y2))
                {
                    strokePrimitiveLine(outSurface, x1, y1, x2, y2, xform, stroke);
                }
                continue;
            }

            if (tagEquals(tagNameStart, tagLen, "polyline"))
            {
                char pts[2048];
                QC::String::memset(pts, 0, sizeof(pts));
                if (copyAttr(tagStart, tagEnd, "points", pts, sizeof(pts)))
                    strokePrimitivePolyline(outSurface, pts, false, xform, stroke);
                continue;
            }

            if (tagEquals(tagNameStart, tagLen, "polygon"))
            {
                char pts[2048];
                QC::String::memset(pts, 0, sizeof(pts));
                if (copyAttr(tagStart, tagEnd, "points", pts, sizeof(pts)))
                    strokePrimitivePolyline(outSurface, pts, true, xform, stroke);
                continue;
            }

            if (tagEquals(tagNameStart, tagLen, "circle"))
            {
                float cx = 0, cy = 0, r = 0;
                if (parseNumberAttr(tagStart, tagEnd, "cx", cx) &&
                    parseNumberAttr(tagStart, tagEnd, "cy", cy) &&
                    parseNumberAttr(tagStart, tagEnd, "r", r))
                {
                    strokePrimitiveCircle(outSurface, cx, cy, r, xform, stroke);
                }
                continue;
            }

            if (tagEquals(tagNameStart, tagLen, "path"))
            {
                const char *ds = nullptr;
                const char *de = nullptr;
                if (findAttr(tagStart, tagEnd, "d", ds, de) && ds && de)
                {
                    // Basic segment cap (avoid pathological inputs)
                    if (static_cast<QC::usize>(de - ds) > 65536)
                        continue;
                    strokePathD(outSurface, ds, de, xform, stroke, options);
                }
                continue;
            }
        }

        return outSurface.isValid();
    }

    bool decodeSVG(const QC::Vector<QC::u8> &buffer, ImageSurface &outSurface, const SVGDecodeOptions &options)
    {
        return decodeSVG(buffer.data(), buffer.size(), outSurface, options);
    }

} // namespace QG
