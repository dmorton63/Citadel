#pragma once

// QKDrvBase - Base class for all kernel drivers
// Namespace: QKDrv

#include "QCTypes.h"

namespace QKDrv
{

    // Input device types
    enum class InputDeviceType : QC::u8
    {
        None = 0,
        Mouse,
        Keyboard,
        Tablet, // Absolute pointing device
        Touchscreen,
        Gamepad
    };

    // Controller types for USB
    enum class ControllerType : QC::u8
    {
        None = 0,
        PS2,
        UHCI, // USB 1.0/1.1
        OHCI, // USB 1.0/1.1 (alternative)
        EHCI, // USB 2.0
        XHCI  // USB 3.0+
    };

    // Mouse report (works for both relative and absolute)
    struct MouseReport
    {
        QC::i32 x;       // Position or delta
        QC::i32 y;       // Position or delta
        QC::i32 deltaX;  // Relative movement X
        QC::i32 deltaY;  // Relative movement Y
        QC::i32 wheel;   // Scroll wheel delta
        QC::u8 buttons;  // Button state bitmask
        bool isAbsolute; // True for tablet/absolute mode
    };

    // Keyboard report
    struct KeyboardReport
    {
        QC::u8 scancode;
        bool pressed;     // True = key down, False = key up
        QC::u8 modifiers; // Shift, Ctrl, Alt, etc.
    };

    // Callbacks
    using MouseCallback = void (*)(const MouseReport &report);
    using KeyboardCallback = void (*)(const KeyboardReport &report);

    // Base driver interface
    class DriverBase
    {
    public:
        virtual ~DriverBase() = default;

        virtual QC::Status initialize() = 0;
        virtual void shutdown() = 0;
        virtual void poll() {} // Optional polling

        virtual const char *name() const = 0;
        virtual ControllerType controllerType() const = 0;
    };

    // Mouse driver interface
    class MouseDriver : public DriverBase
    {
    public:
        virtual ~MouseDriver() = default;

        virtual void setCallback(MouseCallback callback) = 0;
        virtual void setBounds(QC::i32 minX, QC::i32 minY, QC::i32 maxX, QC::i32 maxY) = 0;

        virtual QC::i32 x() const = 0;
        virtual QC::i32 y() const = 0;
        virtual QC::u8 buttons() const = 0;

        virtual bool isAbsolute() const { return false; }
    };

    // Keyboard driver interface
    class KeyboardDriver : public DriverBase
    {
    public:
        virtual ~KeyboardDriver() = default;

        virtual void setCallback(KeyboardCallback callback) = 0;
        virtual bool isKeyPressed(QC::u8 scancode) const = 0;
        virtual QC::u8 modifiers() const = 0;
    };

} // namespace QKDrv
