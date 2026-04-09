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
#include "QCString.h"

namespace QKDrv
{

    namespace
    {
        static bool hasDriverIdPrefix(const char *id)
        {
            if (!id)
                return false;
            constexpr const char prefix[] = "QDRV_";
            for (QC::usize i = 0; prefix[i] != '\0'; ++i)
            {
                if (id[i] == '\0' || id[i] != prefix[i])
                    return false;
            }
            return true;
        }
    }

    Manager &Manager::instance()
    {
        static Manager inst;
        return inst;
    }

    void Manager::initialize()
    {
        QC_LOG_INFO("QKDrv", "Initializing driver manager");

        runModuleInitHooks();

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
            QC_LOG_INFO("QKDrv", "Active mouse driver: %s (%s)", m_mouseDriver->name(),
                        m_mouseDriver->driverId() ? m_mouseDriver->driverId() : "<null>");
        }
        else
        {
            QC_LOG_WARN("QKDrv", "No mouse driver available");
        }

        if (m_keyboardDriver)
        {
            QC_LOG_INFO("QKDrv", "Active keyboard driver: %s (%s)", m_keyboardDriver->name(),
                        m_keyboardDriver->driverId() ? m_keyboardDriver->driverId() : "<null>");
        }
        else
        {
            QC_LOG_WARN("QKDrv", "No keyboard driver available");
        }

        runModuleStartHooks();

        // Attempt to mount any auto-mount volumes that drivers may have registered
        QFS::VolumeManager::instance().mountPending();
    }

    void Manager::probeStorage()
    {
        // Prefer a persistent system volume if present.
        IDE::probeAndRegisterSystemVolume();
        IDE::probeAndRegisterSharedVolume();
        IDE::probeAndRegisterDataVolumes();
    }

    void Manager::shutdown()
    {
        QC_LOG_INFO("QKDrv", "Shutting down driver manager");

        runModuleStopHooks();

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
            if (!hasDriverIdPrefix(keyboard.driverId()))
                QC_LOG_WARN("QKDrv", "Driver id %s must start with QDRV_", keyboard.driverId() ? keyboard.driverId() : "<null>");
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
                if (!hasDriverIdPrefix(mouse.driverId()))
                    QC_LOG_WARN("QKDrv", "Driver id %s must start with QDRV_", mouse.driverId() ? mouse.driverId() : "<null>");
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
                    if (!hasDriverIdPrefix(xhci->driverId()))
                        QC_LOG_WARN("QKDrv", "Driver id %s must start with QDRV_", xhci->driverId() ? xhci->driverId() : "<null>");
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
                    if (!hasDriverIdPrefix(uhci->driverId()))
                        QC_LOG_WARN("QKDrv", "Driver id %s must start with QDRV_", uhci->driverId() ? uhci->driverId() : "<null>");
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
                    if (!hasDriverIdPrefix(nic->driverId()))
                        QC_LOG_WARN("QKDrv", "Driver id %s must start with QDRV_", nic->driverId() ? nic->driverId() : "<null>");
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

    bool Manager::registerLifecycleHooks(const char *moduleId,
                                         ModuleLifecycleFn initFn,
                                         ModuleLifecycleFn startFn,
                                         ModuleLifecycleFn stopFn,
                                         void *userData)
    {
        if (!moduleId || !*moduleId)
            return false;
        for (QC::usize i = 0; i < m_moduleHooks.size(); ++i)
        {
            const char *id = m_moduleHooks[i].moduleId;
            if (id && QC::String::strcmp(id, moduleId) == 0)
                return false;
        }

        ModuleLifecycle hook;
        hook.moduleId = moduleId;
        hook.initFn = initFn;
        hook.startFn = startFn;
        hook.stopFn = stopFn;
        hook.userData = userData;
        m_moduleHooks.push_back(hook);
        return true;
    }

    bool Manager::unregisterLifecycleHooks(const char *moduleId)
    {
        if (!moduleId || !*moduleId)
            return false;
        for (QC::usize i = 0; i < m_moduleHooks.size(); ++i)
        {
            const char *id = m_moduleHooks[i].moduleId;
            if (!id || QC::String::strcmp(id, moduleId) != 0)
                continue;

            for (QC::usize j = i + 1; j < m_moduleHooks.size(); ++j)
                m_moduleHooks[j - 1] = m_moduleHooks[j];
            if (!m_moduleHooks.empty())
                m_moduleHooks.pop_back();
            return true;
        }
        return false;
    }

    void Manager::runModuleInitHooks()
    {
        for (QC::usize i = 0; i < m_moduleHooks.size(); ++i)
        {
            ModuleLifecycle &hook = m_moduleHooks[i];
            if (!hook.initFn || hook.initialized)
                continue;

            const QC::Status st = hook.initFn(hook.userData);
            if (st == QC::Status::Success)
            {
                hook.initialized = true;
            }
            else
            {
                QC_LOG_WARN("QKDrv", "Module %s init hook failed", hook.moduleId ? hook.moduleId : "<null>");
            }
        }
    }

    void Manager::runModuleStartHooks()
    {
        for (QC::usize i = 0; i < m_moduleHooks.size(); ++i)
        {
            ModuleLifecycle &hook = m_moduleHooks[i];
            if (!hook.startFn || !hook.initialized || hook.started)
                continue;

            const QC::Status st = hook.startFn(hook.userData);
            if (st == QC::Status::Success)
            {
                hook.started = true;
            }
            else
            {
                QC_LOG_WARN("QKDrv", "Module %s start hook failed", hook.moduleId ? hook.moduleId : "<null>");
            }
        }
    }

    void Manager::runModuleStopHooks()
    {
        for (QC::usize i = m_moduleHooks.size(); i > 0; --i)
        {
            ModuleLifecycle &hook = m_moduleHooks[i - 1];
            if (!hook.stopFn || !hook.started)
                continue;

            const QC::Status st = hook.stopFn(hook.userData);
            if (st != QC::Status::Success)
                QC_LOG_WARN("QKDrv", "Module %s stop hook failed", hook.moduleId ? hook.moduleId : "<null>");

            hook.started = false;
        }
    }

} // namespace QKDrv
