// Panel implementation
// Namespace: QW::Controls

#include "QWControls/Containers/Panel.h"
#include "QWWindow.h"
#include "QWStyleTypes.h"
#include "QGPen.h"

namespace QW
{
    namespace Controls
    {
        namespace
        {
            PanelBorderStyle translateBorderStyle(BorderStyle style)
            {
                switch (style)
                {
                case BorderStyle::None:
                    return PanelBorderStyle::None;
                case BorderStyle::Flat:
                    return PanelBorderStyle::Flat;
                case BorderStyle::Raised:
                    return PanelBorderStyle::Raised;
                case BorderStyle::Sunken:
                    return PanelBorderStyle::Sunken;
                case BorderStyle::Etched:
                    return PanelBorderStyle::Etched;
                }
                return PanelBorderStyle::Flat;
            }
        }

        Panel::Panel()
            : Container(),
              m_frameVisible(true),
              m_borderStyle(BorderStyle::Flat),
              m_paddingLeft(0),
              m_paddingTop(0),
              m_paddingRight(0),
              m_paddingBottom(0),
              m_borderWidth(1),
              m_hasBorderColorOverride(false),
              m_borderColor(Color(0, 0, 0, 0)),
              m_hasBackgroundOverride(false),
              m_backgroundColor(Color(0, 0, 0, 0))
        {
        }

        Panel::Panel(Window *window, Rect bounds)
            : Container(window, bounds),
              m_frameVisible(true),
              m_borderStyle(BorderStyle::Flat),
              m_paddingLeft(0),
              m_paddingTop(0),
              m_paddingRight(0),
              m_paddingBottom(0),
              m_borderWidth(1),
              m_hasBorderColorOverride(false),
              m_borderColor(Color(0, 0, 0, 0)),
              m_hasBackgroundOverride(false),
              m_backgroundColor(Color(0, 0, 0, 0))
        {
        }

        void Panel::setBorderStyle(BorderStyle style)
        {
            m_borderStyle = style;
            invalidate();
        }

        void Panel::setBorderColor(Color color)
        {
            m_hasBorderColorOverride = true;
            m_borderColor = color;
            invalidate();
        }

        void Panel::setBorderWidth(QC::u32 width)
        {
            m_borderWidth = width;
            invalidate();
        }

        void Panel::setBackgroundColor(Color color)
        {
            m_hasBackgroundOverride = true;
            m_backgroundColor = color;
            invalidate();
        }

        void Panel::clearBackgroundColor()
        {
            if (!m_hasBackgroundOverride)
                return;
            m_hasBackgroundOverride = false;
            invalidate();
        }

        void Panel::setPadding(QC::u32 left, QC::u32 top, QC::u32 right, QC::u32 bottom)
        {
            m_paddingLeft = left;
            m_paddingTop = top;
            m_paddingRight = right;
            m_paddingBottom = bottom;
        }

        Rect Panel::clientRect() const
        {
            QC::u32 bw = m_borderWidth;
            Rect client;
            client.x = static_cast<QC::i32>(bw + m_paddingLeft);
            client.y = static_cast<QC::i32>(bw + m_paddingTop);
            client.width = m_bounds.width - bw * 2 - m_paddingLeft - m_paddingRight;
            client.height = m_bounds.height - bw * 2 - m_paddingTop - m_paddingBottom;
            return client;
        }

        void Panel::paint(const PaintContext &context)
        {
            if (!m_visible || !m_window)
            {
                return;
            }

            Rect abs = absoluteBounds();
            bool drewDecoration = false;

            if (m_frameVisible && context.styleRenderer)
            {
                PanelPaintArgs args{};
                args.bounds = abs;
                args.sunken = (m_borderStyle == BorderStyle::Sunken ||
                               m_borderStyle == BorderStyle::Etched);
                args.borderStyle = translateBorderStyle(m_borderStyle);
                args.borderWidth = (m_borderWidth == 0) ? 1 : m_borderWidth;
                if (m_hasBackgroundOverride)
                {
                    args.hasBackgroundOverride = true;
                    args.backgroundColor = m_backgroundColor;
                }
                if (m_hasBorderColorOverride)
                {
                    args.hasBorderColorOverride = true;
                    args.borderColor = m_borderColor;
                }
                context.styleRenderer->drawPanel(args);
                drewDecoration = true;
            }
            else if (context.painter)
            {
                if (m_hasBackgroundOverride)
                {
                    context.painter->fillRect(abs, m_backgroundColor);
                }

                if (m_frameVisible && m_borderStyle != BorderStyle::None)
                {
                    const QC::u32 bw = (m_borderWidth == 0) ? 1 : m_borderWidth;
                    QC::Color baseColor = m_hasBorderColorOverride ? m_borderColor : QC::Color::buttonShadow();
                    switch (m_borderStyle)
                    {
                    case BorderStyle::Raised:
                        context.painter->drawRaisedBorder(abs,
                                                          baseColor.lighter(0.35f),
                                                          baseColor.darker(0.4f),
                                                          bw);
                        break;
                    case BorderStyle::Sunken:
                        context.painter->drawSunkenBorder(abs,
                                                          baseColor.lighter(0.35f),
                                                          baseColor.darker(0.4f),
                                                          bw);
                        break;
                    case BorderStyle::Etched:
                        context.painter->drawEtchedBorder(abs,
                                                          baseColor.lighter(0.35f),
                                                          baseColor.darker(0.4f));
                        break;
                    case BorderStyle::Flat:
                    default:
                        context.painter->drawRect(abs, QG::Pen(baseColor, bw));
                        break;
                    }
                }

                drewDecoration = m_frameVisible || m_hasBackgroundOverride;
            }

            if (!drewDecoration && context.painter)
            {
                context.painter->fillRect(abs, QC::Color::transparent());
            }

            paintChildren(context);
        }
    }
} // namespace QW
