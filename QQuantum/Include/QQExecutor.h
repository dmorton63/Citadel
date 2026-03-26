#pragma once

// QQuantum Executor - Quantum-inspired execution engine
// Namespace: QQ

#include "QCTypes.h"
#include "QCVector.h"

#include "QQFlowPolicy.h"

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

        // Optional provenance (for Security Center flow policy decisions).
        // These are opaque identifiers provided by callers; they may be empty.
        char origin[32];
        char moduleId[32];

        TaskFunction function;
        void *context;
        void *argument;
        QC::usize argumentSize;

        // AI integration: stable identity + canonical input hashing.
        // These are best-effort and may be zero if not provided.
        QC::u8 signatureHash[32];
        QC::u8 inputHash[32];

        // AI integration: opt-in caching/memoization.
        bool cacheEligible;

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

        // Security Center policy hook (defaults to nullptr => allow).
        void setFlowPolicy(FlowPolicyFn fn) { m_flowPolicy = fn; }
        FlowPolicyFn flowPolicy() const { return m_flowPolicy; }

        void initialize(QC::usize workerCount);
        void shutdown();

        // Task submission
        TaskId submit(const char *name, TaskFunction func, void *context, void *arg);
        TaskId submitWithArgSize(const char *name, TaskFunction func, void *context, void *arg, QC::usize argSize);
        TaskId submitWithArgSizeCached(const char *name, TaskFunction func, void *context, void *arg, QC::usize argSize);
        TaskId submitWithOrigin(const char *name, const char *origin, const char *moduleId,
                    TaskFunction func, void *context, void *arg);
        TaskId submitWithOriginAndArgSize(const char *name, const char *origin, const char *moduleId,
                TaskFunction func, void *context, void *arg, QC::usize argSize);
        TaskId submitWithOriginAndArgSizeCached(const char *name, const char *origin, const char *moduleId,
                TaskFunction func, void *context, void *arg, QC::usize argSize);
        TaskId submitWithPriority(const char *name, TaskFunction func,
                                  void *context, void *arg, TaskPriority priority);
        TaskId submitWithPriorityAndOrigin(const char *name, const char *origin, const char *moduleId,
                           TaskFunction func, void *context, void *arg, TaskPriority priority);
        TaskId submitWithPriorityOriginAndArgSize(const char *name, const char *origin, const char *moduleId,
                    TaskFunction func, void *context, void *arg, QC::usize argSize, TaskPriority priority);
        TaskId submitWithPriorityOriginAndArgSizeCached(const char *name, const char *origin, const char *moduleId,
                    TaskFunction func, void *context, void *arg, QC::usize argSize, TaskPriority priority);
        TaskId submitWithDependencies(const char *name, TaskFunction func,
                                      void *context, void *arg,
                                      const TaskId *dependencies, QC::usize depCount);
        TaskId submitWithDependenciesAndOrigin(const char *name, const char *origin, const char *moduleId,
                               TaskFunction func, void *context, void *arg,
                               const TaskId *dependencies, QC::usize depCount);
        TaskId submitWithDependenciesOriginAndArgSize(const char *name, const char *origin, const char *moduleId,
                       TaskFunction func, void *context, void *arg, QC::usize argSize,
                       const TaskId *dependencies, QC::usize depCount);
        TaskId submitWithDependenciesOriginAndArgSizeCached(const char *name, const char *origin, const char *moduleId,
                       TaskFunction func, void *context, void *arg, QC::usize argSize,
                       const TaskId *dependencies, QC::usize depCount);

        // Task control
        void cancel(TaskId id);
        void suspend(TaskId id);
        void resume(TaskId id);

        // Task queries
        TaskState state(TaskId id) const;
        bool isComplete(TaskId id) const;
        TaskResult result(TaskId id) const;

        // Diagnostics/observability (read-only; may return nullptr).
        const TaskDescriptor *taskDescriptor(TaskId id) const;

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
        TaskId nextTaskIdForDebug() const { return m_nextTaskId; }

        // Recent task listing (best-effort but deterministic)
        QC::usize recentTaskIdCount() const { return m_recentCount; }
        TaskId recentTaskIdAt(QC::usize idx) const;

        // Memoization cache controls (global gate; per-task still must be cacheEligible).
        void setMemoizationEnabled(bool enabled) { m_memoizationEnabled = enabled; }
        bool memoizationEnabled() const { return m_memoizationEnabled; }
        void clearMemoizationCache();
        QC::u64 memoizationHits() const { return m_memoHits; }
        QC::u64 memoizationMisses() const { return m_memoMisses; }
        QC::u64 memoizationRefused() const { return m_memoRefused; }

        // Memoization allowlist (signatureHash-based, checked for cached submits)
        void setMemoizationAllowlistEnabled(bool enabled) { m_memoAllowlistEnabled = enabled; }
        bool memoizationAllowlistEnabled() const { return m_memoAllowlistEnabled; }
        void clearMemoizationAllowlist();
        bool memoizationAllowlistAdd(const QC::u8 sig[32]);
        bool memoizationAllowlistRemove(const QC::u8 sig[32]);
        bool memoizationAllowlistContains(const QC::u8 sig[32]) const;
        QC::usize memoizationAllowlistCount() const { return m_memoAllowlistCount; }

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
        void recentPush(TaskId id);

        FlowPolicyFn m_flowPolicy = nullptr;

        QC::Vector<TaskDescriptor *> m_tasks;
        Scheduler *m_scheduler;

        TaskId m_nextTaskId;
        QC::u64 m_totalExecuted;

        bool m_memoizationEnabled = false;
        QC::u64 m_memoHits = 0;
        QC::u64 m_memoMisses = 0;
        QC::u64 m_memoRefused = 0;

        bool m_memoAllowlistEnabled = true;
        QC::usize m_memoAllowlistCount = 0;
        QC::u8 m_memoAllowlist[64][32];

        TaskId m_recentIds[128];
        QC::usize m_recentHead = 0;
        QC::usize m_recentCount = 0;

        bool m_running;
        QC::usize m_workerCount;
    };

} // namespace QQ
