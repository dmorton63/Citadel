#include "Debug/Framebuffer/QKDebugFramebufferText.h"

#include "QCString.h"

#define LIMINE_API_REVISION 2
#include "limine.h"

namespace
{
    struct EarlyFramebufferInfo
    {
        QC::uptr address = 0;
        QC::u32 width = 0;
        QC::u32 height = 0;
        QC::u32 pitch = 0;
    };

    static EarlyFramebufferInfo g_Fb{};
    static bool g_Ready = false;
    static bool g_Enabled = true;

    static constexpr QC::u32 kCharW = 8;
    static constexpr QC::u32 kCharH = 16;

    static constexpr QC::u32 kMaxLines = 512;
    static constexpr QC::u32 kMaxCols = 256;

    static QC::u32 g_Cols = 0;
    static QC::u32 g_Rows = 0;

    // Ring buffer of rendered text lines (sanitized ASCII).
    static char g_Lines[kMaxLines][kMaxCols + 1];
    static QC::u16 g_LineLen[kMaxLines];
    static QC::u32 g_Start = 0; // physical index of oldest line
    static QC::u32 g_Count = 0; // number of valid lines (<= kMaxLines)

    // Current write position is always the last logical line.
    static QC::u32 g_WritePhys = 0;
    static QC::u32 g_WriteCol = 0;

    // Viewport: top logical line index currently displayed.
    static QC::u32 g_ViewTop = 0;

    // On-screen cursor (pixel coords) for live tail rendering only.
    static QC::u32 g_CursorX = 0;
    static QC::u32 g_CursorY = 0;

    static constexpr QC::u32 kFg = 0xFFFFFFFF; // white
    static constexpr QC::u32 kBg = 0xFF000000; // black

    static void renderViewport();

    static QC::u32 tailTop()
    {
        if (g_Rows == 0)
            return 0;
        return (g_Count > g_Rows) ? (g_Count - g_Rows) : 0;
    }

    static bool atTail()
    {
        return g_ViewTop == tailTop();
    }

    static QC::u32 logicalToPhys(QC::u32 logical)
    {
        return (g_Start + logical) % kMaxLines;
    }

    static void clearLinePhys(QC::u32 phys)
    {
        if (phys >= kMaxLines)
            return;
        QC::String::memset(g_Lines[phys], 0, sizeof(g_Lines[phys]));
        g_LineLen[phys] = 0;
    }

    static void fill(const EarlyFramebufferInfo &fb, QC::u32 argb)
    {
        if (!fb.address)
            return;
        volatile QC::u8 *base = reinterpret_cast<volatile QC::u8 *>(fb.address);
        for (QC::u32 y = 0; y < fb.height; ++y)
        {
            volatile QC::u8 *rowBytes = base + static_cast<QC::usize>(y) * fb.pitch;
            volatile QC::u32 *row = reinterpret_cast<volatile QC::u32 *>(rowBytes);
            for (QC::u32 x = 0; x < fb.width; ++x)
                row[x] = argb;
        }
    }

    static void copyRowBytesUp(QC::u8 *dst, const QC::u8 *src, QC::u32 count)
    {
        QC::u32 offset = 0;

        while (offset + sizeof(QC::u64) <= count)
        {
            *reinterpret_cast<volatile QC::u64 *>(dst + offset) = *reinterpret_cast<const volatile QC::u64 *>(src + offset);
            offset += static_cast<QC::u32>(sizeof(QC::u64));
        }

        while (offset + sizeof(QC::u32) <= count)
        {
            *reinterpret_cast<volatile QC::u32 *>(dst + offset) = *reinterpret_cast<const volatile QC::u32 *>(src + offset);
            offset += static_cast<QC::u32>(sizeof(QC::u32));
        }

        while (offset < count)
        {
            dst[offset] = src[offset];
            ++offset;
        }
    }

    static void fillRowBytes(QC::u8 *dst, QC::u32 count, QC::u32 argb)
    {
        QC::u32 offset = 0;
        const QC::u64 pattern = (static_cast<QC::u64>(argb) << 32) | static_cast<QC::u64>(argb);

        while (offset + sizeof(QC::u64) <= count)
        {
            *reinterpret_cast<volatile QC::u64 *>(dst + offset) = pattern;
            offset += static_cast<QC::u32>(sizeof(QC::u64));
        }

        while (offset + sizeof(QC::u32) <= count)
        {
            *reinterpret_cast<volatile QC::u32 *>(dst + offset) = argb;
            offset += static_cast<QC::u32>(sizeof(QC::u32));
        }

        while (offset < count)
        {
            dst[offset++] = static_cast<QC::u8>(argb & 0xffu);
        }
    }

    static void drawRect(const EarlyFramebufferInfo &fb, QC::u32 x, QC::u32 y, QC::u32 w, QC::u32 h, QC::u32 argb)
    {
        if (!fb.address)
            return;
        if (x >= fb.width || y >= fb.height)
            return;
        if (x + w > fb.width)
            w = fb.width - x;
        if (y + h > fb.height)
            h = fb.height - y;

        volatile QC::u8 *base = reinterpret_cast<volatile QC::u8 *>(fb.address);
        for (QC::u32 yy = 0; yy < h; ++yy)
        {
            volatile QC::u8 *rowBytes = base + static_cast<QC::usize>(y + yy) * fb.pitch;
            volatile QC::u32 *row = reinterpret_cast<volatile QC::u32 *>(rowBytes);
            for (QC::u32 xx = 0; xx < w; ++xx)
                row[x + xx] = argb;
        }
    }

    static void drawChar8x16(const EarlyFramebufferInfo &fb, QC::u32 x, QC::u32 y, char c, QC::u32 fg, QC::u32 bg)
    {
        const auto drawFallback = [&]() {
            drawRect(fb, x, y, 8, 16, bg);
            drawRect(fb, x, y, 8, 1, fg);
            drawRect(fb, x, y + 15, 8, 1, fg);
            drawRect(fb, x, y, 1, 16, fg);
            drawRect(fb, x + 7, y, 1, 16, fg);
            drawRect(fb, x + 3, y + 5, 2, 6, fg);
        };

        // Normalize to uppercase for simplicity.
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');

        struct Glyph8x8
        {
            char ch;
            QC::u8 rows[8];
        };

        static const Glyph8x8 kGlyphs[] = {
            {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
            {'?', {0x3C, 0x66, 0x06, 0x0C, 0x18, 0x00, 0x18, 0x00}},
            {'\'', {0x18, 0x18, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00}},
            {':', {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00}},
            {'!', {0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00}},
            {'-', {0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00}},
            {'(', {0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00}},
            {')', {0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00}},
            {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00}},
            {',', {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30}},
            {'/', {0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00, 0x00}},
            {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00}},
            {'=', {0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00}},

            {'0', {0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00}},
            {'1', {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00}},
            {'2', {0x3C, 0x66, 0x06, 0x0C, 0x18, 0x30, 0x7E, 0x00}},
            {'3', {0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00}},
            {'4', {0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0x00}},
            {'5', {0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00}},
            {'6', {0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00}},
            {'7', {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00}},
            {'8', {0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00}},
            {'9', {0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00}},

            {'A', {0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00}},
            {'B', {0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00}},
            {'C', {0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00}},
            {'D', {0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00}},
            {'E', {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00}},
            {'F', {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x00}},
            {'G', {0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0x00}},
            {'H', {0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00}},
            {'I', {0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00}},
            {'J', {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00}},
            {'K', {0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00}},
            {'L', {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00}},
            {'M', {0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00}},
            {'N', {0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x00}},
            {'O', {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00}},
            {'P', {0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00}},
            {'Q', {0x3C, 0x66, 0x66, 0x66, 0x6E, 0x3C, 0x0E, 0x00}},
            {'R', {0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66, 0x00}},
            {'S', {0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00}},
            {'T', {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00}},
            {'U', {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00}},
            {'V', {0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00}},
            {'W', {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00}},
            {'X', {0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00}},
            {'Y', {0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x18, 0x00}},
            {'Z', {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00}},
        };

        const Glyph8x8 *glyph = nullptr;
        for (QC::usize i = 0; i < (sizeof(kGlyphs) / sizeof(kGlyphs[0])); ++i)
        {
            if (kGlyphs[i].ch == c)
            {
                glyph = &kGlyphs[i];
                break;
            }
        }

        if (!glyph)
        {
            drawFallback();
            return;
        }

        drawRect(fb, x, y, 8, 16, bg);

        volatile QC::u8 *base = reinterpret_cast<volatile QC::u8 *>(fb.address);
        for (QC::u32 row8 = 0; row8 < 8; ++row8)
        {
            const QC::u8 bits = glyph->rows[row8];
            for (QC::u32 dy = 0; dy < 2; ++dy)
            {
                const QC::u32 yy = y + row8 * 2 + dy;
                if (yy >= fb.height)
                    continue;
                volatile QC::u8 *rowBytes = base + static_cast<QC::usize>(yy) * fb.pitch;
                volatile QC::u32 *row = reinterpret_cast<volatile QC::u32 *>(rowBytes);
                for (QC::u32 col = 0; col < 8; ++col)
                {
                    const bool on = (bits & (0x80u >> col)) != 0;
                    const QC::u32 xx = x + col;
                    if (xx < fb.width)
                        row[xx] = on ? fg : bg;
                }
            }
        }
    }

    static char sanitizeChar(char ch)
    {
        if (ch == '\t')
            return ' ';
        if (ch >= 'a' && ch <= 'z')
            ch = static_cast<char>(ch - 'a' + 'A');

        const bool okPrintable =
            (ch == ' ') || (ch == '?') || (ch == '\'') || (ch == ':') || (ch == '!') || (ch == '-') ||
            (ch == '(') || (ch == ')') || (ch == '.') || (ch == ',') || (ch == '/') || (ch == '_') || (ch == '=') ||
            (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z');

        return okPrintable ? ch : '?';
    }

    static void scrollUpOneLine()
    {
        if (!g_Ready || !g_Enabled)
            return;

        // Re-render from the logical line buffer instead of physically shifting
        // the framebuffer one scanline band at a time. This keeps terminal-mode
        // rollover bound to the buffered text state rather than a full pixel copy.
        renderViewport();
    }

    static void renderViewport()
    {
        if (!g_Ready || !g_Enabled)
            return;

        fill(g_Fb, kBg);

        if (g_Rows == 0 || g_Cols == 0)
            return;

        for (QC::u32 row = 0; row < g_Rows; ++row)
        {
            const QC::u32 logical = g_ViewTop + row;
            if (logical >= g_Count)
                break;

            const QC::u32 phys = logicalToPhys(logical);
            const char *line = g_Lines[phys];
            const QC::u32 y = row * kCharH;

            for (QC::u32 col = 0; col < g_Cols; ++col)
            {
                char ch = (line && col < g_LineLen[phys]) ? line[col] : ' ';
                drawChar8x16(g_Fb, col * kCharW, y, ch, kFg, kBg);
            }
        }

        // Restore cursor to the live tail position (best-effort).
        if (atTail() && g_Count > 0)
        {
            const QC::u32 lastLogical = g_Count - 1;
            const QC::u32 row = lastLogical - g_ViewTop;
            g_CursorY = (row < g_Rows) ? (row * kCharH) : (g_Fb.height - kCharH);
            g_CursorX = (g_WriteCol < g_Cols) ? (g_WriteCol * kCharW) : 0;
        }
    }

    static void newline()
    {
        // Advance ring buffer to a fresh line.
        {
            const bool wasTail = atTail();

            if (g_Count == 0)
            {
                g_Start = 0;
                g_Count = 1;
                g_WritePhys = 0;
            }
            else
            {
                const QC::u32 nextPhys = (g_WritePhys + 1) % kMaxLines;
                if (g_Count < kMaxLines)
                {
                    ++g_Count;
                }
                else
                {
                    // Overwrite oldest.
                    g_Start = (g_Start + 1) % kMaxLines;

                    // If the user is viewing history, keep the same content visible
                    // as much as possible by shifting the viewport with the ring.
                    if (!wasTail && g_ViewTop > 0)
                        --g_ViewTop;
                }
                g_WritePhys = nextPhys;
            }

            clearLinePhys(g_WritePhys);
            g_WriteCol = 0;

            if (wasTail)
            {
                g_ViewTop = tailTop();
            }

            const QC::u32 tail = tailTop();
            if (g_ViewTop > tail)
                g_ViewTop = tail;
        }

        // Live render: move cursor to next line and scroll pixels if needed.
        if (g_Enabled && g_Ready && atTail())
        {
            g_CursorX = 0;
            g_CursorY += kCharH;
            if (g_CursorY + kCharH > g_Fb.height)
            {
                scrollUpOneLine();
                g_CursorX = 0;
                g_CursorY = g_Fb.height - kCharH;
            }
        }
    }

    static void putChar(char ch)
    {
        if (ch == '\r')
        {
            // Carriage return resets column (best-effort).
            g_WriteCol = 0;
            if (g_Enabled && g_Ready && atTail())
                g_CursorX = 0;
            return;
        }
        if (ch == '\n')
        {
            newline();
            return;
        }

        if (ch == '\b')
        {
            if (g_WriteCol > 0)
            {
                --g_WriteCol;
                g_Lines[g_WritePhys][g_WriteCol] = ' ';
                if (g_LineLen[g_WritePhys] > g_WriteCol)
                    g_LineLen[g_WritePhys] = static_cast<QC::u16>(g_WriteCol);
                if (g_Enabled && g_Ready && atTail())
                {
                    if (g_CursorX >= kCharW)
                        g_CursorX -= kCharW;
                    drawChar8x16(g_Fb, g_CursorX, g_CursorY, ' ', kFg, kBg);
                }
            }
            return;
        }

        ch = sanitizeChar(ch);

        const QC::u32 cols = g_Cols;
        if (cols == 0)
            return;

        if (g_WriteCol >= cols)
        {
            newline();
        }

        // Update buffer.
        {
            const QC::u32 col = g_WriteCol;
            if (col < kMaxCols)
            {
                g_Lines[g_WritePhys][col] = ch;
                g_Lines[g_WritePhys][col + 1] = '\0';
                const QC::u16 newLen = static_cast<QC::u16>(col + 1);
                if (newLen > g_LineLen[g_WritePhys])
                    g_LineLen[g_WritePhys] = newLen;
            }
            ++g_WriteCol;
        }

        // Live render only when following tail.
        if (g_Enabled && g_Ready && atTail())
        {
            if (g_CursorX + kCharW > g_Fb.width)
            {
                newline();
            }

            if (g_CursorY + kCharH > g_Fb.height)
            {
                scrollUpOneLine();
                g_CursorX = 0;
                g_CursorY = g_Fb.height - kCharH;
            }

            drawChar8x16(g_Fb, g_CursorX, g_CursorY, ch, kFg, kBg);
            g_CursorX += kCharW;
        }
    }
}

namespace QK::Debug::FramebufferText
{
    bool InitFromLimineRequest(QC::u64 FramebufferRequest[])
    {
        g_Fb = {};
        g_Ready = false;
        g_Enabled = true;
        g_CursorX = 0;
        g_CursorY = 0;
        g_Cols = 0;
        g_Rows = 0;
        g_Start = 0;
        g_Count = 0;
        g_WritePhys = 0;
        g_WriteCol = 0;
        g_ViewTop = 0;
        for (QC::u32 i = 0; i < kMaxLines; ++i)
            clearLinePhys(i);

        if (!FramebufferRequest)
            return false;

        // Limine framebuffer response pointer is stored at index 5 in our request array.
        QC::u64 *fb_response = reinterpret_cast<QC::u64 *>(FramebufferRequest[5]);
        if (!fb_response)
            return false;

        const QC::u64 fb_count = fb_response[1];
        if (fb_count == 0)
            return false;

        QC::u64 **fb_array = reinterpret_cast<QC::u64 **>(fb_response[2]);
        if (!fb_array)
            return false;

        QC::u64 *fb = fb_array[0];
        if (!fb)
            return false;

        g_Fb.address = static_cast<QC::uptr>(fb[0]);
        g_Fb.width = static_cast<QC::u32>(fb[1]);
        g_Fb.height = static_cast<QC::u32>(fb[2]);
        g_Fb.pitch = static_cast<QC::u32>(fb[3]);

        if (g_Fb.address == 0 || g_Fb.width == 0 || g_Fb.height == 0 || g_Fb.pitch == 0)
            return false;

        g_Cols = g_Fb.width / kCharW;
        g_Rows = g_Fb.height / kCharH;
        if (g_Cols > kMaxCols)
            g_Cols = kMaxCols;

        // Ensure at least one line exists.
        g_Start = 0;
        g_Count = 1;
        g_WritePhys = 0;
        g_WriteCol = 0;
        g_ViewTop = 0;
        clearLinePhys(g_WritePhys);

        fill(g_Fb, kBg);
        g_Ready = true;
        return true;
    }

    bool IsReady()
    {
        return g_Ready;
    }

    void SetEnabled(bool Enabled)
    {
        g_Enabled = Enabled;
    }

    void PageUp()
    {
        if (!g_Ready || !g_Enabled)
            return;
        if (g_Rows == 0)
            return;

        const QC::u32 step = (g_Rows > 1) ? (g_Rows - 1) : 1;
        if (g_ViewTop > step)
            g_ViewTop -= step;
        else
            g_ViewTop = 0;
        renderViewport();
    }

    void PageDown()
    {
        if (!g_Ready || !g_Enabled)
            return;
        if (g_Rows == 0)
            return;

        const QC::u32 step = (g_Rows > 1) ? (g_Rows - 1) : 1;
        const QC::u32 tail = tailTop();
        if (g_ViewTop + step < tail)
            g_ViewTop += step;
        else
            g_ViewTop = tail;
        renderViewport();
    }

    void FollowTail()
    {
        if (!g_Ready || !g_Enabled)
            return;
        g_ViewTop = tailTop();
        renderViewport();
    }

    bool IsViewingHistory()
    {
        if (!g_Ready)
            return false;
        return !atTail();
    }

    void Write(const char *Message)
    {
        if (!Message || !*Message)
            return;
        if (!g_Ready)
            return;

        for (const char *p = Message; *p; ++p)
        {
            putChar(*p);
        }
    }
}
