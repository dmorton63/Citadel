#pragma once

// ScrollPanel - panel with vertical scrolling for child content
// Namespace: QW::Controls

#include "QWControls/Containers/Panel.h"

namespace QW
{
    namespace Controls
    {
        class ScrollPanel;

        using ScrollPanelScrollChangeHandler = void (*)(ScrollPanel *panel, void *userData);

        class ScrollPanel : public Panel
        {
        public:
            ScrollPanel();
            ScrollPanel(Window *window, Rect bounds);
            ~ScrollPanel() override = default;

            // Scrolling
            QC::i32 scrollOffsetY() const { return m_scrollOffsetY; }
            void setScrollOffsetY(QC::i32 y);

            QC::u32 contentHeight() const { return m_contentHeight; }
            void setContentHeight(QC::u32 height);

            void setScrollChangeHandler(ScrollPanelScrollChangeHandler handler, void *userData);

            // Container overrides
            void paintChildren(const PaintContext &context) override;

            // Event overrides (translate viewport coords -> content coords)
            bool onMouseMove(QC::i32 x, QC::i32 y, QC::i32 deltaX, QC::i32 deltaY) override;
            bool onMouseDown(QC::i32 x, QC::i32 y, QK::Event::MouseButton button) override;
            bool onMouseUp(QC::i32 x, QC::i32 y, QK::Event::MouseButton button) override;
            bool onMouseScroll(QC::i32 delta) override;

        private:
            QC::i32 clampScrollY(QC::i32 y) const;
            IControl *childAtPointScrolled(QC::i32 x, QC::i32 y);

            QC::i32 m_scrollOffsetY = 0;
            QC::u32 m_contentHeight = 0;

            ScrollPanelScrollChangeHandler m_scrollHandler = nullptr;
            void *m_scrollUserData = nullptr;

            static constexpr QC::i32 kScrollStepPx = 24;
        };

    } // namespace Controls
} // namespace QW
