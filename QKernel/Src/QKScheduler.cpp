// QKernel Scheduler - Implementation
// Namespace: QK

#include "QKScheduler.h"
#include "QKTime.h"
#include "QCLogger.h"

extern "C" void QArchSwitchTaskContext(
    QK::TaskContext *from, const QK::TaskContext *to, QC::u64 savedFlags);

namespace QK
{

    Scheduler &Scheduler::instance()
    {
        static Scheduler instance;
        return instance;
    }

    Scheduler::Scheduler()
        : m_policy(SchedulerPolicy::RoundRobin), m_timeSlice(10) // 10ms default time slice
          , m_running(false)
    {
    }

    Scheduler::~Scheduler()
    {
    }

    void Scheduler::initialize()
    {
        QC_LOG_INFO("QKSched", "Initializing scheduler");
        for (QC::u32 i = 0; i < kTaskManagerMaxCpus; ++i)
            m_tickCounts[i] = 0;
    }

    void Scheduler::start()
    {
        QC_LOG_INFO("QKSched", "Starting scheduler with %s policy",
                    m_policy == SchedulerPolicy::RoundRobin ? "RoundRobin" : m_policy == SchedulerPolicy::Priority ? "Priority"
                                                                                                                   : "Multilevel");
        __atomic_store_n(&m_running, true, __ATOMIC_RELEASE);
    }

    void Scheduler::stop()
    {
        QC_LOG_INFO("QKSched", "Stopping scheduler");
        __atomic_store_n(&m_running, false, __ATOMIC_RELEASE);
    }

    void Scheduler::setPolicy(SchedulerPolicy policy)
    {
        m_policy = policy;
    }

    void Scheduler::setTimeSlice(QC::u32 milliseconds)
    {
        if (milliseconds != 0)
            m_timeSlice = milliseconds;
    }

    void Scheduler::schedule(QC::u32 cpuIndex)
    {
        if (cpuIndex >= kTaskManagerMaxCpus)
            return;

        QC::u64 savedFlags = 0;
        asm volatile("pushfq; pop %0; cli" : "=r"(savedFlags) : : "memory");
        if (!__atomic_load_n(&m_running, __ATOMIC_ACQUIRE))
        {
            asm volatile("push %0; popfq" : : "r"(savedFlags) : "memory", "cc");
            return;
        }

        TaskManager &manager = TaskManager::instance();
        const TaskId currentId = manager.getCurrentTaskId(cpuIndex);
        (void)manager.reapTerminatedTasks(cpuIndex, currentId);
        Task *current = manager.getTask(currentId);
        if (current && __atomic_load_n(&current->state, __ATOMIC_ACQUIRE) == TaskState::Running)
            manager.setTaskState(currentId, TaskState::Ready);

        const TaskId nextId = selectNextTask(cpuIndex, currentId);
        if (nextId == 0)
        {
            if (current)
            {
                manager.setCurrentTaskId(cpuIndex, 0);
                contextSwitch(&current->context, &m_idleContexts[cpuIndex], savedFlags);
                return;
            }
            asm volatile("push %0; popfq" : : "r"(savedFlags) : "memory", "cc");
            return;
        }

        Task *next = manager.getTask(nextId);
        if (!next)
        {
            asm volatile("push %0; popfq" : : "r"(savedFlags) : "memory", "cc");
            return;
        }

        manager.setTaskState(nextId, TaskState::Running);
        manager.setCurrentTaskId(cpuIndex, nextId);
        if (nextId != currentId)
        {
            TaskContext *from = current ? &current->context : &m_idleContexts[cpuIndex];
            next->context.cs = from->cs;
            next->context.ss = from->ss;
            contextSwitch(from, &next->context, savedFlags);
            return;
        }
        asm volatile("push %0; popfq" : : "r"(savedFlags) : "memory", "cc");
    }

    void Scheduler::timerTick(QC::u32 cpuIndex)
    {
        if (cpuIndex >= kTaskManagerMaxCpus ||
            !__atomic_load_n(&m_running, __ATOMIC_ACQUIRE))
            return;

        const QC::u32 ticks = __atomic_add_fetch(&m_tickCounts[cpuIndex], 1u, __ATOMIC_RELAXED);
        if (ticks >= m_timeSlice)
        {
            __atomic_store_n(&m_tickCounts[cpuIndex], 0u, __ATOMIC_RELAXED);
            TaskManager::instance().wakeSleepingTasks(cpuIndex, QK::Time::milliseconds());
            schedule(cpuIndex);
        }
    }

    void Scheduler::addTask(TaskId id)
    {
        if (Task *task = TaskManager::instance().getTask(id))
        {
            TaskState expected = TaskState::Created;
            (void)__atomic_compare_exchange_n(
                &task->state, &expected, TaskState::Ready, false,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
        }
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

    bool Scheduler::unblockTask(TaskId id)
    {
        Task *task = TaskManager::instance().getTask(id);
        if (!task)
            return false;
        TaskState expected = TaskState::Blocked;
        return __atomic_compare_exchange_n(
            &task->state, &expected, TaskState::Ready, false,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    }

    TaskId Scheduler::selectNextTask(QC::u32 cpuIndex, TaskId afterId)
    {
        return TaskManager::instance().findNextReadyTask(cpuIndex, afterId);
    }

    void Scheduler::contextSwitch(TaskContext *from, TaskContext *to, QC::u64 savedFlags)
    {
        if (to)
            QArchSwitchTaskContext(from, to, savedFlags);
    }

} // namespace QK
