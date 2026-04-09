#include "QQScheduler.h"

#include "QCString.h"

namespace QQ
{
    namespace
    {
        constexpr QC::usize kPriorityLevels = 6;

        static QC::usize priorityIndex(TaskPriority priority)
        {
            const QC::usize idx = static_cast<QC::usize>(priority);
            return (idx < kPriorityLevels) ? idx : (kPriorityLevels - 1);
        }

        static void removeTaskFromQueue(QC::Vector<ReadyQueueEntry> &queue, TaskDescriptor *task)
        {
            if (!task)
                return;
            for (QC::usize i = 0; i < queue.size(); ++i)
            {
                if (queue[i].task != task)
                    continue;
                for (QC::usize j = i + 1; j < queue.size(); ++j)
                    queue[j - 1] = queue[j];
                queue.pop_back();
                return;
            }
        }
    }

    Scheduler::Scheduler()
        : m_algorithm(SchedulingAlgorithm::Adaptive),
          m_timeQuantum(1000),
          m_workerCount(0),
          m_metrics{},
          m_currentTime(0),
          m_rrIndex(0)
    {
        for (QC::usize i = 0; i < MAX_WORKERS; ++i)
            m_workers[i] = WorkerState{static_cast<QC::u32>(i), false, INVALID_TASK, 0, 0, 0};
    }

    Scheduler::~Scheduler()
    {
    }

    void Scheduler::initialize(QC::usize workerCount)
    {
        shutdown();
        if (workerCount > MAX_WORKERS)
            workerCount = MAX_WORKERS;
        m_workerCount = workerCount;
        for (QC::usize i = 0; i < m_workerCount; ++i)
            registerWorker(static_cast<QC::u32>(i));
    }

    void Scheduler::shutdown()
    {
        for (QC::usize i = 0; i < PRIORITY_LEVELS; ++i)
            m_queues[i].clear();
        for (QC::usize i = 0; i < MAX_WORKERS; ++i)
        {
            m_localQueues[i].clear();
            m_workers[i].active = false;
            m_workers[i].currentTask = INVALID_TASK;
            m_workers[i].queueLength = 0;
        }
        m_workerCount = 0;
        resetMetrics();
    }

    void Scheduler::enqueue(TaskDescriptor *task)
    {
        if (!task)
            return;
        ReadyQueueEntry entry{};
        entry.task = task;
        entry.virtualDeadline = task->deadline;
        entry.insertTime = task->queueTime;
        m_queues[priorityIndex(task->priority)].push_back(entry);
        if (m_workerCount)
        {
            const QC::u32 workerId = leastLoadedWorker();
            if (workerId < MAX_WORKERS)
            {
                m_localQueues[workerId].push_back(entry);
                m_workers[workerId].queueLength = m_localQueues[workerId].size();
            }
        }
    }

    void Scheduler::enqueueWithDeadline(TaskDescriptor *task, QC::u64 deadline)
    {
        if (!task)
            return;
        task->deadline = deadline;
        enqueue(task);
    }

    TaskDescriptor *Scheduler::selectNext(QC::u32 workerId)
    {
        TaskDescriptor *task = nullptr;
        switch (m_algorithm)
        {
        case SchedulingAlgorithm::FIFO:
            task = selectFIFO();
            break;
        case SchedulingAlgorithm::Priority:
            task = selectPriority();
            break;
        case SchedulingAlgorithm::RoundRobin:
            task = selectRoundRobin(workerId);
            break;
        case SchedulingAlgorithm::EDF:
            task = selectEDF();
            break;
        case SchedulingAlgorithm::WorkStealing:
        case SchedulingAlgorithm::Adaptive:
        default:
            task = selectAdaptive(workerId);
            break;
        }

        if (task)
        {
            ++m_metrics.totalScheduled;
            if (workerId < MAX_WORKERS)
            {
                m_workers[workerId].active = true;
                m_workers[workerId].currentTask = task->id;
                m_workers[workerId].queueLength = m_localQueues[workerId].size();
            }
        }
        return task;
    }

    TaskDescriptor *Scheduler::steal(QC::u32 fromWorkerId)
    {
        if (fromWorkerId >= MAX_WORKERS || m_localQueues[fromWorkerId].empty())
            return nullptr;
        ReadyQueueEntry entry = m_localQueues[fromWorkerId].back();
        m_localQueues[fromWorkerId].pop_back();
        m_workers[fromWorkerId].queueLength = m_localQueues[fromWorkerId].size();
        removeTaskFromQueue(m_queues[priorityIndex(entry.task->priority)], entry.task);
        return entry.task;
    }

    bool Scheduler::shouldPreempt(TaskDescriptor *current, TaskDescriptor *incoming)
    {
        if (!current || !incoming)
            return false;

        const QC::u32 currentPriority = static_cast<QC::u32>(current->priority);
        const QC::u32 incomingPriority = static_cast<QC::u32>(incoming->priority);
        if (incomingPriority > currentPriority)
            return true;

        if (incoming->deadline != 0 && current->deadline != 0 && incoming->deadline < current->deadline)
            return true;

        const bool quantumExpired = current->startTime != 0 && m_currentTime >= current->startTime && (m_currentTime - current->startTime) >= m_timeQuantum;
        if (incomingPriority == currentPriority && quantumExpired && incoming->deadline <= current->deadline)
            return true;

        return false;
    }

    void Scheduler::preempt(QC::u32 workerId)
    {
        if (workerId >= MAX_WORKERS)
            return;
        m_workers[workerId].currentTask = INVALID_TASK;
        m_workers[workerId].active = false;
    }

    void Scheduler::registerWorker(QC::u32 workerId)
    {
        if (workerId >= MAX_WORKERS)
            return;
        m_workers[workerId].workerId = workerId;
        m_workers[workerId].active = true;
        m_workers[workerId].currentTask = INVALID_TASK;
        m_workers[workerId].queueLength = 0;
    }

    void Scheduler::unregisterWorker(QC::u32 workerId)
    {
        if (workerId >= MAX_WORKERS)
            return;
        m_workers[workerId].active = false;
        m_workers[workerId].currentTask = INVALID_TASK;
        m_workers[workerId].queueLength = 0;
        m_localQueues[workerId].clear();
    }

    WorkerState *Scheduler::workerState(QC::u32 workerId)
    {
        if (workerId >= MAX_WORKERS)
            return nullptr;
        return &m_workers[workerId];
    }

    void Scheduler::rebalance()
    {
        const QC::u32 heavy = mostLoadedWorker();
        const QC::u32 light = leastLoadedWorker();
        if (heavy >= MAX_WORKERS || light >= MAX_WORKERS || heavy == light)
            return;
        if (m_localQueues[heavy].size() <= (m_localQueues[light].size() + 1))
            return;
        if (TaskDescriptor *task = steal(heavy))
            enqueue(task);
    }

    QC::u32 Scheduler::leastLoadedWorker() const
    {
        QC::u32 best = 0;
        QC::usize bestLen = static_cast<QC::usize>(-1);
        for (QC::usize i = 0; i < m_workerCount; ++i)
        {
            if (m_workers[i].queueLength >= bestLen)
                continue;
            best = static_cast<QC::u32>(i);
            bestLen = m_workers[i].queueLength;
        }
        return best;
    }

    QC::u32 Scheduler::mostLoadedWorker() const
    {
        QC::u32 best = 0;
        QC::usize bestLen = 0;
        for (QC::usize i = 0; i < m_workerCount; ++i)
        {
            if (m_workers[i].queueLength <= bestLen)
                continue;
            best = static_cast<QC::u32>(i);
            bestLen = m_workers[i].queueLength;
        }
        return best;
    }

    void Scheduler::boost(TaskDescriptor *task)
    {
        if (!task || task->priority == TaskPriority::Critical)
            return;
        task->priority = static_cast<TaskPriority>(static_cast<QC::u32>(task->priority) + 1);
    }

    void Scheduler::decay(TaskDescriptor *task)
    {
        if (!task || task->priority == TaskPriority::Lowest)
            return;
        task->priority = static_cast<TaskPriority>(static_cast<QC::u32>(task->priority) - 1);
    }

    void Scheduler::resetMetrics()
    {
        m_metrics = SchedulerMetrics{};
    }

    void Scheduler::tick(QC::u64 currentTime)
    {
        m_currentTime = currentTime;
        rebalance();
    }

    TaskDescriptor *Scheduler::selectFIFO()
    {
        for (QC::usize p = 0; p < PRIORITY_LEVELS; ++p)
        {
            if (m_queues[p].empty())
                continue;
            ReadyQueueEntry entry = m_queues[p][0];
            for (QC::usize i = 1; i < m_queues[p].size(); ++i)
                m_queues[p][i - 1] = m_queues[p][i];
            m_queues[p].pop_back();
            return entry.task;
        }
        return nullptr;
    }

    TaskDescriptor *Scheduler::selectPriority()
    {
        for (QC::isize p = static_cast<QC::isize>(PRIORITY_LEVELS) - 1; p >= 0; --p)
        {
            if (m_queues[static_cast<QC::usize>(p)].empty())
                continue;
            ReadyQueueEntry entry = m_queues[static_cast<QC::usize>(p)].back();
            m_queues[static_cast<QC::usize>(p)].pop_back();
            return entry.task;
        }
        return nullptr;
    }

    TaskDescriptor *Scheduler::selectRoundRobin(QC::u32)
    {
        for (QC::usize attempt = 0; attempt < PRIORITY_LEVELS; ++attempt)
        {
            const QC::usize idx = (m_rrIndex + attempt) % PRIORITY_LEVELS;
            if (m_queues[idx].empty())
                continue;
            ReadyQueueEntry entry = m_queues[idx][0];
            for (QC::usize i = 1; i < m_queues[idx].size(); ++i)
                m_queues[idx][i - 1] = m_queues[idx][i];
            m_queues[idx].pop_back();
            m_rrIndex = static_cast<QC::u32>((idx + 1) % PRIORITY_LEVELS);
            return entry.task;
        }
        return nullptr;
    }

    TaskDescriptor *Scheduler::selectEDF()
    {
        TaskDescriptor *best = nullptr;
        QC::usize bestQueue = 0;
        QC::usize bestIndex = 0;
        for (QC::usize p = 0; p < PRIORITY_LEVELS; ++p)
        {
            for (QC::usize i = 0; i < m_queues[p].size(); ++i)
            {
                TaskDescriptor *task = m_queues[p][i].task;
                if (!task)
                    continue;
                if (!best || task->deadline < best->deadline)
                {
                    best = task;
                    bestQueue = p;
                    bestIndex = i;
                }
            }
        }
        if (!best)
            return nullptr;
        for (QC::usize i = bestIndex + 1; i < m_queues[bestQueue].size(); ++i)
            m_queues[bestQueue][i - 1] = m_queues[bestQueue][i];
        m_queues[bestQueue].pop_back();
        return best;
    }

    TaskDescriptor *Scheduler::selectAdaptive(QC::u32 workerId)
    {
        if (workerId < MAX_WORKERS && !m_localQueues[workerId].empty())
        {
            ReadyQueueEntry entry = m_localQueues[workerId].back();
            m_localQueues[workerId].pop_back();
            m_workers[workerId].queueLength = m_localQueues[workerId].size();
            removeTaskFromQueue(m_queues[priorityIndex(entry.task->priority)], entry.task);
            return entry.task;
        }
        return selectPriority();
    }

    void Scheduler::updateMetrics(TaskDescriptor *, QC::u64 waitTime, QC::u64 execTime)
    {
        ++m_metrics.totalCompleted;
        m_metrics.averageWaitTime = m_metrics.totalCompleted ? ((m_metrics.averageWaitTime * (m_metrics.totalCompleted - 1)) + waitTime) / m_metrics.totalCompleted : waitTime;
        m_metrics.averageExecutionTime = m_metrics.totalCompleted ? ((m_metrics.averageExecutionTime * (m_metrics.totalCompleted - 1)) + execTime) / m_metrics.totalCompleted : execTime;
    }
}
