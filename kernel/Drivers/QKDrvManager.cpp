// QKDrvManager - Driver Manager implementation
// Namespace: QKDrv

#include "QKDrvManager.h"
#include "PS2/QKDrvPS2Mouse.h"
#include "PS2/QKDrvPS2Keyboard.h"
#include "UHCI/QKDrvUHCI.h"
#include "XHCI/xhci.h"
#include "E1000/QKDrvE1000.h"
#include "IDE/QKDrvIDE.h"
#include "QNetStack.h"
#include "QArchPCI.h"
#include "QFSVolumeManager.h"
#include "QCLogger.h"

namespace QKDrv
{

    Manager &Manager::instance()
    {
        static Manager inst;
        return inst;
    }

    void Manager::initialize()
    {
        QC_LOG_INFO("QKDrv", "Initializing driver manager");

        m_mouseDriver = nullptr;
        m_keyboardDriver = nullptr;
        // Note: m_screenWidth/m_screenHeight should be set by setScreenSize() before initialize()

        // Probe for USB controllers first (preferred for pointer devices)
        probeUSB();

        // Always probe PS/2 as fallback
        probePS2();

        // Probe for network devices
        probeNetwork();

        // Probe for legacy storage volumes (e.g., QEMU vvfat shared folder)
        probeStorage();

        if (m_mouseDriver)
        {
            QC_LOG_INFO("QKDrv", "Active mouse driver: %s", m_mouseDriver->name());
        }
        else
        {
            QC_LOG_WARN("QKDrv", "No mouse driver available");
        }

        if (m_keyboardDriver)
        {
            QC_LOG_INFO("QKDrv", "Active keyboard driver: %s", m_keyboardDriver->name());
        }
        else
        {
            QC_LOG_WARN("QKDrv", "No keyboard driver available");
        }

        // Attempt to mount any auto-mount volumes that drivers may have registered
        QFS::VolumeManager::instance().mountPending();
    }

    void Manager::probeStorage()
    {
        // Prefer a persistent system volume if present.
        IDE::probeAndRegisterSystemVolume();
        IDE::probeAndRegisterSharedVolume();
    }

    void Manager::shutdown()
    {
        QC_LOG_INFO("QKDrv", "Shutting down driver manager");

        for (QC::usize i = 0; i < m_controllers.size(); ++i)
        {
            m_controllers[i]->shutdown();
        }

        m_controllers.clear();
        m_mouseDriver = nullptr;
        m_keyboardDriver = nullptr;
    }

    void Manager::probePS2()
    {
        QC_LOG_INFO("QKDrv", "Probing PS/2 devices");

        // Initialize PS/2 keyboard
        PS2::Keyboard &keyboard = PS2::Keyboard::instance();
        if (keyboard.initialize() == QC::Status::Success)
        {
            m_controllers.push_back(&keyboard);
            if (!m_keyboardDriver)
            {
                m_keyboardDriver = &keyboard;
            }
        }

        // Initialize PS/2 mouse - but skip if we already have a USB pointer device
        if (!m_mouseDriver)
        {
            PS2::Mouse &mouse = PS2::Mouse::instance();
            if (mouse.initialize() == QC::Status::Success)
            {
                m_controllers.push_back(&mouse);
                // Only use PS/2 mouse if no USB mouse/tablet available
                if (!m_mouseDriver)
                {
                    m_mouseDriver = &mouse;
                    QC_LOG_INFO("QKDrv", "Setting mouse bounds to %ux%u", m_screenWidth, m_screenHeight);
                    mouse.setBounds(0, 0, m_screenWidth - 1, m_screenHeight - 1);
                }
            }
        }
        else
        {
            QC_LOG_INFO("QKDrv", "Skipping PS/2 mouse - USB pointer device available");
        }
    }

    void Manager::probeUSB()
    {
        QC_LOG_INFO("QKDrv", "Probing USB controllers");

        QArch::PCI &pci = QArch::PCI::instance();
        QC_LOG_INFO("QKDrv", "Scanning %lu PCI devices", pci.devices().size());

        // Find xHCI controllers (USB 3.0) - preferred
        for (QC::usize i = 0; i < pci.devices().size(); ++i)
        {
            QC_LOG_INFO("QKDrv", "Checking PCI device %lu", i);
            QArch::PCIDevice &dev = const_cast<QArch::PCIDevice &>(pci.devices()[i]);
            QC_LOG_INFO("QKDrv", "Device class=%02x subclass=%02x progIF=%02x",
                        static_cast<QC::u8>(dev.classCode), dev.subclass, dev.progIF);
            XHCI::XHCIController *xhci = XHCI::xhci_init(&dev);
            if (xhci)
            {
                QC_LOG_INFO("QKDrv", "Initializing xHCI controller...");
                QC::Status status = xhci->initialize();
                QC_LOG_INFO("QKDrv", "xHCI initialize returned %d", static_cast<int>(status));
                if (status == QC::Status::Success)
                {
                    m_controllers.push_back(xhci);
                    xhci->setScreenSize(m_screenWidth, m_screenHeight);

                    // Prefer USB keyboard when available
                    if (xhci->hasKeyboard() && xhci->keyboardDriver())
                    {
                        QC_LOG_INFO("QKDrv", "Using USB keyboard as keyboard driver");
                        m_keyboardDriver = xhci->keyboardDriver();
                    }

                    // Prefer USB tablet (absolute), then USB mouse (relative)
                    if (xhci->hasTablet() && xhci->tabletDriver())
                    {
                        QC_LOG_INFO("QKDrv", "Using USB tablet as mouse driver");
                        m_mouseDriver = xhci->tabletDriver();
                        if (m_mouseDriver)
                        {
                            QC_LOG_INFO("QKDrv", "Setting tablet bounds to %ux%u", m_screenWidth, m_screenHeight);
                            m_mouseDriver->setBounds(0, 0, m_screenWidth - 1, m_screenHeight - 1);
                        }
                    }
                    else if (xhci->hasMouse() && xhci->mouseDriver())
                    {
                        QC_LOG_INFO("QKDrv", "Using USB mouse as mouse driver");
                        m_mouseDriver = xhci->mouseDriver();
                        if (m_mouseDriver)
                        {
                            QC_LOG_INFO("QKDrv", "Setting mouse bounds to %ux%u", m_screenWidth, m_screenHeight);
                            m_mouseDriver->setBounds(0, 0, m_screenWidth - 1, m_screenHeight - 1);
                        }
                    }
                }
                else
                {
                    delete xhci;
                }
            }
        }

        // Find UHCI controllers (USB 1.1)
        for (QC::usize i = 0; i < pci.devices().size(); ++i)
        {
            QArch::PCIDevice &dev = const_cast<QArch::PCIDevice &>(pci.devices()[i]);
            UHCI::Controller *uhci = UHCI::Controller::probe(&dev);
            if (uhci)
            {
                if (uhci->initialize() == QC::Status::Success)
                {
                    m_controllers.push_back(uhci);
                    uhci->setScreenSize(m_screenWidth, m_screenHeight);

                    // If this controller has a tablet, prefer it
                    if (uhci->hasTablet())
                    {
                        QC_LOG_INFO("QKDrv", "UHCI controller has USB tablet");
                        // TODO: Set uhci as mouse driver when tablet support is complete
                    }
                }
                else
                {
                    delete uhci;
                }
            }
        }
    }

    void Manager::probeNetwork()
    {
        QC_LOG_INFO("QKDrv", "Probing network controllers");

        // Bring up the software network stack before handing it frames.
        QNet::Stack::instance().initialize();

        QArch::PCI &pci = QArch::PCI::instance();
        for (QC::usize i = 0; i < pci.devices().size(); ++i)
        {
            QArch::PCIDevice &dev = const_cast<QArch::PCIDevice &>(pci.devices()[i]);
            if (dev.classCode != QArch::PCIClass::Network)
                continue;

            E1000::Controller *nic = E1000::Controller::probe(&dev);
            if (nic)
            {
                if (nic->initialize() == QC::Status::Success)
                {
                    m_controllers.push_back(nic);
                    QC_LOG_INFO("QKDrv", "Using e1000 NIC");

                    // Wire QNetwork TX -> NIC.
                    QNet::Stack::setTransmitCallback(&E1000::Controller::transmitCallback);
                }
            }
        }
    }

    void Manager::setScreenSize(QC::u32 width, QC::u32 height)
    {
        m_screenWidth = width;
        m_screenHeight = height;

        // Update all mouse drivers
        if (m_mouseDriver)
        {
            m_mouseDriver->setBounds(0, 0, width - 1, height - 1);
        }
    }

    void Manager::poll()
    {
        for (QC::usize i = 0; i < m_controllers.size(); ++i)
        {
            m_controllers[i]->poll();
        }

        // Give storage drivers a chance to surface devices asynchronously
        QFS::VolumeManager::instance().mountPending();
    }

} // namespace QKDrv
