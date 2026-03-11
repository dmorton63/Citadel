// QKDrvUHCI - USB 1.1 UHCI Controller implementation
// Namespace: QKDrv::UHCI

#include "QKDrvUHCI.h"
#include "QArchCPU.h"
#include "QArchPort.h"
#include "QCLogger.h"
#include "QCString.h"

// Early page allocator (defined in QKMain.cpp, identity-mapped)
extern "C" QC::PhysAddr earlyAllocatePage();

namespace QKDrv
{
    namespace UHCI
    {

        // Helper: allocate a DMA page
        static QC::PhysAddr allocateDMAPage()
        {
            return earlyAllocatePage();
        }

        Controller *Controller::probe(QArch::PCIDevice *pciDevice)
        {
            // Check for UHCI: class 0x0C, subclass 0x03, progIF 0x00
            if (pciDevice->classCode == QArch::PCIClass::SerialBus &&
                pciDevice->subclass == 0x03 &&
                pciDevice->progIF == 0x00)
            {
                return new Controller(pciDevice);
            }
            return nullptr;
        }

        Controller::Controller(QArch::PCIDevice *pciDevice)
            : m_pciDevice(pciDevice), m_ioBase(0), m_frameList(nullptr), m_asyncQH(nullptr), m_mouseCallback(nullptr), m_screenWidth(1024), m_screenHeight(768), m_isTablet(false)
        {
        }

        Controller::~Controller()
        {
            shutdown();
        }

        QC::Status Controller::initialize()
        {
            QC_LOG_INFO("UHCI", "Initializing UHCI controller");

            // Get I/O base from BAR4
            m_ioBase = static_cast<QC::u16>(m_pciDevice->bar[4] & ~0x3);

            // Enable bus mastering and I/O space
            QArch::PCI::instance().enableBusMastering(m_pciDevice->address);
            QArch::PCI::instance().enableIOSpace(m_pciDevice->address);

            // Global reset
            writeReg16(Regs::USBCMD, 0x04);
            for (int i = 0; i < 100000; ++i)
            {
                cpu_relax();
            }
            writeReg16(Regs::USBCMD, 0);
            // Host controller reset
            writeReg16(Regs::USBCMD, 0x02);
            while (readReg16(Regs::USBCMD) & 0x02)
                ;

            // Initialize frame list
            initializeFrameList();

            // Set frame list base address (identity mapped: virt == phys)
            writeReg32(Regs::FRBASEADD, static_cast<QC::u32>(
                                            reinterpret_cast<QC::uptr>(m_frameList)));

            // Set SOF timing
            writeReg16(Regs::SOFMOD, 64);

            // Clear frame number
            writeReg16(Regs::FRNUM, 0);

            // Enable interrupts
            writeReg16(Regs::USBINTR, 0x0F);

            // Start controller
            writeReg16(Regs::USBCMD, 0x01);

            QC_LOG_INFO("UHCI", "UHCI controller initialized at I/O 0x%x", m_ioBase);

            // Probe for devices
            probeDevices();

            return QC::Status::Success;
        }

        void Controller::shutdown()
        {
            writeReg16(Regs::USBCMD, 0);
            QC_LOG_INFO("UHCI", "UHCI controller shutdown");
        }

        void Controller::poll()
        {
            // Check status and handle any pending transfers
            QC::u16 status = readReg16(Regs::USBSTS);
            if (status & 0x01)
            {
                // Transfer complete - process results
                writeReg16(Regs::USBSTS, 0x01); // Clear interrupt
            }
        }

        void Controller::initializeFrameList()
        {
            // Allocate frame list (4KB, 1024 entries)
            QC::PhysAddr frameListPhys = allocateDMAPage();
            m_frameList = reinterpret_cast<QC::u32 *>(frameListPhys);

            // Allocate async QH
            m_asyncQH = allocateQH();
            m_asyncQH->headLink = 1;    // Terminate
            m_asyncQH->elementLink = 1; // Terminate

            // Point all frame list entries to async QH (identity mapped: virt == phys)
            QC::u32 asyncQHPhys = static_cast<QC::u32>(
                reinterpret_cast<QC::uptr>(m_asyncQH));
            for (int i = 0; i < 1024; ++i)
            {
                m_frameList[i] = asyncQHPhys | 0x02; // QH type
            }
        }

        void Controller::probeDevices()
        {
            // Check each port for connected devices
            for (QC::u8 port = 0; port < 2; ++port)
            {
                if (isPortConnected(port))
                {
                    QC_LOG_INFO("UHCI", "Device connected on port %u", port);
                    resetPort(port);
                    // TODO: Enumerate device and check if it's a tablet
                }
            }
        }

        void Controller::setScreenSize(QC::u32 width, QC::u32 height)
        {
            m_screenWidth = width;
            m_screenHeight = height;
        }

        QC::u16 Controller::readReg16(QC::u16 offset)
        {
            return QArch::inw(m_ioBase + offset);
        }

        void Controller::writeReg16(QC::u16 offset, QC::u16 value)
        {
            QArch::outw(m_ioBase + offset, value);
        }

        QC::u32 Controller::readReg32(QC::u16 offset)
        {
            return QArch::inl(m_ioBase + offset);
        }

        void Controller::writeReg32(QC::u16 offset, QC::u32 value)
        {
            QArch::outl(m_ioBase + offset, value);
        }

        bool Controller::isPortConnected(QC::u8 port) const
        {
            if (port >= 2)
                return false;
            QC::u16 portReg = (port == 0) ? Regs::PORTSC1 : Regs::PORTSC2;
            return (const_cast<Controller *>(this)->readReg16(portReg) & 0x01) != 0;
        }

        Speed Controller::getPortSpeed(QC::u8 port) const
        {
            if (port >= 2)
                return Speed::Full;
            QC::u16 portReg = (port == 0) ? Regs::PORTSC1 : Regs::PORTSC2;
            QC::u16 status = const_cast<Controller *>(this)->readReg16(portReg);
            return (status & 0x100) ? Speed::Low : Speed::Full;
        }

        void Controller::resetPort(QC::u8 port)
        {
            if (port >= 2)
                return;

            QC::u16 portReg = (port == 0) ? Regs::PORTSC1 : Regs::PORTSC2;

            // Assert reset
            writeReg16(portReg, 0x0200);

            // Wait 50ms
            for (int i = 0; i < 500000; ++i)
            {
                cpu_relax();
            }

            // Clear reset
            writeReg16(portReg, 0);

            // Wait for device to recover
            for (int i = 0; i < 100000; ++i)
            {
                cpu_relax();
            }

            // Enable port
            QC::u16 status = readReg16(portReg);
            writeReg16(portReg, status | 0x04);
        }

        TD *Controller::allocateTD()
        {
            QC::PhysAddr addr = allocateDMAPage();
            TD *td = reinterpret_cast<TD *>(addr);
            QC::String::memset(td, 0, sizeof(TD));
            return td;
        }

        void Controller::freeTD(TD *td)
        {
            // TODO: Return to pool
            (void)td;
        }

        QH *Controller::allocateQH()
        {
            QC::PhysAddr addr = allocateDMAPage();
            QH *qh = reinterpret_cast<QH *>(addr);
            QC::String::memset(qh, 0, sizeof(QH));
            return qh;
        }

        void Controller::freeQH(QH *qh)
        {
            // TODO: Return to pool
            (void)qh;
        }

    } // namespace UHCI
} // namespace QKDrv
