// QWControls Button - Button control implementation
// Namespace: QW::Controls

#include "QWControls/Leaf/Button.h"
#include "QWWindow.h"
#include "QWWindowManager.h"
#include "QCLogger.h"
#include <cstring>

namespace QW
{
    namespace Controls
    {

        Button::Button()
            : ControlBase()
        {
            m_text[0] = '\0';
        }

        Button::Button(Window *window, const char *text, Rect bounds)
            : ControlBase(window, bounds)
        {
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

        void Button::paint(const PaintContext &ctx)
        {
            if (!ctx.styleRenderer || !m_window || !m_visible)
                return;

            ButtonPaintArgs args{};
            args.bounds = absoluteBounds();
            args.text = m_text;
            args.role = m_role;
            args.defaultButton = m_focused;

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
        }

        bool Button::onMouseMove(int x, int y, int dx, int dy)
        {
            if (!m_enabled)
                return false;

            bool inside = hitTest(x, y);

            if (inside && !m_hovered)
            {
                m_hovered = true;
                {
                    auto &wm = QW::WindowManager::instance();
                    m_hoverEnterMs = wm.lastInputMs();
                    m_hoverEnterTs = wm.lastInputTimestamp();
                    m_hasHoverEnter = true;

                    const Rect abs = absoluteBounds();
                    const char *title = (m_window && m_window->title()) ? m_window->title() : "";
                    QC_LOG_INFO("QWButton", "Hover enter '%s' window='%s' bounds=%d,%d %ux%u ms=%llu ts=%llu",
                                m_text, title, abs.x, abs.y, abs.width, abs.height,
                                static_cast<unsigned long long>(m_hoverEnterMs),
                                static_cast<unsigned long long>(m_hoverEnterTs));
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

            bool inside = hitTest(x, y);

            // Click slop: tolerate small motion between down/up.
            // This avoids needing repeated clicks when the pointer jitters a bit.
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
                    auto &wm = QW::WindowManager::instance();
                    const QC::u64 nowMs = wm.lastInputMs();
                    const QC::u64 nowTs = wm.lastInputTimestamp();
                    const QC::u64 dtHoverMs = (m_hasHoverEnter && nowMs >= m_hoverEnterMs) ? (nowMs - m_hoverEnterMs) : 0;
                    const QC::u64 dtPressMs = (m_hasPressDown && nowMs >= m_pressDownMs) ? (nowMs - m_pressDownMs) : 0;

                    const Rect abs = absoluteBounds();
                    const char *title = (m_window && m_window->title()) ? m_window->title() : "";
                    QC_LOG_INFO("QWButton", "Click '%s' window='%s' bounds=%d,%d %ux%u ms=%llu ts=%llu dtHover=%llums dtPress=%llums",
                                m_text, title, abs.x, abs.y, abs.width, abs.height,
                                static_cast<unsigned long long>(nowMs),
                                static_cast<unsigned long long>(nowTs),
                                static_cast<unsigned long long>(dtHoverMs),
                                static_cast<unsigned long long>(dtPressMs));
                    m_clickHandler(this, m_clickUserData);
                }
                else if (!inside && !withinSlop)
                {
                    const Rect abs = absoluteBounds();
                    const char *title = (m_window && m_window->title()) ? m_window->title() : "";
                    QC_LOG_INFO("QWButton", "Release missed '%s' window='%s' up=(%d,%d) down=(%d,%d) bounds=%d,%d %ux%u",
                                m_text, title, x, y, m_pressX, m_pressY, abs.x, abs.y, abs.width, abs.height);
                }

                return true;
            }

            return false;
        }

    } // namespace Controls
} // namespace QW