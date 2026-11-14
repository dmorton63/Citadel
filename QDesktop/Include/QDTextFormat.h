#pragma once

// QDesktop text formatting helpers (bounded, allocation-free)
// Namespace: QD::Text

#include "QCTypes.h"

namespace QD::Text
{
    /// Appends text to dst (capacity cap, always NUL-terminated). Returns the new length.
    inline QC::usize append(char *dst, QC::usize cap, QC::usize len, const char *text)
    {
        if (!dst || cap == 0)
            return 0;
        if (len >= cap)
            len = cap - 1;
        while (text && *text && len + 1 < cap)
            dst[len++] = *text++;
        dst[len] = '\0';
        return len;
    }

    /// Appends a decimal number, left-padded with zeros to minDigits.
    inline QC::usize appendUnsigned(char *dst, QC::usize cap, QC::usize len, QC::u64 value, QC::u32 minDigits = 1)
    {
        char digits[24];
        QC::u32 count = 0;
        do
        {
            digits[count++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        } while (value != 0 && count < sizeof(digits));
        while (count < minDigits && count < sizeof(digits))
            digits[count++] = '0';

        char text[24];
        for (QC::u32 i = 0; i < count; ++i)
            text[i] = digits[count - 1 - i];
        text[count] = '\0';
        return append(dst, cap, len, text);
    }

    /// Appends a byte count using the largest whole unit (B, KiB, MiB, GiB).
    inline QC::usize appendBytes(char *dst, QC::usize cap, QC::usize len, QC::u64 bytes)
    {
        static const char *units[] = {" B", " KiB", " MiB", " GiB"};
        QC::u32 unit = 0;
        while (unit + 1 < 4 && bytes >= 1024ull * 10ull)
        {
            bytes /= 1024ull;
            ++unit;
        }
        len = appendUnsigned(dst, cap, len, bytes);
        return append(dst, cap, len, units[unit]);
    }
}
