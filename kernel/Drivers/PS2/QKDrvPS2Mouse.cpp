// QKDrvPS2Mouse - PS/2 Mouse driver implementation
// Namespace: QKDrv::PS2

#include "QKDrvPS2Mouse.h"
#include "QArchPort.h"
#include "QKInterrupts.h"
#include "QCLogger.h"
#include "QCString.h"

namespace QKDrv
{
    namespace PS2
    {

        // PS/2 mouse ports
        constexpr QC::u16 MOUSE_DATA_PORT = 0x60;
        constexpr QC::u16 MOUSE_STATUS_PORT = 0x64;
        constexpr QC::u16 MOUSE_COMMAND_PORT = 0x64;

        Mouse &Mouse::instance()
        {
            static Mouse inst;
            return inst;
        }

        Mouse::Mouse()
            : m_callback(nullptr), m_x(0), m_y(0), m_minX(0), m_minY(0), m_maxX(1024), m_maxY(768), m_buttons(0), m_packetIndex(0), m_hasScrollWheel(false)
        {
            QC::String::memset(m_packetBuffer, 0, sizeof(m_packetBuffer));
        }

        QC::Status Mouse::initialize()
        {
            QC_LOG_INFO("PS2Mouse", "Initializing PS/2 mouse");

            // Enable auxiliary device
            while (QArch::inb(MOUSE_STATUS_PORT) & 0x02)
                ;
            QArch::outb(MOUSE_COMMAND_PORT, 0xA8);

            // Enable interrupts
            while (QArch::inb(MOUSE_STATUS_PORT) & 0x02)
                ;
            QArch::outb(MOUSE_COMMAND_PORT, 0x20);
            while (!(QArch::inb(MOUSE_STATUS_PORT) & 0x01))
                ;
            QC::u8 status = QArch::inb(MOUSE_DATA_PORT);
            status |= 0x02; // Enable IRQ12

            while (QArch::inb(MOUSE_STATUS_PORT) & 0x02)
                ;
            QArch::outb(MOUSE_COMMAND_PORT, 0x60);
            while (QArch::inb(MOUSE_STATUS_PORT) & 0x02)
                ;
            QArch::outb(MOUSE_DATA_PORT, status);

            // Set defaults
            sendCommand(0xF6);
            waitForAck();

            // Enable mouse
            sendCommand(0xF4);
            waitForAck();

            // Try to enable scroll wheel (IntelliMouse)
            sendCommand(0xF3);
            waitForAck();
            sendCommand(200);
            waitForAck();
            sendCommand(0xF3);
            waitForAck();
            sendCommand(100);
            waitForAck();
            sendCommand(0xF3);
            waitForAck();
            sendCommand(80);
            waitForAck();
            sendCommand(0xF2);
            waitForAck();
            while (!(QArch::inb(MOUSE_STATUS_PORT) & 0x01))
                ;
            if (QArch::inb(MOUSE_DATA_PORT) == 3)
            {
                m_hasScrollWheel = true;
                QC_LOG_INFO("PS2Mouse", "Scroll wheel detected");
            }

            // Register interrupt handler
            QK::InterruptManager::instance().registerHandler(
                QK::IRQ_MOUSE,
                [](QK::InterruptFrame *)
                {
                    Mouse::instance().handleInterrupt();
                });
            QK::InterruptManager::instance().enableInterrupt(12);

            QC_LOG_INFO("PS2Mouse", "PS/2 mouse initialized");

            return QC::Status::Success;
        }

        void Mouse::shutdown()
        {
            // Disable mouse
            sendCommand(0xF5);
            waitForAck();
            QC_LOG_INFO("PS2Mouse", "PS/2 mouse shutdown");
        }

        void Mouse::sendCommand(QC::u8 cmd)
        {
            while (QArch::inb(MOUSE_STATUS_PORT) & 0x02)
                ;
            QArch::outb(MOUSE_COMMAND_PORT, 0xD4);
            while (QArch::inb(MOUSE_STATUS_PORT) & 0x02)
                ;
            QArch::outb(MOUSE_DATA_PORT, cmd);
        }

        void Mouse::waitForAck()
        {
            while (!(QArch::inb(MOUSE_STATUS_PORT) & 0x01))
                ;
            QArch::inb(MOUSE_DATA_PORT); // Discard ACK
        }

        void Mouse::setBounds(QC::i32 minX, QC::i32 minY, QC::i32 maxX, QC::i32 maxY)
        {
            m_minX = minX;
            m_minY = minY;
            m_maxX = maxX;
            m_maxY = maxY;

            // Center the mouse cursor when bounds are set
            m_x = (minX + maxX) / 2;
            m_y = (minY + maxY) / 2;
        }

        void Mouse::handleInterrupt()
        {
            QC::u8 status = QArch::inb(MOUSE_STATUS_PORT);
            if (!(status & 0x20))
                return; // Not mouse data

            QC::u8 data = QArch::inb(MOUSE_DATA_PORT);
            m_packetBuffer[m_packetIndex++] = data;

            QC::usize expectedSize = m_hasScrollWheel ? 4 : 3;
            if (m_packetIndex < expectedSize)
                return;

            m_packetIndex = 0;

            // Parse packet
            QC::i32 deltaX = m_packetBuffer[1];
            QC::i32 deltaY = m_packetBuffer[2];
            QC::i32 deltaZ = 0;

            if (m_packetBuffer[0] & 0x10)
                deltaX |= 0xFFFFFF00; // Sign extend
            if (m_packetBuffer[0] & 0x20)
                deltaY |= 0xFFFFFF00;

            if (m_hasScrollWheel)
            {
                deltaZ = static_cast<QC::i8>(m_packetBuffer[3]);
            }

            // Update position
            m_x -= deltaX;
            m_y += deltaY;

            // Clamp to bounds
            if (m_x < m_minX)
                m_x = m_minX;
            if (m_x > m_maxX)
                m_x = m_maxX;
            if (m_y < m_minY)
                m_y = m_minY;
            if (m_y > m_maxY)
                m_y = m_maxY;

            // Update buttons
            m_buttons = m_packetBuffer[0] & 0x07;

            if (m_callback)
            {
                MouseReport report;
                report.x = deltaX;
                report.y = deltaY;
                report.wheel = deltaZ;
                report.buttons = m_buttons;
                report.isAbsolute = false;
                m_callback(report);
            }
        }

    } // namespace PS2
} // namespace QKDrv
