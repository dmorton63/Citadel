#pragma once

#include "QWControls/Base/ControlBase.h"
#include "QKEventTypes.h"
#include "QWStyleTypes.h"

namespace QW
{
    namespace Controls
    {

        class Button;

        using ButtonClickHandler = void (*)(Button *button, void *userData);

        class Button : public ControlBase
        {
        public:
            Button();
            Button(Window *window, const char *text, Rect bounds);
            virtual ~Button() = default;

            // Content
            const char *text() const { return m_text; }
            void setText(const char *text);

            // Behavior
            void setClickHandler(ButtonClickHandler handler, void *userData);

            ButtonRole role() const { return m_role; }
            void setRole(ButtonRole role);

            // Rendering (delegated to StyleRenderer)
            void paint(const PaintContext &ctx) override;

            // Event handling
            bool onMouseMove(int x, int y, int dx, int dy) override;
            bool onMouseDown(int x, int y, QK::Event::MouseButton button) override;
            bool onMouseUp(int x, int y, QK::Event::MouseButton button) override;

        private:
            char m_text[256];
            bool m_pressed = false;
            bool m_hovered = false;
            int m_pressX = 0;
            int m_pressY = 0;
            bool m_hasPressPos = false;
            QC::u64 m_hoverEnterMs = 0;
            QC::u64 m_hoverEnterTs = 0;
            bool m_hasHoverEnter = false;
            QC::u64 m_pressDownMs = 0;
            QC::u64 m_pressDownTs = 0;
            bool m_hasPressDown = false;
            ButtonRole m_role = ButtonRole::Default;

            ButtonClickHandler m_clickHandler = nullptr;
            void *m_clickUserData = nullptr;
        };

    } // namespace Controls
} // namespace QW