// QKDrvPS2Keyboard - PS/2 Keyboard driver implementation
// Namespace: QKDrv::PS2

#include "QKDrvPS2Keyboard.h"
#include "QArchPort.h"
#include "QKInterrupts.h"
#include "QCLogger.h"
#include "QCString.h"

namespace QKDrv
{
    namespace PS2
    {

        // PS/2 keyboard ports
        constexpr QC::u16 KEYBOARD_DATA_PORT = 0x60;
        constexpr QC::u16 KEYBOARD_STATUS_PORT = 0x64;
        constexpr QC::u16 KEYBOARD_COMMAND_PORT = 0x64;

        Keyboard &Keyboard::instance()
        {
            static Keyboard inst;
            return inst;
        }

        Keyboard::Keyboard()
            : m_callback(nullptr), m_ps2Callback(nullptr), m_shiftPressed(false), m_ctrlPressed(false), m_altPressed(false), m_capsLock(false), m_extended(false)
        {
            QC::String::memset(m_keyStates, 0, sizeof(m_keyStates));
        }

        QC::Status Keyboard::initialize()
        {
            QC_LOG_INFO("PS2Kbd", "Initializing PS/2 keyboard");

            // Wait for controller ready
            while (QArch::inb(KEYBOARD_STATUS_PORT) & 0x02)
                ;

            // Enable keyboard
            QArch::outb(KEYBOARD_COMMAND_PORT, 0xAE);

            // Drain any pending keyboard data
            for (int i = 0; i < 16; ++i)
            {
                if (QArch::inb(KEYBOARD_STATUS_PORT) & 0x01)
                {
                    QArch::inb(KEYBOARD_DATA_PORT); // Discard pending data
                }
                else
                {
                    break;
                }
            }

            // Register interrupt handler
            QK::InterruptManager::instance().registerHandler(
                QK::IRQ_KEYBOARD,
                [](QK::InterruptFrame *)
                {
                    Keyboard::instance().handleInterrupt();
                });
            QK::InterruptManager::instance().enableInterrupt(1);

            QC_LOG_INFO("PS2Kbd", "PS/2 keyboard initialized");

            return QC::Status::Success;
        }

        void Keyboard::shutdown()
        {
            // Disable keyboard
            while (QArch::inb(KEYBOARD_STATUS_PORT) & 0x02)
                ;
            QArch::outb(KEYBOARD_COMMAND_PORT, 0xAD);
            QC_LOG_INFO("PS2Kbd", "PS/2 keyboard shutdown");
        }

        void Keyboard::poll()
        {
            // Check if there's keyboard data available (bit 0 = data ready, bit 5 = aux/mouse data)
            QC::u8 status = QArch::inb(KEYBOARD_STATUS_PORT);
            if ((status & 0x01) && !(status & 0x20))
            {
                handleInterrupt();
            }
        }

        void Keyboard::setCallback(KeyboardCallback callback)
        {
            m_callback = callback;
        }

        bool Keyboard::isKeyPressed(QC::u8 scancode) const
        {
            return m_keyStates[scancode];
        }

        bool Keyboard::isKeyPressed(Key key) const
        {
            return m_keyStates[static_cast<QC::u8>(key)];
        }

        QC::u8 Keyboard::modifiers() const
        {
            QC::u8 mods = 0;
            if (m_shiftPressed)
                mods |= 0x01;
            if (m_ctrlPressed)
                mods |= 0x02;
            if (m_altPressed)
                mods |= 0x04;
            if (m_capsLock)
                mods |= 0x08;
            return mods;
        }

        void Keyboard::handleInterrupt()
        {
            QC::u8 scanCode = QArch::inb(KEYBOARD_DATA_PORT);

            // Handle extended scan codes
            if (scanCode == 0xE0)
            {
                m_extended = true;
                return;
            }

            bool released = (scanCode & 0x80) != 0;
            scanCode &= 0x7F;

            Key key = scanCodeToKey(scanCode);
            m_extended = false;

            if (key == Key::None)
                return;

            m_keyStates[static_cast<QC::u8>(key)] = !released;

            // Update modifier states
            switch (key)
            {
            case Key::LeftShift:
            case Key::RightShift:
                m_shiftPressed = !released;
                break;
            case Key::LeftCtrl:
            case Key::RightCtrl:
                m_ctrlPressed = !released;
                break;
            case Key::LeftAlt:
            case Key::RightAlt:
                m_altPressed = !released;
                break;
            case Key::CapsLock:
                if (!released)
                    m_capsLock = !m_capsLock;
                break;
            default:
                break;
            }

            // Call generic keyboard callback
            if (m_callback)
            {
                KeyboardReport report;
                report.scancode = static_cast<QC::u8>(key);
                report.pressed = !released;
                report.modifiers = modifiers();
                m_callback(report);
            }

            // Call PS2-specific callback
            if (m_ps2Callback)
            {
                KeyEvent event;
                event.key = key;
                event.pressed = !released;
                event.shift = m_shiftPressed;
                event.ctrl = m_ctrlPressed;
                event.alt = m_altPressed;
                event.character = keyToChar(key);
                m_ps2Callback(event);
            }
        }

        Key Keyboard::scanCodeToKey(QC::u8 scanCode)
        {
            // Basic scan code set 1 mapping
            static const Key scanCodeMap[] = {
                Key::None, Key::Escape, Key::Num1, Key::Num2, Key::Num3, Key::Num4,
                Key::Num5, Key::Num6, Key::Num7, Key::Num8, Key::Num9, Key::Num0,
                Key::Minus, Key::Equals, Key::Backspace, Key::Tab,
                Key::Q, Key::W, Key::E, Key::R, Key::T, Key::Y, Key::U, Key::I,
                Key::O, Key::P, Key::LeftBracket, Key::RightBracket, Key::Enter,
                Key::LeftCtrl, Key::A, Key::S, Key::D, Key::F, Key::G, Key::H,
                Key::J, Key::K, Key::L, Key::Semicolon, Key::Apostrophe,
                Key::Backtick, Key::LeftShift, Key::Backslash,
                Key::Z, Key::X, Key::C, Key::V, Key::B, Key::N, Key::M,
                Key::Comma, Key::Period, Key::Slash, Key::RightShift,
                Key::KpMultiply, Key::LeftAlt, Key::Space, Key::CapsLock};

            if (scanCode < sizeof(scanCodeMap) / sizeof(scanCodeMap[0]))
            {
                return scanCodeMap[scanCode];
            }

            return Key::None;
        }

        char Keyboard::keyToChar(Key key)
        {
            const bool shift = m_shiftPressed;
            const bool caps = m_capsLock;

            auto letter = [&](char c) -> char
            {
                const bool upper = (shift ^ caps);
                return upper ? static_cast<char>(c - 32) : c;
            };

            switch (key)
            {
            // Letters
            case Key::Q:
                return letter('q');
            case Key::W:
                return letter('w');
            case Key::E:
                return letter('e');
            case Key::R:
                return letter('r');
            case Key::T:
                return letter('t');
            case Key::Y:
                return letter('y');
            case Key::U:
                return letter('u');
            case Key::I:
                return letter('i');
            case Key::O:
                return letter('o');
            case Key::P:
                return letter('p');
            case Key::A:
                return letter('a');
            case Key::S:
                return letter('s');
            case Key::D:
                return letter('d');
            case Key::F:
                return letter('f');
            case Key::G:
                return letter('g');
            case Key::H:
                return letter('h');
            case Key::J:
                return letter('j');
            case Key::K:
                return letter('k');
            case Key::L:
                return letter('l');
            case Key::Z:
                return letter('z');
            case Key::X:
                return letter('x');
            case Key::C:
                return letter('c');
            case Key::V:
                return letter('v');
            case Key::B:
                return letter('b');
            case Key::N:
                return letter('n');
            case Key::M:
                return letter('m');

            // Digits + shifted symbols
            case Key::Num1:
                return shift ? '!' : '1';
            case Key::Num2:
                return shift ? '@' : '2';
            case Key::Num3:
                return shift ? '#' : '3';
            case Key::Num4:
                return shift ? '$' : '4';
            case Key::Num5:
                return shift ? '%' : '5';
            case Key::Num6:
                return shift ? '^' : '6';
            case Key::Num7:
                return shift ? '&' : '7';
            case Key::Num8:
                return shift ? '*' : '8';
            case Key::Num9:
                return shift ? '(' : '9';
            case Key::Num0:
                return shift ? ')' : '0';

            // Punctuation
            case Key::Space:
                return ' ';
            case Key::Enter:
                return '\n';
            case Key::Tab:
                return '\t';
            case Key::Backspace:
                return '\b';

            case Key::Minus:
                return shift ? '_' : '-';
            case Key::Equals:
                return shift ? '+' : '=';
            case Key::LeftBracket:
                return shift ? '{' : '[';
            case Key::RightBracket:
                return shift ? '}' : ']';
            case Key::Backslash:
                return shift ? '|' : '\\';
            case Key::Semicolon:
                return shift ? ':' : ';';
            case Key::Apostrophe:
                return shift ? '"' : '\'';
            case Key::Backtick:
                return shift ? '~' : '`';
            case Key::Comma:
                return shift ? '<' : ',';
            case Key::Period:
                return shift ? '>' : '.';
            case Key::Slash:
                return shift ? '?' : '/';

            default:
                return 0;
            }
        }

    } // namespace PS2
} // namespace QKDrv
