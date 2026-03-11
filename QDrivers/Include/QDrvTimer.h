#pragma once

// QDrivers Timer - PIT/APIC Timer driver
// Namespace: QDrv

#include "QCTypes.h"

namespace QDrv
{

    using TimerCallback = void (*)(QC::u64 ticks);

    class Timer
    {
    public:
        static Timer &instance();

        void initialize(QC::u32 frequencyHz = 1000);

        void setCallback(TimerCallback callback);

        QC::u64 ticks() const { return m_ticks; }
        QC::u64 milliseconds() const;
        QC::u64 microseconds() const;

        void sleep(QC::u64 milliseconds);
        void usleep(QC::u64 microseconds);

        QC::u32 frequency() const { return m_frequency; }

        // Called from interrupt handler
        void handleInterrupt();

    private:
        Timer();
        ~Timer();
        Timer(const Timer &) = delete;
        Timer &operator=(const Timer &) = delete;

        void initializePIT(QC::u32 frequencyHz);
        void initializeAPICTimer();

        TimerCallback m_callback;
        QC::u64 m_ticks;
        QC::u32 m_frequency;
        bool m_useAPIC;
    };

    // High-resolution timing
    class HighResTimer
    {
    public:
        static HighResTimer &instance();

        void initialize();

        QC::u64 readTSC() const;
        QC::u64 tscFrequency() const { return m_tscFrequency; }

        QC::u64 nanoseconds() const;

    private:
        HighResTimer();
        ~HighResTimer();

        void calibrate();

        QC::u64 m_tscFrequency;
        QC::u64 m_startTSC;
    };

} // namespace QDrv
