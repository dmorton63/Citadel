#pragma once

// QKDrvUHCI - USB 1.1 UHCI Controller driver
// Namespace: QKDrv::UHCI

#include "../QKDrvBase.h"
#include "QArchPCI.h"

namespace QKDrv
{
    namespace UHCI
    {

        // UHCI I/O registers
        namespace Regs
        {
            constexpr QC::u16 USBCMD = 0x00;
            constexpr QC::u16 USBSTS = 0x02;
            constexpr QC::u16 USBINTR = 0x04;
            constexpr QC::u16 FRNUM = 0x06;
            constexpr QC::u16 FRBASEADD = 0x08;
            constexpr QC::u16 SOFMOD = 0x0C;
            constexpr QC::u16 PORTSC1 = 0x10;
            constexpr QC::u16 PORTSC2 = 0x12;
        }

        // USB device speed
        enum class Speed : QC::u8
        {
            Low = 0,
            Full = 1
        };

        // Transfer Descriptor
        struct TD
        {
            QC::u32 linkPointer;
            QC::u32 ctrlStatus;
            QC::u32 token;
            QC::u32 bufferPointer;
            QC::u32 reserved[4];
        } __attribute__((packed));

        // Queue Head
        struct QH
        {
            QC::u32 headLink;
            QC::u32 elementLink;
            QC::u32 reserved[2];
        } __attribute__((packed));

        // HID Mouse report
        struct HIDMouseReport
        {
            QC::u8 buttons;
            QC::i8 x;
            QC::i8 y;
            QC::i8 wheel;
        } __attribute__((packed));

        // HID Tablet report (absolute coordinates)
        struct HIDTabletReport
        {
            QC::u8 buttons;
            QC::u16 x;
            QC::u16 y;
            QC::i8 wheel;
        } __attribute__((packed));

        class Controller : public DriverBase
        {
        public:
            static Controller *probe(QArch::PCIDevice *pciDevice);

            Controller(QArch::PCIDevice *pciDevice);
            ~Controller();

            // DriverBase interface
            QC::Status initialize() override;
            void shutdown() override;
            void poll() override;
            const char *name() const override { return "UHCI"; }
            ControllerType controllerType() const override { return ControllerType::UHCI; }

            // USB operations
            QC::u8 portCount() const { return 2; }
            bool isPortConnected(QC::u8 port) const;
            Speed getPortSpeed(QC::u8 port) const;
            void resetPort(QC::u8 port);

            // Mouse support
            void setMouseCallback(MouseCallback callback) { m_mouseCallback = callback; }
            void setScreenSize(QC::u32 width, QC::u32 height);

            bool hasTablet() const { return m_isTablet; }

        private:
            QC::u16 readReg16(QC::u16 offset);
            void writeReg16(QC::u16 offset, QC::u16 value);
            QC::u32 readReg32(QC::u16 offset);
            void writeReg32(QC::u16 offset, QC::u32 value);

            TD *allocateTD();
            void freeTD(TD *td);
            QH *allocateQH();
            void freeQH(QH *qh);

            void initializeFrameList();
            void probeDevices();

            QArch::PCIDevice *m_pciDevice;
            QC::u16 m_ioBase;

            QC::u32 *m_frameList;
            QH *m_asyncQH;

            MouseCallback m_mouseCallback;
            QC::u32 m_screenWidth;
            QC::u32 m_screenHeight;
            bool m_isTablet; // True if USB tablet detected
        };

    } // namespace UHCI
} // namespace QKDrv
