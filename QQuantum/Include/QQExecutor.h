#pragma once

// QQuantum Executor - Quantum-inspired execution engine
// Namespace: QQ

#include "QCTypes.h"
#include "QCVector.h"

namespace QQ
{

    class Scheduler;

    // Task ID
    using TaskId = QC::u64;
    constexpr TaskId INVALID_TASK = 0;

    // Task state
    enum class TaskState : QC::u8
    {
        Pending,
        Queued,
        Running,
        Suspended,
        Completed,
        Failed,
        Cancelled
    };

    // Task priority
    enum class TaskPriority : QC::u8
    {
        Lowest = 0,
        Low = 1,
        Normal = 2,
        High = 3,
        Highest = 4,
        Critical = 5
    };

    // Task result
    struct TaskResult
    {
        bool success;
        QC::i64 value;
        void *data;
        QC::usize dataSize;
    };

    // Task function signature
    using TaskFunction = TaskResult (*)(void *context, void *arg);

    // Task dependency
    struct TaskDependency
    {
        TaskId taskId;
        bool completed;
    };

    // Task descriptor
    struct TaskDescriptor
    {
        TaskId id;
        char name[64];

        TaskFunction function;
        void *context;
        void *argument;

        TaskState state;
        TaskPriority priority;

        TaskResult result;

        // Dependencies
        QC::Vector<TaskDependency> dependencies;

        // Timing
        QC::u64 queueTime;
        QC::u64 startTime;
        QC::u64 endTime;
        QC::u64 deadline; // Optional deadline

        // Affinity
        QC::u32 cpuAffinity; // Bitmask
    };

    class Executor
    {
    public:
        static Executor &instance();

        void initialize(QC::usize workerCount);
        void shutdown();

        // Task submission
        TaskId submit(const char *name, TaskFunction func, void *context, void *arg);
        TaskId submitWithPriority(const char *name, TaskFunction func,
                                  void *context, void *arg, TaskPriority priority);
        TaskId submitWithDependencies(const char *name, TaskFunction func,
                                      void *context, void *arg,
                                      const TaskId *dependencies, QC::usize depCount);

        // Task control
        void cancel(TaskId id);
        void suspend(TaskId id);
        void resume(TaskId id);

        // Task queries
        TaskState state(TaskId id) const;
        bool isComplete(TaskId id) const;
        TaskResult result(TaskId id) const;

        // Waiting
        void wait(TaskId id);
        bool waitTimeout(TaskId id, QC::u64 milliseconds);
        void waitAll(const TaskId *ids, QC::usize count);
        TaskId waitAny(const TaskId *ids, QC::usize count);

        // Batch operations
        void submitBatch(TaskDescriptor *tasks, QC::usize count, TaskId *outIds);
        void cancelAll();

        // Statistics
        QC::usize pendingCount() const;
        QC::usize runningCount() const;
        QC::usize completedCount() const;
        QC::u64 totalTasksExecuted() const { return m_totalExecuted; }

        // Scheduler access
        Scheduler *scheduler() { return m_scheduler; }

    private:
        Executor();
        ~Executor();
        Executor(const Executor &) = delete;
        Executor &operator=(const Executor &) = delete;

        TaskId allocateTaskId();
        TaskDescriptor *findTask(TaskId id);
        bool areDependenciesMet(TaskDescriptor *task);
        void executeTask(TaskDescriptor *task);

        QC::Vector<TaskDescriptor *> m_tasks;
        Scheduler *m_scheduler;

        TaskId m_nextTaskId;
        QC::u64 m_totalExecuted;

        bool m_running;
        QC::usize m_workerCount;
    };

} // namespace QQ
