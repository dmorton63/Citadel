#pragma once

// QQuantum Scheduler - Advanced task scheduling
// Namespace: QQ

#include "QCTypes.h"
#include "QCVector.h"
#include "QQExecutor.h"

namespace QQ
{

    // Scheduling algorithm
    enum class SchedulingAlgorithm : QC::u8
    {
        FIFO,         // First In First Out
        Priority,     // Static priority
        RoundRobin,   // Time-sliced round robin
        WorkStealing, // Work stealing between workers
        EDF,          // Earliest Deadline First
        Adaptive      // Adaptive based on workload
    };

    // Worker state
    struct WorkerState
    {
        QC::u32 workerId;
        bool active;
        TaskId currentTask;
        QC::usize queueLength;
        QC::u64 cpuTime;
        QC::u64 idleTime;
    };

    // Scheduling metrics
    struct SchedulerMetrics
    {
        QC::u64 totalScheduled;
        QC::u64 totalCompleted;
        QC::u64 totalMissedDeadlines;
        QC::u64 averageWaitTime;
        QC::u64 averageExecutionTime;
        QC::f64 cpuUtilization;
        QC::f64 throughput;
    };

    // Ready queue entry
    struct ReadyQueueEntry
    {
        TaskDescriptor *task;
        QC::u64 virtualDeadline;
        QC::u64 insertTime;
    };

    class Scheduler
    {
    public:
        Scheduler();
        ~Scheduler();

        void initialize(QC::usize workerCount);
        void shutdown();

        // Algorithm selection
        void setAlgorithm(SchedulingAlgorithm algo) { m_algorithm = algo; }
        SchedulingAlgorithm algorithm() const { return m_algorithm; }

        // Time quantum for round-robin
        void setTimeQuantum(QC::u64 microseconds) { m_timeQuantum = microseconds; }
        QC::u64 timeQuantum() const { return m_timeQuantum; }

        // Task enqueueing
        void enqueue(TaskDescriptor *task);
        void enqueueWithDeadline(TaskDescriptor *task, QC::u64 deadline);

        // Task selection
        TaskDescriptor *selectNext(QC::u32 workerId);
        TaskDescriptor *steal(QC::u32 fromWorkerId);

        // Preemption
        bool shouldPreempt(TaskDescriptor *current, TaskDescriptor *incoming);
        void preempt(QC::u32 workerId);

        // Worker management
        void registerWorker(QC::u32 workerId);
        void unregisterWorker(QC::u32 workerId);
        WorkerState *workerState(QC::u32 workerId);

        // Load balancing
        void rebalance();
        QC::u32 leastLoadedWorker() const;
        QC::u32 mostLoadedWorker() const;

        // Priority adjustment
        void boost(TaskDescriptor *task);
        void decay(TaskDescriptor *task);

        // Metrics
        SchedulerMetrics &metrics() { return m_metrics; }
        const SchedulerMetrics &metrics() const { return m_metrics; }
        void resetMetrics();

        // Tick (called periodically)
        void tick(QC::u64 currentTime);

    private:
        TaskDescriptor *selectFIFO();
        TaskDescriptor *selectPriority();
        TaskDescriptor *selectRoundRobin(QC::u32 workerId);
        TaskDescriptor *selectEDF();
        TaskDescriptor *selectAdaptive(QC::u32 workerId);

        void updateMetrics(TaskDescriptor *task, QC::u64 waitTime, QC::u64 execTime);

        SchedulingAlgorithm m_algorithm;
        QC::u64 m_timeQuantum;

        // Per-priority queues
        static constexpr QC::usize PRIORITY_LEVELS = 6;
        QC::Vector<ReadyQueueEntry> m_queues[PRIORITY_LEVELS];

        // Per-worker state
        static constexpr QC::usize MAX_WORKERS = 64;
        WorkerState m_workers[MAX_WORKERS];
        QC::usize m_workerCount;

        // Per-worker local queues (for work stealing)
        QC::Vector<ReadyQueueEntry> m_localQueues[MAX_WORKERS];

        SchedulerMetrics m_metrics;

        QC::u64 m_currentTime;
        QC::u32 m_rrIndex; // Round-robin index
    };

} // namespace QQ
