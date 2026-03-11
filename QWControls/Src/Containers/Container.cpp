// Container implementation
// Namespace: QW::Controls

#include "QWControls/Containers/Container.h"
#include "QWControls/Containers/Panel.h"
#include "QWWindow.h"

namespace QW
{
    namespace Controls
    {
        Container::Container()
            : ControlBase(),
              m_focusedChild(nullptr),
              m_hoveredChild(nullptr),
              m_capturedChild(nullptr)
        {
        }

        Container::Container(Window *window, Rect bounds)
            : ControlBase(window, bounds),
              m_focusedChild(nullptr),
              m_hoveredChild(nullptr),
              m_capturedChild(nullptr)
        {
        }

        Container::~Container()
        {
            m_children.clear();
        }

        Panel *Container::asPanel()
        {
            return nullptr;
        }

        const Panel *Container::asPanel() const
        {
            return nullptr;
        }

        void Container::addChild(IControl *child)
        {
            if (!child)
            {
                return;
            }

            Panel *oldParent = child->parent();
            if (oldParent && oldParent != this->asPanel())
            {
                oldParent->removeChild(child);
            }

            child->setParent(this->asPanel());
            child->setWindow(m_window);
            m_children.push_back(child);
        }

        void Container::removeChild(IControl *child)
        {
            if (!child)
            {
                return;
            }

            for (QC::usize i = 0; i < m_children.size(); ++i)
            {
                if (m_children[i] == child)
                {
                    if (m_focusedChild == child)
                    {
                        m_focusedChild = nullptr;
                    }
                    if (m_hoveredChild == child)
                    {
                        m_hoveredChild = nullptr;
                    }
                    if (m_capturedChild == child)
                    {
                        m_capturedChild = nullptr;
                    }

                    child->setParent(nullptr);

                    for (QC::usize j = i; j < m_children.size() - 1; ++j)
                    {
                        m_children[j] = m_children[j + 1];
                    }
                    m_children.pop_back();
                    return;
                }
            }
        }

        void Container::removeChildAt(QC::usize index)
        {
            if (index >= m_children.size())
            {
                return;
            }

            IControl *child = m_children[index];
            removeChild(child);
        }

        void Container::clearChildren()
        {
            for (QC::usize i = 0; i < m_children.size(); ++i)
            {
                m_children[i]->setParent(nullptr);
            }
            m_children.clear();
            m_focusedChild = nullptr;
            m_hoveredChild = nullptr;
            m_capturedChild = nullptr;
        }

        IControl *Container::childAt(QC::usize index)
        {
            if (index >= m_children.size())
            {
                return nullptr;
            }
            return m_children[index];
        }

        const IControl *Container::childAt(QC::usize index) const
        {
            if (index >= m_children.size())
            {
                return nullptr;
            }
            return m_children[index];
        }

        IControl *Container::findChild(ControlId id)
        {
            for (QC::usize i = 0; i < m_children.size(); ++i)
            {
                if (m_children[i]->id() == id)
                {
                    return m_children[i];
                }

                if (m_children[i]->isContainer())
                {
                    Panel *childPanel = m_children[i]->asPanel();
                    if (childPanel)
                    {
                        IControl *found = childPanel->findChild(id);
                        if (found)
                        {
                            return found;
                        }
                    }
                }
            }
            return nullptr;
        }

        IControl *Container::childAtPoint(QC::i32 x, QC::i32 y)
        {
            for (QC::isize i = static_cast<QC::isize>(m_children.size()) - 1; i >= 0; --i)
            {
                IControl *child = m_children[static_cast<QC::usize>(i)];
                if (child->isVisible() && child->isEnabled() && child->hitTest(x, y))
                {
                    return child;
                }
            }
            return nullptr;
        }

        void Container::paint(const PaintContext &context)
        {
            if (!m_visible || !m_window)
            {
                return;
            }

            paintChildren(context);
        }

        void Container::paintChildren(const PaintContext &context)
        {
            for (QC::usize i = 0; i < m_children.size(); ++i)
            {
                if (m_children[i]->isVisible())
                {
                    m_children[i]->paint(context);
                }
            }
        }

        bool Container::onEvent(const QK::Event::Event &event)
        {
            if (!m_enabled || !m_visible)
            {
                return false;
            }

            return ControlBase::onEvent(event);
        }

        bool Container::onMouseMove(QC::i32 x, QC::i32 y, QC::i32 deltaX, QC::i32 deltaY)
        {
            IControl *child = childAtPoint(x, y);
            bool handled = false;

            if (child != m_hoveredChild)
            {
                if (m_hoveredChild)
                {
                    handled = m_hoveredChild->onMouseMove(x, y, deltaX, deltaY) || handled;
                }

                m_hoveredChild = child;
            }

            if (child)
            {
                handled = child->onMouseMove(x, y, deltaX, deltaY) || handled;
            }

            // Even though events follow the cursor, a captured child (e.g. dragging a scrollbar)
            // still needs move notifications to update/release internal state.
            if (m_capturedChild && m_capturedChild != child)
            {
                handled = m_capturedChild->onMouseMove(x, y, deltaX, deltaY) || handled;
            }

            return handled;
        }

        bool Container::onMouseDown(QC::i32 x, QC::i32 y, QK::Event::MouseButton button)
        {
            // Mouse down must always be delivered to the topmost visible+enabled control
            // under the cursor. Do not "click-through" to underlying controls.
            IControl *child = childAtPoint(x, y);
            if (!child)
                return false;

            const bool handled = child->onMouseDown(x, y, button);
            if (handled)
            {
                m_capturedChild = child;
                setFocusedChild(child);
            }
            return handled;
        }

        bool Container::onMouseUp(QC::i32 x, QC::i32 y, QK::Event::MouseButton button)
        {
            // Mouse up follows the cursor, but we also notify the captured child (if any)
            // so pressed/dragging state can terminate even when the cursor moved away.
            IControl *child = childAtPoint(x, y);

            bool handled = false;
            if (child)
            {
                handled = child->onMouseUp(x, y, button) || handled;
            }

            if (m_capturedChild && m_capturedChild != child)
            {
                handled = m_capturedChild->onMouseUp(x, y, button) || handled;
            }

            m_capturedChild = nullptr;
            return handled;
        }

        bool Container::onMouseScroll(QC::i32 delta)
        {
            if (m_hoveredChild)
            {
                return m_hoveredChild->onMouseScroll(delta);
            }
            if (m_focusedChild)
            {
                return m_focusedChild->onMouseScroll(delta);
            }
            return false;
        }

        bool Container::onKeyDown(QC::u8 scancode, QC::u8 keycode, char character, QK::Event::Modifiers mods)
        {
            if (m_focusedChild)
            {
                return m_focusedChild->onKeyDown(scancode, keycode, character, mods);
            }
            return false;
        }

        bool Container::onKeyUp(QC::u8 scancode, QC::u8 keycode, QK::Event::Modifiers mods)
        {
            if (m_focusedChild)
            {
                return m_focusedChild->onKeyUp(scancode, keycode, mods);
            }
            return false;
        }

        void Container::setFocusedChild(IControl *child)
        {
            if (m_focusedChild == child)
            {
                return;
            }

            if (m_focusedChild)
            {
                m_focusedChild->setFocused(false);
            }

            m_focusedChild = child;

            if (m_focusedChild)
            {
                m_focusedChild->setFocused(true);
            }
        }

        void Container::focusNext()
        {
            if (m_children.empty())
            {
                return;
            }

            QC::usize startIndex = 0;

            if (m_focusedChild)
            {
                for (QC::usize i = 0; i < m_children.size(); ++i)
                {
                    if (m_children[i] == m_focusedChild)
                    {
                        startIndex = i + 1;
                        break;
                    }
                }
            }

            for (QC::usize i = 0; i < m_children.size(); ++i)
            {
                QC::usize index = (startIndex + i) % m_children.size();
                IControl *child = m_children[index];
                if (child->isEnabled() && child->isVisible())
                {
                    setFocusedChild(child);
                    return;
                }
            }
        }

        void Container::focusPrevious()
        {
            if (m_children.empty())
            {
                return;
            }

            QC::isize startIndex = static_cast<QC::isize>(m_children.size()) - 1;

            if (m_focusedChild)
            {
                for (QC::usize i = 0; i < m_children.size(); ++i)
                {
                    if (m_children[i] == m_focusedChild)
                    {
                        startIndex = static_cast<QC::isize>(i) - 1;
                        break;
                    }
                }
            }

            for (QC::usize i = 0; i < m_children.size(); ++i)
            {
                QC::isize index = startIndex - static_cast<QC::isize>(i);
                if (index < 0)
                {
                    index += static_cast<QC::isize>(m_children.size());
                }
                IControl *child = m_children[static_cast<QC::usize>(index)];
                if (child->isEnabled() && child->isVisible())
                {
                    setFocusedChild(child);
                    return;
                }
            }
        }

        Point Container::windowToLocal(QC::i32 x, QC::i32 y) const
        {
            Rect abs = absoluteBounds();
            return {x - abs.x, y - abs.y};
        }

        Point Container::localToWindow(QC::i32 x, QC::i32 y) const
        {
            Rect abs = absoluteBounds();
            return {x + abs.x, y + abs.y};
        }
    }
} // namespace QW
