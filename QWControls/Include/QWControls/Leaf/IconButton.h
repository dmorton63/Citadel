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
        class IconButton;

        using IconButtonClickHandler = void (*)(IconButton *button, void *userData);

        class IconButton : public ControlBase
        {
        public:
            IconButton();
            IconButton(Window *window, Rect bounds);
            virtual ~IconButton() = default;

            // Content
            const QG::ImageSurface *icon() const { return m_icon; }
            void setIcon(const QG::ImageSurface *icon);

            const char *tooltipText() const { return m_tooltip; }
            void setTooltipText(const char *text);

            // Behavior
            void setClickHandler(IconButtonClickHandler handler, void *userData);

            ButtonRole role() const { return m_role; }
            void setRole(ButtonRole role);

            // Optional per-control text scale override (<= 0 disables override)
            float textScaleOverride() const { return m_textScaleOverride; }
            void setTextScaleOverride(float scale);

            // Rendering
            void paint(const PaintContext &ctx) override;

            // Geometry
            bool hitTest(int x, int y) const override;

            // Event handling
            bool onMouseMove(int x, int y, int dx, int dy) override;
            bool onMouseDown(int x, int y, QK::Event::MouseButton button) override;
            bool onMouseUp(int x, int y, QK::Event::MouseButton button) override;

        private:
            Rect contentHitRect(const Rect &absBounds) const;

            char m_tooltip[256];
            const QG::ImageSurface *m_icon = nullptr;
            float m_textScaleOverride = 0.0f;

            bool m_pressed = false;
            bool m_hovered = false;
            int m_pressX = 0;
            int m_pressY = 0;
            bool m_hasPressPos = false;

            ButtonRole m_role = ButtonRole::Default;

            IconButtonClickHandler m_clickHandler = nullptr;
            void *m_clickUserData = nullptr;
        };

    } // namespace Controls
} // namespace QW
