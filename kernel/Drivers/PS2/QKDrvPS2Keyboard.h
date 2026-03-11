#pragma once

// QKDrvPS2Keyboard - PS/2 Keyboard driver
// Namespace: QKDrv::PS2

#include "../QKDrvBase.h"

namespace QKDrv
{
    namespace PS2
    {

        // Scan code to key mapping
        enum class Key : QC::u8
        {
            None = 0,
            Escape,
            F1,
            F2,
            F3,
            F4,
            F5,
            F6,
            F7,
            F8,
            F9,
            F10,
            F11,
            F12,
            Backtick,
            Num1,
            Num2,
            Num3,
            Num4,
            Num5,
            Num6,
            Num7,
            Num8,
            Num9,
            Num0,
            Minus,
            Equals,
            Backspace,
            Tab,
            Q,
            W,
            E,
            R,
            T,
            Y,
            U,
            I,
            O,
            P,
            LeftBracket,
            RightBracket,
            Backslash,
            CapsLock,
            A,
            S,
            D,
            F,
            G,
            H,
            J,
            K,
            L,
            Semicolon,
            Apostrophe,
            Enter,
            LeftShift,
            Z,
            X,
            C,
            V,
            B,
            N,
            M,
            Comma,
            Period,
            Slash,
            RightShift,
            LeftCtrl,
            LeftAlt,
            Space,
            RightAlt,
            RightCtrl,
            Insert,
            Delete,
            Home,
            End,
            PageUp,
            PageDown,
            Up,
            Down,
            Left,
            Right,
            NumLock,
            KpDivide,
            KpMultiply,
            KpMinus,
            Kp7,
            Kp8,
            Kp9,
            KpPlus,
            Kp4,
            Kp5,
            Kp6,
            Kp1,
            Kp2,
            Kp3,
            KpEnter,
            Kp0,
            KpDot,
            PrintScreen,
            ScrollLock,
            Pause
        };

        struct KeyEvent
        {
            Key key;
            bool pressed;
            bool shift;
            bool ctrl;
            bool alt;
            char character; // ASCII character if applicable
        };

        using PS2KeyboardCallback = void (*)(const KeyEvent &event);

        class Keyboard : public KeyboardDriver
        {
        public:
            static Keyboard &instance();

            // DriverBase interface
            QC::Status initialize() override;
            void shutdown() override;
            void poll() override; // Poll for keyboard input (fallback if IRQ not working)
            const char *name() const override { return "PS2Keyboard"; }
            ControllerType controllerType() const override { return ControllerType::PS2; }

            // KeyboardDriver interface
            void setCallback(KeyboardCallback callback) override;
            bool isKeyPressed(QC::u8 scancode) const override;
            QC::u8 modifiers() const override;

            // PS2-specific
            void setPS2Callback(PS2KeyboardCallback callback) { m_ps2Callback = callback; }
            bool isKeyPressed(Key key) const;
            bool isShiftPressed() const { return m_shiftPressed; }
            bool isCtrlPressed() const { return m_ctrlPressed; }
            bool isAltPressed() const { return m_altPressed; }
            bool isCapsLockOn() const { return m_capsLock; }

            // Called from interrupt handler
            void handleInterrupt();

        private:
            Keyboard();
            ~Keyboard() = default;
            Keyboard(const Keyboard &) = delete;
            Keyboard &operator=(const Keyboard &) = delete;

            Key scanCodeToKey(QC::u8 scanCode);
            char keyToChar(Key key);

            KeyboardCallback m_callback;
            PS2KeyboardCallback m_ps2Callback;
            bool m_keyStates[256];
            bool m_shiftPressed;
            bool m_ctrlPressed;
            bool m_altPressed;
            bool m_capsLock;
            bool m_extended;
        };

    } // namespace PS2
} // namespace QKDrv
