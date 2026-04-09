#include "QQExecutor.h"

#include "QQScheduler.h"

#include "QCString.h"

#include "QCSha256.h"
#include "QCCanonicalArgs.h"

#include "QCLogger.h"

#include "QKTime.h"

namespace QQ
{
    namespace
    {
        constexpr QC::usize kMemoMaxData = 256;

        struct MemoEntry
        {
            bool valid;
            QC::u8 sig[32];
            QC::u8 in[32];
            TaskResult result;
            QC::u8 data[kMemoMaxData];
            QC::u64 hits;
            QC::u64 lastUsed;
        };

        constexpr QC::usize kMemoCap = 64;
        static MemoEntry g_memo[kMemoCap];
        static QC::u64 g_memoEvictions = 0;
        static QC::u64 g_memoClock = 0;
        static ResultCachePolicy g_cachePolicy = ResultCachePolicy::LRU;

        static bool hashEq(const QC::u8 a[32], const QC::u8 b[32])
        {
            for (int i = 0; i < 32; ++i)
                if (a[i] != b[i])
                    return false;
            return true;
        }

        static QC::u32 priorityScore(TaskPriority priority)
        {
            return static_cast<QC::u32>(priority);
        }

        static char lowerAscii(char c)
        {
            if (c >= 'A' && c <= 'Z')
                return static_cast<char>(c - 'A' + 'a');
            return c;
        }

        static bool containsCaseInsensitive(const char *haystack, const char *needle)
        {
            if (!haystack || !needle || !*needle)
                return false;
            for (const char *h = haystack; *h; ++h)
            {
                const char *a = h;
                const char *b = needle;
                while (*a && *b && lowerAscii(*a) == lowerAscii(*b))
                {
                    ++a;
                    ++b;
                }
                if (*b == 0)
                    return true;
            }
            return false;
        }

        static bool isUnsafeMemoDomain(const char *tag)
        {
            if (!tag || !*tag)
                return false;
            static const char *kUnsafeTokens[] = {
                "io", "file", "fs", "disk", "net", "tcp", "udp", "dns", "http", "socket",
                "time", "clock", "rand", "security", "driver", "qdrv"
            };
            for (QC::usize i = 0; i < (sizeof(kUnsafeTokens) / sizeof(kUnsafeTokens[0])); ++i)
            {
                if (containsCaseInsensitive(tag, kUnsafeTokens[i]))
                    return true;
            }
            return false;
        }

        static bool hasCanonicalArgs(const TaskDescriptor *task)
        {
            if (!task || !task->argument || task->argumentSize < sizeof(QC::CanonicalArgs::Header))
                return false;
            QC::CanonicalArgs::Header hdr;
            const void *payload = nullptr;
            QC::usize payloadLen = 0;
            return QC::CanonicalArgs::parse(task->argument, task->argumentSize, hdr, payload, payloadLen);
        }

        static bool passesMemoSafetyRules(const TaskDescriptor *task)
        {
            if (!task)
                return false;

            // Cache only deterministic/pure candidates for now.
            if (!hasCanonicalArgs(task))
                return false;
            if (task->context != nullptr)
                return false;
            if (isUnsafeMemoDomain(task->origin) || isUnsafeMemoDomain(task->moduleId))
                return false;
            return true;
        }

        static bool memoLookup(const QC::u8 sig[32], const QC::u8 in[32], TaskResult &out)
        {
            for (QC::usize i = 0; i < kMemoCap; ++i)
            {
                MemoEntry &e = g_memo[i];
                if (!e.valid)
                    continue;
                if (hashEq(e.sig, sig) && hashEq(e.in, in))
                {
                    ++e.hits;
                    e.lastUsed = ++g_memoClock;
                    out = e.result;
                    return true;
                }
            }
            return false;
        }

        static QC::usize selectMemoVictim()
        {
            for (QC::usize i = 0; i < kMemoCap; ++i)
            {
                if (!g_memo[i].valid)
                    return i;
            }

            QC::usize victim = 0;
            for (QC::usize i = 1; i < kMemoCap; ++i)
            {
                if (g_cachePolicy == ResultCachePolicy::LFU)
                {
                    if (g_memo[i].hits < g_memo[victim].hits ||
                        (g_memo[i].hits == g_memo[victim].hits && g_memo[i].lastUsed < g_memo[victim].lastUsed))
                    {
                        victim = i;
                    }
                }
                else if (g_memo[i].lastUsed < g_memo[victim].lastUsed)
                {
                    victim = i;
                }
            }
            return victim;
        }

        static bool memoStore(const QC::u8 sig[32], const QC::u8 in[32], const TaskResult &res)
        {
            // Only cache safe results: either no data, or small enough to copy.
            if (res.data && res.dataSize > kMemoMaxData)
                return false;

            MemoEntry &e = g_memo[selectMemoVictim()];
            if (e.valid)
                ++g_memoEvictions;
            e.valid = true;
            for (int i = 0; i < 32; ++i)
            {
                e.sig[i] = sig[i];
                e.in[i] = in[i];
            }

            e.result = res;
            if (res.data && res.dataSize)
            {
                QC::String::memcpy(e.data, res.data, res.dataSize);
                e.result.data = e.data;
            }
            else
            {
                e.result.data = nullptr;
                e.result.dataSize = 0;
            }
            e.hits = 0;
            e.lastUsed = ++g_memoClock;
            return true;
        }
    }

    static void logTaskExec(const TaskDescriptor *task, QC::u64 execMs);

    static QC::u8 flowBand(TaskPriority p)
    {
        switch (p)
        {
        case TaskPriority::Lowest:
        case TaskPriority::Low:
            return 0; // low
        case TaskPriority::Normal:
            return 1; // medium
        case TaskPriority::High:
        case TaskPriority::Highest:
        case TaskPriority::Critical:
            return 2; // high
        default:
            return 1;
        }
    }

    Executor &Executor::instance()
    {
        static Executor inst;
        return inst;
    }

    Executor::Executor()
        : m_tasks(),
          m_scheduler(nullptr),
          m_nextTaskId(1),
          m_totalSubmitted(0),
          m_totalExecuted(0),
          m_totalCachedCompletions(0),
          m_totalBuildMs(0),
          m_totalExecutionMs(0),
                    m_totalQueueDelayMs(0),
          m_schedulerPromotions(0),
                    m_schedulerDemotions(0),
                    m_crossFlowPromotions(0),
                    m_crossFlowDemotions(0),
                    m_policyAllowCount(0),
                    m_policyThrottleCount(0),
                    m_policySuspendCount(0),
                    m_policyCancelCount(0),
                    m_redundantSubmissions(0),
                      m_mergedSubmissions(0),
                      m_flowQueueLow(0),
                      m_flowQueueMedium(0),
                      m_flowQueueHigh(0),
                      m_supervisorTicks(0),
          m_running(false),
          m_workerCount(0)
    {
        for (QC::usize i = 0; i < 128; ++i)
            m_recentIds[i] = INVALID_TASK;
        for (QC::usize i = 0; i < MAX_SIGNATURE_METRICS; ++i)
            m_signatureMetrics[i].valid = false;
                    for (QC::usize i = 0; i < MAX_FLOW_BIASES; ++i)
                        m_flowBiases[i].valid = false;
    }

    Executor::~Executor()
    {
    }

    TaskId Executor::recentTaskIdAt(QC::usize idx) const
    {
        if (idx >= m_recentCount)
            return INVALID_TASK;
        // idx=0 is most recent
        const QC::usize pos = (m_recentHead + 128 - 1 - idx) % 128;
        return m_recentIds[pos];
    }

    void Executor::recentPush(TaskId id)
    {
        m_recentIds[m_recentHead % 128] = id;
        m_recentHead = (m_recentHead + 1) % 128;
        if (m_recentCount < 128)
            ++m_recentCount;
    }

    void Executor::clearMemoizationCache()
    {
        for (QC::usize i = 0; i < kMemoCap; ++i)
            g_memo[i].valid = false;
        g_memoEvictions = 0;
        g_memoClock = 0;
        m_memoHits = 0;
        m_memoMisses = 0;
        m_memoRefused = 0;
        m_memoEvictions = 0;
        m_memoSafetyRejected = 0;
    }

    QC::usize Executor::pruneMemoizationCache(QC::usize targetEntries)
    {
        QC::usize entries = memoizationCacheEntries();
        while (entries > targetEntries)
        {
            const QC::usize victim = selectMemoVictim();
            if (!g_memo[victim].valid)
                break;
            g_memo[victim].valid = false;
            g_memo[victim].hits = 0;
            g_memo[victim].lastUsed = 0;
            --entries;
        }
        return entries;
    }

    QC::usize Executor::memoizationCacheEntries() const
    {
        QC::usize used = 0;
        for (QC::usize i = 0; i < kMemoCap; ++i)
        {
            if (g_memo[i].valid)
                ++used;
        }
        return used;
    }

    QC::usize Executor::memoizationCacheCapacity() const
    {
        return kMemoCap;
    }

    void Executor::setResultCachePolicy(ResultCachePolicy policy)
    {
        g_cachePolicy = policy;
    }

    ResultCachePolicy Executor::resultCachePolicy() const
    {
        return g_cachePolicy;
    }

    const Executor::SignatureMetricsEntry *Executor::findSignatureMetrics(const QC::u8 sig[32]) const
    {
        if (!sig)
            return nullptr;
        for (QC::usize i = 0; i < MAX_SIGNATURE_METRICS; ++i)
        {
            if (!m_signatureMetrics[i].valid)
                continue;
            if (hashEq(m_signatureMetrics[i].sig, sig))
                return &m_signatureMetrics[i];
        }
        return nullptr;
    }

    Executor::SignatureMetricsEntry *Executor::findOrCreateSignatureMetrics(const QC::u8 sig[32])
    {
        if (!sig)
            return nullptr;
        for (QC::usize i = 0; i < MAX_SIGNATURE_METRICS; ++i)
        {
            if (m_signatureMetrics[i].valid && hashEq(m_signatureMetrics[i].sig, sig))
                return &m_signatureMetrics[i];
        }
        for (QC::usize i = 0; i < MAX_SIGNATURE_METRICS; ++i)
        {
            if (m_signatureMetrics[i].valid)
                continue;
            m_signatureMetrics[i].valid = true;
            for (int j = 0; j < 32; ++j)
                m_signatureMetrics[i].sig[j] = sig[j];
            return &m_signatureMetrics[i];
        }
        // Simple overwrite of the least-recent slot by task id modulo table size is enough for MVP.
        SignatureMetricsEntry &slot = m_signatureMetrics[m_nextTaskId % MAX_SIGNATURE_METRICS];
        slot.valid = true;
        slot.submissions = 0;
        slot.completions = 0;
        slot.memoHits = 0;
        slot.totalBuildMs = 0;
        slot.totalExecMs = 0;
        for (int j = 0; j < 32; ++j)
            slot.sig[j] = sig[j];
        return &slot;
    }

    Executor::FlowBiasEntry *Executor::findOrCreateFlowBias(const char *origin)
    {
        if (!origin || !origin[0])
            return nullptr;
        for (QC::usize i = 0; i < MAX_FLOW_BIASES; ++i)
        {
            if (m_flowBiases[i].valid && QC::String::strcmp(m_flowBiases[i].origin, origin) == 0)
                return &m_flowBiases[i];
        }
        for (QC::usize i = 0; i < MAX_FLOW_BIASES; ++i)
        {
            if (m_flowBiases[i].valid)
                continue;
            m_flowBiases[i].valid = true;
            QC::String::strncpy(m_flowBiases[i].origin, origin, sizeof(m_flowBiases[i].origin) - 1);
            return &m_flowBiases[i];
        }
        return nullptr;
    }

    const Executor::FlowBiasEntry *Executor::findFlowBias(const char *origin) const
    {
        if (!origin || !origin[0])
            return nullptr;
        for (QC::usize i = 0; i < MAX_FLOW_BIASES; ++i)
        {
            if (m_flowBiases[i].valid && QC::String::strcmp(m_flowBiases[i].origin, origin) == 0)
                return &m_flowBiases[i];
        }
        return nullptr;
    }

    TaskDescriptor *Executor::findMergeCandidate(const QC::u8 sig[32], const QC::u8 in[32]) const
    {
        if (!sig || !in)
            return nullptr;
        for (QC::usize i = 0; i < m_tasks.size(); ++i)
        {
            TaskDescriptor *task = m_tasks[i];
            if (!task)
                continue;
            if (task->mergedInto != INVALID_TASK)
                continue;
            if (task->state == TaskState::Completed || task->state == TaskState::Failed || task->state == TaskState::Cancelled)
                continue;
            if (hashEq(task->signatureHash, sig) && hashEq(task->inputHash, in))
                return task;
        }
        return nullptr;
    }

    TaskPriority Executor::chooseAdaptivePriority(const TaskDescriptor &task, TaskPriority requestedPriority)
    {
        TaskPriority decided = requestedPriority;
        const SignatureMetricsEntry *metrics = findSignatureMetrics(task.signatureHash);
        if (metrics && metrics->submissions > 0)
        {
            const QC::u64 avgBuild = metrics->totalBuildMs / metrics->submissions;
            const QC::u64 avgExec = metrics->completions ? (metrics->totalExecMs / metrics->completions) : 0;
            const bool hotExpensive = (avgExec >= 15) || (avgBuild >= 5);
            const bool coldCheap = (avgExec <= 1) && (avgBuild <= 1);
            const bool highlyMemoized = (metrics->memoHits * 2 >= metrics->submissions) && task.cacheEligible;

            if (requestedPriority == TaskPriority::Normal)
            {
                if (hotExpensive)
                    decided = TaskPriority::High;
                else if (coldCheap || highlyMemoized)
                    decided = TaskPriority::Low;
            }
            else if (priorityScore(requestedPriority) < priorityScore(TaskPriority::High) && hotExpensive)
            {
                decided = TaskPriority::High;
            }
        }

        // Cross-flow influence rules: dampen overloaded origins and lightly boost cold origins.
        if (task.origin[0])
        {
            const QC::usize sameOriginActive = activeCountForOrigin(task.origin);
            if (sameOriginActive >= 4 && priorityScore(decided) > priorityScore(TaskPriority::Low))
            {
                decided = static_cast<TaskPriority>(priorityScore(decided) - 1);
                ++m_crossFlowDemotions;
            }

            const QC::usize distinctOrigins = distinctActiveOriginCount();
            if (sameOriginActive == 0 && distinctOrigins >= 2 && priorityScore(decided) < priorityScore(TaskPriority::High))
            {
                decided = static_cast<TaskPriority>(priorityScore(decided) + 1);
                ++m_crossFlowPromotions;
            }
        }

        if (const FlowBiasEntry *bias = findFlowBias(task.origin))
        {
            QC::i32 adjusted = static_cast<QC::i32>(priorityScore(decided)) + bias->bias;
            if (adjusted < static_cast<QC::i32>(TaskPriority::Lowest))
                adjusted = static_cast<QC::i32>(TaskPriority::Lowest);
            if (adjusted > static_cast<QC::i32>(TaskPriority::Critical))
                adjusted = static_cast<QC::i32>(TaskPriority::Critical);
            decided = static_cast<TaskPriority>(adjusted);
        }

        if (priorityScore(decided) > priorityScore(requestedPriority))
            ++m_schedulerPromotions;
        else if (priorityScore(decided) < priorityScore(requestedPriority))
            ++m_schedulerDemotions;
        return decided;
    }

    QC::u32 Executor::estimateWeightCost(const TaskDescriptor &task) const
    {
        QC::u64 cost = 1;

        // Baseline from priority class and graph fan-in.
        cost += static_cast<QC::u64>(priorityScore(task.priority) + 1) * 10;
        cost += static_cast<QC::u64>(task.dependencies.size()) * 15;

        // Input size pressure.
        cost += static_cast<QC::u64>(task.argumentSize / 32);

        // Historical timing pressure if we have observed this signature before.
        if (const SignatureMetricsEntry *m = findSignatureMetrics(task.signatureHash))
        {
            if (m->submissions)
                cost += (m->totalBuildMs / m->submissions);
            if (m->completions)
                cost += (m->totalExecMs / m->completions);
        }

        if (cost > 0xFFFFFFFFULL)
            cost = 0xFFFFFFFFULL;
        return static_cast<QC::u32>(cost);
    }

    void Executor::recordBuildMetric(TaskDescriptor *task)
    {
        if (!task)
            return;
        ++m_totalSubmitted;
        m_totalBuildMs += task->buildDurationMs;
        if (SignatureMetricsEntry *entry = findOrCreateSignatureMetrics(task->signatureHash))
        {
            ++entry->submissions;
            entry->totalBuildMs += task->buildDurationMs;
        }
    }

    void Executor::recordExecutionMetric(TaskDescriptor *task, bool memoHit)
    {
        if (!task)
            return;
        m_totalExecutionMs += task->executionDurationMs;
        m_totalQueueDelayMs += task->queueDelayMs;
        if (SignatureMetricsEntry *entry = findOrCreateSignatureMetrics(task->signatureHash))
        {
            if (memoHit)
                ++entry->memoHits;
            if (task->state == TaskState::Completed || task->state == TaskState::Failed)
            {
                ++entry->completions;
                entry->totalExecMs += task->executionDurationMs;
            }
        }
    }

    void Executor::clearMemoizationAllowlist()
    {
        m_memoAllowlistCount = 0;
        for (QC::usize i = 0; i < 64; ++i)
            for (QC::usize j = 0; j < 32; ++j)
                m_memoAllowlist[i][j] = 0;
    }

    bool Executor::memoizationAllowlistContains(const QC::u8 sig[32]) const
    {
        if (!sig)
            return false;
        for (QC::usize i = 0; i < m_memoAllowlistCount; ++i)
        {
            bool eq = true;
            for (int j = 0; j < 32; ++j)
            {
                if (m_memoAllowlist[i][j] != sig[j])
                {
                    eq = false;
                    break;
                }
            }
            if (eq)
                return true;
        }
        return false;
    }

    bool Executor::memoizationAllowlistEntryAt(QC::usize index, QC::u8 outSig[32]) const
    {
        if (!outSig || index >= m_memoAllowlistCount)
            return false;
        for (int j = 0; j < 32; ++j)
            outSig[j] = m_memoAllowlist[index][j];
        return true;
    }

    bool Executor::memoizationAllowlistAdd(const QC::u8 sig[32])
    {
        if (!sig)
            return false;
        if (memoizationAllowlistContains(sig))
            return true;
        if (m_memoAllowlistCount >= 64)
            return false;
        for (int j = 0; j < 32; ++j)
            m_memoAllowlist[m_memoAllowlistCount][j] = sig[j];
        ++m_memoAllowlistCount;
        return true;
    }

    bool Executor::memoizationAllowlistRemove(const QC::u8 sig[32])
    {
        if (!sig)
            return false;
        for (QC::usize i = 0; i < m_memoAllowlistCount; ++i)
        {
            bool eq = true;
            for (int j = 0; j < 32; ++j)
            {
                if (m_memoAllowlist[i][j] != sig[j])
                {
                    eq = false;
                    break;
                }
            }
            if (!eq)
                continue;

            const QC::usize last = m_memoAllowlistCount - 1;
            if (i != last)
            {
                for (int j = 0; j < 32; ++j)
                    m_memoAllowlist[i][j] = m_memoAllowlist[last][j];
            }
            for (int j = 0; j < 32; ++j)
                m_memoAllowlist[last][j] = 0;
            --m_memoAllowlistCount;
            return true;
        }
        return false;
    }

    void Executor::initialize(QC::usize workerCount)
    {
        (void)resizeCorePool(workerCount);
        m_running = true;
    }

    void Executor::shutdown()
    {
        cancelAll();
        if (m_scheduler)
        {
            m_scheduler->shutdown();
            delete m_scheduler;
            m_scheduler = nullptr;
        }
        m_running = false;
        m_workerCount = 0;
    }

    QC::Status Executor::resizeCorePool(QC::usize workerCount)
    {
        if (!m_scheduler)
            m_scheduler = new Scheduler();
        if (!m_scheduler)
            return QC::Status::OutOfMemory;
        m_scheduler->initialize(workerCount);
        m_workerCount = workerCount;
        return QC::Status::Success;
    }

    TaskId Executor::allocateTaskId()
    {
        if (m_nextTaskId == INVALID_TASK)
            ++m_nextTaskId;
        return m_nextTaskId++;
    }

    TaskDescriptor *Executor::findTask(TaskId id)
    {
        for (QC::usize i = 0; i < m_tasks.size(); ++i)
        {
            TaskDescriptor *t = m_tasks[i];
            if (t && t->id == id)
                return t;
        }
        return nullptr;
    }

    bool Executor::areDependenciesMet(TaskDescriptor *task)
    {
        if (!task)
            return false;
        for (QC::usize i = 0; i < task->dependencies.size(); ++i)
        {
            const TaskDependency &dep = task->dependencies[i];
            TaskDescriptor *d = findTask(dep.taskId);
            if (!d)
                return false;
            if (d->state != TaskState::Completed)
                return false;
        }
        return true;
    }

    bool Executor::hasLiveRedundantTask(const QC::u8 sig[32], const QC::u8 in[32]) const
    {
        if (!sig || !in)
            return false;
        for (QC::usize i = 0; i < m_tasks.size(); ++i)
        {
            const TaskDescriptor *t = m_tasks[i];
            if (!t)
                continue;
            if (t->state == TaskState::Completed || t->state == TaskState::Failed || t->state == TaskState::Cancelled)
                continue;
            if (hashEq(t->signatureHash, sig) && hashEq(t->inputHash, in))
                return true;
        }
        return false;
    }

    QC::usize Executor::activeCountForOrigin(const char *origin) const
    {
        if (!origin || !origin[0])
            return 0;
        QC::usize c = 0;
        for (QC::usize i = 0; i < m_tasks.size(); ++i)
        {
            const TaskDescriptor *t = m_tasks[i];
            if (!t)
                continue;
            if (t->state == TaskState::Completed || t->state == TaskState::Failed || t->state == TaskState::Cancelled)
                continue;
            if (QC::String::strcmp(t->origin, origin) == 0)
                ++c;
        }
        return c;
    }

    QC::usize Executor::distinctActiveOriginCount() const
    {
        QC::usize count = 0;
        char seen[16][32];
        for (QC::usize i = 0; i < 16; ++i)
            QC::String::memset(seen[i], 0, sizeof(seen[i]));

        for (QC::usize i = 0; i < m_tasks.size(); ++i)
        {
            const TaskDescriptor *t = m_tasks[i];
            if (!t || !t->origin[0])
                continue;
            if (t->state == TaskState::Completed || t->state == TaskState::Failed || t->state == TaskState::Cancelled)
                continue;

            bool known = false;
            for (QC::usize s = 0; s < count; ++s)
            {
                if (QC::String::strcmp(seen[s], t->origin) == 0)
                {
                    known = true;
                    break;
                }
            }
            if (known)
                continue;
            if (count < 16)
            {
                QC::String::strncpy(seen[count], t->origin, sizeof(seen[count]) - 1);
                ++count;
            }
        }

        return count;
    }

    void Executor::emitLog(TaskLogEvent event, const TaskDescriptor *task) const
    {
        if (!m_logHook || !task)
            return;
        m_logHook(event, *task, m_logHookUser);
    }

    void Executor::executeTask(TaskDescriptor *task)
    {
        if (!task)
            return;
        QC::u32 workerId = 0;
        if (m_workerCount)
            workerId = static_cast<QC::u32>((task->id - 1) % m_workerCount);
        if (m_scheduler)
        {
            if (WorkerState *worker = m_scheduler->workerState(workerId))
            {
                worker->currentTask = task->id;
                worker->active = true;
                worker->queueLength = pendingCount();
            }
        }
        if (!task->function)
        {
            task->state = TaskState::Failed;
            task->result = TaskResult{false, 0, nullptr, 0};
            return;
        }

        task->state = TaskState::Running;
        emitLog(TaskLogEvent::StateChange, task);
        task->startTime = QK::Time::milliseconds();
        task->queueDelayMs = (task->startTime >= task->queueTime) ? (task->startTime - task->queueTime) : 0;
        task->result = task->function(task->context, task->argument);
        task->endTime = QK::Time::milliseconds();
        task->executionDurationMs = (task->endTime >= task->startTime) ? (task->endTime - task->startTime) : 0;
        task->state = task->result.success ? TaskState::Completed : TaskState::Failed;
        task->weightCost = estimateWeightCost(*task);
        emitLog(TaskLogEvent::Completed, task);
        ++m_totalExecuted;
        if (m_scheduler)
        {
            if (WorkerState *worker = m_scheduler->workerState(workerId))
            {
                worker->currentTask = INVALID_TASK;
                worker->active = true;
                worker->queueLength = pendingCount();
                worker->cpuTime += task->executionDurationMs;
            }
        }
    }

    void Executor::pumpReadyTasks()
    {
        m_flowQueueLow = 0;
        m_flowQueueMedium = 0;
        m_flowQueueHigh = 0;
        for (;;)
        {
            TaskDescriptor *best = nullptr;
            for (QC::usize i = 0; i < m_tasks.size(); ++i)
            {
                TaskDescriptor *task = m_tasks[i];
                if (!task)
                    continue;
                if (!(task->state == TaskState::Pending || task->state == TaskState::Queued || task->state == TaskState::Blocked))
                    continue;
                if (!areDependenciesMet(task))
                {
                    task->state = TaskState::Blocked;
                    continue;
                }
                if (task->state == TaskState::Pending || task->state == TaskState::Blocked)
                    task->state = TaskState::Queued;
                const QC::u8 band = flowBand(task->priority);
                if (band == 0)
                    ++m_flowQueueLow;
                else if (band == 1)
                    ++m_flowQueueMedium;
                else
                    ++m_flowQueueHigh;
                if (!best || priorityScore(task->priority) > priorityScore(best->priority) ||
                    (task->priority == best->priority && task->id < best->id))
                {
                    best = task;
                }
            }

            if (!best)
                break;

            if (best->mergedInto != INVALID_TASK)
            {
                TaskDescriptor *source = findTask(best->mergedInto);
                if (!source || !isComplete(source->id))
                {
                    best->state = TaskState::Blocked;
                    continue;
                }

                best->startTime = QK::Time::milliseconds();
                best->endTime = best->startTime;
                best->queueDelayMs = (best->startTime >= best->queueTime) ? (best->startTime - best->queueTime) : 0;
                best->executionDurationMs = 0;
                best->result = source->result;
                best->state = source->state;
                best->weightCost = estimateWeightCost(*best);
                emitLog(TaskLogEvent::Completed, best);
                recordExecutionMetric(best, false);
                continue;
            }

            if (m_memoizationEnabled && best->cacheEligible)
            {
                TaskResult cached;
                if (memoLookup(best->signatureHash, best->inputHash, cached))
                {
                    ++m_memoHits;
                    m_memoEvictions = g_memoEvictions;
                    best->result = cached;
                    best->startTime = QK::Time::milliseconds();
                    best->endTime = best->startTime;
                    best->queueDelayMs = (best->startTime >= best->queueTime) ? (best->startTime - best->queueTime) : 0;
                    best->executionDurationMs = 0;
                    best->state = cached.success ? TaskState::Completed : TaskState::Failed;
                    best->weightCost = estimateWeightCost(*best);
                    if (best->state == TaskState::Completed)
                        ++m_totalCachedCompletions;
                    emitLog(TaskLogEvent::Completed, best);
                    recordExecutionMetric(best, true);
                    logTaskExec(best, 0);
                    continue;
                }
                ++m_memoMisses;
                m_memoEvictions = g_memoEvictions;
            }

            executeTask(best);

            if (m_memoizationEnabled && best->cacheEligible)
            {
                if (!memoStore(best->signatureHash, best->inputHash, best->result))
                    ++m_memoRefused;
                m_memoEvictions = g_memoEvictions;
            }

            recordExecutionMetric(best, false);
            logTaskExec(best, best->executionDurationMs);
        }
    }

    void Executor::supervisorLoopOnce()
    {
        ++m_supervisorTicks;
        pumpReadyTasks();
        if (m_scheduler)
            m_scheduler->tick(QK::Time::milliseconds());
    }

    TaskFlowQueueSnapshot Executor::taskFlowQueueSnapshot() const
    {
        TaskFlowQueueSnapshot snap{};
        snap.low = m_flowQueueLow;
        snap.medium = m_flowQueueMedium;
        snap.high = m_flowQueueHigh;
        return snap;
    }

    static void CopyName(char dst[64], const char *src)
    {
        if (!dst)
            return;
        dst[0] = 0;
        if (!src)
            return;
        QC::String::strncpy(dst, src, 63);
        dst[63] = 0;
    }

    static void CopyTag(char dst[32], const char *src)
    {
        if (!dst)
            return;
        dst[0] = 0;
        if (!src)
            return;
        QC::String::strncpy(dst, src, 31);
        dst[31] = 0;
    }

    static void ThrottleDelayMs(QC::u64 milliseconds)
    {
        if (milliseconds == 0)
            return;
        QK::Time::sleep(milliseconds);
    }

    static void computeTaskHashes(TaskDescriptor *task)
    {
        if (!task)
            return;
        Executor::hashFunctionSignature(task->name, task->moduleId, task->function, task->argument, task->argumentSize, task->signatureHash);
        Executor::hashFunctionInput(task->argument, task->argumentSize, task->inputHash);
    }

    void Executor::hashFunctionInput(const void *arg, QC::usize argSize, QC::u8 outHash[32])
    {
        if (!outHash)
            return;
        if (arg && argSize)
        {
            QC::Sha256(reinterpret_cast<const QC::u8 *>(arg), argSize, outHash);
            return;
        }
        const QC::usize p = reinterpret_cast<QC::usize>(arg);
        QC::Sha256(reinterpret_cast<const QC::u8 *>(&p), sizeof(p), outHash);
    }

    void Executor::hashFunctionSignature(const char *name,
                                         const char *moduleId,
                                         TaskFunction func,
                                         const void *arg,
                                         QC::usize argSize,
                                         QC::u8 outSig[32])
    {
        if (!outSig)
            return;

        QC::CanonicalArgs::Header hdr{};
        const void *payload = nullptr;
        QC::usize payloadLen = 0;
        const bool hasCanonical = (arg && argSize >= sizeof(QC::CanonicalArgs::Header)) &&
                                  QC::CanonicalArgs::parse(arg, argSize, hdr, payload, payloadLen);

        char sigBuf[256];
        QC::String::memset(sigBuf, 0, sizeof(sigBuf));
        QC::usize idx = 0;
        auto append = [&](const char *s) {
            if (!s)
                return;
            for (int i = 0; s[i] && idx + 1 < sizeof(sigBuf); ++i)
                sigBuf[idx++] = s[i];
            sigBuf[idx] = 0;
        };

        append("mod=");
        append((moduleId && moduleId[0]) ? moduleId : "(none)");
        append(";fn=");
        {
            const QC::usize fn = reinterpret_cast<QC::usize>(func);
            char fnBuf[32];
            QC::String::memset(fnBuf, 0, sizeof(fnBuf));
            QC::usize tmp = fn;
            char rev[32];
            int r = 0;
            if (tmp == 0)
                rev[r++] = '0';
            while (tmp && r < 31)
            {
                rev[r++] = static_cast<char>('0' + (tmp % 10));
                tmp /= 10;
            }
            int w = 0;
            for (int i = r - 1; i >= 0 && w < 31; --i)
                fnBuf[w++] = rev[i];
            fnBuf[w] = 0;
            append(fnBuf);
        }

        if (hasCanonical)
        {
            append(";schema=");
            char num[16];
            QC::String::memset(num, 0, sizeof(num));
            QC::usize tmp = hdr.schemaId;
            char rev[16];
            int r = 0;
            if (tmp == 0)
                rev[r++] = '0';
            while (tmp && r < 15)
            {
                rev[r++] = static_cast<char>('0' + (tmp % 10));
                tmp /= 10;
            }
            int w = 0;
            for (int i = r - 1; i >= 0 && w < 15; --i)
                num[w++] = rev[i];
            append(num);
            append(";ver=");
            QC::String::memset(num, 0, sizeof(num));
            tmp = hdr.version;
            r = 0;
            if (tmp == 0)
                rev[r++] = '0';
            while (tmp && r < 15)
            {
                rev[r++] = static_cast<char>('0' + (tmp % 10));
                tmp /= 10;
            }
            w = 0;
            for (int i = r - 1; i >= 0 && w < 15; --i)
                num[w++] = rev[i];
            append(num);
        }
        else
        {
            append(";name=");
            append((name && name[0]) ? name : "(none)");
        }

        QC::Sha256(reinterpret_cast<const QC::u8 *>(sigBuf), QC::String::strlen(sigBuf), outSig);
    }

    static void logTaskExec(const TaskDescriptor *task, QC::u64 execMs)
    {
        if (!task)
            return;

        char sigHex[72];
        char inHex[72];
        if (!QC::Sha256DigestToLowerHex(task->signatureHash, sigHex, sizeof(sigHex)))
            sigHex[0] = 0;
        if (!QC::Sha256DigestToLowerHex(task->inputHash, inHex, sizeof(inHex)))
            inHex[0] = 0;

        QC_LOG_INFO("QQuantum", "exec sig=%s in=%s ms=%llu", sigHex, inHex, static_cast<unsigned long long>(execMs));
    }

    TaskId Executor::submit(const char *name, TaskFunction func, void *context, void *arg)
    {
        return submitWithPriorityOriginAndArgSize(name, nullptr, nullptr, func, context, arg, 0, TaskPriority::Normal);
    }

    TaskId Executor::submitWithArgSize(const char *name, TaskFunction func, void *context, void *arg, QC::usize argSize)
    {
        return submitWithPriorityOriginAndArgSize(name, nullptr, nullptr, func, context, arg, argSize, TaskPriority::Normal);
    }

    TaskId Executor::submitWithArgSizeCached(const char *name, TaskFunction func, void *context, void *arg, QC::usize argSize)
    {
        return submitWithPriorityOriginAndArgSizeCached(name, nullptr, nullptr, func, context, arg, argSize, TaskPriority::Normal);
    }

    TaskId Executor::submitWithOrigin(const char *name, const char *origin, const char *moduleId, TaskFunction func, void *context, void *arg)
    {
        return submitWithPriorityOriginAndArgSize(name, origin, moduleId, func, context, arg, 0, TaskPriority::Normal);
    }

    TaskId Executor::submitWithOriginAndArgSize(const char *name, const char *origin, const char *moduleId,
                                               TaskFunction func, void *context, void *arg, QC::usize argSize)
    {
        return submitWithPriorityOriginAndArgSize(name, origin, moduleId, func, context, arg, argSize, TaskPriority::Normal);
    }

    TaskId Executor::submitWithOriginAndArgSizeCached(const char *name, const char *origin, const char *moduleId,
                                                     TaskFunction func, void *context, void *arg, QC::usize argSize)
    {
        return submitWithPriorityOriginAndArgSizeCached(name, origin, moduleId, func, context, arg, argSize, TaskPriority::Normal);
    }

    TaskId Executor::submitWithPriority(const char *name, TaskFunction func, void *context, void *arg, TaskPriority priority)
    {
        return submitWithPriorityOriginAndArgSize(name, nullptr, nullptr, func, context, arg, 0, priority);
    }

    TaskId Executor::submitWithPriorityAndOrigin(const char *name, const char *origin, const char *moduleId,
                                                 TaskFunction func, void *context, void *arg, TaskPriority priority)
    {
        return submitWithPriorityOriginAndArgSize(name, origin, moduleId, func, context, arg, 0, priority);
    }

    TaskId Executor::submitWithPriorityOriginAndArgSizeCached(const char *name, const char *origin, const char *moduleId,
                                                              TaskFunction func, void *context, void *arg, QC::usize argSize,
                                                              TaskPriority priority)
    {
        const QC::u64 buildStart = QK::Time::milliseconds();
        TaskDescriptor *task = new TaskDescriptor();
        task->id = allocateTaskId();
        recentPush(task->id);
        CopyName(task->name, name ? name : "task");
        CopyTag(task->origin, origin);
        CopyTag(task->moduleId, moduleId);
        task->function = func;
        task->context = context;
        task->argument = arg;
        task->argumentSize = argSize;
        QC::String::memset(task->signatureHash, 0, sizeof(task->signatureHash));
        QC::String::memset(task->inputHash, 0, sizeof(task->inputHash));
        task->cacheEligible = true;
        task->state = TaskState::Pending;
        task->priority = priority;
        task->result = TaskResult{false, 0, nullptr, 0};
        task->queueTime = QK::Time::milliseconds();
        task->startTime = 0;
        task->endTime = 0;
        task->queueDelayMs = 0;
        task->buildDurationMs = 0;
        task->executionDurationMs = 0;
        task->weightCost = 0;
        task->deadline = 0;
        task->cpuAffinity = 0;

        computeTaskHashes(task);
        task->priority = chooseAdaptivePriority(*task, priority);
        task->weightCost = estimateWeightCost(*task);

        if (TaskDescriptor *merge = findMergeCandidate(task->signatureHash, task->inputHash))
        {
            task->mergedInto = merge->id;
            task->cacheEligible = false;
            task->dependencies.push_back(TaskDependency{merge->id, false});
            task->state = TaskState::Blocked;
            ++m_mergedSubmissions;
        }

        // Allowlist gate for cached submits.
        if (task->cacheEligible && m_memoAllowlistEnabled && !memoizationAllowlistContains(task->signatureHash))
            task->cacheEligible = false;

        // Safety gate for cached submits (pure + deterministic only).
        if (task->cacheEligible && !passesMemoSafetyRules(task))
        {
            task->cacheEligible = false;
            ++m_memoSafetyRejected;
        }

        if (hasLiveRedundantTask(task->signatureHash, task->inputHash))
        {
            ++m_redundantSubmissions;
            emitLog(TaskLogEvent::RedundancyDetected, task);
        }

        // Security Center policy hook.
        if (m_flowPolicy)
        {
            const FlowDecision d = m_flowPolicy(*task);
            emitLog(TaskLogEvent::PolicyDecision, task);
            if (d.type == FlowDecisionType::ThrottleDelay && d.throttleDelayMs)
            {
                ThrottleDelayMs(d.throttleDelayMs);
                ++m_policyThrottleCount;
            }
            else if (d.type == FlowDecisionType::IsolateCancel)
            {
                task->state = TaskState::Cancelled;
                ++m_policyCancelCount;
            }
            else if (d.type == FlowDecisionType::IsolateSuspend)
            {
                task->state = TaskState::Suspended;
                ++m_policySuspendCount;
            }
            else
            {
                ++m_policyAllowCount;
            }
        }
        else
        {
            ++m_policyAllowCount;
        }

        task->buildDurationMs = QK::Time::milliseconds() - buildStart;
        recordBuildMetric(task);

        m_tasks.push_back(task);
        emitLog(TaskLogEvent::Submitted, task);

        if (task->state == TaskState::Pending)
        {
            supervisorLoopOnce();
        }

        return task->id;
    }

    TaskId Executor::submitWithPriorityOriginAndArgSize(const char *name, const char *origin, const char *moduleId,
                                                        TaskFunction func, void *context, void *arg, QC::usize argSize,
                                                        TaskPriority priority)
    {
        const QC::u64 buildStart = QK::Time::milliseconds();
        TaskDescriptor *task = new TaskDescriptor();
        task->id = allocateTaskId();
        recentPush(task->id);
        CopyName(task->name, name ? name : "task");
        CopyTag(task->origin, origin);
        CopyTag(task->moduleId, moduleId);
        task->function = func;
        task->context = context;
        task->argument = arg;
        task->argumentSize = argSize;
        QC::String::memset(task->signatureHash, 0, sizeof(task->signatureHash));
        QC::String::memset(task->inputHash, 0, sizeof(task->inputHash));
        task->cacheEligible = false;
        task->state = TaskState::Pending;
        task->priority = priority;
        task->result = TaskResult{false, 0, nullptr, 0};
        task->queueTime = QK::Time::milliseconds();
        task->startTime = 0;
        task->endTime = 0;
        task->queueDelayMs = 0;
        task->buildDurationMs = 0;
        task->executionDurationMs = 0;
        task->weightCost = 0;
        task->deadline = 0;
        task->cpuAffinity = 0;

        computeTaskHashes(task);
        task->priority = chooseAdaptivePriority(*task, priority);
        task->weightCost = estimateWeightCost(*task);

        if (TaskDescriptor *merge = findMergeCandidate(task->signatureHash, task->inputHash))
        {
            task->mergedInto = merge->id;
            task->dependencies.push_back(TaskDependency{merge->id, false});
            task->state = TaskState::Blocked;
            ++m_mergedSubmissions;
        }

        if (hasLiveRedundantTask(task->signatureHash, task->inputHash))
        {
            ++m_redundantSubmissions;
            emitLog(TaskLogEvent::RedundancyDetected, task);
        }

        // Security Center policy hook.
        if (m_flowPolicy)
        {
            const FlowDecision d = m_flowPolicy(*task);
            emitLog(TaskLogEvent::PolicyDecision, task);
            if (d.type == FlowDecisionType::ThrottleDelay && d.throttleDelayMs)
            {
                ThrottleDelayMs(d.throttleDelayMs);
                ++m_policyThrottleCount;
            }
            else if (d.type == FlowDecisionType::IsolateCancel)
            {
                task->state = TaskState::Cancelled;
                ++m_policyCancelCount;
            }
            else if (d.type == FlowDecisionType::IsolateSuspend)
            {
                task->state = TaskState::Suspended;
                ++m_policySuspendCount;
            }
            else
            {
                ++m_policyAllowCount;
            }
        }
        else
        {
            ++m_policyAllowCount;
        }

        task->buildDurationMs = QK::Time::milliseconds() - buildStart;
        recordBuildMetric(task);

        m_tasks.push_back(task);
        emitLog(TaskLogEvent::Submitted, task);

        // Minimal execution behavior: if allowed and deps met, run immediately.
        if (task->state == TaskState::Pending)
        {
            supervisorLoopOnce();
        }

        return task->id;
    }

    TaskId Executor::submitWithDependencies(const char *name, TaskFunction func, void *context, void *arg,
                                                     const TaskId *dependencies, QC::usize depCount)
    {
        return submitWithDependenciesOriginAndArgSize(name, nullptr, nullptr, func, context, arg, 0, dependencies, depCount);
    }

    TaskId Executor::submitWithDependenciesAndOrigin(const char *name, const char *origin, const char *moduleId,
                                                     TaskFunction func, void *context, void *arg,
                                                     const TaskId *dependencies, QC::usize depCount)
    {
        return submitWithDependenciesOriginAndArgSize(name, origin, moduleId, func, context, arg, 0, dependencies, depCount);
    }

    TaskId Executor::submitWithDependenciesOriginAndArgSize(const char *name, const char *origin, const char *moduleId,
                                                            TaskFunction func, void *context, void *arg, QC::usize argSize,
                                                            const TaskId *dependencies, QC::usize depCount)
    {
        const QC::u64 buildStart = QK::Time::milliseconds();
        TaskDescriptor *task = new TaskDescriptor();
        task->id = allocateTaskId();
        recentPush(task->id);
        CopyName(task->name, name ? name : "task");
        CopyTag(task->origin, origin);
        CopyTag(task->moduleId, moduleId);
        task->function = func;
        task->context = context;
        task->argument = arg;
        task->argumentSize = argSize;
        QC::String::memset(task->signatureHash, 0, sizeof(task->signatureHash));
        QC::String::memset(task->inputHash, 0, sizeof(task->inputHash));
        task->cacheEligible = false;
        task->state = TaskState::Pending;
        task->priority = TaskPriority::Normal;
        task->result = TaskResult{false, 0, nullptr, 0};
        task->queueTime = QK::Time::milliseconds();
        task->startTime = 0;
        task->endTime = 0;
        task->queueDelayMs = 0;
        task->buildDurationMs = 0;
        task->executionDurationMs = 0;
        task->weightCost = 0;
        task->deadline = 0;
        task->cpuAffinity = 0;

        computeTaskHashes(task);
        task->priority = chooseAdaptivePriority(*task, TaskPriority::Normal);
        task->weightCost = estimateWeightCost(*task);

        if (TaskDescriptor *merge = findMergeCandidate(task->signatureHash, task->inputHash))
        {
            task->mergedInto = merge->id;
            task->dependencies.push_back(TaskDependency{merge->id, false});
            task->state = TaskState::Blocked;
            ++m_mergedSubmissions;
        }

        if (hasLiveRedundantTask(task->signatureHash, task->inputHash))
        {
            ++m_redundantSubmissions;
            emitLog(TaskLogEvent::RedundancyDetected, task);
        }

        for (QC::usize i = 0; i < depCount; ++i)
            task->dependencies.push_back(TaskDependency{dependencies[i], false});

        if (!areDependenciesMet(task))
            task->state = TaskState::Blocked;

        // Security Center policy hook.
        if (m_flowPolicy)
        {
            const FlowDecision d = m_flowPolicy(*task);
            emitLog(TaskLogEvent::PolicyDecision, task);
            if (d.type == FlowDecisionType::ThrottleDelay && d.throttleDelayMs)
            {
                ThrottleDelayMs(d.throttleDelayMs);
                ++m_policyThrottleCount;
            }
            else if (d.type == FlowDecisionType::IsolateCancel)
            {
                task->state = TaskState::Cancelled;
                ++m_policyCancelCount;
            }
            else if (d.type == FlowDecisionType::IsolateSuspend)
            {
                task->state = TaskState::Suspended;
                ++m_policySuspendCount;
            }
            else
            {
                ++m_policyAllowCount;
            }
        }
        else
        {
            ++m_policyAllowCount;
        }

        task->buildDurationMs = QK::Time::milliseconds() - buildStart;
        recordBuildMetric(task);

        m_tasks.push_back(task);
        emitLog(TaskLogEvent::Submitted, task);

        if (task->state == TaskState::Pending && areDependenciesMet(task))
        {
            supervisorLoopOnce();
        }

        return task->id;
    }

    TaskId Executor::submitWithDependenciesOriginAndArgSizeCached(const char *name, const char *origin, const char *moduleId,
                                                                  TaskFunction func, void *context, void *arg, QC::usize argSize,
                                                                  const TaskId *dependencies, QC::usize depCount)
    {
        // For MVP: only cache non-dependent tasks. Keep semantics simple.
        (void)depCount;
        if (dependencies && depCount)
            return submitWithDependenciesOriginAndArgSize(name, origin, moduleId, func, context, arg, argSize, dependencies, depCount);
        return submitWithPriorityOriginAndArgSizeCached(name, origin, moduleId, func, context, arg, argSize, TaskPriority::Normal);
    }

    void Executor::cancel(TaskId id)
    {
        if (TaskDescriptor *t = findTask(id))
            t->state = TaskState::Cancelled;
    }

    void Executor::suspend(TaskId id)
    {
        if (TaskDescriptor *t = findTask(id))
            t->state = TaskState::Suspended;
    }

    void Executor::resume(TaskId id)
    {
        if (TaskDescriptor *t = findTask(id))
        {
            if (t->state == TaskState::Suspended)
            {
                t->state = areDependenciesMet(t) ? TaskState::Pending : TaskState::Blocked;
                supervisorLoopOnce();
            }
        }
    }

    TaskState Executor::state(TaskId id) const
    {
        return const_cast<Executor *>(this)->findTask(id) ? const_cast<Executor *>(this)->findTask(id)->state : TaskState::Failed;
    }

    bool Executor::isComplete(TaskId id) const
    {
        TaskDescriptor *t = const_cast<Executor *>(this)->findTask(id);
        if (!t)
            return true;
        return (t->state == TaskState::Completed) || (t->state == TaskState::Failed) || (t->state == TaskState::Cancelled);
    }

    TaskResult Executor::result(TaskId id) const
    {
        TaskDescriptor *t = const_cast<Executor *>(this)->findTask(id);
        if (!t)
            return TaskResult{false, 0, nullptr, 0};
        return t->result;
    }

    const TaskDescriptor *Executor::taskDescriptor(TaskId id) const
    {
        return const_cast<Executor *>(this)->findTask(id);
    }

    void Executor::wait(TaskId id)
    {
        // Minimal busy-wait.
        while (!isComplete(id))
        {
            supervisorLoopOnce();
        }
    }

    bool Executor::waitTimeout(TaskId id, QC::u64 milliseconds)
    {
        volatile QC::u64 spin = 0;
        const QC::u64 iters = milliseconds * 25000u;
        for (QC::u64 i = 0; i < iters; ++i)
        {
            if (isComplete(id))
                return true;
            supervisorLoopOnce();
            spin += i;
        }
        (void)spin;
        return isComplete(id);
    }

    void Executor::waitAll(const TaskId *ids, QC::usize count)
    {
        if (!ids)
            return;
        for (QC::usize i = 0; i < count; ++i)
            wait(ids[i]);
    }

    TaskId Executor::waitAny(const TaskId *ids, QC::usize count)
    {
        if (!ids || count == 0)
            return INVALID_TASK;

        for (;;)
        {
            for (QC::usize i = 0; i < count; ++i)
            {
                if (isComplete(ids[i]))
                    return ids[i];
            }
            supervisorLoopOnce();
        }
    }

    void Executor::submitBatch(TaskDescriptor *tasks, QC::usize count, TaskId *outIds)
    {
        if (!tasks)
            return;
        for (QC::usize i = 0; i < count; ++i)
        {
            TaskDescriptor &t = tasks[i];
            const TaskId id = submitWithPriority(t.name, t.function, t.context, t.argument, t.priority);
            if (outIds)
                outIds[i] = id;
        }
    }

    void Executor::cancelAll()
    {
        for (QC::usize i = 0; i < m_tasks.size(); ++i)
        {
            if (m_tasks[i])
                m_tasks[i]->state = TaskState::Cancelled;
        }
    }

    QC::usize Executor::pendingCount() const
    {
        QC::usize c = 0;
        for (QC::usize i = 0; i < m_tasks.size(); ++i)
        {
            TaskDescriptor *t = m_tasks[i];
            if (t && (t->state == TaskState::Pending || t->state == TaskState::Queued || t->state == TaskState::Blocked))
                ++c;
        }
        return c;
    }

    QC::usize Executor::runningCount() const
    {
        QC::usize c = 0;
        for (QC::usize i = 0; i < m_tasks.size(); ++i)
        {
            TaskDescriptor *t = m_tasks[i];
            if (t && (t->state == TaskState::Running))
                ++c;
        }
        return c;
    }

    QC::usize Executor::completedCount() const
    {
        QC::usize c = 0;
        for (QC::usize i = 0; i < m_tasks.size(); ++i)
        {
            TaskDescriptor *t = m_tasks[i];
            if (t && (t->state == TaskState::Completed))
                ++c;
        }
        return c;
    }

    Executor::PerformanceCounters Executor::performanceCounters() const
    {
        PerformanceCounters c;
        c.totalSubmitted = m_totalSubmitted;
        c.totalExecuted = m_totalExecuted;
        c.totalCachedCompletions = m_totalCachedCompletions;
        c.totalBuildMs = m_totalBuildMs;
        c.totalExecutionMs = m_totalExecutionMs;
        c.totalQueueDelayMs = m_totalQueueDelayMs;
        c.schedulerPromotions = m_schedulerPromotions;
        c.schedulerDemotions = m_schedulerDemotions;
        c.crossFlowPromotions = m_crossFlowPromotions;
        c.crossFlowDemotions = m_crossFlowDemotions;
        c.policyAllow = m_policyAllowCount;
        c.policyThrottle = m_policyThrottleCount;
        c.policySuspend = m_policySuspendCount;
        c.policyCancel = m_policyCancelCount;
        c.redundantSubmissions = m_redundantSubmissions;
        c.memoHits = m_memoHits;
        c.memoMisses = m_memoMisses;
        c.memoRefused = m_memoRefused;
        c.memoEvictions = m_memoEvictions;
        c.memoSafetyRejected = m_memoSafetyRejected;
        return c;
    }

    QC::usize Executor::copySignatureMetrics(SignatureMetricsSnapshot *out, QC::usize cap) const
    {
        if (!out || cap == 0)
            return 0;
        QC::usize count = 0;
        for (QC::usize i = 0; i < MAX_SIGNATURE_METRICS && count < cap; ++i)
        {
            if (!m_signatureMetrics[i].valid)
                continue;
            SignatureMetricsSnapshot &dst = out[count++];
            QC::String::memset(&dst, 0, sizeof(dst));
            QC::String::memcpy(dst.sig, m_signatureMetrics[i].sig, sizeof(dst.sig));
            dst.submissions = m_signatureMetrics[i].submissions;
            dst.completions = m_signatureMetrics[i].completions;
            dst.memoHits = m_signatureMetrics[i].memoHits;
            dst.totalBuildMs = m_signatureMetrics[i].totalBuildMs;
            dst.totalExecMs = m_signatureMetrics[i].totalExecMs;
        }
        return count;
    }

    QC::usize Executor::copyFlowStatistics(FlowStatistics *out, QC::usize cap) const
    {
        if (!out || cap == 0)
            return 0;
        for (QC::usize i = 0; i < cap; ++i)
            QC::String::memset(&out[i], 0, sizeof(out[i]));

        QC::usize count = 0;
        for (QC::usize i = 0; i < m_tasks.size(); ++i)
        {
            const TaskDescriptor *task = m_tasks[i];
            if (!task)
                continue;
            const char *name = task->origin[0] ? task->origin : (task->moduleId[0] ? task->moduleId : "global");
            QC::usize slot = cap;
            for (QC::usize j = 0; j < count; ++j)
            {
                if (QC::String::strcmp(out[j].name, name) == 0)
                {
                    slot = j;
                    break;
                }
            }
            if (slot == cap)
            {
                if (count >= cap)
                    continue;
                slot = count++;
                QC::String::strncpy(out[slot].name, name, sizeof(out[slot].name) - 1);
                if (const FlowBiasEntry *bias = findFlowBias(name))
                {
                    out[slot].priorityBias = bias->bias;
                    out[slot].promotions = bias->promotions;
                    out[slot].demotions = bias->demotions;
                }
            }

            switch (task->state)
            {
            case TaskState::Pending:
            case TaskState::Queued:
            case TaskState::Blocked:
                ++out[slot].pending;
                break;
            case TaskState::Running:
                ++out[slot].running;
                break;
            case TaskState::Completed:
                ++out[slot].completed;
                break;
            default:
                break;
            }
            if (task->mergedInto != INVALID_TASK)
                ++out[slot].merged;
        }
        return count;
    }

    void Executor::promoteFlow(const char *origin)
    {
        if (FlowBiasEntry *entry = findOrCreateFlowBias(origin))
        {
            if (entry->bias < 2)
                ++entry->bias;
            ++entry->promotions;
        }
    }

    void Executor::demoteFlow(const char *origin)
    {
        if (FlowBiasEntry *entry = findOrCreateFlowBias(origin))
        {
            if (entry->bias > -2)
                --entry->bias;
            ++entry->demotions;
        }
    }
}
