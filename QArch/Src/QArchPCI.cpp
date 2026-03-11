// QArch PCI - Implementation
// Namespace: QArch

#include "QArchPCI.h"
#include "QArchPort.h"
#include "QCLogger.h"

namespace QArch
{

    // PCI configuration ports
    constexpr QC::u16 PCI_CONFIG_ADDRESS = 0xCF8;
    constexpr QC::u16 PCI_CONFIG_DATA = 0xCFC;

    PCI &PCI::instance()
    {
        static PCI instance;
        return instance;
    }

    PCI::PCI()
    {
    }

    PCI::~PCI()
    {
    }

    void PCI::initialize()
    {
        QC_LOG_INFO("QArchPCI", "Initializing PCI subsystem");
        enumerate();
        QC_LOG_INFO("QArchPCI", "Found %lu PCI devices", m_devices.size());
    }

    void PCI::enumerate()
    {
        for (QC::u16 bus = 0; bus < 256; ++bus)
        {
            for (QC::u8 device = 0; device < 32; ++device)
            {
                checkDevice(static_cast<QC::u8>(bus), device);
            }
        }
    }

    void PCI::checkDevice(QC::u8 bus, QC::u8 device)
    {
        PCIAddress addr = {bus, device, 0};
        QC::u16 vendorId = readConfig16(addr, 0);

        if (vendorId == 0xFFFF)
            return; // No device

        checkFunction(bus, device, 0);

        // Check for multi-function device
        QC::u8 headerType = readConfig8(addr, 0x0E);
        if (headerType & 0x80)
        {
            for (QC::u8 function = 1; function < 8; ++function)
            {
                PCIAddress funcAddr = {bus, device, function};
                if (readConfig16(funcAddr, 0) != 0xFFFF)
                {
                    checkFunction(bus, device, function);
                }
            }
        }
    }

    void PCI::checkFunction(QC::u8 bus, QC::u8 device, QC::u8 function)
    {
        PCIAddress addr = {bus, device, function};

        PCIDevice dev;
        dev.address = addr;
        dev.vendorId = readConfig16(addr, 0x00);
        dev.deviceId = readConfig16(addr, 0x02);
        dev.classCode = readConfig8(addr, 0x0B);
        dev.subclass = readConfig8(addr, 0x0A);
        dev.progIF = readConfig8(addr, 0x09);
        dev.revision = readConfig8(addr, 0x08);
        dev.headerType = readConfig8(addr, 0x0E) & 0x7F;

        // Read BARs for type 0 headers
        if (dev.headerType == 0)
        {
            for (int i = 0; i < 6; ++i)
            {
                dev.bar[i] = getBAR(addr, static_cast<QC::u8>(i));
            }
            dev.interruptLine = readConfig8(addr, 0x3C);
            dev.interruptPin = readConfig8(addr, 0x3D);
        }

        m_devices.push_back(dev);

        QC_LOG_DEBUG("QArchPCI", "Device %02x:%02x.%x - %04x:%04x class %02x:%02x",
                     bus, device, function, dev.vendorId, dev.deviceId,
                     dev.classCode, dev.subclass);
    }

    QC::u32 PCI::makeAddress(PCIAddress addr, QC::u8 offset)
    {
        return (1U << 31) | // Enable bit
               (static_cast<QC::u32>(addr.bus) << 16) |
               (static_cast<QC::u32>(addr.device) << 11) |
               (static_cast<QC::u32>(addr.function) << 8) |
               (offset & 0xFC);
    }

    QC::u8 PCI::readConfig8(PCIAddress addr, QC::u8 offset)
    {
        outl(PCI_CONFIG_ADDRESS, makeAddress(addr, offset));
        return inb(PCI_CONFIG_DATA + (offset & 3));
    }

    QC::u16 PCI::readConfig16(PCIAddress addr, QC::u8 offset)
    {
        outl(PCI_CONFIG_ADDRESS, makeAddress(addr, offset));
        return inw(PCI_CONFIG_DATA + (offset & 2));
    }

    QC::u32 PCI::readConfig32(PCIAddress addr, QC::u8 offset)
    {
        outl(PCI_CONFIG_ADDRESS, makeAddress(addr, offset));
        return inl(PCI_CONFIG_DATA);
    }

    void PCI::writeConfig8(PCIAddress addr, QC::u8 offset, QC::u8 value)
    {
        outl(PCI_CONFIG_ADDRESS, makeAddress(addr, offset));
        outb(PCI_CONFIG_DATA + (offset & 3), value);
    }

    void PCI::writeConfig16(PCIAddress addr, QC::u8 offset, QC::u16 value)
    {
        outl(PCI_CONFIG_ADDRESS, makeAddress(addr, offset));
        outw(PCI_CONFIG_DATA + (offset & 2), value);
    }

    void PCI::writeConfig32(PCIAddress addr, QC::u8 offset, QC::u32 value)
    {
        outl(PCI_CONFIG_ADDRESS, makeAddress(addr, offset));
        outl(PCI_CONFIG_DATA, value);
    }

    PCIDevice *PCI::findDevice(QC::u16 vendorId, QC::u16 deviceId)
    {
        for (QC::usize i = 0; i < m_devices.size(); ++i)
        {
            if (m_devices[i].vendorId == vendorId && m_devices[i].deviceId == deviceId)
            {
                return &m_devices[i];
            }
        }
        return nullptr;
    }

    PCIDevice *PCI::findDeviceByClass(QC::u8 classCode, QC::u8 subclass)
    {
        for (QC::usize i = 0; i < m_devices.size(); ++i)
        {
            if (m_devices[i].classCode == classCode && m_devices[i].subclass == subclass)
            {
                return &m_devices[i];
            }
        }
        return nullptr;
    }

    QC::PhysAddr PCI::getBAR(PCIAddress addr, QC::u8 bar)
    {
        QC::u8 offset = 0x10 + bar * 4;
        QC::u32 value = readConfig32(addr, offset);

        if (value & 1)
        {
            // I/O BAR
            return value & ~0x3;
        }
        else
        {
            // Memory BAR
            if ((value & 0x6) == 0x4)
            {
                // 64-bit BAR
                QC::u32 high = readConfig32(addr, offset + 4);
                return (static_cast<QC::u64>(high) << 32) | (value & ~0xF);
            }
            return value & ~0xF;
        }
    }

    QC::usize PCI::getBARSize(PCIAddress addr, QC::u8 bar)
    {
        QC::u8 offset = 0x10 + bar * 4;
        QC::u32 original = readConfig32(addr, offset);

        writeConfig32(addr, offset, 0xFFFFFFFF);
        QC::u32 size = readConfig32(addr, offset);
        writeConfig32(addr, offset, original);

        if (original & 1)
        {
            // I/O BAR
            size = ~(size & ~0x3) + 1;
        }
        else
        {
            // Memory BAR
            size = ~(size & ~0xF) + 1;
        }

        return size;
    }

    bool PCI::isMMIOBar(PCIAddress addr, QC::u8 bar)
    {
        QC::u8 offset = 0x10 + bar * 4;
        return (readConfig32(addr, offset) & 1) == 0;
    }

    void PCI::enableBusMastering(PCIAddress addr)
    {
        QC::u16 command = readConfig16(addr, 0x04);
        command |= (1 << 2); // Bus Master Enable
        writeConfig16(addr, 0x04, command);
    }

    void PCI::enableMemorySpace(PCIAddress addr)
    {
        QC::u16 command = readConfig16(addr, 0x04);
        command |= (1 << 1); // Memory Space Enable
        writeConfig16(addr, 0x04, command);
    }

    void PCI::enableIOSpace(PCIAddress addr)
    {
        QC::u16 command = readConfig16(addr, 0x04);
        command |= (1 << 0); // I/O Space Enable
        writeConfig16(addr, 0x04, command);
    }

} // namespace QArch
