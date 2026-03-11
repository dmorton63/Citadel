// QDrivers Timer - Implementation
// Namespace: QDrv

#include "QDrvTimer.h"
#include "QArchPort.h"
#include "QKInterrupts.h"
#include "QKTime.h"
#include "QCLogger.h"

namespace QDrv
{

    // PIT ports
    constexpr QC::u16 PIT_CHANNEL0 = 0x40;
    constexpr QC::u16 PIT_COMMAND = 0x43;
    constexpr QC::u32 PIT_FREQUENCY = 1193182;

    Timer &Timer::instance()
    {
        static Timer instance;
        return instance;
    }

    Timer::Timer()
        : m_callback(nullptr), m_ticks(0), m_frequency(1000), m_useAPIC(false)
    {
    }

    Timer::~Timer()
    {
    }

    void Timer::initialize(QC::u32 frequencyHz)
    {
        QC_LOG_INFO("QDrvTimer", "Initializing timer at %u Hz", frequencyHz);

        m_frequency = frequencyHz;
        initializePIT(frequencyHz);

        // Register interrupt handler
        QK::InterruptManager::instance().registerHandler(
            QK::IRQ_TIMER,
            [](QK::InterruptFrame *)
            {
                Timer::instance().handleInterrupt();
            });
        QK::InterruptManager::instance().enableInterrupt(0);

        // Expose a kernel-level time API without forcing QKernel to include driver headers.
        QK::Time::setProvider(
            []() -> QC::u64 { return Timer::instance().milliseconds(); },
            [](QC::u64 ms) { Timer::instance().sleep(ms); });

        QC_LOG_INFO("QDrvTimer", "Timer initialized");
    }

    void Timer::initializePIT(QC::u32 frequencyHz)
    {
        QC::u32 divisor = PIT_FREQUENCY / frequencyHz;
        if (divisor > 65535)
            divisor = 65535;
        if (divisor < 1)
            divisor = 1;

        // Channel 0, lobyte/hibyte, rate generator
        QArch::outb(PIT_COMMAND, 0x36);
        QArch::outb(PIT_CHANNEL0, divisor & 0xFF);
        QArch::outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
    }

    void Timer::initializeAPICTimer()
    {
        // TODO: Implement APIC timer for SMP systems
        m_useAPIC = false;
    }

    void Timer::setCallback(TimerCallback callback)
    {
        m_callback = callback;
    }

    QC::u64 Timer::milliseconds() const
    {
        return (m_ticks * 1000) / m_frequency;
    }

    QC::u64 Timer::microseconds() const
    {
        return (m_ticks * 1000000) / m_frequency;
    }

    void Timer::sleep(QC::u64 ms)
    {
        QC::u64 target = m_ticks + (ms * m_frequency) / 1000;
        while (m_ticks < target)
        {
            asm volatile("hlt");
        }
    }

    void Timer::usleep(QC::u64 us)
    {
        QC::u64 target = m_ticks + (us * m_frequency) / 1000000;
        while (m_ticks < target)
        {
            asm volatile("pause");
        }
    }

    void Timer::handleInterrupt()
    {
        ++m_ticks;

        if (m_callback)
        {
            m_callback(m_ticks);
        }

        // Notify scheduler
        // QK::Scheduler::instance().timerTick();
    }

    // HighResTimer implementation

    HighResTimer &HighResTimer::instance()
    {
        static HighResTimer instance;
        return instance;
    }

    HighResTimer::HighResTimer()
        : m_tscFrequency(0), m_startTSC(0)
    {
    }

    HighResTimer::~HighResTimer()
    {
    }

    void HighResTimer::initialize()
    {
        QC_LOG_INFO("QDrvTimer", "Initializing high-resolution timer");

        m_startTSC = readTSC();
        calibrate();

        QC_LOG_INFO("QDrvTimer", "TSC frequency: %lu MHz", m_tscFrequency / 1000000);
    }

    QC::u64 HighResTimer::readTSC() const
    {
        QC::u32 lo, hi;
        asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
        return (static_cast<QC::u64>(hi) << 32) | lo;
    }

    void HighResTimer::calibrate()
    {
        // Use PIT to calibrate TSC
        QC::u64 start = readTSC();

        // Wait for ~10ms using PIT
        QArch::outb(PIT_COMMAND, 0x30);
        QArch::outb(PIT_CHANNEL0, 0xFF);
        QArch::outb(PIT_CHANNEL0, 0xFF);

        while (true)
        {
            QArch::outb(PIT_COMMAND, 0x00);
            QC::u8 lo = QArch::inb(PIT_CHANNEL0);
            QC::u8 hi = QArch::inb(PIT_CHANNEL0);
            QC::u16 count = (hi << 8) | lo;
            if (count < 0xFFFF - 11932)
                break; // ~10ms elapsed
        }

        QC::u64 end = readTSC();
        m_tscFrequency = (end - start) * 100; // Approximate
    }

    QC::u64 HighResTimer::nanoseconds() const
    {
        QC::u64 current = readTSC() - m_startTSC;
        return (current * 1000000000ULL) / m_tscFrequency;
    }

} // namespace QDrv
