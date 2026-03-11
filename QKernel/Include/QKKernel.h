#pragma once

// QKernel - Main kernel class
// Namespace: QK

#include "QCTypes.h"

namespace QK
{

    class Kernel
    {
    public:
        static Kernel &instance();

        void initialize();
        void run();
        void shutdown();

        bool isRunning() const { return m_running; }
        QC::u64 uptime() const { return m_uptime; }

    private:
        Kernel();
        ~Kernel();
        Kernel(const Kernel &) = delete;
        Kernel &operator=(const Kernel &) = delete;

        void initializeSubsystems();
        void mainLoop();

        bool m_running;
        QC::u64 m_uptime;
    };

} // namespace QK
