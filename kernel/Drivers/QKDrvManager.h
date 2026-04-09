#pragma once

// QKDrvManager - Driver Manager for probing and selecting drivers
// Namespace: QKDrv

#include "QKDrvBase.h"
#include "QCVector.h"

namespace QKDrv
{

    using ModuleLifecycleFn = QC::Status (*)(void *user);

    class Manager
    {
    public:
        static Manager &instance();

        // Initialize - probe for all available drivers
        void initialize();
        void shutdown();

        // Get the best mouse driver (prefers USB tablet > USB mouse > PS/2)
        MouseDriver *mouseDriver() { return m_mouseDriver; }
        KeyboardDriver *keyboardDriver() { return m_keyboardDriver; }

        // All detected controllers
        const QC::Vector<DriverBase *> &controllers() const { return m_controllers; }

        // Set screen size for tablet drivers
        void setScreenSize(QC::u32 width, QC::u32 height);

        // Poll all active drivers
        void poll();

        // Runtime module lifecycle hooks.
        bool registerLifecycleHooks(const char *moduleId,
                                    ModuleLifecycleFn initFn,
                                    ModuleLifecycleFn startFn,
                                    ModuleLifecycleFn stopFn,
                                    void *userData);
        bool unregisterLifecycleHooks(const char *moduleId);

    private:
        Manager() = default;
        ~Manager() = default;
        Manager(const Manager &) = delete;
        Manager &operator=(const Manager &) = delete;

        void probePS2();
        void probeUSB();
        void probeNetwork();
        void probeStorage();

        struct ModuleLifecycle
        {
            const char *moduleId = nullptr;
            ModuleLifecycleFn initFn = nullptr;
            ModuleLifecycleFn startFn = nullptr;
            ModuleLifecycleFn stopFn = nullptr;
            void *userData = nullptr;
            bool initialized = false;
            bool started = false;
        };

        void runModuleInitHooks();
        void runModuleStartHooks();
        void runModuleStopHooks();

        QC::Vector<DriverBase *> m_controllers;
        QC::Vector<ModuleLifecycle> m_moduleHooks;
        MouseDriver *m_mouseDriver = nullptr;
        KeyboardDriver *m_keyboardDriver = nullptr;
        QC::u32 m_screenWidth = 0;
        QC::u32 m_screenHeight = 0;
    };

} // namespace QKDrv
