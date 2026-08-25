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

            QC::i32 inset = 0;
            const QC::i32 topBandEnd = top + r - 1;
            const QC::i32 bottomBandStart = bottom - r + 1;
            if (y <= topBandEnd || y >= bottomBandStart)
            {
                const QC::i32 centerY = (y <= topBandEnd)
                                          ? (top + r)
                                          : (bottom - r + 1);
                const QC::u32 diameter = radius * 2u;
                const QC::i32 dyScaled = ((y * 2) + 1) - (centerY * 2);
                const QC::u32 dyScaledAbs = static_cast<QC::u32>(dyScaled < 0 ? -dyScaled : dyScaled);
                const QC::u32 diameter2 = diameter * diameter;
                const QC::u32 dy2 = dyScaledAbs * dyScaledAbs;
                const QC::u32 inside = (dy2 >= diameter2) ? 0 : (diameter2 - dy2);
                const QC::u32 maxDxScaled = isqrt_u32(inside);
                inset = static_cast<QC::i32>((diameter - maxDxScaled) / 2u);
                if (inset < 0)
                    inset = 0;
                if (inset > r)
                    inset = r;
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

        // Respect the painter clip rect (used for dirty-rect repaint).
        // The rounded-rect implementation below draws directly into the target
        // pixel buffer, so it must explicitly clip or it can overwrite pixels
        // outside the dirty region (e.g., wiping button text until a later hover
        // triggers a repaint).
        QC::Rect clipped;
        if (!clipRect(rect, clipped))
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

            // Clip to the requested draw bounds (includes painter clip rect).
            const QC::i32 clipTop = clipped.y;
            const QC::i32 clipBottom = clipped.y + static_cast<QC::i32>(clipped.height);
            if (y < clipTop || y >= clipBottom)
                return;

            if (x2 < 0 || x1 >= static_cast<QC::i32>(m_target.width))
                return;
            if (x1 < 0)
                x1 = 0;
            const QC::i32 maxX = static_cast<QC::i32>(m_target.width) - 1;
            if (x2 > maxX)
                x2 = maxX;

            const QC::i32 clipLeft = clipped.x;
            const QC::i32 clipRight = clipped.x + static_cast<QC::i32>(clipped.width);
            if (x1 < clipLeft)
                x1 = clipLeft;
            if (x2 >= clipRight)
                x2 = clipRight - 1;
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
            const QC::i32 yStart = clipped.y;
            const QC::i32 yEnd = clipped.y + static_cast<QC::i32>(clipped.height);
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

            QC::i32 yStart = outer.y;
            QC::i32 yEnd = outer.y + static_cast<QC::i32>(outer.height);
            const QC::i32 clipTop = clipped.y;
            const QC::i32 clipBottom = clipped.y + static_cast<QC::i32>(clipped.height);
            if (yStart < clipTop)
                yStart = clipTop;
            if (yEnd > clipBottom)
                yEnd = clipBottom;
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
        if (!ensureSurface() || opacity == 0)
            return;

        QC::Rect shadowRect = rect;
        shadowRect.x += offset.x;
        shadowRect.y += offset.y;

        if (blurRadius <= 0)
        {
            // No blur — flat fill.
            fillRectAlpha(shadowRect, color.withAlpha(opacity));
            return;
        }

        // Box-blur shadow: expand rect by blurRadius on each side, then paint
        // rows with alpha that falls off linearly from the center outward.
        const QC::i32 r = blurRadius;
        const QC::Rect expanded{shadowRect.x - r,
                                shadowRect.y - r,
                                shadowRect.width  + static_cast<QC::u32>(2 * r),
                                shadowRect.height + static_cast<QC::u32>(2 * r)};
        QC::Rect clipped{};
        if (!clipRect(expanded, clipped))
            return;

        const QC::i32 cx1 = shadowRect.x;
        const QC::i32 cy1 = shadowRect.y;
        const QC::i32 cx2 = shadowRect.x + static_cast<QC::i32>(shadowRect.width);
        const QC::i32 cy2 = shadowRect.y + static_cast<QC::i32>(shadowRect.height);
        const QC::i32 ex1 = expanded.x;
        const QC::i32 ey1 = expanded.y;
        const QC::i32 ex2 = ex1 + static_cast<QC::i32>(expanded.width);
        const QC::i32 ey2 = ey1 + static_cast<QC::i32>(expanded.height);

        QC::u32 *pixels = reinterpret_cast<QC::u32 *>(m_target.pixels);
        const QC::u32 stride = m_target.pitch / sizeof(QC::u32);
        const QC::u32 surfW = m_target.width;
        const QC::u32 surfH = m_target.height;

        for (QC::i32 py = std::max(clipped.y, ey1); py < std::min(clipped.y + static_cast<QC::i32>(clipped.height), ey2); ++py)
        {
            if (py < 0 || static_cast<QC::u32>(py) >= surfH)
                continue;

            // Distance outside core shadow rect in Y direction.
            QC::i32 yDist = 0;
            if (py < cy1) yDist = cy1 - py;
            else if (py >= cy2) yDist = py - cy2 + 1;

            for (QC::i32 px = std::max(clipped.x, ex1); px < std::min(clipped.x + static_cast<QC::i32>(clipped.width), ex2); ++px)
            {
                if (px < 0 || static_cast<QC::u32>(px) >= surfW)
                    continue;

                QC::i32 xDist = 0;
                if (px < cx1) xDist = cx1 - px;
                else if (px >= cx2) xDist = px - cx2 + 1;

                const QC::i32 dist = xDist > yDist ? xDist : yDist;
                if (dist > r)
                    continue;

                // Linear falloff from opacity at dist=0 to 0 at dist=r.
                const QC::u32 a = static_cast<QC::u32>(opacity) * static_cast<QC::u32>(r - dist) / static_cast<QC::u32>(r);
                if (a == 0)
                    continue;

                QC::u32 &dst = pixels[py * stride + px];
                const QC::u32 sr = (color.value >> 16) & 0xFF;
                const QC::u32 sg = (color.value >>  8) & 0xFF;
                const QC::u32 sb = (color.value      ) & 0xFF;
                const QC::u32 dr = (dst >> 16) & 0xFF;
                const QC::u32 dg = (dst >>  8) & 0xFF;
                const QC::u32 db = (dst      ) & 0xFF;
                const QC::u32 nr = (sr * a + dr * (255u - a)) / 255u;
                const QC::u32 ng = (sg * a + dg * (255u - a)) / 255u;
                const QC::u32 nb = (sb * a + db * (255u - a)) / 255u;
                dst = (0xFF000000u) | (nr << 16) | (ng << 8) | nb;
            }
        }
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

        // Base bounds: surface dimensions.
        QC::Rect hardClip(0, 0, m_target.width, m_target.height);

        // Also honor the painter's clip rect (used for dirty-rect repaint).
        // PainterSurface::clipRect() returns full bounds if no clip is active.
        QC::Rect softClip = hardClip;
        if (m_surface)
        {
            softClip = m_surface->clipRect();
        }

        const QC::i32 clipX1 = std::max(hardClip.x, softClip.x);
        const QC::i32 clipY1 = std::max(hardClip.y, softClip.y);
        const QC::i32 clipX2 = std::min(hardClip.x + static_cast<QC::i32>(hardClip.width), softClip.x + static_cast<QC::i32>(softClip.width));
        const QC::i32 clipY2 = std::min(hardClip.y + static_cast<QC::i32>(hardClip.height), softClip.y + static_cast<QC::i32>(softClip.height));

        QC::i32 x1 = std::max(rect.x, clipX1);
        QC::i32 y1 = std::max(rect.y, clipY1);
        QC::i32 x2 = std::min(rect.x + static_cast<QC::i32>(rect.width), clipX2);
        QC::i32 y2 = std::min(rect.y + static_cast<QC::i32>(rect.height), clipY2);

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
