#pragma once

#include "QWInterfaces/IControl.h"

namespace QW
{
    class Window;

    namespace Controls
    {
        class Panel;

        class ControlBase : public IControl
        {
        public:
            ControlBase();
            ControlBase(Window *window, Rect bounds);
            virtual ~ControlBase() = default;

            // Identity
            ControlId id() const override { return m_id; }
            void setId(ControlId id) override { m_id = id; }

            // Hierarchy
            Panel *parent() const override { return m_parent; }
            void setParent(Panel *parent) override;

            Window *window() const override { return m_window; }
            void setWindow(Window *window) override { m_window = window; }

            // Geometry
            Rect bounds() const override { return m_bounds; }
            void setBounds(const Rect &bounds) override;

            Rect absoluteBounds() const override;
            bool hitTest(int x, int y) const override;

            // State
            bool isEnabled() const override { return m_enabled; }
            void setEnabled(bool enabled) override;

            bool isVisible() const override { return m_visible; }
            void setVisible(bool visible) override { m_visible = visible; }

            bool isFocused() const override { return m_focused; }
            void setFocused(bool focused) override;

            ControlState state() const override { return m_state; }

            // Rendering
            void invalidate() override;

            // Event routing
            bool onEvent(const QK::Event::Event &event) override;

            // Default handlers
            bool onMouseMove(int, int, int, int) override { return false; }
            bool onMouseDown(int, int, QK::Event::MouseButton) override { return false; }
            bool onMouseUp(int, int, QK::Event::MouseButton) override { return false; }
            bool onMouseScroll(int) override { return false; }
            bool onKeyDown(uint8_t, uint8_t, char, QK::Event::Modifiers) override { return false; }
            bool onKeyUp(uint8_t, uint8_t, QK::Event::Modifiers) override { return false; }
            void onFocus() override {}
            void onBlur() override {}

        protected:
            void setState(ControlState state) { m_state = state; }

            // Dirty flag + cached absolute bounds
            void markAbsoluteBoundsDirty();
            void recomputeAbsoluteBounds() const;

            ControlId m_id;
            Panel *m_parent;
            Window *m_window;
            Rect m_bounds;
            bool m_enabled;
            bool m_visible;
            bool m_focused;
            ControlState m_state;

            // Cached absolute bounds
            mutable Rect m_cachedAbsoluteBounds;
            mutable bool m_absBoundsDirty = true;

        private:
            static ControlId s_nextId;
        };

    } // namespace Controls
} // namespace QW