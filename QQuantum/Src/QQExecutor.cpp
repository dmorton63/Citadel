#include "QQExecutor.h"

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
        };

        constexpr QC::usize kMemoCap = 64;
        static MemoEntry g_memo[kMemoCap];
        static QC::usize g_memoNext = 0;

        static bool hashEq(const QC::u8 a[32], const QC::u8 b[32])
        {
            for (int i = 0; i < 32; ++i)
                if (a[i] != b[i])
                    return false;
            return true;
        }

        static bool memoLookup(const QC::u8 sig[32], const QC::u8 in[32], TaskResult &out)
        {
            for (QC::usize i = 0; i < kMemoCap; ++i)
            {
                const MemoEntry &e = g_memo[i];
                if (!e.valid)
                    continue;
                if (hashEq(e.sig, sig) && hashEq(e.in, in))
                {
                    out = e.result;
                    return true;
                }
            }
            return false;
        }

        static bool memoStore(const QC::u8 sig[32], const QC::u8 in[32], const TaskResult &res)
        {
            // Only cache safe results: either no data, or small enough to copy.
            if (res.data && res.dataSize > kMemoMaxData)
                return false;

            MemoEntry &e = g_memo[g_memoNext % kMemoCap];
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
            g_memoNext = (g_memoNext + 1) % kMemoCap;
            return true;
        }
    }

    Executor &Executor::instance()
    {
        static Executor inst;
        return inst;
    }

    Executor::Executor() : m_tasks(), m_scheduler(nullptr), m_nextTaskId(1), m_totalExecuted(0), m_running(false), m_workerCount(0)
    {
        for (QC::usize i = 0; i < 128; ++i)
            m_recentIds[i] = INVALID_TASK;
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
        g_memoNext = 0;
        m_memoHits = 0;
        m_memoMisses = 0;
        m_memoRefused = 0;
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
        m_workerCount = workerCount;
        m_running = true;
    }

    void Executor::shutdown()
    {
        cancelAll();
        m_running = false;
        m_workerCount = 0;
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

    void Executor::executeTask(TaskDescriptor *task)
    {
        if (!task)
            return;
        if (!task->function)
        {
            task->state = TaskState::Failed;
            task->result = TaskResult{false, 0, nullptr, 0};
            return;
        }

        task->state = TaskState::Running;
        task->result = task->function(task->context, task->argument);
        task->state = task->result.success ? TaskState::Completed : TaskState::Failed;
        ++m_totalExecuted;
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

        // Signature identity (preferred): moduleId + function pointer + canonical schema/version.
        // This makes memoization keys stable across differing task names.
        {
            // Try to parse canonical args header (if provided).
            QC::CanonicalArgs::Header hdr{};
            const void *payload = nullptr;
            QC::usize payloadLen = 0;
            const bool hasCanonical = (task->argument && task->argumentSize >= sizeof(QC::CanonicalArgs::Header)) &&
                                      QC::CanonicalArgs::parse(task->argument, task->argumentSize, hdr, payload, payloadLen);

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
            append(task->moduleId[0] ? task->moduleId : "(none)");
            append(";fn=");
            {
                const QC::usize fn = reinterpret_cast<QC::usize>(task->function);
                // hash is over bytes anyway; use decimal text to avoid endian concerns.
                char fnBuf[32];
                QC::String::memset(fnBuf, 0, sizeof(fnBuf));
                // simple decimal conversion
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
                char sBuf[16];
                QC::String::memset(sBuf, 0, sizeof(sBuf));
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
                    sBuf[w++] = rev[i];
                sBuf[w] = 0;
                append(sBuf);

                append(";ver=");
                char vBuf[16];
                QC::String::memset(vBuf, 0, sizeof(vBuf));
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
                    vBuf[w++] = rev[i];
                vBuf[w] = 0;
                append(vBuf);
            }
            else
            {
                // Fallback: still include the name so signatures remain debuggable.
                append(";name=");
                append(task->name[0] ? task->name : "(none)");
            }

            QC::Sha256(reinterpret_cast<const QC::u8 *>(sigBuf), QC::String::strlen(sigBuf), task->signatureHash);
        }

        // Canonical input: bytes of (argSize, arg bytes...) if provided, otherwise arg pointer only.
        if (task->argument && task->argumentSize)
        {
            // Temporary v1 behavior: hash argument bytes only. (Caller must provide canonical bytes.)
            QC::Sha256(reinterpret_cast<const QC::u8 *>(task->argument), task->argumentSize, task->inputHash);
        }
        else
        {
            const QC::usize p = reinterpret_cast<QC::usize>(task->argument);
            QC::Sha256(reinterpret_cast<const QC::u8 *>(&p), sizeof(p), task->inputHash);
        }
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
        task->queueTime = 0;
        task->startTime = 0;
        task->endTime = 0;
        task->deadline = 0;
        task->cpuAffinity = 0;

        computeTaskHashes(task);

        // Allowlist gate for cached submits.
        if (task->cacheEligible && m_memoAllowlistEnabled && !memoizationAllowlistContains(task->signatureHash))
            task->cacheEligible = false;

        // Security Center policy hook.
        if (m_flowPolicy)
        {
            const FlowDecision d = m_flowPolicy(*task);
            if (d.type == FlowDecisionType::ThrottleDelay && d.throttleDelayMs)
                ThrottleDelayMs(d.throttleDelayMs);
            else if (d.type == FlowDecisionType::IsolateCancel)
                task->state = TaskState::Cancelled;
            else if (d.type == FlowDecisionType::IsolateSuspend)
                task->state = TaskState::Suspended;
        }

        m_tasks.push_back(task);

        if (task->state == TaskState::Pending)
        {
            task->state = TaskState::Queued;

            if (m_memoizationEnabled && task->cacheEligible)
            {
                TaskResult cached;
                if (memoLookup(task->signatureHash, task->inputHash, cached))
                {
                    ++m_memoHits;
                    task->result = cached;
                    task->state = cached.success ? TaskState::Completed : TaskState::Failed;
                    logTaskExec(task, 0);
                    return task->id;
                }
                ++m_memoMisses;
            }

            const QC::u64 t0 = QK::Time::milliseconds();
            executeTask(task);
            const QC::u64 t1 = QK::Time::milliseconds();

            if (m_memoizationEnabled && task->cacheEligible)
            {
                if (!memoStore(task->signatureHash, task->inputHash, task->result))
                {
                    // Cache refused (likely oversized result blob).
                    ++m_memoRefused;
                }
            }
            logTaskExec(task, (t1 >= t0) ? (t1 - t0) : 0);
        }

        return task->id;
    }

    TaskId Executor::submitWithPriorityOriginAndArgSize(const char *name, const char *origin, const char *moduleId,
                                                        TaskFunction func, void *context, void *arg, QC::usize argSize,
                                                        TaskPriority priority)
    {
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
        task->queueTime = 0;
        task->startTime = 0;
        task->endTime = 0;
        task->deadline = 0;
        task->cpuAffinity = 0;

        computeTaskHashes(task);

        // Security Center policy hook.
        if (m_flowPolicy)
        {
            const FlowDecision d = m_flowPolicy(*task);
            if (d.type == FlowDecisionType::ThrottleDelay && d.throttleDelayMs)
                ThrottleDelayMs(d.throttleDelayMs);
            else if (d.type == FlowDecisionType::IsolateCancel)
            {
                task->state = TaskState::Cancelled;
            }
            else if (d.type == FlowDecisionType::IsolateSuspend)
            {
                task->state = TaskState::Suspended;
            }
        }

        m_tasks.push_back(task);

        // Minimal execution behavior: if allowed and deps met, run immediately.
        if (task->state == TaskState::Pending)
        {
            task->state = TaskState::Queued;
            const QC::u64 t0 = QK::Time::milliseconds();
            executeTask(task);
            const QC::u64 t1 = QK::Time::milliseconds();
            logTaskExec(task, (t1 >= t0) ? (t1 - t0) : 0);
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
        TaskDescriptor *task = new TaskDescriptor();
        task->id = allocateTaskId();
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
        task->queueTime = 0;
        task->startTime = 0;
        task->endTime = 0;
        task->deadline = 0;
        task->cpuAffinity = 0;

        computeTaskHashes(task);

        for (QC::usize i = 0; i < depCount; ++i)
            task->dependencies.push_back(TaskDependency{dependencies[i], false});

        // Security Center policy hook.
        if (m_flowPolicy)
        {
            const FlowDecision d = m_flowPolicy(*task);
            if (d.type == FlowDecisionType::ThrottleDelay && d.throttleDelayMs)
                ThrottleDelayMs(d.throttleDelayMs);
            else if (d.type == FlowDecisionType::IsolateCancel)
            {
                task->state = TaskState::Cancelled;
            }
            else if (d.type == FlowDecisionType::IsolateSuspend)
            {
                task->state = TaskState::Suspended;
            }
        }

        m_tasks.push_back(task);

        if (task->state == TaskState::Pending && areDependenciesMet(task))
        {
            task->state = TaskState::Queued;
            const QC::u64 t0 = QK::Time::milliseconds();
            executeTask(task);
            const QC::u64 t1 = QK::Time::milliseconds();
            logTaskExec(task, (t1 >= t0) ? (t1 - t0) : 0);
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
                t->state = TaskState::Pending;
                if (areDependenciesMet(t))
                {
                    t->state = TaskState::Queued;
                    executeTask(t);
                }
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
            if (t && (t->state == TaskState::Pending || t->state == TaskState::Queued))
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
}
