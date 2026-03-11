#pragma once

// QArch PCI - PCI Bus Access
// Namespace: QArch

#include "QCTypes.h"
#include "QCVector.h"

namespace QArch
{

    struct PCIAddress
    {
        QC::u8 bus;
        QC::u8 device;
        QC::u8 function;

        bool operator==(const PCIAddress &other) const
        {
            return bus == other.bus && device == other.device && function == other.function;
        }
    };

    struct PCIDevice
    {
        PCIAddress address;
        QC::u16 vendorId;
        QC::u16 deviceId;
        QC::u8 classCode;
        QC::u8 subclass;
        QC::u8 progIF;
        QC::u8 revision;
        QC::u8 headerType;
        QC::u64 bar[6];
        QC::u8 interruptLine;
        QC::u8 interruptPin;
    };

    class PCI
    {
    public:
        static PCI &instance();

        void initialize();
        void enumerate();

        // Configuration space access
        QC::u8 readConfig8(PCIAddress addr, QC::u8 offset);
        QC::u16 readConfig16(PCIAddress addr, QC::u8 offset);
        QC::u32 readConfig32(PCIAddress addr, QC::u8 offset);

        void writeConfig8(PCIAddress addr, QC::u8 offset, QC::u8 value);
        void writeConfig16(PCIAddress addr, QC::u8 offset, QC::u16 value);
        void writeConfig32(PCIAddress addr, QC::u8 offset, QC::u32 value);

        // Device lookup
        PCIDevice *findDevice(QC::u16 vendorId, QC::u16 deviceId);
        PCIDevice *findDeviceByClass(QC::u8 classCode, QC::u8 subclass);

        const QC::Vector<PCIDevice> &devices() const { return m_devices; }

        // BAR handling
        QC::PhysAddr getBAR(PCIAddress addr, QC::u8 bar);
        QC::usize getBARSize(PCIAddress addr, QC::u8 bar);
        bool isMMIOBar(PCIAddress addr, QC::u8 bar);

        // Power/interrupt management
        void enableBusMastering(PCIAddress addr);
        void enableMemorySpace(PCIAddress addr);
        void enableIOSpace(PCIAddress addr);

    private:
        PCI();
        ~PCI();
        PCI(const PCI &) = delete;
        PCI &operator=(const PCI &) = delete;

        void checkDevice(QC::u8 bus, QC::u8 device);
        void checkFunction(QC::u8 bus, QC::u8 device, QC::u8 function);
        QC::u32 makeAddress(PCIAddress addr, QC::u8 offset);

        QC::Vector<PCIDevice> m_devices;
    };

    // PCI class codes
    namespace PCIClass
    {
        constexpr QC::u8 Unclassified = 0x00;
        constexpr QC::u8 MassStorage = 0x01;
        constexpr QC::u8 Network = 0x02;
        constexpr QC::u8 Display = 0x03;
        constexpr QC::u8 Multimedia = 0x04;
        constexpr QC::u8 Memory = 0x05;
        constexpr QC::u8 Bridge = 0x06;
        constexpr QC::u8 Communication = 0x07;
        constexpr QC::u8 Peripheral = 0x08;
        constexpr QC::u8 Input = 0x09;
        constexpr QC::u8 Docking = 0x0A;
        constexpr QC::u8 Processor = 0x0B;
        constexpr QC::u8 SerialBus = 0x0C;
        constexpr QC::u8 Wireless = 0x0D;
    }

    // USB subclass
    namespace PCISubclass
    {
        constexpr QC::u8 USB_UHCI = 0x00;
        constexpr QC::u8 USB_OHCI = 0x10;
        constexpr QC::u8 USB_EHCI = 0x20;
        constexpr QC::u8 USB_XHCI = 0x30;
    }

} // namespace QArch
