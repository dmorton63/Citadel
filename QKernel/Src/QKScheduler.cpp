// QKernel Scheduler - Implementation
// Namespace: QK

#include "QKScheduler.h"
#include "QCLogger.h"

namespace QK
{

    Scheduler &Scheduler::instance()
    {
        static Scheduler instance;
        return instance;
    }

    Scheduler::Scheduler()
        : m_policy(SchedulerPolicy::RoundRobin), m_timeSlice(10) // 10ms default time slice
          ,
          m_tickCount(0), m_running(false)
    {
    }

    Scheduler::~Scheduler()
    {
    }

    void Scheduler::initialize()
    {
        QC_LOG_INFO("QKSched", "Initializing scheduler");
        m_tickCount = 0;
    }

    void Scheduler::start()
    {
        QC_LOG_INFO("QKSched", "Starting scheduler with %s policy",
                    m_policy == SchedulerPolicy::RoundRobin ? "RoundRobin" : m_policy == SchedulerPolicy::Priority ? "Priority"
                                                                                                                   : "Multilevel");
        m_running = true;
    }

    void Scheduler::stop()
    {
        QC_LOG_INFO("QKSched", "Stopping scheduler");
        m_running = false;
    }

    void Scheduler::setPolicy(SchedulerPolicy policy)
    {
        m_policy = policy;
    }

    void Scheduler::setTimeSlice(QC::u32 milliseconds)
    {
        m_timeSlice = milliseconds;
    }

    void Scheduler::schedule()
    {
        if (!m_running)
            return;

        TaskId nextTask = selectNextTask();
        if (nextTask == 0)
            return;

        Task *current = TaskManager::instance().getCurrentTask();
        Task *next = TaskManager::instance().getTask(nextTask);

        if (current && next && current->id != next->id)
        {
            contextSwitch(current, next);
        }
    }

    void Scheduler::timerTick()
    {
        ++m_tickCount;

        // Check if current task's time slice has expired
        if (m_tickCount >= m_timeSlice)
        {
            m_tickCount = 0;
            schedule();
        }
    }

    void Scheduler::addTask(TaskId id)
    {
        TaskManager::instance().setTaskState(id, TaskState::Ready);
    }

    void Scheduler::removeTask(TaskId id)
    {
        // Just mark as terminated, actual cleanup happens elsewhere
        TaskManager::instance().setTaskState(id, TaskState::Terminated);
    }

    void Scheduler::blockTask(TaskId id)
    {
        TaskManager::instance().setTaskState(id, TaskState::Blocked);
    }

    void Scheduler::unblockTask(TaskId id)
    {
        TaskManager::instance().setTaskState(id, TaskState::Ready);
    }

    TaskId Scheduler::selectNextTask()
    {
        // Simple round-robin implementation
        // TODO: Implement proper scheduling algorithms
        return 0;
    }

    void Scheduler::contextSwitch(Task *from, Task *to)
    {
        if (!from || !to)
            return;

        // Save current task context
        from->state = TaskState::Ready;

        // Switch to new task
        to->state = TaskState::Running;

        // TODO: Actually switch contexts (assembly required)
        // switch_context(&from->context, &to->context);
    }

} // namespace QK
