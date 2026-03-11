#pragma once

// QKDrvPS2Mouse - PS/2 Mouse driver
// Namespace: QKDrv::PS2

#include "../QKDrvBase.h"

namespace QKDrv
{
    namespace PS2
    {

        class Mouse : public MouseDriver
        {
        public:
            static Mouse &instance();

            // DriverBase interface
            QC::Status initialize() override;
            void shutdown() override;
            const char *name() const override { return "PS2Mouse"; }
            ControllerType controllerType() const override { return ControllerType::PS2; }

            // MouseDriver interface
            void setCallback(MouseCallback callback) override { m_callback = callback; }
            void setBounds(QC::i32 minX, QC::i32 minY, QC::i32 maxX, QC::i32 maxY) override;

            QC::i32 x() const override { return m_x; }
            QC::i32 y() const override { return m_y; }
            QC::u8 buttons() const override { return m_buttons; }

            bool isAbsolute() const override { return false; }

            // Called from interrupt handler
            void handleInterrupt();

        private:
            Mouse();
            ~Mouse() = default;
            Mouse(const Mouse &) = delete;
            Mouse &operator=(const Mouse &) = delete;

            void sendCommand(QC::u8 cmd);
            void waitForAck();

            MouseCallback m_callback;
            QC::i32 m_x;
            QC::i32 m_y;
            QC::i32 m_minX, m_minY, m_maxX, m_maxY;
            QC::u8 m_buttons;

            QC::u8 m_packetBuffer[4];
            QC::u8 m_packetIndex;
            bool m_hasScrollWheel;
        };

    } // namespace PS2
} // namespace QKDrv
