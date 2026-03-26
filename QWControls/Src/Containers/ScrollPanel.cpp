// ScrollPanel implementation
// Namespace: QW::Controls

#include "QWControls/Containers/ScrollPanel.h"

#include "QGPainter.h"

namespace QW
{
    namespace Controls
    {

        ScrollPanel::ScrollPanel()
            : Panel()
        {
        }

        ScrollPanel::ScrollPanel(Window *window, Rect bounds)
            : Panel(window, bounds)
        {
        }

        void ScrollPanel::setScrollChangeHandler(ScrollPanelScrollChangeHandler handler, void *userData)
        {
            m_scrollHandler = handler;
            m_scrollUserData = userData;
        }

        QC::i32 ScrollPanel::clampScrollY(QC::i32 y) const
        {
            // Viewport height is our client area height (approx). Content height is provided by the owner.
            const QC::i32 viewportH = static_cast<QC::i32>(clientRect().height);
            const QC::i32 contentH = static_cast<QC::i32>(m_contentHeight);

            QC::i32 maxY = 0;
            if (contentH > viewportH)
                maxY = contentH - viewportH;

            if (y < 0)
                y = 0;
            if (y > maxY)
                y = maxY;
            return y;
        }

        void ScrollPanel::setScrollOffsetY(QC::i32 y)
        {
            const QC::i32 clamped = clampScrollY(y);
            if (m_scrollOffsetY == clamped)
                return;
            m_scrollOffsetY = clamped;
            invalidate();

            if (m_scrollHandler)
                m_scrollHandler(this, m_scrollUserData);
        }

        void ScrollPanel::setContentHeight(QC::u32 height)
        {
            if (m_contentHeight == height)
                return;
            m_contentHeight = height;
            const QC::i32 before = m_scrollOffsetY;
            m_scrollOffsetY = clampScrollY(m_scrollOffsetY);
            invalidate();

            if (m_scrollHandler && m_scrollOffsetY != before)
                m_scrollHandler(this, m_scrollUserData);
        }

        IControl *ScrollPanel::childAtPointScrolled(QC::i32 x, QC::i32 y)
        {
            // Translate viewport y into content y.
            const QC::i32 cy = y + m_scrollOffsetY;

            for (QC::isize i = static_cast<QC::isize>(m_children.size()) - 1; i >= 0; --i)
            {
                IControl *child = m_children[static_cast<QC::usize>(i)];
                if (child->isVisible() && child->isEnabled() && child->hitTest(x, cy))
                {
                    return child;
                }
            }
            return nullptr;
        }

        void ScrollPanel::paintChildren(const PaintContext &context)
        {
            if (!context.painter)
            {
                Panel::paintChildren(context);
                return;
            }

            // Clip children to our viewport.
            const QC::Rect viewport = absoluteBounds();
            const QC::Rect oldClip = context.painter->clipRect();
            const QC::Rect clip = oldClip.intersection(viewport);
            context.painter->setClipRect(clip);

            // Translate painter so children draw as if their Y is shifted by -scroll.
            const QC::Point oldOrigin = context.painter->origin();
            context.painter->translate(0, -m_scrollOffsetY);

            for (QC::usize i = 0; i < m_children.size(); ++i)
            {
                if (m_children[i]->isVisible())
                {
                    m_children[i]->paint(context);
                }
            }

            // Restore painter state.
            context.painter->setOrigin(oldOrigin);
            context.painter->setClipRect(oldClip);
        }

        bool ScrollPanel::onMouseMove(QC::i32 x, QC::i32 y, QC::i32 deltaX, QC::i32 deltaY)
        {
            IControl *child = childAtPointScrolled(x, y);
            bool handled = false;

            if (child != m_hoveredChild)
            {
                if (m_hoveredChild)
                {
                    handled = m_hoveredChild->onMouseMove(x, y + m_scrollOffsetY, deltaX, deltaY) || handled;
                }

                m_hoveredChild = child;
            }

            if (child)
            {
                handled = child->onMouseMove(x, y + m_scrollOffsetY, deltaX, deltaY) || handled;
            }

            if (m_capturedChild && m_capturedChild != child)
            {
                handled = m_capturedChild->onMouseMove(x, y + m_scrollOffsetY, deltaX, deltaY) || handled;
            }

            return handled;
        }

        bool ScrollPanel::onMouseDown(QC::i32 x, QC::i32 y, QK::Event::MouseButton button)
        {
            IControl *child = childAtPointScrolled(x, y);
            if (!child)
                return false;

            const bool handled = child->onMouseDown(x, y + m_scrollOffsetY, button);
            if (handled)
            {
                m_capturedChild = child;
                setFocusedChild(child);
            }
            return handled;
        }

        bool ScrollPanel::onMouseUp(QC::i32 x, QC::i32 y, QK::Event::MouseButton button)
        {
            IControl *child = childAtPointScrolled(x, y);

            bool handled = false;
            if (child)
            {
                handled = child->onMouseUp(x, y + m_scrollOffsetY, button) || handled;
            }

            if (m_capturedChild && m_capturedChild != child)
            {
                handled = m_capturedChild->onMouseUp(x, y + m_scrollOffsetY, button) || handled;
            }

            m_capturedChild = nullptr;
            return handled;
        }

        bool ScrollPanel::onMouseScroll(QC::i32 delta)
        {
            // Positive delta scrolls up in existing controls; keep that convention.
            if (delta == 0)
                return false;

            const QC::i32 step = (delta > 0) ? -kScrollStepPx : kScrollStepPx;
            const QC::i32 before = m_scrollOffsetY;
            m_scrollOffsetY = clampScrollY(m_scrollOffsetY + step);
            if (m_scrollOffsetY != before)
            {
                invalidate();

                if (m_scrollHandler)
                    m_scrollHandler(this, m_scrollUserData);
                return true;
            }
            return false;
        }

    } // namespace Controls
} // namespace QW
