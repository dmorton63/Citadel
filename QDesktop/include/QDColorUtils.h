#pragma once

// QDesktop Color Utilities
// Namespace: QD

#include "QCColor.h"
#include "QCTypes.h"
#include "QCString.h"

namespace QD
{
    inline bool parseColorString(const char *text, QC::Color &out)
    {
        if (!text)
            return false;

        auto isSpace = [](char c) -> bool
        {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        };

        auto skipSpaces = [&](const char *&p) -> void
        {
            while (p && *p && isSpace(*p))
                ++p;
        };

        auto startsWithIgnoreCaseAscii = [](const char *s, const char *prefix) -> bool
        {
            if (!s || !prefix)
                return false;
            while (*prefix)
            {
                const char a = *s;
                const char b = *prefix;
                if (a == 0)
                    return false;
                const char la = (a >= 'A' && a <= 'Z') ? static_cast<char>(a + 32) : a;
                const char lb = (b >= 'A' && b <= 'Z') ? static_cast<char>(b + 32) : b;
                if (la != lb)
                    return false;
                ++s;
                ++prefix;
            }
            return true;
        };

        auto parseUInt = [&](const char *&p, QC::u32 &value) -> bool
        {
            skipSpaces(p);
            if (!p || *p < '0' || *p > '9')
                return false;
            QC::u32 v = 0;
            while (*p >= '0' && *p <= '9')
            {
                v = v * 10 + static_cast<QC::u32>(*p - '0');
                ++p;
                if (v > 1000000)
                    return false;
            }
            value = v;
            return true;
        };

        auto parseAlphaByte = [&](const char *&p, QC::u8 &alphaOut) -> bool
        {
            skipSpaces(p);
            if (!p || (!(*p >= '0' && *p <= '9') && *p != '.'))
                return false;

            bool hasDot = false;
            QC::u32 intPart = 0;
            while (*p >= '0' && *p <= '9')
            {
                intPart = intPart * 10 + static_cast<QC::u32>(*p - '0');
                ++p;
                if (intPart > 1000000)
                    return false;
            }

            float value = static_cast<float>(intPart);

            if (*p == '.')
            {
                hasDot = true;
                ++p;
                QC::u32 frac = 0;
                QC::u32 scale = 1;
                while (*p >= '0' && *p <= '9')
                {
                    if (scale < 1000000)
                    {
                        frac = frac * 10 + static_cast<QC::u32>(*p - '0');
                        scale *= 10;
                    }
                    ++p;
                }
                if (scale > 1)
                {
                    value += static_cast<float>(frac) / static_cast<float>(scale);
                }
            }

            skipSpaces(p);

            QC::u32 a = 0;
            if (hasDot)
            {
                if (value < 0.0f)
                    value = 0.0f;
                if (value > 1.0f)
                    value = 1.0f;
                a = static_cast<QC::u32>(value * 255.0f + 0.5f);
            }
            else
            {
                if (value <= 1.0f)
                {
                    a = static_cast<QC::u32>(value * 255.0f);
                }
                else
                {
                    a = static_cast<QC::u32>(value);
                }

                if (a > 255)
                    a = 255;
            }

            alphaOut = static_cast<QC::u8>(a);
            return true;
        };

        auto expectChar = [&](const char *&p, char ch) -> bool
        {
            skipSpaces(p);
            if (!p || *p != ch)
                return false;
            ++p;
            return true;
        };

        if (text[0] != '#')
        {
            // Support CSS-ish: rgba(r, g, b, a)
            if (!startsWithIgnoreCaseAscii(text, "rgba("))
                return false;

            const char *p = text + 5;
            QC::u32 r32 = 0;
            QC::u32 g32 = 0;
            QC::u32 b32 = 0;
            QC::u8 a8 = 0;

            if (!parseUInt(p, r32) || r32 > 255)
                return false;
            if (!expectChar(p, ','))
                return false;
            if (!parseUInt(p, g32) || g32 > 255)
                return false;
            if (!expectChar(p, ','))
                return false;
            if (!parseUInt(p, b32) || b32 > 255)
                return false;
            if (!expectChar(p, ','))
                return false;
            if (!parseAlphaByte(p, a8))
                return false;
            if (!expectChar(p, ')'))
                return false;
            skipSpaces(p);
            if (*p != '\0')
                return false;

            out = QC::Color(static_cast<QC::u8>(r32), static_cast<QC::u8>(g32), static_cast<QC::u8>(b32), a8);
            return true;
        }

        const QC::usize len = QC::String::strlen(text);
        if (len != 7 && len != 9)
            return false;

        auto hex = [](char c) -> int
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return 10 + (c - 'a');
            if (c >= 'A' && c <= 'F')
                return 10 + (c - 'A');
            return -1;
        };

        auto parsePair = [&](char hi, char lo, QC::u8 &value) -> bool
        {
            int h = hex(hi);
            int l = hex(lo);
            if (h < 0 || l < 0)
                return false;
            value = static_cast<QC::u8>((h << 4) | l);
            return true;
        };

        QC::u8 a = 0xFF;
        QC::u8 r = 0;
        QC::u8 g = 0;
        QC::u8 b = 0;

        const bool hasAlpha = (len == 9);
        const char *cursor = text + 1;
        if (hasAlpha)
        {
            if (!parsePair(cursor[0], cursor[1], a))
                return false;
            cursor += 2;
        }

        if (!parsePair(cursor[0], cursor[1], r))
            return false;
        cursor += 2;
        if (!parsePair(cursor[0], cursor[1], g))
            return false;
        cursor += 2;
        if (!parsePair(cursor[0], cursor[1], b))
            return false;

        out = QC::Color(r, g, b, a);
        return true;
    }

} // namespace QD
