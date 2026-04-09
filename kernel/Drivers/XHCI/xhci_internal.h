#pragma once

// xHCI internal driver definitions
// Namespace: QKDrv::XHCI

#include "xhci.h"
#include "xhci_regs.h"
#include "xhci_trb.h"
#include "xhci_context.h"
#include "QArchPCI.h"

namespace QKDrv
{
    namespace XHCI
    {
        enum class Speed : QC::u8
        {
            Full = 1,
            Low = 2,
            High = 3,
            Super = 4,
            SuperPlus = 5
        };

        struct USBDeviceDescriptor
        {
            QC::u8 bLength;
            QC::u8 bDescriptorType;
            QC::u16 bcdUSB;
            QC::u8 bDeviceClass;
            QC::u8 bDeviceSubClass;
            QC::u8 bDeviceProtocol;
            QC::u8 bMaxPacketSize0;
            QC::u16 idVendor;
            QC::u16 idProduct;
            QC::u16 bcdDevice;
            QC::u8 iManufacturer;
            QC::u8 iProduct;
            QC::u8 iSerialNumber;
            QC::u8 bNumConfigurations;
        } __attribute__((packed));

        struct USBConfigDescriptor
        {
            QC::u8 bLength;
            QC::u8 bDescriptorType;
            QC::u16 wTotalLength;
            QC::u8 bNumInterfaces;
            QC::u8 bConfigurationValue;
            QC::u8 iConfiguration;
            QC::u8 bmAttributes;
            QC::u8 bMaxPower;
        } __attribute__((packed));

        struct USBInterfaceDescriptor
        {
            QC::u8 bLength;
            QC::u8 bDescriptorType;
            QC::u8 bInterfaceNumber;
            QC::u8 bAlternateSetting;
            QC::u8 bNumEndpoints;
            QC::u8 bInterfaceClass;
            QC::u8 bInterfaceSubClass;
            QC::u8 bInterfaceProtocol;
            QC::u8 iInterface;
        } __attribute__((packed));

        struct USBEndpointDescriptor
        {
            QC::u8 bLength;
            QC::u8 bDescriptorType;
            QC::u8 bEndpointAddress;
            QC::u8 bmAttributes;
            QC::u16 wMaxPacketSize;
            QC::u8 bInterval;
        } __attribute__((packed));

        struct USBHIDDescriptor
        {
            QC::u8 bLength;
            QC::u8 bDescriptorType;
            QC::u16 bcdHID;
            QC::u8 bCountryCode;
            QC::u8 bNumDescriptors;
            QC::u8 bClassDescriptorType;
            QC::u16 wDescriptorLength;
        } __attribute__((packed));

        struct DeviceInfo
        {
            QC::u8 slotId;
            QC::u8 port;
            Speed speed;
            bool isHID;
            bool isTablet;
            bool isMouse;
            bool isKeyboard;
            QC::u8 hidEndpoint;
            QC::u8 hidInterval;
            QC::u16 hidMaxPacket;
            TRB *transferRing;
            QC::PhysAddr transferRingPhys;
            QC::usize transferEnqueue;
            bool transferCycle;
            QC::u8 *hidBuffer;
            QC::PhysAddr hidBufferPhys;
            QC::u32 logicalMaxX;
            QC::u32 logicalMaxY;
        };

        enum class HIDDeviceKind : QC::u8
        {
            None = 0,
            Mouse,
            Tablet,
            Keyboard
        };

        struct HIDMouseReport
        {
            QC::u8 buttons;
            QC::i8 dx;
            QC::i8 dy;
            QC::i8 wheel;
        } __attribute__((packed));

        struct HIDTabletReport
        {
            QC::u8 buttons;
            QC::u16 x;
            QC::u16 y;
            QC::i8 wheel;
        } __attribute__((packed));

        constexpr QC::usize MAX_DEVICES = 16;
        constexpr QC::usize RING_SIZE = 256;

        class XHCIControllerImpl;

        class TabletDriver : public MouseDriver
        {
        public:
            explicit TabletDriver(XHCIControllerImpl *controller);

            QC::Status initialize() override { return QC::Status::Success; }
            void shutdown() override {}
            void poll() override;
            const char *name() const override { return "USB Tablet"; }
            const char *driverId() const override { return "QDRV_XHCI_TABLET"; }
            ControllerType controllerType() const override { return ControllerType::XHCI; }

            void setCallback(MouseCallback callback) override { m_callback = callback; }
            void setBounds(QC::i32 minX, QC::i32 minY, QC::i32 maxX, QC::i32 maxY) override;

            QC::i32 x() const override { return m_x; }
            QC::i32 y() const override { return m_y; }
            QC::u8 buttons() const override { return m_buttons; }

            bool isAbsolute() const override { return true; }

            void updatePosition(QC::i32 x, QC::i32 y, QC::i32 wheel, QC::u8 buttons);

        private:
            XHCIControllerImpl *m_controller;
            MouseCallback m_callback;
            QC::i32 m_x;
            QC::i32 m_y;
            QC::i32 m_minX;
            QC::i32 m_minY;
            QC::i32 m_maxX;
            QC::i32 m_maxY;
            QC::u8 m_buttons;
        };

        class HIDMouseDriver : public MouseDriver
        {
        public:
            explicit HIDMouseDriver(XHCIControllerImpl *controller);

            QC::Status initialize() override { return QC::Status::Success; }
            void shutdown() override {}
            void poll() override;
            const char *name() const override { return "USB Mouse"; }
            const char *driverId() const override { return "QDRV_XHCI_MOUSE"; }
            ControllerType controllerType() const override { return ControllerType::XHCI; }

            void setCallback(MouseCallback callback) override { m_callback = callback; }
            void setBounds(QC::i32 minX, QC::i32 minY, QC::i32 maxX, QC::i32 maxY) override;

            QC::i32 x() const override { return m_x; }
            QC::i32 y() const override { return m_y; }
            QC::u8 buttons() const override { return m_buttons; }

            bool isAbsolute() const override { return false; }

            void updateDelta(QC::i32 dx, QC::i32 dy, QC::i32 wheel, QC::u8 buttons);

        private:
            XHCIControllerImpl *m_controller;
            MouseCallback m_callback;
            QC::i32 m_x;
            QC::i32 m_y;
            float m_fx;
            float m_fy;
            QC::i32 m_minX;
            QC::i32 m_minY;
            QC::i32 m_maxX;
            QC::i32 m_maxY;
            QC::u8 m_buttons;
        };

        class HIDKeyboardDriver : public KeyboardDriver
        {
        public:
            explicit HIDKeyboardDriver(XHCIControllerImpl *controller);

            QC::Status initialize() override { return QC::Status::Success; }
            void shutdown() override {}
            void poll() override;
            const char *name() const override { return "USB Keyboard"; }
            const char *driverId() const override { return "QDRV_XHCI_KEYBOARD"; }
            ControllerType controllerType() const override { return ControllerType::XHCI; }

            void setCallback(KeyboardCallback callback) override { m_callback = callback; }
            bool isKeyPressed(QC::u8 scancode) const override;
            QC::u8 modifiers() const override { return m_modifiers; }

            void handleBootReport(const QC::u8 *data, QC::u32 length);

        private:
            void emit(QC::u8 scancode, bool pressed);

            XHCIControllerImpl *m_controller;
            KeyboardCallback m_callback;
            bool m_keyStates[256];
            QC::u8 m_prevBootMods;
            QC::u8 m_prevKeys[6];
            QC::u8 m_modifiers;
            bool m_capsLock;
        };

        class XHCIControllerImpl : public XHCIController
        {
        public:
            static XHCIControllerImpl *create(QArch::PCIDevice *pciDevice);

            explicit XHCIControllerImpl(QArch::PCIDevice *pciDevice);
            ~XHCIControllerImpl() override;

            QC::Status initialize() override;
            void shutdown() override;
            void poll() override;
            const char *name() const override { return "xHCI"; }
            const char *driverId() const override { return "QDRV_XHCI_CONTROLLER"; }
            ControllerType controllerType() const override { return ControllerType::XHCI; }

            void setMouseCallback(MouseCallback callback) override { m_mouseCallback = callback; }
            void setScreenSize(QC::u32 width, QC::u32 height) override;

            bool hasTablet() const override { return m_tabletSlot != 0; }
            MouseDriver *tabletDriver() override { return m_tabletDriver; }

            bool hasMouse() const override { return m_mouseSlot != 0; }
            MouseDriver *mouseDriver() override { return m_mouseDriver; }

            bool hasKeyboard() const override { return m_keyboardSlot != 0; }
            KeyboardDriver *keyboardDriver() override { return m_keyboardDriver; }

            void hardwareReset() override;
            bool submitTransfer(QC::u8 slotId,
                                QC::u8 endpointId,
                                QC::PhysAddr buffer,
                                QC::u32 length,
                                QC::u32 trbFlags) override;

        private:
            void takeOwnership();
            void initializeEventRing();
            void initializeCommandRing();
            void initializeDCBAA();
            void initializeScratchpad();
            void reset();
            bool isPortConnected(QC::u8 port) const;
            Speed getPortSpeed(QC::u8 port) const;
            void resetPort(QC::u8 port);

            void ringCommandDoorbell();
            TRB *enqueueCommand(const TRB &trb);
            bool waitForCommand(QC::u32 timeoutMs = 100);

            QC::u8 enableSlot();
            bool addressDevice(QC::u8 slotId, QC::u8 port, Speed speed);
            bool configureEndpoint(QC::u8 slotId, const USBEndpointDescriptor &ep);
            bool setConfiguration(QC::u8 slotId, QC::u8 configValue);
            bool setHIDProtocol(QC::u8 slotId, QC::u8 interfaceNumber, QC::u16 protocol);

            bool controlTransfer(QC::u8 slotId, QC::u8 reqType, QC::u8 request,
                                 QC::u16 value, QC::u16 index, void *data, QC::u16 length);
            bool scheduleInterruptIn(DeviceInfo &dev);
            bool submitTransfer(DeviceInfo &dev, QC::u8 endpointId,
                                QC::PhysAddr buffer, QC::u32 length, QC::u32 trbFlags);

            void processEvents();
            void handleTransferEvent(const TRB &event);
            void handlePortStatusChange(const TRB &event);
            void handleCommandComplete(const TRB &event);

            void probeDevices();
            bool enumerateDevice(QC::u8 port);
            HIDDeviceKind identifyHID(QC::u8 slotId, DeviceInfo &dev, const QC::u8 *configData, QC::u16 length);
            bool fetchHIDLogicalRanges(QC::u8 slotId, QC::u8 interfaceNumber,
                                       QC::u16 reportLength, QC::u32 &logicalMaxX,
                                       QC::u32 &logicalMaxY);
            void parseHIDLogicalRanges(const QC::u8 *descriptor, QC::u16 length,
                                       QC::u32 &logicalMaxX, QC::u32 &logicalMaxY);

            void ringDoorbell(QC::u8 slot, QC::u8 target);

            QArch::PCIDevice *m_pciDevice;
            QC::VirtAddr m_mmioBase;

            volatile CapRegs *m_capRegs;
            volatile OpRegs *m_opRegs;
            volatile QC::u32 *m_doorbells;
            volatile PortRegs *m_portRegs;
            volatile InterrupterRegs *m_interrupter;

            QC::u8 m_portCount;
            QC::u8 m_maxSlots;
            QC::u8 m_maxIntrs;
            QC::u32 m_pageSize;
            QC::u32 m_contextSize;

            bool m_portEnumerating[32];

            QC::u64 *m_dcbaa;

            TRB *m_commandRing;
            QC::usize m_commandEnqueue;
            bool m_commandCycle;

            TRB *m_eventRing;
            QC::PhysAddr m_eventRingPhys;
            QC::usize m_eventDequeue;
            bool m_eventCycle;

            ERSTEntry *m_erst;
            QC::PhysAddr m_erstPhys;

            QC::u8 *m_inputContext;
            QC::PhysAddr m_inputContextPhys;

            QC::u8 *m_outputContext;
            QC::PhysAddr m_outputContextPhys;

            QC::u8 *m_scratchpad;
            QC::PhysAddr m_scratchpadPhys;

            QC::u8 *m_deviceContexts[MAX_DEVICES + 1];
            DeviceInfo m_devices[MAX_DEVICES];
            QC::u8 m_deviceCount;

            QC::u8 m_tabletSlot;
            QC::u8 m_mouseSlot;
            QC::u8 m_keyboardSlot;
            bool m_commandPending;
            CompletionCode m_lastCompletionCode;
            QC::u8 m_lastSlotId;

            bool m_transferPending;
            CompletionCode m_transferCompletionCode;

            MouseCallback m_mouseCallback;
            QC::u32 m_screenWidth;
            QC::u32 m_screenHeight;

            TabletDriver *m_tabletDriver;
            HIDMouseDriver *m_mouseDriver;
            HIDKeyboardDriver *m_keyboardDriver;

            TRB *m_ep0Rings[MAX_DEVICES + 1];
            QC::PhysAddr m_ep0RingPhys[MAX_DEVICES + 1];
            QC::usize m_ep0Enqueue[MAX_DEVICES + 1];
            bool m_ep0Cycle[MAX_DEVICES + 1];
        };
    }
} // namespace QKDrv
