#pragma once

// QKernel Scheduler - Task scheduling
// Namespace: QK

#include "QCTypes.h"
#include "QKTaskManager.h"

namespace QK
{

    enum class SchedulerPolicy : QC::u8
    {
        RoundRobin,
        Priority,
        Multilevel
    };

    class Scheduler
    {
    public:
        static Scheduler &instance();

        void initialize();
        void start();
        void stop();

        void setPolicy(SchedulerPolicy policy);
        SchedulerPolicy getPolicy() const { return m_policy; }
        bool isRunning() const { return __atomic_load_n(&m_running, __ATOMIC_ACQUIRE); }

        void setTimeSlice(QC::u32 milliseconds);
        QC::u32 getTimeSlice() const { return m_timeSlice; }

        void schedule(QC::u32 cpuIndex = 0);
        // Called from each CPU's local timer interrupt; switches at the time-slice boundary.
        void timerTick(QC::u32 cpuIndex = 0);

        void addTask(TaskId id);
        void removeTask(TaskId id);
        void blockTask(TaskId id);
        // Moves a Blocked task back to Ready; returns false if it was not Blocked.
        bool unblockTask(TaskId id);

    private:
        Scheduler();
        ~Scheduler();
        Scheduler(const Scheduler &) = delete;
        Scheduler &operator=(const Scheduler &) = delete;

        TaskId selectNextTask(QC::u32 cpuIndex, TaskId afterId);
        void contextSwitch(TaskContext *from, TaskContext *to, QC::u64 savedFlags);

        SchedulerPolicy m_policy;
        QC::u32 m_timeSlice;
        QC::u32 m_tickCounts[kTaskManagerMaxCpus];
        TaskContext m_idleContexts[kTaskManagerMaxCpus];
        bool m_running;
    };

} // namespace QK
