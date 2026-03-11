// QKernel Shutdown Controller - implementation
// Namespace: QK::Shutdown

#include "QKShutdownController.h"

#include "QCLogger.h"
#include "QCBuiltins.h"
#include "QKEventManager.h"

namespace QK
{
    namespace Shutdown
    {
        namespace
        {
            constexpr const char *LOG_MODULE = "QShutdown";

            inline void postShutdownPhaseEvent(QK::Event::Type type, Reason reason)
            {
                auto &eventMgr = QK::Event::EventManager::instance();
                if (!eventMgr.isInitialized())
                {
                    return;
                }
                eventMgr.postShutdownEvent(type, static_cast<QC::u32>(reason));
            }

            bool handleShutdownRequestEvent(const QK::Event::Event &event, void *)
            {
                if (event.type() != QK::Event::Type::ShutdownRequest)
                {
                    return false;
                }

                Reason reason = static_cast<Reason>(event.asShutdown().reasonCode);
                Controller::instance().requestShutdown(reason);
                return true;
            }
        }

        Controller &Controller::instance()
        {
            static Controller controller;
            return controller;
        }

        Controller::Controller()
            : m_phase(Phase::Idle),
              m_reason(Reason::UserRequest),
              m_uiHandler(nullptr),
              m_uiUserData(nullptr),
              m_nextSubsystemHandle(1)
        {
            auto &eventMgr = QK::Event::EventManager::instance();
            if (eventMgr.isInitialized())
            {
                auto listenerId = eventMgr.addListener(QK::Event::Type::ShutdownRequest, handleShutdownRequestEvent, nullptr);
                if (listenerId == QK::Event::InvalidListenerId)
                {
                    QC_LOG_WARN(LOG_MODULE, "Failed to register shutdown request listener");
                }
                else
                {
                    QC_LOG_INFO(LOG_MODULE, "Shutdown request listener registered (id=%u)", static_cast<QC::u32>(listenerId));
                }
            }
            else
            {
                QC_LOG_WARN(LOG_MODULE, "EventManager not initialized; shutdown events unavailable");
            }
        }

        void Controller::registerUIHandler(UIRequestHandler handler, void *userData)
        {
            m_uiHandler = handler;
            m_uiUserData = userData;
        }

        SubsystemHandle Controller::registerSubsystem(SubsystemCallback callback, void *userData, const char *name)
        {
            if (!callback)
                return InvalidSubsystemHandle;

            SubsystemEntry entry;
            entry.handle = m_nextSubsystemHandle++;
            entry.callback = callback;
            entry.userData = userData;
            entry.name = name;
            m_subsystems.push_back(entry);

            QC_LOG_INFO(LOG_MODULE, "Registered shutdown subsystem '%s' (handle=%u)",
                        entry.name ? entry.name : "anonymous",
                        static_cast<QC::u32>(entry.handle));
            return entry.handle;
        }

        void Controller::unregisterSubsystem(SubsystemHandle handle)
        {
            if (handle == InvalidSubsystemHandle)
                return;

            for (QC::usize i = 0; i < m_subsystems.size(); ++i)
            {
                if (m_subsystems[i].handle == handle)
                {
                    QC_LOG_INFO(LOG_MODULE, "Unregistered shutdown subsystem '%s' (handle=%u)",
                                m_subsystems[i].name ? m_subsystems[i].name : "anonymous",
                                static_cast<QC::u32>(handle));

                    // Move last entry into this slot to keep vector packed
                    if (i != m_subsystems.size() - 1)
                    {
                        m_subsystems[i] = m_subsystems.back();
                    }
                    m_subsystems.pop_back();
                    return;
                }
            }
        }

        void Controller::requestShutdown(Reason reason)
        {
            if (m_phase != Phase::Idle)
            {
                QC_LOG_WARN(LOG_MODULE, "Shutdown already in progress (phase=%u)", static_cast<QC::u32>(m_phase));
                return;
            }

            m_reason = reason;
            QC_LOG_INFO(LOG_MODULE, "Shutdown requested (reason=%u)", static_cast<QC::u32>(reason));

            m_phase = Phase::NotifyingSubsystems;
            postShutdownPhaseEvent(QK::Event::Type::ShutdownPrepare, reason);
            if (!notifySubsystems())
            {
                QC_LOG_WARN(LOG_MODULE, "Subsystem veto detected; shutdown aborted");
                reset();
                return;
            }

            m_phase = Phase::AwaitingUserDecision;

            if (m_uiHandler)
            {
                if (!m_uiHandler(reason, m_uiUserData))
                {
                    QC_LOG_WARN(LOG_MODULE, "UI handler declined shutdown prompt; proceeding automatically");
                    confirm(UserChoice::Proceed);
                }
                return;
            }

            QC_LOG_INFO(LOG_MODULE, "No UI handler registered; proceeding directly to power-off");
            confirm(UserChoice::Proceed);
        }

        void Controller::confirm(UserChoice choice)
        {
            if (m_phase == Phase::Idle)
            {
                QC_LOG_WARN(LOG_MODULE, "confirm() called with no pending shutdown");
                return;
            }

            switch (choice)
            {
            case UserChoice::Cancel:
                QC_LOG_INFO(LOG_MODULE, "Shutdown request canceled");
                reset();
                return;

            case UserChoice::Proceed:
                break;
            }

            m_phase = Phase::PoweringOff;
            postShutdownPhaseEvent(QK::Event::Type::ShutdownNow, m_reason);
            powerOffHardware();
        }

        bool Controller::notifySubsystems()
        {
            if (m_subsystems.empty())
            {
                QC_LOG_INFO(LOG_MODULE, "No registered subsystems to notify");
                return true;
            }

            for (QC::usize i = 0; i < m_subsystems.size(); ++i)
            {
                auto &entry = m_subsystems[i];
                if (!entry.callback)
                    continue;

                QC_LOG_INFO(LOG_MODULE, "Requesting shutdown approval from '%s'", entry.name ? entry.name : "anonymous");
                bool ok = entry.callback(m_reason, entry.userData);
                if (!ok)
                {
                    QC_LOG_WARN(LOG_MODULE, "Subsystem '%s' rejected shutdown", entry.name ? entry.name : "anonymous");
                    return false;
                }
                QC_LOG_INFO(LOG_MODULE, "Subsystem '%s' approved shutdown", entry.name ? entry.name : "anonymous");
            }

            return true;
        }

        void Controller::powerOffHardware()
        {
            QC_LOG_INFO(LOG_MODULE, "Issuing ACPI power-off sequence");

            // Common hypervisor/firmware shutdown ports.
            // QEMU (PC/i440fx) often reacts to 0x604; some setups require the full S5 value 0x3400.
            // Bochs:               0xB004
            // VirtualBox:          0x4004 (expects 0x3400)
            // QEMU debug-exit:     0xF4 (requires -device isa-debug-exit)
            QC::outw(0x604, 0x2000);
            QC::outw(0x604, 0x3400);
            QC::outw(0xB004, 0x2000);
            QC::outw(0xB004, 0x3400);
            QC::outw(0x4004, 0x3400);

            // If QEMU is launched with isa-debug-exit, this will immediately terminate QEMU.
            // Note: the device exits only when bit0 is set.
            QC::outl(0xF4, 0x11);

            QC::cli();
            for (;;)
            {
                QC::halt();
            }
        }

        void Controller::reset()
        {
            m_phase = Phase::Idle;
        }

    } // namespace Shutdown
} // namespace QK
