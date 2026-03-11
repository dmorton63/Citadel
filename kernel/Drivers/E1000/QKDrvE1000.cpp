// QKDrvE1000 - Intel e1000 NIC driver (minimal)
// Namespace: QKDrv::E1000

#include "E1000/QKDrvE1000.h"

#include "QCLogger.h"
#include "QKMemTranslator.h"
#include "QNetStack.h"
#include "QNetEthernet.h"

// DMA helpers (defined in QKMain.cpp)
extern "C" QC::PhysAddr earlyAllocatePage();
extern "C" QC::VirtAddr physToVirt(QC::PhysAddr phys);

namespace QKDrv::E1000
{

    static Controller *s_instance = nullptr;
    static QC::u8 s_controllerBuffer[sizeof(Controller)] __attribute__((aligned(16)));

    // Register offsets
    static constexpr QC::u32 REG_CTRL = 0x0000;
    static constexpr QC::u32 REG_STATUS = 0x0008;

    static constexpr QC::u32 REG_TDBAL = 0x3800;
    static constexpr QC::u32 REG_TDBAH = 0x3804;
    static constexpr QC::u32 REG_TDLEN = 0x3808;
    static constexpr QC::u32 REG_TDH = 0x3810;
    static constexpr QC::u32 REG_TDT = 0x3818;
    static constexpr QC::u32 REG_TCTL = 0x0400;
    static constexpr QC::u32 REG_TIPG = 0x0410;

    static constexpr QC::u32 REG_RDBAL = 0x2800;
    static constexpr QC::u32 REG_RDBAH = 0x2804;
    static constexpr QC::u32 REG_RDLEN = 0x2808;
    static constexpr QC::u32 REG_RDH = 0x2810;
    static constexpr QC::u32 REG_RDT = 0x2818;
    static constexpr QC::u32 REG_RCTL = 0x0100;

    static constexpr QC::u32 REG_RAL0 = 0x5400;
    static constexpr QC::u32 REG_RAH0 = 0x5404;

    // CTRL bits
    static constexpr QC::u32 CTRL_RST = (1u << 26);
    static constexpr QC::u32 CTRL_SLU = (1u << 6);

    // RCTL bits
    static constexpr QC::u32 RCTL_EN = (1u << 1);
    static constexpr QC::u32 RCTL_SBP = (1u << 2);
    static constexpr QC::u32 RCTL_UPE = (1u << 3);
    static constexpr QC::u32 RCTL_MPE = (1u << 4);
    static constexpr QC::u32 RCTL_LPE = (1u << 5);
    static constexpr QC::u32 RCTL_BAM = (1u << 15);
    static constexpr QC::u32 RCTL_SECRC = (1u << 26);

    // TCTL bits
    static constexpr QC::u32 TCTL_EN = (1u << 1);
    static constexpr QC::u32 TCTL_PSP = (1u << 3);

    // Tx cmd
    static constexpr QC::u8 TX_CMD_EOP = (1u << 0);
    static constexpr QC::u8 TX_CMD_IFCS = (1u << 1);
    static constexpr QC::u8 TX_CMD_RS = (1u << 3);

    // Tx status
    static constexpr QC::u8 TX_STATUS_DD = (1u << 0);

    // Rx status
    static constexpr QC::u8 RX_STATUS_DD = (1u << 0);
    static constexpr QC::u8 RX_STATUS_EOP = (1u << 1);

    Controller *Controller::probe(QArch::PCIDevice *pciDevice)
    {
        if (!pciDevice)
            return nullptr;

        // QEMU e1000 is typically 8086:100E
        const bool isE1000 = (pciDevice->vendorId == 0x8086) &&
                             (pciDevice->deviceId == 0x100E || pciDevice->deviceId == 0x100F || pciDevice->deviceId == 0x10D3);

        if (pciDevice->classCode != QArch::PCIClass::Network || pciDevice->subclass != 0x00)
        {
            return nullptr;
        }

        if (!isE1000)
        {
            // For now, only bind to known e1000 IDs.
            return nullptr;
        }

        if (s_instance)
        {
            return s_instance;
        }

        QC_LOG_INFO("e1000", "Found e1000 NIC: %04x:%04x", pciDevice->vendorId, pciDevice->deviceId);
        s_instance = new (s_controllerBuffer) Controller(pciDevice);
        return s_instance;
    }

    Controller *Controller::instance()
    {
        return s_instance;
    }

    Controller::Controller(QArch::PCIDevice *pciDevice)
        : m_pciDevice(pciDevice), m_mmioBase(0), m_initialized(false), m_rxRing(nullptr), m_rxRingPhys(0), m_rxTail(0),
          m_txRing(nullptr), m_txRingPhys(0), m_txTail(0)
    {
        for (QC::usize i = 0; i < 6; ++i)
            m_mac[i] = 0;
        for (QC::usize i = 0; i < RX_DESC_COUNT; ++i)
        {
            m_rxBufVirt[i] = nullptr;
            m_rxBufPhys[i] = 0;
        }
        for (QC::usize i = 0; i < TX_DESC_COUNT; ++i)
        {
            m_txBufVirt[i] = nullptr;
            m_txBufPhys[i] = 0;
        }
    }

    QC::u32 Controller::readReg(QC::u32 offset) const
    {
        return *reinterpret_cast<volatile QC::u32 *>(m_mmioBase + offset);
    }

    void Controller::writeReg(QC::u32 offset, QC::u32 value)
    {
        *reinterpret_cast<volatile QC::u32 *>(m_mmioBase + offset) = value;
    }

    void Controller::reset()
    {
        QC::u32 ctrl = readReg(REG_CTRL);
        writeReg(REG_CTRL, ctrl | CTRL_RST);

        // Wait a bit for reset to complete
        for (QC::usize i = 0; i < 100000; ++i)
        {
            if ((readReg(REG_CTRL) & CTRL_RST) == 0)
                break;
            asm volatile("pause");
        }

        // Set link up
        ctrl = readReg(REG_CTRL);
        writeReg(REG_CTRL, ctrl | CTRL_SLU);
    }

    void Controller::readMACFromRegisters()
    {
        QC::u32 ral = readReg(REG_RAL0);
        QC::u32 rah = readReg(REG_RAH0);

        m_mac[0] = ral & 0xFF;
        m_mac[1] = (ral >> 8) & 0xFF;
        m_mac[2] = (ral >> 16) & 0xFF;
        m_mac[3] = (ral >> 24) & 0xFF;
        m_mac[4] = rah & 0xFF;
        m_mac[5] = (rah >> 8) & 0xFF;

        QC_LOG_INFO("e1000", "MAC %02x:%02x:%02x:%02x:%02x:%02x",
                    m_mac[0], m_mac[1], m_mac[2], m_mac[3], m_mac[4], m_mac[5]);

        // Export MAC to the software network stack.
        QNet::Stack::instance().initialize();
        if (auto *eth = QNet::Stack::instance().ethernet())
        {
            QNet::MACAddress mac{};
            for (int i = 0; i < 6; ++i)
                mac.bytes[i] = m_mac[i];
            eth->setMACAddress(mac);
        }
    }

    void Controller::setupReceiveAddress()
    {
        QC::u32 ral = static_cast<QC::u32>(m_mac[0]) |
                      (static_cast<QC::u32>(m_mac[1]) << 8) |
                      (static_cast<QC::u32>(m_mac[2]) << 16) |
                      (static_cast<QC::u32>(m_mac[3]) << 24);
        QC::u32 rah = static_cast<QC::u32>(m_mac[4]) |
                      (static_cast<QC::u32>(m_mac[5]) << 8) |
                      (1u << 31); // AV

        writeReg(REG_RAL0, ral);
        writeReg(REG_RAH0, rah);
    }

    void Controller::initRX()
    {
        // Allocate RX ring
        m_rxRingPhys = earlyAllocatePage();
        m_rxRing = reinterpret_cast<RxDesc *>(physToVirt(m_rxRingPhys));

        // Zero ring
        for (QC::usize i = 0; i < (4096 / sizeof(RxDesc)); ++i)
        {
            m_rxRing[i].addr = 0;
            m_rxRing[i].status = 0;
            m_rxRing[i].length = 0;
            m_rxRing[i].errors = 0;
        }

        for (QC::usize i = 0; i < RX_DESC_COUNT; ++i)
        {
            m_rxBufPhys[i] = earlyAllocatePage();
            m_rxBufVirt[i] = reinterpret_cast<QC::u8 *>(physToVirt(m_rxBufPhys[i]));
            m_rxRing[i].addr = m_rxBufPhys[i];
            m_rxRing[i].status = 0;
        }

        writeReg(REG_RDBAL, static_cast<QC::u32>(m_rxRingPhys & 0xFFFFFFFF));
        writeReg(REG_RDBAH, static_cast<QC::u32>((m_rxRingPhys >> 32) & 0xFFFFFFFF));
        writeReg(REG_RDLEN, RX_DESC_COUNT * sizeof(RxDesc));
        writeReg(REG_RDH, 0);
        writeReg(REG_RDT, RX_DESC_COUNT - 1);

        m_rxTail = RX_DESC_COUNT - 1;

        setupReceiveAddress();

        // Enable receiver
        QC::u32 rctl = 0;
        rctl |= RCTL_EN;
        rctl |= RCTL_BAM;   // accept broadcast
        rctl |= RCTL_SECRC; // strip CRC
        rctl |= RCTL_UPE;   // unicast promiscuous (simple bring-up)
        rctl |= RCTL_MPE;   // multicast promiscuous

        writeReg(REG_RCTL, rctl);
    }

    void Controller::initTX()
    {
        // Allocate TX ring
        m_txRingPhys = earlyAllocatePage();
        m_txRing = reinterpret_cast<TxDesc *>(physToVirt(m_txRingPhys));

        for (QC::usize i = 0; i < (4096 / sizeof(TxDesc)); ++i)
        {
            m_txRing[i].addr = 0;
            m_txRing[i].status = TX_STATUS_DD;
            m_txRing[i].cmd = 0;
            m_txRing[i].length = 0;
        }

        for (QC::usize i = 0; i < TX_DESC_COUNT; ++i)
        {
            m_txBufPhys[i] = earlyAllocatePage();
            m_txBufVirt[i] = reinterpret_cast<QC::u8 *>(physToVirt(m_txBufPhys[i]));
            m_txRing[i].addr = m_txBufPhys[i];
            m_txRing[i].status = TX_STATUS_DD;
        }

        writeReg(REG_TDBAL, static_cast<QC::u32>(m_txRingPhys & 0xFFFFFFFF));
        writeReg(REG_TDBAH, static_cast<QC::u32>((m_txRingPhys >> 32) & 0xFFFFFFFF));
        writeReg(REG_TDLEN, TX_DESC_COUNT * sizeof(TxDesc));
        writeReg(REG_TDH, 0);
        writeReg(REG_TDT, 0);

        m_txTail = 0;

        // Enable transmitter
        QC::u32 tctl = 0;
        tctl |= TCTL_EN;
        tctl |= TCTL_PSP;
        tctl |= (0x10u << 4);  // CT
        tctl |= (0x40u << 12); // COLD
        writeReg(REG_TCTL, tctl);

        // IPG
        writeReg(REG_TIPG, 0x0060200A);
    }

    QC::Status Controller::initialize()
    {
        if (m_initialized)
            return QC::Status::Success;

        if (!m_pciDevice)
            return QC::Status::Error;

        QArch::PCI::instance().enableBusMastering(m_pciDevice->address);
        QArch::PCI::instance().enableMemorySpace(m_pciDevice->address);

        QC::PhysAddr bar0 = m_pciDevice->bar[0];
        if (bar0 == 0)
        {
            QC_LOG_ERROR("e1000", "BAR0 is zero");
            return QC::Status::Error;
        }

        QC_LOG_INFO("e1000", "BAR0 at 0x%lx", bar0);

        // 128KB is plenty for e1000 register space
        m_mmioBase = QK::Memory::Translator::instance().mapMMIO(bar0, 0x20000);
        if (!m_mmioBase)
        {
            QC_LOG_ERROR("e1000", "Failed to map MMIO");
            return QC::Status::OutOfMemory;
        }

        reset();
        readMACFromRegisters();

        initRX();
        initTX();

        m_initialized = true;
        QC_LOG_INFO("e1000", "Initialized (STATUS=0x%x)", readReg(REG_STATUS));
        return QC::Status::Success;
    }

    void Controller::shutdown()
    {
        // TODO: disable RX/TX and unmap MMIO
        m_initialized = false;
    }

    void Controller::transmitCallback(const void *data, QC::usize length)
    {
        if (s_instance)
        {
            s_instance->transmit(data, length);
        }
    }

    void Controller::transmit(const void *data, QC::usize length)
    {
        if (!m_initialized || !data || length == 0)
            return;

        if (length > RX_BUF_SIZE)
        {
            // For now, drop jumbo frames.
            return;
        }

        const QC::u32 idx = m_txTail % TX_DESC_COUNT;

        // If descriptor not done, drop (ring full)
        if ((m_txRing[idx].status & TX_STATUS_DD) == 0)
        {
            return;
        }

        // Copy into DMA buffer
        QC::u8 *dst = m_txBufVirt[idx];
        const QC::u8 *src = static_cast<const QC::u8 *>(data);
        for (QC::usize i = 0; i < length; ++i)
            dst[i] = src[i];

        m_txRing[idx].length = static_cast<QC::u16>(length);
        m_txRing[idx].cmd = static_cast<QC::u8>(TX_CMD_EOP | TX_CMD_IFCS | TX_CMD_RS);
        m_txRing[idx].status = 0;

        m_txTail = (m_txTail + 1) % TX_DESC_COUNT;
        writeReg(REG_TDT, m_txTail);
    }

    void Controller::poll()
    {
        if (!m_initialized)
            return;

        // Process received packets
        while (true)
        {
            const QC::u32 next = (m_rxTail + 1) % RX_DESC_COUNT;
            RxDesc &desc = m_rxRing[next];

            if ((desc.status & RX_STATUS_DD) == 0)
                break;

            const bool eop = (desc.status & RX_STATUS_EOP) != 0;
            if (!eop)
            {
                // Not handling multi-descriptor frames yet.
                desc.status = 0;
                m_rxTail = next;
                writeReg(REG_RDT, m_rxTail);
                continue;
            }

            const QC::u16 len = desc.length;
            if (len > 0 && len <= RX_BUF_SIZE)
            {
                QNet::Stack::instance().receivePacket(m_rxBufVirt[next], len);
            }

            desc.status = 0;
            m_rxTail = next;
            writeReg(REG_RDT, m_rxTail);
        }
    }

} // namespace QKDrv::E1000
