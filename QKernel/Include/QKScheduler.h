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

        void setTimeSlice(QC::u32 milliseconds);
        QC::u32 getTimeSlice() const { return m_timeSlice; }

        void schedule();
        void timerTick();

        void addTask(TaskId id);
        void removeTask(TaskId id);
        void blockTask(TaskId id);
        void unblockTask(TaskId id);

    private:
        Scheduler();
        ~Scheduler();
        Scheduler(const Scheduler &) = delete;
        Scheduler &operator=(const Scheduler &) = delete;

        TaskId selectNextTask();
        void contextSwitch(Task *from, Task *to);

        SchedulerPolicy m_policy;
        QC::u32 m_timeSlice;
        QC::u32 m_tickCount;
        bool m_running;
    };

} // namespace QK
