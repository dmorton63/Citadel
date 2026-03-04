// QWindowing SurfaceBackend implementation

#include "QWSurfaceBackend.h"

#include <algorithm>

namespace QW
{

    namespace
    {
        inline QC::u32 umin(QC::u32 a, QC::u32 b)
        {
            return a < b ? a : b;
        }

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

    SurfaceBackend::SurfaceBackend()
        : m_surface(nullptr)
    {
        m_caps.supportsRoundedRect = true;
        m_caps.supportsShadows = true;
        m_caps.supportsAlpha = true;
        m_target = TargetDesc{};
    }

    void SurfaceBackend::setSurface(QG::PainterSurface *surface,
                                    QC::u32 *pixels,
                                    QC::u32 width,
                                    QC::u32 height,
                                    QC::u32 pitchBytes)
    {
        m_surface = surface;
        if (!surface || !pixels || width == 0 || height == 0)
        {
            m_target = TargetDesc{};
            return;
        }

        m_target.width = width;
        m_target.height = height;
        m_target.pitch = pitchBytes ? pitchBytes : width * sizeof(QC::u32);
        m_target.pixels = reinterpret_cast<QC::u8 *>(pixels);
        m_target.format = QG::PixelFormat::ARGB8888;
    }

    bool SurfaceBackend::beginFrame()
    {
        return ensureSurface();
    }

    void SurfaceBackend::endFrame()
    {
        // No swap needed for software surfaces
    }

    void SurfaceBackend::clear(QC::Color color)
    {
        if (!ensureSurface())
            return;

        m_surface->clear(color);
    }

    void SurfaceBackend::drawRect(const QC::Rect &rect,
                                  QC::Color fill,
                                  QC::Color stroke,
                                  QC::u32 strokeWidth)
    {
        if (!ensureSurface())
            return;

        if (fill.a > 0 && rect.width > 0 && rect.height > 0)
        {
            m_surface->fillRect(rect, QG::Brush::solid(fill));
        }

        if (stroke.a > 0 && strokeWidth > 0)
        {
            QG::Pen pen(stroke);
            pen.setWidth(strokeWidth);
            m_surface->drawRect(rect, pen);
        }
    }

    void SurfaceBackend::drawGradient(const QC::Rect &rect,
                                      QC::Color from,
                                      QC::Color to,
                                      QG::GradientDirection direction)
    {
        if (!ensureSurface())
            return;

        if (direction == QG::GradientDirection::Vertical)
        {
            m_surface->fillGradientV(rect, from, to);
        }
        else
        {
            m_surface->fillGradientH(rect, from, to);
        }
    }

    void SurfaceBackend::drawRoundedRect(const QC::Rect &rect,
                                         QC::u32 radius,
                                         QC::Color fill,
                                         QC::Color stroke,
                                         QC::u32 strokeWidth)
    {
        if (!ensureSurface())
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

        auto blendSpan = [&](QC::i32 y, QC::i32 x1, QC::i32 x2, QC::Color color)
        {
            if (color.a == 0)
                return;
            if (y < 0 || y >= static_cast<QC::i32>(m_target.height))
                return;

            if (x2 < 0 || x1 >= static_cast<QC::i32>(m_target.width))
                return;
            if (x1 < 0)
                x1 = 0;
            const QC::i32 maxX = static_cast<QC::i32>(m_target.width) - 1;
            if (x2 > maxX)
                x2 = maxX;
            if (x1 > x2)
                return;

            QC::u32 *row = reinterpret_cast<QC::u32 *>(m_target.pixels + y * m_target.pitch);
            if (color.a == 255)
            {
                for (QC::i32 x = x1; x <= x2; ++x)
                {
                    row[x] = color.value;
                }
            }
            else
            {
                for (QC::i32 x = x1; x <= x2; ++x)
                {
                    QC::u32 &dstPixel = row[x];
                    QC::Color dstColor(dstPixel);
                    dstPixel = color.blend(dstColor).value;
                }
            }
        };

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
                blendSpan(y, x1, x2, fill);
            }
        }

        // Stroke (inside the rect)
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
                // Fallback: square border for tiny shapes
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
                    blendSpan(y, x1o, x2o, stroke);
                    continue;
                }

                if (x1i > x1o)
                {
                    blendSpan(y, x1o, x1i - 1, stroke);
                }
                if (x2o > x2i)
                {
                    blendSpan(y, x2i + 1, x2o, stroke);
                }
            }
        }
    }

    void SurfaceBackend::drawShadow(const QC::Rect &rect,
                                    QC::Point offset,
                                    QC::i32 blurRadius,
                                    QC::Color color,
                                    QC::u8 opacity)
    {
        (void)blurRadius; // Blur not supported yet on surface backend
        if (!ensureSurface() || opacity == 0)
            return;

        QC::Rect shadowRect = rect;
        shadowRect.x += offset.x;
        shadowRect.y += offset.y;
        fillRectAlpha(shadowRect, color.withAlpha(opacity));
    }

    void SurfaceBackend::blit(const QC::Rect &rect,
                              const QC::u32 *pixels,
                              QC::u32 stride,
                              bool useAlpha)
    {
        if (!ensureSurface() || !pixels)
            return;

        if (useAlpha)
        {
            m_surface->blitAlpha(rect.x, rect.y, pixels, rect.width, rect.height, stride);
        }
        else
        {
            m_surface->blit(rect.x, rect.y, pixels, rect.width, rect.height, stride);
        }
    }

    bool SurfaceBackend::clipRect(const QC::Rect &rect, QC::Rect &clipped) const
    {
        if (!ensureSurface())
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

    void SurfaceBackend::fillRectAlpha(const QC::Rect &rect, QC::Color color)
    {
        if (!ensureSurface() || color.a == 0)
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

} // namespace QW
