// QWControls IconButton - Icon-only button with tooltip-on-hover
// Namespace: QW::Controls

#include "QWControls/Leaf/IconButton.h"
#include "QWWindow.h"
#include "QGPainter.h"
#include "QG/Image.h"
#include <cstring>

namespace QW
{
    namespace Controls
    {

        namespace
        {
            inline QC::i32 imax(QC::i32 a, QC::i32 b) { return a > b ? a : b; }
            inline QC::i32 imin(QC::i32 a, QC::i32 b) { return a < b ? a : b; }
        }

        IconButton::IconButton()
            : ControlBase()
        {
            m_tooltip[0] = '\0';
        }

        IconButton::IconButton(Window *window, Rect bounds)
            : ControlBase(window, bounds)
        {
            m_tooltip[0] = '\0';
        }

        void IconButton::setIcon(const QG::ImageSurface *icon)
        {
            if (m_icon == icon)
                return;
            m_icon = icon;
            invalidate();
        }

        void IconButton::setTooltipText(const char *text)
        {
            if (!text)
            {
                m_tooltip[0] = '\0';
                return;
            }

            std::strncpy(m_tooltip, text, sizeof(m_tooltip) - 1);
            m_tooltip[sizeof(m_tooltip) - 1] = '\0';
        }

        void IconButton::setClickHandler(IconButtonClickHandler handler, void *userData)
        {
            m_clickHandler = handler;
            m_clickUserData = userData;
        }

        void IconButton::setRole(ButtonRole role)
        {
            if (m_role == role)
                return;
            m_role = role;
            invalidate();
        }

        void IconButton::setTextScaleOverride(float scale)
        {
            if (m_textScaleOverride == scale)
                return;
            m_textScaleOverride = scale;
            invalidate();
        }

        Rect IconButton::contentHitRect(const Rect &absBounds) const
        {
            if (absBounds.width == 0 || absBounds.height == 0)
                return absBounds;

            // Match StyleRenderer icon sizing logic for icon-only buttons.
            QC::i32 iconSize = static_cast<QC::i32>(absBounds.height) - 12;
            if (iconSize < 12)
                iconSize = static_cast<QC::i32>(absBounds.height);
            if (iconSize > 24)
                iconSize = 24;

            const QC::i32 contentW = iconSize;
            const QC::i32 contentH = iconSize;

            QC::i32 contentX = absBounds.x + (static_cast<QC::i32>(absBounds.width) - contentW) / 2;
            QC::i32 contentY = absBounds.y + (static_cast<QC::i32>(absBounds.height) - contentH) / 2;

            const QC::i32 padX = 8;
            const QC::i32 padY = 6;

            QC::i32 l = contentX - padX;
            QC::i32 t = contentY - padY;
            QC::i32 r = contentX + contentW + padX;
            QC::i32 b = contentY + contentH + padY;

            const QC::i32 bl = absBounds.x;
            const QC::i32 bt = absBounds.y;
            const QC::i32 br = absBounds.x + static_cast<QC::i32>(absBounds.width);
            const QC::i32 bb = absBounds.y + static_cast<QC::i32>(absBounds.height);

            l = imax(l, bl);
            t = imax(t, bt);
            r = imin(r, br);
            b = imin(b, bb);

            if (r <= l || b <= t)
                return Rect{0, 0, 0, 0};

            return Rect{l, t, static_cast<QC::u32>(r - l), static_cast<QC::u32>(b - t)};
        }

        bool IconButton::hitTest(int x, int y) const
        {
            if (!m_visible)
                return false;

            const Rect abs = absoluteBounds();
            Rect hit = abs;
            if (m_icon && m_icon->isValid())
            {
                hit = contentHitRect(abs);
            }

            return (x >= hit.x && y >= hit.y &&
                    x < hit.x + static_cast<int>(hit.width) &&
                    y < hit.y + static_cast<int>(hit.height));
        }

        void IconButton::paint(const PaintContext &ctx)
        {
            if (!ctx.styleRenderer || !m_window || !m_visible)
                return;

            const bool hasScaleOverride = (m_textScaleOverride > 0.0f);
            float oldScale = 1.0f;
            if (hasScaleOverride && ctx.painter)
            {
                oldScale = ctx.painter->textScale();
                ctx.painter->setTextScale(m_textScaleOverride);
            }

            ButtonPaintArgs args{};
            args.bounds = absoluteBounds();
            args.icon = m_icon;
            args.text = nullptr; // Icon-only; tooltip is drawn separately.
            args.role = m_role;
            args.defaultButton = m_focused;
            args.borderless = true;

            if (!m_enabled)
                args.state = ButtonPaintArgs::State::Disabled;
            else if (m_pressed)
                args.state = ButtonPaintArgs::State::Pressed;
            else if (m_hovered)
                args.state = ButtonPaintArgs::State::Hovered;
            else
                args.state = ButtonPaintArgs::State::Normal;

            ctx.styleRenderer->drawButton(args);

            // Tooltip-on-hover
            if (m_hovered && m_tooltip[0] && ctx.painter)
            {
                const QC::Size textSize = ctx.painter->measureText(m_tooltip);
                const QC::i32 tipW = static_cast<QC::i32>(textSize.width) + 16;
                const QC::i32 tipH = static_cast<QC::i32>(textSize.height) + 12;

                const Rect abs = args.bounds;
                const Rect hit = (m_icon && m_icon->isValid()) ? contentHitRect(abs) : abs;

                QC::i32 tipX = hit.x + static_cast<QC::i32>(hit.width) + 6;
                QC::i32 tipY = hit.y + (static_cast<QC::i32>(hit.height) - tipH) / 2;

                // Clamp within window bounds (desktop is typically 0,0).
                Rect winB = m_window ? m_window->bounds() : Rect{0, 0, 0, 0};
                const QC::i32 winRight = winB.x + static_cast<QC::i32>(winB.width);
                const QC::i32 winBottom = winB.y + static_cast<QC::i32>(winB.height);

                if (winB.width > 0)
                {
                    if (tipX + tipW > winRight)
                        tipX = hit.x - 6 - tipW;
                    tipX = imax(tipX, winB.x);
                    tipX = imin(tipX, winRight - tipW);
                }
                if (winB.height > 0)
                {
                    tipY = imax(tipY, winB.y);
                    tipY = imin(tipY, winBottom - tipH);
                }

                ButtonPaintArgs tipArgs{};
                tipArgs.bounds = Rect{tipX, tipY, static_cast<QC::u32>(tipW), static_cast<QC::u32>(tipH)};
                tipArgs.text = m_tooltip;
                tipArgs.icon = nullptr;
                tipArgs.role = m_role;
                tipArgs.defaultButton = false;
                tipArgs.borderless = true;
                tipArgs.state = ButtonPaintArgs::State::Hovered;

                ctx.styleRenderer->drawButton(tipArgs);
            }

            if (hasScaleOverride && ctx.painter)
            {
                ctx.painter->setTextScale(oldScale);
            }
        }

        bool IconButton::onMouseMove(int x, int y, int dx, int dy)
        {
            (void)dx;
            (void)dy;

            if (!m_enabled)
                return false;

            const bool inside = hitTest(x, y);

            if (inside && !m_hovered)
            {
                m_hovered = true;
                invalidate();
                return true;
            }
            if (!inside && m_hovered)
            {
                m_hovered = false;
                invalidate();
                return true;
            }

            return inside;
        }

        bool IconButton::onMouseDown(int x, int y, QK::Event::MouseButton button)
        {
            if (!m_enabled || button != QK::Event::MouseButton::Left)
                return false;

            if (hitTest(x, y))
            {
                m_pressed = true;
                m_pressX = x;
                m_pressY = y;
                m_hasPressPos = true;
                invalidate();
                return true;
            }

            m_hasPressPos = false;
            return false;
        }

        bool IconButton::onMouseUp(int x, int y, QK::Event::MouseButton button)
        {
            if (!m_enabled || button != QK::Event::MouseButton::Left)
                return false;

            const bool inside = hitTest(x, y);

            static constexpr int kClickSlop = 16;
            bool withinSlop = false;
            if (m_hasPressPos)
            {
                const int dx = x - m_pressX;
                const int dy = y - m_pressY;
                const int adx = (dx < 0) ? -dx : dx;
                const int ady = (dy < 0) ? -dy : dy;
                withinSlop = (adx <= kClickSlop) && (ady <= kClickSlop);
            }

            if (m_pressed)
            {
                m_pressed = false;
                m_hasPressPos = false;
                invalidate();

                if ((inside || withinSlop) && m_clickHandler)
                {
                    m_clickHandler(this, m_clickUserData);
                }

                return true;
            }

            return false;
        }

    } // namespace Controls
} // namespace QW
