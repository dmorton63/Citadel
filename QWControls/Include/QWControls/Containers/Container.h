#pragma once

// Container - child management and event routing
// Namespace: QW::Controls

#include "QCVector.h"
#include "QWControls/Base/ControlBase.h"

namespace QW
{
    class Window;

    namespace Controls
    {
        class Panel;

        class Container : public ControlBase
        {
        public:
            Container();
            Container(Window *window, Rect bounds);
            virtual ~Container();

            bool isContainer() const override { return true; }
            Panel *asPanel() override;
            const Panel *asPanel() const override;

            void addChild(IControl *child);
            void removeChild(IControl *child);
            void removeChildAt(QC::usize index);
            void clearChildren();

            QC::usize childCount() const { return m_children.size(); }
            IControl *childAt(QC::usize index);
            const IControl *childAt(QC::usize index) const;

            const QC::Vector<IControl *> &children() const { return m_children; }
            IControl *findChild(ControlId id);
            IControl *childAtPoint(QC::i32 x, QC::i32 y);

            void paint(const PaintContext &context) override;
            virtual void paintChildren(const PaintContext &context);

            bool onEvent(const QK::Event::Event &event) override;
            bool onMouseMove(QC::i32 x, QC::i32 y, QC::i32 deltaX, QC::i32 deltaY) override;
            bool onMouseDown(QC::i32 x, QC::i32 y, QK::Event::MouseButton button) override;
            bool onMouseUp(QC::i32 x, QC::i32 y, QK::Event::MouseButton button) override;
            bool onMouseScroll(QC::i32 delta) override;
            bool onKeyDown(QC::u8 scancode, QC::u8 keycode, char character, QK::Event::Modifiers mods) override;
            bool onKeyUp(QC::u8 scancode, QC::u8 keycode, QK::Event::Modifiers mods) override;

            IControl *focusedChild() const { return m_focusedChild; }
            void setFocusedChild(IControl *child);
            void focusNext();
            void focusPrevious();

            Point windowToLocal(QC::i32 x, QC::i32 y) const;
            Point localToWindow(QC::i32 x, QC::i32 y) const;

        protected:
            QC::Vector<IControl *> m_children;
            IControl *m_focusedChild;
            IControl *m_hoveredChild;
            IControl *m_capturedChild;
        };
    }
} // namespace QW
