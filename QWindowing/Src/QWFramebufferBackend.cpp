// QWindowing FramebufferBackend implementation

#include "QWFramebufferBackend.h"

#include <algorithm>

namespace QW
{
    namespace
    {
        inline QC::Color lerpColor(const QC::Color &from, const QC::Color &to, QC::f32 t)
        {
            return QC::Color::lerp(from, to, t);
        }

        inline QC::u32 umin(QC::u32 a, QC::u32 b)
        {
            return a < b ? a : b;
        }

        // Integer sqrt for 32-bit values (floor).
        inline QC::u32 isqrt_u32(QC::u32 x)
        {
            QC::u32 op = x;
            QC::u32 res = 0;
            QC::u32 one = 1u << 30;
            while (one > op)
            {
                one >>= 2;
            }
            while (one != 0)
            {
                if (op >= res + one)
                {
                    op -= res + one;
                    res += one << 1;
                }
                res >>= 1;
                one >>= 2;
            }
            return res;
        }

        inline bool spanForRoundedRow(const QC::Rect &rect, QC::u32 radius, QC::i32 y, QC::i32 &x1, QC::i32 &x2)
        {
            if (rect.width == 0 || rect.height == 0)
                return false;

            const QC::i32 left = rect.x;
            const QC::i32 right = rect.x + static_cast<QC::i32>(rect.width) - 1;

            if (radius == 0)
            {
                x1 = left;
                x2 = right;
                return x1 <= x2;
            }

            const QC::u32 maxR = umin(rect.width / 2, rect.height / 2);
            if (radius > maxR)
                radius = maxR;
            if (radius == 0)
            {
                x1 = left;
                x2 = right;
                return x1 <= x2;
            }

            const QC::i32 top = rect.y;
            const QC::i32 bottom = rect.y + static_cast<QC::i32>(rect.height) - 1;
            const QC::i32 r = static_cast<QC::i32>(radius);

            const QC::i32 cyTop = top + (r - 1);
            const QC::i32 cyBottom = bottom - (r - 1);

            QC::i32 dy = 0;
            if (y < cyTop)
                dy = cyTop - y;
            else if (y > cyBottom)
                dy = y - cyBottom;

            QC::i32 inset = 0;
            if (dy > 0)
            {
                const QC::u32 r2 = radius * radius;
                const QC::u32 dy2 = static_cast<QC::u32>(dy * dy);
                const QC::u32 inside = (dy2 >= r2) ? 0 : (r2 - dy2);
                const QC::u32 maxX = isqrt_u32(inside);
                inset = (r - 1) - static_cast<QC::i32>(maxX);
                if (inset < 0)
                    inset = 0;
                if (inset > (r - 1))
                    inset = (r - 1);
            }

            x1 = left + inset;
            x2 = right - inset;
            return x1 <= x2;
        }
    } // namespace

    FramebufferBackend::FramebufferBackend(Framebuffer *framebuffer)
        : m_framebuffer(framebuffer)
    {
        m_caps.supportsRoundedRect = true;
        m_caps.supportsShadows = true;
        m_caps.supportsAlpha = true;
        updateTarget();
    }

    void FramebufferBackend::setFramebuffer(Framebuffer *framebuffer)
    {
        m_framebuffer = framebuffer;
        updateTarget();
    }

    bool FramebufferBackend::beginFrame()
    {
        return updateTarget();
    }

    void FramebufferBackend::endFrame()
    {
        if (m_framebuffer)
        {
            m_framebuffer->swap();
        }
    }

    void FramebufferBackend::clear(QC::Color color)
    {
        if (!updateTarget())
            return;

        m_renderer.clear(color);
    }

    void FramebufferBackend::drawRect(const QC::Rect &rect,
                                      QC::Color fill,
                                      QC::Color stroke,
                                      QC::u32 strokeWidth)
    {
        if (!updateTarget())
            return;

        if (rect.width == 0 || rect.height == 0)
            return;

        if (fill.a > 0)
        {
            m_renderer.fillRect(rect, fill);
        }

        if (strokeWidth == 0 || stroke.a == 0)
            return;

        QC::Rect outline = rect;
        for (QC::u32 i = 0; i < strokeWidth; ++i)
        {
            if (outline.width == 0 || outline.height == 0)
                break;

            m_renderer.drawRect(outline, stroke);

            if (outline.width < 2 || outline.height < 2)
                break;

            outline.x += 1;
            outline.y += 1;
            outline.width -= 2;
            outline.height -= 2;
        }
    }

    void FramebufferBackend::drawGradient(const QC::Rect &rect,
                                          QC::Color from,
                                          QC::Color to,
                                          QG::GradientDirection direction)
    {
        if (!updateTarget())
            return;

        if (rect.width == 0 || rect.height == 0)
            return;

        QC::Rect clipped;
        if (!clipRect(rect, clipped))
            return;

        if (direction == QG::GradientDirection::Vertical)
        {
            QC::f32 denom = rect.height > 1 ? static_cast<QC::f32>(rect.height - 1) : 1.0f;
            for (QC::i32 row = 0; row < static_cast<QC::i32>(clipped.height); ++row)
            {
                QC::i32 globalY = clipped.y + row;
                QC::i32 relative = globalY - rect.y;
                QC::f32 t = denom == 0.0f ? 0.0f : static_cast<QC::f32>(relative) / denom;
                QC::Color color = lerpColor(from, to, t);
                m_renderer.drawHLine(clipped.x, globalY, clipped.width, color);
            }
        }
        else
        {
            QC::f32 denom = rect.width > 1 ? static_cast<QC::f32>(rect.width - 1) : 1.0f;
            for (QC::i32 col = 0; col < static_cast<QC::i32>(clipped.width); ++col)
            {
                QC::i32 globalX = clipped.x + col;
                QC::i32 relative = globalX - rect.x;
                QC::f32 t = denom == 0.0f ? 0.0f : static_cast<QC::f32>(relative) / denom;
                QC::Color color = lerpColor(from, to, t);
                m_renderer.drawVLine(globalX, clipped.y, clipped.height, color);
            }
        }
    }

    void FramebufferBackend::drawRoundedRect(const QC::Rect &rect,
                                             QC::u32 radius,
                                             QC::Color fill,
                                             QC::Color stroke,
                                             QC::u32 strokeWidth)
    {
        if (!updateTarget())
            return;

        if (rect.width == 0 || rect.height == 0)
            return;

        const QC::u32 maxR = umin(rect.width / 2, rect.height / 2);
        if (radius > maxR)
            radius = maxR;

        if (radius == 0)
        {
            drawRect(rect, fill, stroke, strokeWidth);
            return;
        }

        // Fill
        if (fill.a > 0)
        {
            const QC::i32 yStart = rect.y;
            const QC::i32 yEnd = rect.y + static_cast<QC::i32>(rect.height);
            for (QC::i32 y = yStart; y < yEnd; ++y)
            {
                QC::i32 x1 = 0, x2 = -1;
                if (!spanForRoundedRow(rect, radius, y, x1, x2))
                    continue;

                const QC::u32 len = static_cast<QC::u32>(x2 - x1 + 1);
                if (len == 0)
                    continue;
                m_renderer.drawHLine(x1, y, len, fill);
            }
        }

        // Stroke (inside the rect, like drawRect does)
        if (strokeWidth == 0 || stroke.a == 0)
            return;

        for (QC::u32 layer = 0; layer < strokeWidth; ++layer)
        {
            QC::Rect outer = rect;
            const QC::u32 inset = layer;
            if (outer.width <= inset * 2 || outer.height <= inset * 2)
                break;
            outer.x += static_cast<QC::i32>(inset);
            outer.y += static_cast<QC::i32>(inset);
            outer.width -= inset * 2;
            outer.height -= inset * 2;

            QC::Rect inner = outer;
            if (inner.width > 2 && inner.height > 2)
            {
                inner.x += 1;
                inner.y += 1;
                inner.width -= 2;
                inner.height -= 2;
            }
            else
            {
                inner.width = 0;
                inner.height = 0;
            }

            QC::u32 rOuter = radius > layer ? (radius - layer) : 0;
            const QC::u32 rInner = (rOuter > 0) ? (rOuter - 1) : 0;
            if (rOuter == 0)
            {
                drawRect(outer, QC::Color::transparent(), stroke, 1);
                continue;
            }

            const QC::i32 yStart = outer.y;
            const QC::i32 yEnd = outer.y + static_cast<QC::i32>(outer.height);
            for (QC::i32 y = yStart; y < yEnd; ++y)
            {
                QC::i32 x1o = 0, x2o = -1;
                if (!spanForRoundedRow(outer, rOuter, y, x1o, x2o))
                    continue;

                bool hasInner = false;
                QC::i32 x1i = 0, x2i = -1;
                if (inner.width > 0 && inner.height > 0 &&
                    y >= inner.y && y < inner.y + static_cast<QC::i32>(inner.height))
                {
                    hasInner = spanForRoundedRow(inner, rInner, y, x1i, x2i);
                }

                if (!hasInner)
                {
                    const QC::u32 len = static_cast<QC::u32>(x2o - x1o + 1);
                    if (len)
                        m_renderer.drawHLine(x1o, y, len, stroke);
                    continue;
                }

                if (x1i > x1o)
                {
                    const QC::u32 len = static_cast<QC::u32>(x1i - x1o);
                    if (len)
                        m_renderer.drawHLine(x1o, y, len, stroke);
                }
                if (x2o > x2i)
                {
                    const QC::u32 len = static_cast<QC::u32>(x2o - x2i);
                    if (len)
                        m_renderer.drawHLine(x2i + 1, y, len, stroke);
                }
            }
        }
    }

    void FramebufferBackend::drawShadow(const QC::Rect &rect,
                                        QC::Point offset,
                                        QC::i32 blurRadius,
                                        QC::Color color,
                                        QC::u8 opacity)
    {
        (void)blurRadius; // Future blur support
        if (!updateTarget())
            return;

        if (opacity == 0)
            return;

        QC::Rect shadow = rect;
        shadow.x += offset.x;
        shadow.y += offset.y;
        fillRectAlpha(shadow, color.withAlpha(opacity));
    }

    void FramebufferBackend::blit(const QC::Rect &rect,
                                  const QC::u32 *pixels,
                                  QC::u32 stride,
                                  bool useAlpha)
    {
        if (!updateTarget() || !pixels)
            return;

        if (rect.width == 0 || rect.height == 0)
            return;

        QC::u32 stridePixels = stride == 0 ? rect.width : stride;
        QC::u32 strideBytes = stridePixels * sizeof(QC::u32);

        if (useAlpha)
        {
            m_renderer.blitAlpha(rect.x, rect.y, pixels, rect.width, rect.height, strideBytes);
        }
        else
        {
            m_renderer.blit(rect.x, rect.y, pixels, rect.width, rect.height, strideBytes);
        }
    }

    bool FramebufferBackend::updateTarget()
    {
        if (!m_framebuffer)
        {
            m_target = TargetDesc{};
            return false;
        }

        void *buffer = m_framebuffer->backBuffer();
        if (!buffer)
            buffer = m_framebuffer->buffer();
        if (!buffer)
        {
            m_target = TargetDesc{};
            return false;
        }

        m_target.width = m_framebuffer->width();
        m_target.height = m_framebuffer->height();
        m_target.pitch = m_framebuffer->pitch();
        m_target.format = convertFormat(m_framebuffer->format());
        m_target.pixels = static_cast<QC::u8 *>(buffer);

        if (m_target.format != QG::PixelFormat::ARGB8888 &&
            m_target.format != QG::PixelFormat::ABGR8888)
        {
            m_target.pixels = nullptr;
            return false;
        }

        m_renderer.setTarget(static_cast<QC::u32 *>(buffer),
                             m_target.width,
                             m_target.height,
                             m_target.pitch);
        return true;
    }

    bool FramebufferBackend::clipRect(const QC::Rect &rect, QC::Rect &clipped) const
    {
        if (m_target.width == 0 || m_target.height == 0)
            return false;

        QC::i32 x1 = std::max(rect.x, 0);
        QC::i32 y1 = std::max(rect.y, 0);
        QC::i32 x2 = std::min(rect.x + static_cast<QC::i32>(rect.width), static_cast<QC::i32>(m_target.width));
        QC::i32 y2 = std::min(rect.y + static_cast<QC::i32>(rect.height), static_cast<QC::i32>(m_target.height));

        if (x2 <= x1 || y2 <= y1)
            return false;

        clipped.x = x1;
        clipped.y = y1;
        clipped.width = static_cast<QC::u32>(x2 - x1);
        clipped.height = static_cast<QC::u32>(y2 - y1);
        return true;
    }

    void FramebufferBackend::fillRectAlpha(const QC::Rect &rect, QC::Color color)
    {
        if (color.a == 0 || !m_target.pixels)
            return;

        QC::Rect clipped;
        if (!clipRect(rect, clipped))
            return;

        for (QC::i32 row = 0; row < static_cast<QC::i32>(clipped.height); ++row)
        {
            QC::u32 *dstRow = reinterpret_cast<QC::u32 *>(m_target.pixels + (clipped.y + row) * m_target.pitch);
            for (QC::i32 col = 0; col < static_cast<QC::i32>(clipped.width); ++col)
            {
                QC::u32 &dstPixel = dstRow[clipped.x + col];
                QC::Color dstColor(dstPixel);
                dstPixel = color.blend(dstColor).value;
            }
        }
    }

    QG::PixelFormat FramebufferBackend::convertFormat(PixelFormat format)
    {
        switch (format)
        {
        case PixelFormat::RGB565:
            return QG::PixelFormat::RGB565;
        case PixelFormat::BGR565:
            return QG::PixelFormat::BGR565;
        case PixelFormat::RGB888:
            return QG::PixelFormat::RGB888;
        case PixelFormat::BGR888:
            return QG::PixelFormat::BGR888;
        case PixelFormat::ABGR8888:
            return QG::PixelFormat::ABGR8888;
        case PixelFormat::ARGB8888:
        default:
            return QG::PixelFormat::ARGB8888;
        }
    }

} // namespace QW
