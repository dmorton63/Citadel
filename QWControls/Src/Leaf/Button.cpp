// QWControls Button - Button control implementation
// Namespace: QW::Controls

#include "QWControls/Leaf/Button.h"
#include "QWWindow.h"
#include "QWWindowManager.h"
#include "QCLogger.h"
#include "QGPainter.h"
#include "QG/Image.h"
#include <cstring>

namespace QW
{
    namespace Controls
    {
        namespace
        {
            static constexpr bool kButtonTrace = false;

            static constexpr bool kMouseInfoLogsEnabled = false;

            inline QC::i32 imax(QC::i32 a, QC::i32 b) { return a > b ? a : b; }
            inline QC::i32 imin(QC::i32 a, QC::i32 b) { return a < b ? a : b; }

            static ButtonVariant variantFromBorderless(bool borderless)
            {
                return borderless ? ButtonVariant::Borderless : ButtonVariant::Standard;
            }

            static ButtonContentMode resolveContentMode(ButtonContentMode mode,
                                                        const char *text,
                                                        const QG::ImageSurface *icon)
            {
                if (mode != ButtonContentMode::Auto)
                    return mode;

                const bool hasText = (text && text[0] != '\0');
                const bool hasIcon = (icon && icon->isValid());

                if (hasText && hasIcon)
                    return ButtonContentMode::TextAndIcon;
                if (hasIcon)
                    return ButtonContentMode::Icon;
                return ButtonContentMode::Text;
            }

            static bool hitTestWithPadding(const Rect &r, int x, int y, int pad)
            {
                const int l = r.x - pad;
                const int t = r.y - pad;
                const int rr = r.x + static_cast<int>(r.width) + pad;
                const int bb = r.y + static_cast<int>(r.height) + pad;
                return (x >= l && x < rr && y >= t && y < bb);
            }
        }

        Button::Button()
            : ControlBase()
        {
            m_text[0] = '\0';
            m_tooltip[0] = '\0';
        }

        Button::Button(Window *window, const char *text, Rect bounds)
            : ControlBase(window, bounds)
        {
            m_tooltip[0] = '\0';
            setText(text);
        }

        void Button::setText(const char *text)
        {
            if (text)
            {
                std::strncpy(m_text, text, sizeof(m_text) - 1);
                m_text[sizeof(m_text) - 1] = '\0';
            }
            else
            {
                m_text[0] = '\0';
            }
        }

        void Button::setIcon(const QG::ImageSurface *icon)
        {
            if (m_icon == icon)
                return;
            m_icon = icon;
            invalidate();
        }

        void Button::setTooltipText(const char *text)
        {
            if (!text)
            {
                m_tooltip[0] = '\0';
                return;
            }

            std::strncpy(m_tooltip, text, sizeof(m_tooltip) - 1);
            m_tooltip[sizeof(m_tooltip) - 1] = '\0';
        }

        void Button::setContentMode(ButtonContentMode mode)
        {
            if (m_contentMode == mode)
                return;
            m_contentMode = mode;
            invalidate();
        }

        void Button::setVariant(ButtonVariant variant)
        {
            if (m_variant == variant)
                return;

            m_variant = variant;
            m_borderless = (variant == ButtonVariant::Borderless ||
                            variant == ButtonVariant::Icon ||
                            variant == ButtonVariant::Ghost);
            invalidate();
        }

        void Button::setBorderless(bool borderless)
        {
            if (m_borderless == borderless)
                return;
            m_borderless = borderless;
            m_variant = variantFromBorderless(borderless);
            invalidate();
        }

        void Button::setClickHandler(ButtonClickHandler handler, void *userData)
        {
            m_clickHandler = handler;
            m_clickUserData = userData;
        }

        void Button::setRole(ButtonRole role)
        {
            if (m_role == role)
                return;

            m_role = role;
            invalidate();
        }

        Rect Button::contentHitRect(const Rect &absBounds) const
        {
            if (absBounds.width == 0 || absBounds.height == 0)
                return absBounds;

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

        bool Button::hitTest(int x, int y) const
        {
            if (!m_visible)
                return false;

            const Rect abs = absoluteBounds();
            Rect hit = abs;

            const ButtonContentMode effectiveMode = resolveContentMode(m_contentMode, m_text, m_icon);
            const bool useIconHitRect = ((effectiveMode == ButtonContentMode::Icon) ||
                                         (m_variant == ButtonVariant::Icon));
            if (useIconHitRect && m_icon && m_icon->isValid())
            {
                hit = contentHitRect(abs);
            }

            return (x >= hit.x && y >= hit.y &&
                    x < hit.x + static_cast<int>(hit.width) &&
                    y < hit.y + static_cast<int>(hit.height));
        }

        void Button::paint(const PaintContext &ctx)
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
            args.text = m_text;
            args.icon = m_icon;
            args.contentMode = m_contentMode;
            args.variant = m_variant;
            args.role = m_role;
            args.defaultButton = m_focused;
            args.borderless = m_borderless;
            args.cornerRadius = m_cornerRadius;

            // Map internal state to style state
            if (!m_enabled)
                args.state = ButtonPaintArgs::State::Disabled;
            else if (m_pressed)
                args.state = ButtonPaintArgs::State::Pressed;
            else if (m_hovered)
                args.state = ButtonPaintArgs::State::Hovered;
            else
                args.state = ButtonPaintArgs::State::Normal;

            ctx.styleRenderer->drawButton(args);

            if (m_hovered && m_tooltip[0] && ctx.painter)
            {
                const QC::Size textSize = ctx.painter->measureText(m_tooltip);
                const QC::i32 tipW = static_cast<QC::i32>(textSize.width) + 24;
                const QC::i32 tipH = static_cast<QC::i32>(textSize.height) + 12;

                const ButtonContentMode effectiveMode = resolveContentMode(m_contentMode, m_text, m_icon);
                const Rect hit = ((effectiveMode == ButtonContentMode::Icon || m_variant == ButtonVariant::Icon) &&
                                  m_icon && m_icon->isValid())
                                     ? contentHitRect(args.bounds)
                                     : args.bounds;

                QC::i32 tipX = hit.x + static_cast<QC::i32>(hit.width) + 6;
                QC::i32 tipY = hit.y + (static_cast<QC::i32>(hit.height) - tipH) / 2;

                Rect winB = m_window ? m_window->bounds() : Rect{0, 0, 0, 0};
                const QC::i32 winRight = winB.x + static_cast<QC::i32>(winB.width);
                const QC::i32 winBottom = winB.y + static_cast<QC::i32>(winB.height);

                if (winB.width > 0)
                {
                    if (tipX + tipW > winRight)
                        tipX = hit.x - 6 - tipW;
                    const QC::i32 maxX = (winRight > tipW) ? (winRight - tipW) : winB.x;
                    tipX = imax(tipX, winB.x);
                    tipX = imin(tipX, maxX);
                }
                if (winB.height > 0)
                {
                    const QC::i32 maxY = (winBottom > tipH) ? (winBottom - tipH) : winB.y;
                    tipY = imax(tipY, winB.y);
                    tipY = imin(tipY, maxY);
                }

                ButtonPaintArgs tipArgs{};
                tipArgs.bounds = Rect{tipX, tipY, static_cast<QC::u32>(tipW), static_cast<QC::u32>(tipH)};
                tipArgs.text = m_tooltip;
                tipArgs.role = m_role;
                tipArgs.contentMode = ButtonContentMode::Text;
                tipArgs.variant = ButtonVariant::Borderless;
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

        bool Button::onMouseMove(int x, int y, int dx, int dy)
        {
            (void)dx;
            (void)dy;

            if (!m_enabled)
                return false;

            bool inside = hitTest(x, y);
            if (!inside && m_pressed)
            {
                // Keep press state stable during small drag jitter near edges.
                inside = hitTestWithPadding(absoluteBounds(), x, y, 2);
            }

            if (inside && !m_hovered)
            {
                m_hovered = true;
                if (kButtonTrace)
                {
                    auto &wm = QW::WindowManager::instance();
                    m_hoverEnterMs = wm.lastInputMs();
                    m_hoverEnterTs = wm.lastInputTimestamp();
                    m_hasHoverEnter = true;

                    const Rect abs = absoluteBounds();
                    const char *title = (m_window && m_window->title()) ? m_window->title() : "";
                        if (kMouseInfoLogsEnabled)
                        {
                            QC_LOG_INFO("QWButton", "Hover enter '%s' window='%s' bounds=%d,%d %ux%u ms=%llu ts=%llu",
                                        m_text, title, abs.x, abs.y, abs.width, abs.height,
                                        static_cast<unsigned long long>(m_hoverEnterMs),
                                        static_cast<unsigned long long>(m_hoverEnterTs));
                        }
                }
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

        bool Button::onMouseDown(int x, int y, QK::Event::MouseButton button)
        {
            if (!m_enabled || button != QK::Event::MouseButton::Left)
                return false;

            if (hitTest(x, y))
            {
                m_pressed = true;
                m_pressX = x;
                m_pressY = y;
                m_hasPressPos = true;
                if (kButtonTrace)
                {
                    auto &wm = QW::WindowManager::instance();
                    m_pressDownMs = wm.lastInputMs();
                    m_pressDownTs = wm.lastInputTimestamp();
                    m_hasPressDown = true;
                }
                invalidate();
                return true;
            }

            m_hasPressPos = false;

            return false;
        }

        bool Button::onMouseUp(int x, int y, QK::Event::MouseButton button)
        {
            if (!m_enabled || button != QK::Event::MouseButton::Left)
                return false;

            static constexpr int kReleasePad = 1;
            static constexpr int kClickSlop = 4;

            bool inside = hitTest(x, y);
            if (!inside)
            {
                inside = hitTestWithPadding(absoluteBounds(), x, y, kReleasePad);
            }

            // Click slop: tolerate small motion between down/up.
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

                const bool nearBounds = hitTestWithPadding(absoluteBounds(), x, y, kReleasePad);
                if ((inside || (withinSlop && nearBounds)) && m_clickHandler)
                {
                    if (kButtonTrace)
                    {
                        auto &wm = QW::WindowManager::instance();
                        const QC::u64 nowMs = wm.lastInputMs();
                        const QC::u64 nowTs = wm.lastInputTimestamp();
                        const QC::u64 dtHoverMs = (m_hasHoverEnter && nowMs >= m_hoverEnterMs) ? (nowMs - m_hoverEnterMs) : 0;
                        const QC::u64 dtPressMs = (m_hasPressDown && nowMs >= m_pressDownMs) ? (nowMs - m_pressDownMs) : 0;

                        const Rect abs = absoluteBounds();
                        const char *title = (m_window && m_window->title()) ? m_window->title() : "";
                            if (kMouseInfoLogsEnabled)
                            {
                                QC_LOG_INFO("QWButton", "Click '%s' window='%s' bounds=%d,%d %ux%u ms=%llu ts=%llu dtHover=%llums dtPress=%llums",
                                            m_text, title, abs.x, abs.y, abs.width, abs.height,
                                            static_cast<unsigned long long>(nowMs),
                                            static_cast<unsigned long long>(nowTs),
                                            static_cast<unsigned long long>(dtHoverMs),
                                            static_cast<unsigned long long>(dtPressMs));
                            }
                    }
                    m_clickHandler(this, m_clickUserData);
                }
                else if (!inside && !withinSlop)
                {
                    if (kButtonTrace)
                    {
                        const Rect abs = absoluteBounds();
                        const char *title = (m_window && m_window->title()) ? m_window->title() : "";
                            if (kMouseInfoLogsEnabled)
                            {
                                QC_LOG_INFO("QWButton", "Release missed '%s' window='%s' up=(%d,%d) down=(%d,%d) bounds=%d,%d %ux%u",
                                            m_text, title, x, y, m_pressX, m_pressY, abs.x, abs.y, abs.width, abs.height);
                            }
                    }
                }

                return true;
            }

            return false;
        }

    } // namespace Controls
} // namespace QW
