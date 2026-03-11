#pragma once

// Core UI control interface contract
// Namespace: QW::Controls

#include "QCTypes.h"
#include "QCGeometry.h"
#include "QCColor.h"
#include "QKEventTypes.h"
#include "QKEventListener.h"
#include "QWPaintContext.h"

namespace QW
{
    using Rect = QC::Rect;
    using Point = QC::Point;
    using Color = QC::Color;

    class Window;
    class StyleRenderer;

    namespace Controls
    {
        class Panel;

        using PaintContext = QW::PaintContext;

        /// Control ID type
        using ControlId = QC::u32;
        constexpr ControlId InvalidControlId = 0;

        /// Control state description for styling/interaction
        enum class ControlState : QC::u8
        {
            Normal,
            Hovered,
            Focused,
            Pressed,
            Disabled
        };

        /// Base interface for all UI controls
        class IControl : public QK::Event::IEventReceiver
        {
        public:
            virtual ~IControl() = default;

            // Type information
            virtual bool isContainer() const { return false; }
            virtual Panel *asPanel() { return nullptr; }
            virtual const Panel *asPanel() const { return nullptr; }

            // Identity
            virtual ControlId id() const = 0;
            virtual void setId(ControlId id) = 0;

            // Hierarchy
            virtual Panel *parent() const = 0;
            virtual void setParent(Panel *parent) = 0;

            virtual Window *window() const = 0;
            virtual void setWindow(Window *window) = 0;

            // Geometry
            virtual Rect bounds() const = 0;
            virtual void setBounds(const Rect &bounds) = 0;
            virtual Rect absoluteBounds() const = 0;
            virtual bool hitTest(int x, int y) const = 0;

            // State
            virtual bool isEnabled() const = 0;
            virtual void setEnabled(bool enabled) = 0;

            virtual bool isVisible() const = 0;
            virtual void setVisible(bool visible) = 0;

            virtual bool isFocused() const = 0;
            virtual void setFocused(bool focused) = 0;

            virtual ControlState state() const = 0;

            // Rendering
            virtual void paint(const PaintContext &ctx) = 0;
            virtual void invalidate() = 0;

            // Event handling
            virtual bool onEvent(const QK::Event::Event &event) override = 0;

            virtual bool onMouseMove(int x, int y, int dx, int dy) = 0;
            virtual bool onMouseDown(int x, int y, QK::Event::MouseButton button) = 0;
            virtual bool onMouseUp(int x, int y, QK::Event::MouseButton button) = 0;
            virtual bool onMouseScroll(int delta) = 0;

            virtual bool onKeyDown(uint8_t scancode, uint8_t keycode, char character, QK::Event::Modifiers mods) = 0;
            virtual bool onKeyUp(uint8_t scancode, uint8_t keycode, QK::Event::Modifiers mods) = 0;

            virtual void onFocus() = 0;
            virtual void onBlur() = 0;
        };
    }
} // namespace QW
