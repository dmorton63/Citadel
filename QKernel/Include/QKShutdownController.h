#pragma once

// QKernel Shutdown Controller - coordinates graceful shutdown sequences
// Namespace: QK::Shutdown

#include "QCTypes.h"
#include "QCVector.h"

namespace QK
{
    namespace Shutdown
    {
        /// Reason a shutdown was requested
        enum class Reason : QC::u8
        {
            UserRequest = 0,
            ShellCommand,
            KeyboardShortcut,
            SidebarPowerButton,
            SystemPolicy
        };

        /// Current phase of the shutdown state machine
        enum class Phase : QC::u8
        {
            Idle = 0,
            NotifyingSubsystems,
            AwaitingUserDecision,
            PoweringOff
        };

        /// User decision coming from UI
        enum class UserChoice : QC::u8
        {
            Proceed,
            Cancel
        };

        using UIRequestHandler = bool (*)(Reason reason, void *userData);
        using SubsystemCallback = bool (*)(Reason reason, void *userData);
        using SubsystemHandle = QC::u32;
        constexpr SubsystemHandle InvalidSubsystemHandle = 0;

        /// Centralized controller that orchestrates graceful shutdown.
        class Controller
        {
        public:
            static Controller &instance();

            void requestShutdown(Reason reason);
            void confirm(UserChoice choice);
            void registerUIHandler(UIRequestHandler handler, void *userData);
            SubsystemHandle registerSubsystem(SubsystemCallback callback, void *userData, const char *name);
            void unregisterSubsystem(SubsystemHandle handle);

            Phase phase() const { return m_phase; }
            Reason reason() const { return m_reason; }

        private:
            Controller();

            bool notifySubsystems();
            void powerOffHardware();
            void reset();

            struct SubsystemEntry
            {
                SubsystemHandle handle;
                SubsystemCallback callback;
                void *userData;
                const char *name;
            };

            Phase m_phase;
            Reason m_reason;
            UIRequestHandler m_uiHandler;
            void *m_uiUserData;
            QC::Vector<SubsystemEntry> m_subsystems;
            SubsystemHandle m_nextSubsystemHandle;
        };

    } // namespace Shutdown
} // namespace QK
