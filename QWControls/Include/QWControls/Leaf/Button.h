#pragma once

#include "QWControls/Base/ControlBase.h"
#include "QKEventTypes.h"
#include "QWStyleTypes.h"

namespace QG
{
    struct ImageSurface;
}

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

            const QG::ImageSurface *icon() const { return m_icon; }
            void setIcon(const QG::ImageSurface *icon);

            bool borderless() const { return m_borderless; }
            void setBorderless(bool borderless);

            // Behavior
            void setClickHandler(ButtonClickHandler handler, void *userData);

            ButtonRole role() const { return m_role; }
            void setRole(ButtonRole role);

            // Optional per-button text scale override (<= 0 disables override)
            float textScaleOverride() const { return m_textScaleOverride; }
            void setTextScaleOverride(float scale)
            {
                if (m_textScaleOverride == scale)
                    return;
                m_textScaleOverride = scale;
                invalidate();
            }

            // Rendering (delegated to StyleRenderer)
            void paint(const PaintContext &ctx) override;

            // Event handling
            bool onMouseMove(int x, int y, int dx, int dy) override;
            bool onMouseDown(int x, int y, QK::Event::MouseButton button) override;
            bool onMouseUp(int x, int y, QK::Event::MouseButton button) override;

        private:
            char m_text[256];
            const QG::ImageSurface *m_icon = nullptr;
            bool m_borderless = false;
            float m_textScaleOverride = 0.0f;
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