// QWindowing Renderer - 2D rendering primitives implementation
// Namespace: QW

#include "QWRenderer.h"
#include "QKMemHeap.h"
#include "QCMemUtil.h"

namespace QW
{

    Renderer::Renderer()
        : m_buffer(nullptr),
          m_width(0),
          m_height(0),
          m_pitch(0),
          m_clipRect{0, 0, 0, 0},
          m_hasClipRect(false)
    {
    }

    Renderer::~Renderer()
    {
    }

    void Renderer::setTarget(QC::u32 *buffer, QC::u32 width, QC::u32 height, QC::u32 pitch)
    {
        m_buffer = buffer;
        m_width = width;
        m_height = height;
        m_pitch = pitch;
        clearClipRect();
    }

    void Renderer::setClipRect(const Rect &rect)
    {
        m_clipRect = rect;
        m_hasClipRect = true;
    }

    void Renderer::clearClipRect()
    {
        m_clipRect = Rect{0, 0, m_width, m_height};
        m_hasClipRect = false;
    }

    void Renderer::clear(Color color)
    {
        fillRect(Rect{0, 0, m_width, m_height}, color);
    }

    void Renderer::setPixel(QC::i32 x, QC::i32 y, Color color)
    {
        if (!clipPoint(x, y))
            return;
        if (x < 0 || y < 0)
            return;
        if (static_cast<QC::u32>(x) >= m_width || static_cast<QC::u32>(y) >= m_height)
            return;

        QC::u32 *row = reinterpret_cast<QC::u32 *>(
            reinterpret_cast<QC::u8 *>(m_buffer) + y * m_pitch);

        if (color.a == 255)
        {
            row[x] = color.value;
            return;
        }
        if (color.a == 0)
        {
            return;
        }

        Color dst;
        dst.value = row[x];
        row[x] = color.blend(dst).value;
    }

    Color Renderer::getPixel(QC::i32 x, QC::i32 y) const
    {
        if (x < 0 || y < 0)
            return Color();
        if (static_cast<QC::u32>(x) >= m_width || static_cast<QC::u32>(y) >= m_height)
            return Color();

        const QC::u32 *row = reinterpret_cast<const QC::u32 *>(
            reinterpret_cast<const QC::u8 *>(m_buffer) + y * m_pitch);
        Color c;
        c.value = row[x];
        return c;
    }

    void Renderer::drawLine(QC::i32 x1, QC::i32 y1, QC::i32 x2, QC::i32 y2, Color color)
    {
        // Bresenham's line algorithm
        QC::i32 dx = x2 > x1 ? x2 - x1 : x1 - x2;
        QC::i32 dy = y2 > y1 ? y2 - y1 : y1 - y2;
        QC::i32 sx = x1 < x2 ? 1 : -1;
        QC::i32 sy = y1 < y2 ? 1 : -1;
        QC::i32 err = dx - dy;

        while (true)
        {
            setPixel(x1, y1, color);

            if (x1 == x2 && y1 == y2)
                break;

            QC::i32 e2 = 2 * err;
            if (e2 > -dy)
            {
                err -= dy;
                x1 += sx;
            }
            if (e2 < dx)
            {
                err += dx;
                y1 += sy;
            }
        }
    }

    void Renderer::drawHLine(QC::i32 x, QC::i32 y, QC::u32 length, Color color)
    {
        for (QC::u32 i = 0; i < length; ++i)
        {
            setPixel(x + static_cast<QC::i32>(i), y, color);
        }
    }

    void Renderer::drawVLine(QC::i32 x, QC::i32 y, QC::u32 length, Color color)
    {
        for (QC::u32 i = 0; i < length; ++i)
        {
            setPixel(x, y + static_cast<QC::i32>(i), color);
        }
    }

    void Renderer::drawRect(const Rect &rect, Color color)
    {
        drawHLine(rect.x, rect.y, rect.width, color);
        drawHLine(rect.x, rect.y + static_cast<QC::i32>(rect.height) - 1, rect.width, color);
        drawVLine(rect.x, rect.y, rect.height, color);
        drawVLine(rect.x + static_cast<QC::i32>(rect.width) - 1, rect.y, rect.height, color);
    }

    void Renderer::fillRect(const Rect &rect, Color color)
    {
        Rect clipped = rect;
        if (!clipRect(clipped))
            return;

        if (color.a == 0)
        {
            return;
        }

        const bool opaque = (color.a == 255);

        for (QC::i32 y = clipped.y; y < clipped.y + static_cast<QC::i32>(clipped.height); ++y)
        {
            QC::u32 *row = reinterpret_cast<QC::u32 *>(
                reinterpret_cast<QC::u8 *>(m_buffer) + y * m_pitch);

            if (opaque)
            {
                for (QC::i32 x = clipped.x; x < clipped.x + static_cast<QC::i32>(clipped.width); ++x)
                {
                    row[x] = color.value;
                }
            }
            else
            {
                for (QC::i32 x = clipped.x; x < clipped.x + static_cast<QC::i32>(clipped.width); ++x)
                {
                    Color dst;
                    dst.value = row[x];
                    row[x] = color.blend(dst).value;
                }
            }
        }
    }

    void Renderer::drawCircle(QC::i32 cx, QC::i32 cy, QC::u32 radius, Color color)
    {
        // Midpoint circle algorithm
        QC::i32 x = static_cast<QC::i32>(radius);
        QC::i32 y = 0;
        QC::i32 err = 0;

        while (x >= y)
        {
            setPixel(cx + x, cy + y, color);
            setPixel(cx + y, cy + x, color);
            setPixel(cx - y, cy + x, color);
            setPixel(cx - x, cy + y, color);
            setPixel(cx - x, cy - y, color);
            setPixel(cx - y, cy - x, color);
            setPixel(cx + y, cy - x, color);
            setPixel(cx + x, cy - y, color);

            y++;
            err += 1 + 2 * y;
            if (2 * (err - x) + 1 > 0)
            {
                x--;
                err += 1 - 2 * x;
            }
        }
    }

    void Renderer::fillCircle(QC::i32 cx, QC::i32 cy, QC::u32 radius, Color color)
    {
        QC::i32 x = static_cast<QC::i32>(radius);
        QC::i32 y = 0;
        QC::i32 err = 0;

        while (x >= y)
        {
            drawHLine(cx - x, cy + y, static_cast<QC::u32>(x * 2 + 1), color);
            drawHLine(cx - x, cy - y, static_cast<QC::u32>(x * 2 + 1), color);
            drawHLine(cx - y, cy + x, static_cast<QC::u32>(y * 2 + 1), color);
            drawHLine(cx - y, cy - x, static_cast<QC::u32>(y * 2 + 1), color);

            y++;
            err += 1 + 2 * y;
            if (2 * (err - x) + 1 > 0)
            {
                x--;
                err += 1 - 2 * x;
            }
        }
    }

    void Renderer::drawTriangle(Point p1, Point p2, Point p3, Color color)
    {
        drawLine(p1.x, p1.y, p2.x, p2.y, color);
        drawLine(p2.x, p2.y, p3.x, p3.y, color);
        drawLine(p3.x, p3.y, p1.x, p1.y, color);
    }

    void Renderer::fillTriangle(Point p1, Point p2, Point p3, Color color)
    {
        (void)p1;
        (void)p2;
        (void)p3;
        (void)color;
        // TODO: Implement triangle fill with scanline algorithm
    }

    void Renderer::drawChar(QC::i32 x, QC::i32 y, char c, Color color)
    {
        (void)x;
        (void)y;
        (void)c;
        (void)color;
        // TODO: Implement with bitmap font
    }

    void Renderer::drawString(QC::i32 x, QC::i32 y, const char *str, Color color)
    {
        if (!str)
            return;

        QC::i32 curX = x;
        while (*str)
        {
            drawChar(curX, y, *str, color);
            curX += 8; // Assuming 8-pixel wide font
            str++;
        }
    }

    Size Renderer::measureString(const char *str) const
    {
        if (!str)
            return Size{0, 0};

        QC::u32 len = 0;
        while (str[len])
            len++;

        // Assuming 8x16 font
        return Size{len * 8, 16};
    }

    void Renderer::blit(QC::i32 x, QC::i32 y, const QC::u32 *src,
                        QC::u32 srcWidth, QC::u32 srcHeight, QC::u32 srcPitch)
    {
        if (!src || !m_buffer)
            return;

        // Clip to screen
        QC::i32 startX = x < 0 ? -x : 0;
        QC::i32 startY = y < 0 ? -y : 0;
        QC::i32 endX = static_cast<QC::i32>(srcWidth);
        QC::i32 endY = static_cast<QC::i32>(srcHeight);

        if (x + endX > static_cast<QC::i32>(m_width))
            endX = static_cast<QC::i32>(m_width) - x;
        if (y + endY > static_cast<QC::i32>(m_height))
            endY = static_cast<QC::i32>(m_height) - y;

        for (QC::i32 sy = startY; sy < endY; ++sy)
        {
            const QC::u32 *srcRow = reinterpret_cast<const QC::u32 *>(
                reinterpret_cast<const QC::u8 *>(src) + sy * srcPitch);
            QC::u32 *dstRow = reinterpret_cast<QC::u32 *>(
                reinterpret_cast<QC::u8 *>(m_buffer) + (y + sy) * m_pitch);

            for (QC::i32 sx = startX; sx < endX; ++sx)
            {
                dstRow[x + sx] = srcRow[sx];
            }
        }
    }

    void Renderer::blitScaled(const Rect &dest, const QC::u32 *src,
                              QC::u32 srcWidth, QC::u32 srcHeight, QC::u32 srcPitch)
    {
        (void)dest;
        (void)src;
        (void)srcWidth;
        (void)srcHeight;
        (void)srcPitch;
        // TODO: Implement scaled blit with bilinear filtering
    }

    void Renderer::blitAlpha(QC::i32 x, QC::i32 y, const QC::u32 *src,
                             QC::u32 srcWidth, QC::u32 srcHeight, QC::u32 srcPitch)
    {
        if (!src || !m_buffer)
            return;

        // Clip to screen
        QC::i32 startX = x < 0 ? -x : 0;
        QC::i32 startY = y < 0 ? -y : 0;
        QC::i32 endX = static_cast<QC::i32>(srcWidth);
        QC::i32 endY = static_cast<QC::i32>(srcHeight);

        if (x + endX > static_cast<QC::i32>(m_width))
            endX = static_cast<QC::i32>(m_width) - x;
        if (y + endY > static_cast<QC::i32>(m_height))
            endY = static_cast<QC::i32>(m_height) - y;

        for (QC::i32 sy = startY; sy < endY; ++sy)
        {
            const QC::u32 *srcRow = reinterpret_cast<const QC::u32 *>(
                reinterpret_cast<const QC::u8 *>(src) + sy * srcPitch);
            QC::u32 *dstRow = reinterpret_cast<QC::u32 *>(
                reinterpret_cast<QC::u8 *>(m_buffer) + (y + sy) * m_pitch);

            for (QC::i32 sx = startX; sx < endX; ++sx)
            {
                QC::u32 srcPixel = srcRow[sx];
                QC::u8 alpha = (srcPixel >> 24) & 0xFF;

                if (alpha == 255)
                {
                    dstRow[x + sx] = srcPixel;
                }
                else if (alpha > 0)
                {
                    QC::u32 dstPixel = dstRow[x + sx];

                    QC::u8 sr = (srcPixel >> 16) & 0xFF;
                    QC::u8 sg = (srcPixel >> 8) & 0xFF;
                    QC::u8 sb = srcPixel & 0xFF;

                    QC::u8 dr = (dstPixel >> 16) & 0xFF;
                    QC::u8 dg = (dstPixel >> 8) & 0xFF;
                    QC::u8 db = dstPixel & 0xFF;

                    QC::u8 outR = static_cast<QC::u8>((sr * alpha + dr * (255 - alpha)) / 255);
                    QC::u8 outG = static_cast<QC::u8>((sg * alpha + dg * (255 - alpha)) / 255);
                    QC::u8 outB = static_cast<QC::u8>((sb * alpha + db * (255 - alpha)) / 255);

                    dstRow[x + sx] = (0xFF << 24) | (outR << 16) | (outG << 8) | outB;
                }
            }
        }
    }

    bool Renderer::clipPoint(QC::i32 &x, QC::i32 &y) const
    {
        Rect clip = m_hasClipRect ? m_clipRect : Rect{0, 0, m_width, m_height};

        if (x < clip.x || x >= clip.x + static_cast<QC::i32>(clip.width))
            return false;
        if (y < clip.y || y >= clip.y + static_cast<QC::i32>(clip.height))
            return false;

        return true;
    }

    bool Renderer::clipRect(Rect &rect) const
    {
        Rect clip = m_hasClipRect ? m_clipRect : Rect{0, 0, m_width, m_height};

        QC::i32 x1 = rect.x > clip.x ? rect.x : clip.x;
        QC::i32 y1 = rect.y > clip.y ? rect.y : clip.y;
        QC::i32 x2 = (rect.x + static_cast<QC::i32>(rect.width)) <
                             (clip.x + static_cast<QC::i32>(clip.width))
                         ? (rect.x + static_cast<QC::i32>(rect.width))
                         : (clip.x + static_cast<QC::i32>(clip.width));
        QC::i32 y2 = (rect.y + static_cast<QC::i32>(rect.height)) <
                             (clip.y + static_cast<QC::i32>(clip.height))
                         ? (rect.y + static_cast<QC::i32>(rect.height))
                         : (clip.y + static_cast<QC::i32>(clip.height));

        if (x2 <= x1 || y2 <= y1)
        {
            return false;
        }

        rect.x = x1;
        rect.y = y1;
        rect.width = static_cast<QC::u32>(x2 - x1);
        rect.height = static_cast<QC::u32>(y2 - y1);

        return true;
    }

} // namespace QW
