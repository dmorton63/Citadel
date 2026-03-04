// Frame rendering implementation
// Namespace: QW::Controls

#include "QWControls/Containers/Frame.h"

namespace QW
{
    namespace Controls
    {
        FrameColors::FrameColors()
            : background(Color(240, 240, 240, 255)),
              backgroundEnd(Color(220, 220, 220, 255)),
              borderLight(Color(255, 255, 255, 255)),
              borderDark(Color(100, 100, 100, 255)),
              borderMid(Color(160, 160, 160, 255)),
              shadow(Color(0, 0, 0, 80)),
              glow(Color(0, 120, 215, 128))
        {
        }

        FrameColors FrameColors::defaultColors()
        {
            FrameColors c;
            c.background = Color(240, 240, 240, 255);
            c.backgroundEnd = Color(220, 220, 220, 255);
            c.borderLight = Color(255, 255, 255, 255);
            c.borderDark = Color(100, 100, 100, 255);
            c.borderMid = Color(160, 160, 160, 255);
            c.shadow = Color(0, 0, 0, 80);
            c.glow = Color(0, 120, 215, 128);
            return c;
        }

        FrameColors FrameColors::vistaColors()
        {
            FrameColors c;
            c.background = Color(245, 246, 247, 255);
            c.backgroundEnd = Color(225, 226, 227, 255);
            c.borderLight = Color(255, 255, 255, 255);
            c.borderDark = Color(112, 112, 112, 255);
            c.borderMid = Color(174, 179, 185, 255);
            c.shadow = Color(0, 0, 0, 60);
            c.glow = Color(60, 127, 177, 180);
            return c;
        }

        FrameColors FrameColors::darkColors()
        {
            FrameColors c;
            c.background = Color(45, 45, 48, 255);
            c.backgroundEnd = Color(30, 30, 30, 255);
            c.borderLight = Color(70, 70, 70, 255);
            c.borderDark = Color(20, 20, 20, 255);
            c.borderMid = Color(63, 63, 70, 255);
            c.shadow = Color(0, 0, 0, 120);
            c.glow = Color(0, 122, 204, 180);
            return c;
        }

        FrameColors FrameColors::lightColors()
        {
            FrameColors c;
            c.background = Color(255, 255, 255, 255);
            c.backgroundEnd = Color(245, 245, 245, 255);
            c.borderLight = Color(255, 255, 255, 255);
            c.borderDark = Color(180, 180, 180, 255);
            c.borderMid = Color(200, 200, 200, 255);
            c.shadow = Color(0, 0, 0, 40);
            c.glow = Color(0, 120, 215, 100);
            return c;
        }

        FrameMetrics::FrameMetrics()
            : borderWidth(1),
              shadowOffset(2),
              shadowSize(4),
              cornerRadius(0),
              paddingLeft(0),
              paddingTop(0),
              paddingRight(0),
              paddingBottom(0)
        {
        }

        void FrameMetrics::setPadding(QC::u32 all)
        {
            paddingLeft = paddingTop = paddingRight = paddingBottom = all;
        }

        void FrameMetrics::setPadding(QC::u32 horizontal, QC::u32 vertical)
        {
            paddingLeft = paddingRight = horizontal;
            paddingTop = paddingBottom = vertical;
        }

        void FrameMetrics::setPadding(QC::u32 left, QC::u32 top, QC::u32 right, QC::u32 bottom)
        {
            paddingLeft = left;
            paddingTop = top;
            paddingRight = right;
            paddingBottom = bottom;
        }

        Frame::Frame()
            : m_style(FrameStyle::BorderFlat | FrameStyle::FillSolid),
              m_bounds{0, 0, 0, 0},
              m_colors(),
              m_metrics()
        {
        }

        Frame::Frame(QC::u32 style)
            : m_style(style),
              m_bounds{0, 0, 0, 0},
              m_colors(),
              m_metrics()
        {
        }

        Frame::Frame(QC::u32 style, const FrameColors &colors)
            : m_style(style),
              m_bounds{0, 0, 0, 0},
              m_colors(colors),
              m_metrics()
        {
        }

        void Frame::setBorderStyle(QC::u32 borderFlag)
        {
            m_style = (m_style & ~FrameStyle::BorderMask) | (borderFlag & FrameStyle::BorderMask);
        }

        void Frame::setPosition(QC::i32 x, QC::i32 y)
        {
            m_bounds.x = x;
            m_bounds.y = y;
        }

        void Frame::setSize(QC::u32 width, QC::u32 height)
        {
            m_bounds.width = width;
            m_bounds.height = height;
        }

        Rect Frame::contentRect() const
        {
            Rect content;
            QC::u32 totalBorder = m_metrics.borderWidth;

            if (hasStyle(FrameStyle::BorderDouble) || hasStyle(FrameStyle::BorderEtched) ||
                hasStyle(FrameStyle::BorderGroove))
            {
                totalBorder *= 2;
            }

            content.x = m_bounds.x + static_cast<QC::i32>(totalBorder + m_metrics.paddingLeft);
            content.y = m_bounds.y + static_cast<QC::i32>(totalBorder + m_metrics.paddingTop);
            content.width = m_bounds.width - totalBorder * 2 - m_metrics.paddingLeft - m_metrics.paddingRight;
            content.height = m_bounds.height - totalBorder * 2 - m_metrics.paddingTop - m_metrics.paddingBottom;

            return content;
        }

        void Frame::paint(QG::IPainter *painter) const
        {
            if (!painter)
            {
                return;
            }

            if (hasStyle(FrameStyle::DropShadow) || hasStyle(FrameStyle::DropShadowSoft))
            {
                paintShadow(painter);
            }

            paintBackground(painter);

            if (hasStyle(FrameStyle::InnerShadow))
            {
                paintInnerShadow(painter);
            }

            paintBorder(painter);
        }

        void Frame::paintShadow(QG::IPainter *painter) const
        {
            paintDropShadow(painter);
        }

        void Frame::paintDropShadow(QG::IPainter *painter) const
        {
            QC::i32 offset = static_cast<QC::i32>(m_metrics.shadowOffset);
            QC::u32 size = m_metrics.shadowSize;

            Rect shadowRect = {
                m_bounds.x + offset,
                m_bounds.y + offset,
                m_bounds.width,
                m_bounds.height};

            if (hasStyle(FrameStyle::DropShadowSoft) && size > 1)
            {
                for (QC::u32 i = 0; i < size; ++i)
                {
                    // Soft shadow: draw a larger, lighter outer layer first,
                    // then smaller, darker layers towards the control.
                    QC::u8 alpha = static_cast<QC::u8>((m_colors.shadow.a * (i + 1)) / size);
                    Color layerColor(m_colors.shadow.r, m_colors.shadow.g, m_colors.shadow.b, alpha);

                    QC::u32 layer = size - i;
                    Rect layerRect = {
                        shadowRect.x - static_cast<QC::i32>(layer),
                        shadowRect.y - static_cast<QC::i32>(layer),
                        shadowRect.width + layer * 2,
                        shadowRect.height + layer * 2};

                    painter->fillRect(layerRect, layerColor);
                }
            }
            else
            {
                painter->fillRect(shadowRect, m_colors.shadow);
            }
        }

        void Frame::paintInnerShadow(QG::IPainter *painter) const
        {
            QC::u32 size = m_metrics.shadowSize > 3 ? 3 : m_metrics.shadowSize;
            QC::u8 baseAlpha = m_colors.shadow.a / 2;

            for (QC::u32 i = 0; i < size; ++i)
            {
                QC::u8 alpha = static_cast<QC::u8>((baseAlpha * (size - i)) / size);
                Color layerColor(m_colors.shadow.r, m_colors.shadow.g, m_colors.shadow.b, alpha);

                QC::i32 inset = static_cast<QC::i32>(m_metrics.borderWidth + i);

                Rect top = {m_bounds.x + inset, m_bounds.y + inset,
                            m_bounds.width - static_cast<QC::u32>(inset * 2), 1};
                painter->fillRect(top, layerColor);

                Rect left = {m_bounds.x + inset, m_bounds.y + inset,
                             1, m_bounds.height - static_cast<QC::u32>(inset * 2)};
                painter->fillRect(left, layerColor);
            }
        }

        void Frame::paintBackground(QG::IPainter *painter) const
        {
            QC::u32 fillType = fillStyle();

            if (fillType & FrameStyle::FillTransparent)
            {
                return;
            }

            if (fillType & FrameStyle::FillGradientV)
            {
                paintFillGradientV(painter);
            }
            else if (fillType & FrameStyle::FillGradientH)
            {
                paintFillGradientH(painter);
            }
            else
            {
                paintFillSolid(painter);
            }
        }

        void Frame::paintFillSolid(QG::IPainter *painter) const
        {
            painter->fillRect(m_bounds, m_colors.background);
        }

        void Frame::paintFillGradientV(QG::IPainter *painter) const
        {
            if (m_bounds.height == 0)
            {
                return;
            }

            for (QC::u32 y = 0; y < m_bounds.height; ++y)
            {
                QC::u32 t = (y * 255) / m_bounds.height;
                QC::u32 invT = 255 - t;

                Color lineColor;
                lineColor.r = static_cast<QC::u8>((m_colors.background.r * invT + m_colors.backgroundEnd.r * t) / 255);
                lineColor.g = static_cast<QC::u8>((m_colors.background.g * invT + m_colors.backgroundEnd.g * t) / 255);
                lineColor.b = static_cast<QC::u8>((m_colors.background.b * invT + m_colors.backgroundEnd.b * t) / 255);
                lineColor.a = static_cast<QC::u8>((m_colors.background.a * invT + m_colors.backgroundEnd.a * t) / 255);

                Rect line = {m_bounds.x, m_bounds.y + static_cast<QC::i32>(y), m_bounds.width, 1};
                painter->fillRect(line, lineColor);
            }
        }

        void Frame::paintFillGradientH(QG::IPainter *painter) const
        {
            if (m_bounds.width == 0)
            {
                return;
            }

            for (QC::u32 x = 0; x < m_bounds.width; ++x)
            {
                QC::u32 t = (x * 255) / m_bounds.width;
                QC::u32 invT = 255 - t;

                Color lineColor;
                lineColor.r = static_cast<QC::u8>((m_colors.background.r * invT + m_colors.backgroundEnd.r * t) / 255);
                lineColor.g = static_cast<QC::u8>((m_colors.background.g * invT + m_colors.backgroundEnd.g * t) / 255);
                lineColor.b = static_cast<QC::u8>((m_colors.background.b * invT + m_colors.backgroundEnd.b * t) / 255);
                lineColor.a = static_cast<QC::u8>((m_colors.background.a * invT + m_colors.backgroundEnd.a * t) / 255);

                Rect line = {m_bounds.x + static_cast<QC::i32>(x), m_bounds.y, 1, m_bounds.height};
                painter->fillRect(line, lineColor);
            }
        }

        void Frame::paintBorder(QG::IPainter *painter) const
        {
            QC::u32 borderType = borderStyle();

            if (borderType == FrameStyle::None)
            {
                return;
            }

            if (borderType & FrameStyle::BorderRaised)
            {
                paintBorderRaised(painter);
            }
            else if (borderType & FrameStyle::BorderSunken)
            {
                paintBorderSunken(painter);
            }
            else if (borderType & FrameStyle::BorderEtched)
            {
                paintBorderEtched(painter);
            }
            else if (borderType & FrameStyle::BorderGroove)
            {
                paintBorderGroove(painter);
            }
            else if (borderType & FrameStyle::BorderDouble)
            {
                paintBorderDouble(painter);
            }
            else
            {
                paintBorderFlat(painter);
            }
        }

        void Frame::paintBorderFlat(QG::IPainter *painter) const
        {
            QC::u32 w = m_metrics.borderWidth;

            for (QC::u32 i = 0; i < w; ++i)
            {
                Rect borderRect = {
                    m_bounds.x + static_cast<QC::i32>(i),
                    m_bounds.y + static_cast<QC::i32>(i),
                    m_bounds.width - i * 2,
                    m_bounds.height - i * 2};
                painter->drawRect(borderRect, m_colors.borderMid);
            }
        }

        void Frame::paintBorderRaised(QG::IPainter *painter) const
        {
            QC::u32 w = m_metrics.borderWidth;

            for (QC::u32 i = 0; i < w; ++i)
            {
                QC::i32 inset = static_cast<QC::i32>(i);

                Rect top = {m_bounds.x + inset, m_bounds.y + inset,
                            m_bounds.width - static_cast<QC::u32>(inset * 2), 1};
                painter->fillRect(top, m_colors.borderLight);

                Rect left = {m_bounds.x + inset, m_bounds.y + inset,
                             1, m_bounds.height - static_cast<QC::u32>(inset * 2)};
                painter->fillRect(left, m_colors.borderLight);

                Rect bottom = {m_bounds.x + inset,
                               m_bounds.y + static_cast<QC::i32>(m_bounds.height) - 1 - inset,
                               m_bounds.width - static_cast<QC::u32>(inset * 2), 1};
                painter->fillRect(bottom, m_colors.borderDark);

                Rect right = {m_bounds.x + static_cast<QC::i32>(m_bounds.width) - 1 - inset,
                              m_bounds.y + inset,
                              1, m_bounds.height - static_cast<QC::u32>(inset * 2)};
                painter->fillRect(right, m_colors.borderDark);
            }
        }

        void Frame::paintBorderSunken(QG::IPainter *painter) const
        {
            QC::u32 w = m_metrics.borderWidth;

            for (QC::u32 i = 0; i < w; ++i)
            {
                QC::i32 inset = static_cast<QC::i32>(i);

                Rect top = {m_bounds.x + inset, m_bounds.y + inset,
                            m_bounds.width - static_cast<QC::u32>(inset * 2), 1};
                painter->fillRect(top, m_colors.borderDark);

                Rect left = {m_bounds.x + inset, m_bounds.y + inset,
                             1, m_bounds.height - static_cast<QC::u32>(inset * 2)};
                painter->fillRect(left, m_colors.borderDark);

                Rect bottom = {m_bounds.x + inset,
                               m_bounds.y + static_cast<QC::i32>(m_bounds.height) - 1 - inset,
                               m_bounds.width - static_cast<QC::u32>(inset * 2), 1};
                painter->fillRect(bottom, m_colors.borderLight);

                Rect right = {m_bounds.x + static_cast<QC::i32>(m_bounds.width) - 1 - inset,
                              m_bounds.y + inset,
                              1, m_bounds.height - static_cast<QC::u32>(inset * 2)};
                painter->fillRect(right, m_colors.borderLight);
            }
        }

        void Frame::paintBorderEtched(QG::IPainter *painter) const
        {
            QC::u32 w = m_metrics.borderWidth;
            for (QC::u32 i = 0; i < w; ++i)
            {
                QC::i32 inset = static_cast<QC::i32>(i);

                Rect outer = {m_bounds.x + inset, m_bounds.y + inset,
                              m_bounds.width - static_cast<QC::u32>(inset * 2),
                              m_bounds.height - static_cast<QC::u32>(inset * 2)};
                painter->drawRect(outer, m_colors.borderLight);

                Rect inner = {outer.x + 1, outer.y + 1,
                              outer.width - 2,
                              outer.height - 2};
                painter->drawRect(inner, m_colors.borderDark);
            }
        }

        void Frame::paintBorderDouble(QG::IPainter *painter) const
        {
            QC::i32 inset = 0;
            for (QC::u32 i = 0; i < 2; ++i)
            {
                Rect rect = {m_bounds.x + inset,
                             m_bounds.y + inset,
                             m_bounds.width - inset * 2,
                             m_bounds.height - inset * 2};
                painter->drawRect(rect, m_colors.borderMid);
                inset += m_metrics.borderWidth;
            }
        }

        void Frame::paintBorderGroove(QG::IPainter *painter) const
        {
            QC::u32 w = m_metrics.borderWidth;
            for (QC::u32 i = 0; i < w; ++i)
            {
                QC::i32 inset = static_cast<QC::i32>(i);
                Rect outer = {m_bounds.x + inset, m_bounds.y + inset,
                              m_bounds.width - static_cast<QC::u32>(inset * 2),
                              m_bounds.height - static_cast<QC::u32>(inset * 2)};
                painter->drawRect(outer, m_colors.borderDark);

                Rect inner = {outer.x + 1, outer.y + 1,
                              outer.width - 2,
                              outer.height - 2};
                painter->drawRect(inner, m_colors.borderLight);
            }
        }
    }
} // namespace QW
