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
        Blocked,
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

    // Explicit task-graph node model for flow metadata and tooling.
    struct TaskNode
    {
        TaskId id;
        char name[64];
        TaskPriority priority;
        TaskState state;
        QC::Vector<TaskDependency> dependencies;
        QC::u32 weightCost;
    };

    // Explicit flow container model for grouped task graph execution.
    struct TaskFlow
    {
        TaskId id;
        char name[64];
        TaskPriority priority;
        TaskState state;
        QC::u32 weightCost;
        QC::Vector<TaskNode> nodes;
    };

    enum class ResultCachePolicy : QC::u8
    {
        LRU = 0,
        LFU = 1
    };

    struct SignatureMetricsSnapshot
    {
        QC::u8 sig[32] = {0};
        QC::u64 submissions = 0;
        QC::u64 completions = 0;
        QC::u64 memoHits = 0;
        QC::u64 totalBuildMs = 0;
        QC::u64 totalExecMs = 0;
    };

    struct FlowStatistics
    {
        char name[32] = {0};
        QC::u32 pending = 0;
        QC::u32 running = 0;
        QC::u32 completed = 0;
        QC::u32 merged = 0;
        QC::i32 priorityBias = 0;
        QC::u64 promotions = 0;
        QC::u64 demotions = 0;
    };

    struct TaskFlowQueueSnapshot
    {
        QC::u32 low = 0;
        QC::u32 medium = 0;
        QC::u32 high = 0;
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
        TaskId mergedInto = INVALID_TASK;

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
        QC::u64 queueDelayMs;
        QC::u64 buildDurationMs;
        QC::u64 executionDurationMs;
        QC::u32 weightCost;
        QC::u64 deadline; // Optional deadline

        // Affinity
        QC::u32 cpuAffinity; // Bitmask
    };

    class Executor
    {
    public:
        enum class TaskLogEvent : QC::u8
        {
            Submitted = 0,
            PolicyDecision,
            RedundancyDetected,
            StateChange,
            Completed
        };

        using TaskLogHook = void (*)(TaskLogEvent event, const TaskDescriptor &task, void *userData);

        struct PerformanceCounters
        {
            QC::u64 totalSubmitted = 0;
            QC::u64 totalExecuted = 0;
            QC::u64 totalCachedCompletions = 0;

            QC::u64 totalBuildMs = 0;
            QC::u64 totalExecutionMs = 0;
            QC::u64 totalQueueDelayMs = 0;

            QC::u64 schedulerPromotions = 0;
            QC::u64 schedulerDemotions = 0;
            QC::u64 crossFlowPromotions = 0;
            QC::u64 crossFlowDemotions = 0;

            QC::u64 policyAllow = 0;
            QC::u64 policyThrottle = 0;
            QC::u64 policySuspend = 0;
            QC::u64 policyCancel = 0;

            QC::u64 redundantSubmissions = 0;

            QC::u64 memoHits = 0;
            QC::u64 memoMisses = 0;
            QC::u64 memoRefused = 0;
            QC::u64 memoEvictions = 0;
            QC::u64 memoSafetyRejected = 0;
        };

        static Executor &instance();

        // Security Center policy hook (defaults to nullptr => allow).
        void setFlowPolicy(FlowPolicyFn fn) { m_flowPolicy = fn; }
        FlowPolicyFn flowPolicy() const { return m_flowPolicy; }

        void initialize(QC::usize workerCount);
        void shutdown();

        void setLogHook(TaskLogHook hook, void *userData = nullptr)
        {
            m_logHook = hook;
            m_logHookUser = userData;
        }

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
        QC::u64 totalCachedCompletions() const { return m_totalCachedCompletions; }
        QC::u64 totalBuildMilliseconds() const { return m_totalBuildMs; }
        QC::u64 totalExecutionMilliseconds() const { return m_totalExecutionMs; }
        QC::u64 averageBuildMilliseconds() const { return m_totalSubmitted ? (m_totalBuildMs / m_totalSubmitted) : 0; }
        QC::u64 averageExecutionMilliseconds() const { return m_totalExecuted ? (m_totalExecutionMs / m_totalExecuted) : 0; }
        QC::u64 schedulerPromotions() const { return m_schedulerPromotions; }
        QC::u64 schedulerDemotions() const { return m_schedulerDemotions; }
        QC::u64 totalQueueDelayMilliseconds() const { return m_totalQueueDelayMs; }
        QC::u64 averageQueueDelayMilliseconds() const { return m_totalExecuted ? (m_totalQueueDelayMs / m_totalExecuted) : 0; }
        QC::u64 crossFlowPromotions() const { return m_crossFlowPromotions; }
        QC::u64 crossFlowDemotions() const { return m_crossFlowDemotions; }
        QC::u64 policyAllowCount() const { return m_policyAllowCount; }
        QC::u64 policyThrottleCount() const { return m_policyThrottleCount; }
        QC::u64 policySuspendCount() const { return m_policySuspendCount; }
        QC::u64 policyCancelCount() const { return m_policyCancelCount; }
        QC::u64 redundantSubmissions() const { return m_redundantSubmissions; }
        TaskId nextTaskIdForDebug() const { return m_nextTaskId; }
        PerformanceCounters performanceCounters() const;

        // Recent task listing (best-effort but deterministic)
        QC::usize recentTaskIdCount() const { return m_recentCount; }
        TaskId recentTaskIdAt(QC::usize idx) const;

        // Memoization cache controls (global gate; per-task still must be cacheEligible).
        void setMemoizationEnabled(bool enabled) { m_memoizationEnabled = enabled; }
        bool memoizationEnabled() const { return m_memoizationEnabled; }
        void clearMemoizationCache();
        QC::usize pruneMemoizationCache(QC::usize targetEntries);
        QC::usize memoizationCacheEntries() const;
        QC::usize memoizationCacheCapacity() const;
        QC::u64 memoizationHits() const { return m_memoHits; }
        QC::u64 memoizationMisses() const { return m_memoMisses; }
        QC::u64 memoizationRefused() const { return m_memoRefused; }
        QC::u64 memoizationEvictions() const { return m_memoEvictions; }
        QC::u64 memoizationSafetyRejected() const { return m_memoSafetyRejected; }
        void setResultCachePolicy(ResultCachePolicy policy);
        ResultCachePolicy resultCachePolicy() const;

        // Memoization allowlist (signatureHash-based, checked for cached submits)
        void setMemoizationAllowlistEnabled(bool enabled) { m_memoAllowlistEnabled = enabled; }
        bool memoizationAllowlistEnabled() const { return m_memoAllowlistEnabled; }
        void clearMemoizationAllowlist();
        bool memoizationAllowlistAdd(const QC::u8 sig[32]);
        bool memoizationAllowlistRemove(const QC::u8 sig[32]);
        bool memoizationAllowlistContains(const QC::u8 sig[32]) const;
        bool memoizationAllowlistEntryAt(QC::usize index, QC::u8 outSig[32]) const;
        QC::usize memoizationAllowlistCount() const { return m_memoAllowlistCount; }

        QC::usize copySignatureMetrics(SignatureMetricsSnapshot *out, QC::usize cap) const;
        QC::usize copyFlowStatistics(FlowStatistics *out, QC::usize cap) const;
        void supervisorLoopOnce();
        TaskFlowQueueSnapshot taskFlowQueueSnapshot() const;
        QC::u64 mergedSubmissions() const { return m_mergedSubmissions; }
        void promoteFlow(const char *origin);
        void demoteFlow(const char *origin);

        QC::Status resizeCorePool(QC::usize workerCount);
        QC::usize corePoolSize() const { return m_workerCount; }

        static void hashFunctionInput(const void *arg, QC::usize argSize, QC::u8 outHash[32]);
        static void hashFunctionSignature(const char *name,
                                          const char *moduleId,
                                          TaskFunction func,
                                          const void *arg,
                                          QC::usize argSize,
                                          QC::u8 outSig[32]);

        // Scheduler access
        Scheduler *scheduler() { return m_scheduler; }
        const Scheduler *scheduler() const { return m_scheduler; }

    private:
        struct SignatureMetricsEntry
        {
            bool valid = false;
            QC::u8 sig[32] = {0};
            QC::u64 submissions = 0;
            QC::u64 completions = 0;
            QC::u64 memoHits = 0;
            QC::u64 totalBuildMs = 0;
            QC::u64 totalExecMs = 0;
        };

        Executor();
        ~Executor();
        Executor(const Executor &) = delete;
        Executor &operator=(const Executor &) = delete;

        TaskId allocateTaskId();
        TaskDescriptor *findTask(TaskId id);
        bool areDependenciesMet(TaskDescriptor *task);
        void executeTask(TaskDescriptor *task);
        void recentPush(TaskId id);
        void pumpReadyTasks();
        bool hasLiveRedundantTask(const QC::u8 sig[32], const QC::u8 in[32]) const;
        QC::usize activeCountForOrigin(const char *origin) const;
        QC::usize distinctActiveOriginCount() const;
        void emitLog(TaskLogEvent event, const TaskDescriptor *task) const;
        SignatureMetricsEntry *findOrCreateSignatureMetrics(const QC::u8 sig[32]);
        const SignatureMetricsEntry *findSignatureMetrics(const QC::u8 sig[32]) const;
        TaskDescriptor *findMergeCandidate(const QC::u8 sig[32], const QC::u8 in[32]) const;
        TaskPriority chooseAdaptivePriority(const TaskDescriptor &task, TaskPriority requestedPriority);
        QC::u32 estimateWeightCost(const TaskDescriptor &task) const;
        void recordBuildMetric(TaskDescriptor *task);
        void recordExecutionMetric(TaskDescriptor *task, bool memoHit);

        struct FlowBiasEntry
        {
            bool valid = false;
            char origin[32] = {0};
            QC::i32 bias = 0;
            QC::u64 promotions = 0;
            QC::u64 demotions = 0;
        };

        FlowBiasEntry *findOrCreateFlowBias(const char *origin);
        const FlowBiasEntry *findFlowBias(const char *origin) const;

        FlowPolicyFn m_flowPolicy = nullptr;

        QC::Vector<TaskDescriptor *> m_tasks;
        Scheduler *m_scheduler;

        TaskId m_nextTaskId;
        QC::u64 m_totalSubmitted;
        QC::u64 m_totalExecuted;
        QC::u64 m_totalCachedCompletions;
        QC::u64 m_totalBuildMs;
        QC::u64 m_totalExecutionMs;
        QC::u64 m_totalQueueDelayMs;
        QC::u64 m_schedulerPromotions;
        QC::u64 m_schedulerDemotions;
        QC::u64 m_crossFlowPromotions;
        QC::u64 m_crossFlowDemotions;
        QC::u64 m_policyAllowCount;
        QC::u64 m_policyThrottleCount;
        QC::u64 m_policySuspendCount;
        QC::u64 m_policyCancelCount;
        QC::u64 m_redundantSubmissions;
        QC::u64 m_mergedSubmissions;
        QC::u32 m_flowQueueLow;
        QC::u32 m_flowQueueMedium;
        QC::u32 m_flowQueueHigh;
        QC::u64 m_supervisorTicks;

        static constexpr QC::usize MAX_SIGNATURE_METRICS = 128;
        SignatureMetricsEntry m_signatureMetrics[MAX_SIGNATURE_METRICS];
        static constexpr QC::usize MAX_FLOW_BIASES = 32;
        FlowBiasEntry m_flowBiases[MAX_FLOW_BIASES];

        bool m_memoizationEnabled = false;
        QC::u64 m_memoHits = 0;
        QC::u64 m_memoMisses = 0;
        QC::u64 m_memoRefused = 0;
        QC::u64 m_memoEvictions = 0;
        QC::u64 m_memoSafetyRejected = 0;

        bool m_memoAllowlistEnabled = true;
        QC::usize m_memoAllowlistCount = 0;
        QC::u8 m_memoAllowlist[64][32];

        TaskId m_recentIds[128];
        QC::usize m_recentHead = 0;
        QC::usize m_recentCount = 0;

        TaskLogHook m_logHook = nullptr;
        void *m_logHookUser = nullptr;

        bool m_running;
        QC::usize m_workerCount;
    };

} // namespace QQ
