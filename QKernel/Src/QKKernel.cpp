// QKernel - Main kernel implementation
// Namespace: QK

#include "QKKernel.h"
#include "QKTaskManager.h"
#include "QKScheduler.h"
#include "QKInterrupts.h"
#include "QCLogger.h"

namespace QK
{

    Kernel &Kernel::instance()
    {
        static Kernel instance;
        return instance;
    }

    Kernel::Kernel() : m_running(false), m_uptime(0)
    {
    }

    Kernel::~Kernel()
    {
    }

    void Kernel::initialize()
    {
        QC_LOG_INFO("QKernel", "Initializing QAIOS kernel...");

        initializeSubsystems();

        QC_LOG_INFO("QKernel", "Kernel initialization complete");
    }

    void Kernel::initializeSubsystems()
    {
        // Initialize interrupt handling
        InterruptManager::instance().initialize();

        // Initialize task management
        // TaskManager is initialized on first use

        // Initialize scheduler
        Scheduler::instance().initialize();
    }

    void Kernel::run()
    {
        if (m_running)
        {
            QC_LOG_WARN("QKernel", "Kernel already running");
            return;
        }

        m_running = true;
        QC_LOG_INFO("QKernel", "Starting kernel main loop");

        Scheduler::instance().start();

        mainLoop();
    }

    void Kernel::mainLoop()
    {
        while (m_running)
        {
            // Scheduler handles task switching
            Scheduler::instance().schedule();

            // Halt until next interrupt
            asm volatile("hlt");
        }
    }

    void Kernel::shutdown()
    {
        QC_LOG_INFO("QKernel", "Shutting down kernel...");

        m_running = false;
        Scheduler::instance().stop();

        QC_LOG_INFO("QKernel", "Kernel shutdown complete");
    }

} // namespace QK
