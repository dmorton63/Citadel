// QKernel Task Manager - Implementation
// Namespace: QK

#include "QKTaskManager.h"
#include "QKScheduler.h"
#include "QKTime.h"
#include "QCBuiltins.h"
#include "QCLogger.h"
#include "QCString.h"

namespace QK
{
    static_assert(offsetof(TaskContext, rsp) == 56);
    static_assert(offsetof(TaskContext, rip) == 128);
    static_assert(offsetof(TaskContext, rflags) == 136);
    static_assert(offsetof(TaskContext, cs) == 144);
    static_assert(offsetof(TaskContext, fxState) == 176);
    static_assert(sizeof(TaskContext) == 688);

    TaskManager &TaskManager::instance()
    {
        static TaskManager instance;
        return instance;
    }

    TaskManager::TaskManager() : m_nextId(1), m_taskCount(0), m_creationLock(0)
    {
    }

    TaskManager::~TaskManager()
    {
    }

    TaskId TaskManager::createTask(const char *name, void (*entry)(), TaskPriority priority)
    {
        return createTaskInternal(name, entry, nullptr, nullptr, priority, 0, kDefaultTaskStackSize);
    }

    TaskId TaskManager::createTask(
        const char *name,
        TaskEntry entry,
        void *argument,
        TaskPriority priority,
        QC::u32 cpuIndex,
        QC::usize stackSize)
    {
        return createTaskInternal(name, nullptr, entry, argument, priority, cpuIndex, stackSize);
    }

    TaskId TaskManager::createTaskInternal(
        const char *name,
        void (*entry)(),
        TaskEntry entryWithArgument,
        void *argument,
        TaskPriority priority,
        QC::u32 cpuIndex,
        QC::usize stackSize)
    {
        if (!entry && !entryWithArgument)
        {
            QC_LOG_ERROR("QKTaskMgr", "Cannot create a task without an entry point");
            return 0;
        }
        if (cpuIndex >= kTaskManagerMaxCpus)
        {
            QC_LOG_ERROR("QKTaskMgr", "Cannot create task on invalid CPU index %u", cpuIndex);
            return 0;
        }
        if (stackSize < kMinimumTaskStackSize)
        {
            QC_LOG_ERROR("QKTaskMgr", "Task stack size %lu is below the %lu-byte minimum",
                         stackSize, kMinimumTaskStackSize);
            return 0;
        }

        while (__atomic_exchange_n(&m_creationLock, 1u, __ATOMIC_ACQUIRE) != 0)
            QC::pause();
        const QC::u32 count = __atomic_load_n(&m_taskCount, __ATOMIC_ACQUIRE);
        if (count >= kTaskManagerMaxTasks || m_nextId == 0)
        {
            __atomic_store_n(&m_creationLock, 0u, __ATOMIC_RELEASE);
            QC_LOG_ERROR("QKTaskMgr", "Task table capacity reached");
            return 0;
        }

        QC::u8 *stack = new QC::u8[stackSize];
        if (!stack)
        {
            __atomic_store_n(&m_creationLock, 0u, __ATOMIC_RELEASE);
            QC_LOG_ERROR("QKTaskMgr", "Failed to allocate task stack");
            return 0;
        }

        Task task{};
        task.id = m_nextId++;
        task.state = TaskState::Created;
        task.priority = priority;
        task.entry = entry;
        task.entryWithArgument = entryWithArgument;
        task.argument = argument;
        task.cpuIndex = cpuIndex;
        task.stackSize = stackSize;
        task.stackBase = reinterpret_cast<QC::VirtAddr>(stack);
        QC::String::strncpy(task.name, name ? name : "task", sizeof(task.name) - 1);

        const QC::uptr stackTop =
            (reinterpret_cast<QC::uptr>(stack) + task.stackSize) & ~static_cast<QC::uptr>(0xF);
        task.context.rsp = stackTop - sizeof(QC::u64);
        *reinterpret_cast<QC::u64 *>(task.context.rsp) = 0;
        task.context.rip = reinterpret_cast<QC::u64>(&TaskManager::taskBootstrap);
        task.context.rdi = task.id;
        task.context.rflags = 0x202;
        task.context.cs = 0x08;
        task.context.ss = 0x10;
        asm volatile("fxsave64 %0" : "=m"(task.context.fxState) : : "memory");

        m_tasks[count] = task;
        __atomic_store_n(&m_taskCount, count + 1, __ATOMIC_RELEASE);
        __atomic_store_n(&m_creationLock, 0u, __ATOMIC_RELEASE);

        QC_LOG_DEBUG("QKTaskMgr", "Created task '%s' with ID %u on CPU %u", task.name, task.id, cpuIndex);
        return task.id;
    }

    void TaskManager::destroyTask(TaskId id)
    {
        if (Task *task = getTask(id))
        {
            QC_LOG_DEBUG("QKTaskMgr", "Destroying task '%s'", task->name);
            TaskState expected = TaskState::Created;
            if (__atomic_compare_exchange_n(
                    &task->state, &expected, TaskState::Terminated, false,
                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            {
                const QC::VirtAddr stack =
                    __atomic_exchange_n(&task->stackBase, static_cast<QC::VirtAddr>(0), __ATOMIC_ACQ_REL);
                if (stack != 0)
                    delete[] reinterpret_cast<QC::u8 *>(stack);
                return;
            }
            setTaskState(id, TaskState::Terminated);
        }
    }

    Task *TaskManager::getTask(TaskId id)
    {
        if (id == 0)
            return nullptr;

        const QC::u32 count = __atomic_load_n(&m_taskCount, __ATOMIC_ACQUIRE);
        for (QC::u32 i = 0; i < count; ++i)
        {
            if (m_tasks[i].id == id)
                return &m_tasks[i];
        }
        return nullptr;
    }

    Task *TaskManager::getCurrentTask(QC::u32 cpuIndex)
    {
        return getTask(getCurrentTaskId(cpuIndex));
    }

    TaskId TaskManager::getCurrentTaskId(QC::u32 cpuIndex) const
    {
        if (cpuIndex >= kTaskManagerMaxCpus)
            return 0;
        return __atomic_load_n(&m_currentTaskIds[cpuIndex], __ATOMIC_ACQUIRE);
    }

    void TaskManager::setCurrentTaskId(QC::u32 cpuIndex, TaskId id)
    {
        if (cpuIndex < kTaskManagerMaxCpus)
            __atomic_store_n(&m_currentTaskIds[cpuIndex], id, __ATOMIC_RELEASE);
    }

    TaskId TaskManager::findNextReadyTask(QC::u32 cpuIndex, TaskId afterId)
    {
        const QC::u32 count = __atomic_load_n(&m_taskCount, __ATOMIC_ACQUIRE);
        TaskId nextAfter = 0;
        TaskId first = 0;
        for (QC::u32 i = 0; i < count; ++i)
        {
            const Task &task = m_tasks[i];
            if (task.cpuIndex != cpuIndex ||
                __atomic_load_n(&task.state, __ATOMIC_ACQUIRE) != TaskState::Ready)
                continue;

            if (first == 0 || task.id < first)
                first = task.id;
            if (task.id > afterId && (nextAfter == 0 || task.id < nextAfter))
                nextAfter = task.id;
        }
        return nextAfter != 0 ? nextAfter : first;
    }

    void TaskManager::setTaskState(TaskId id, TaskState state)
    {
        if (Task *task = getTask(id))
            __atomic_store_n(&task->state, state, __ATOMIC_RELEASE);
    }

    void TaskManager::setTaskPriority(TaskId id, TaskPriority priority)
    {
        if (Task *task = getTask(id))
            task->priority = priority;
    }

    bool TaskManager::sleep(QC::u64 milliseconds, QC::u32 cpuIndex)
    {
        const TaskId id = getCurrentTaskId(cpuIndex);
        Task *task = getTask(id);
        if (!task || !Scheduler::instance().isRunning() || !QK::Time::available())
        {
            QC_LOG_ERROR("QKTaskMgr", "Task sleep requires a running scheduler and monotonic clock");
            return false;
        }

        if (milliseconds == 0)
        {
            yield(cpuIndex);
            return true;
        }

        QC::u64 now = QK::Time::milliseconds();
        if (milliseconds > ~static_cast<QC::u64>(0) - now)
        {
            QC_LOG_ERROR("QKTaskMgr", "Task sleep deadline overflow");
            return false;
        }

        QC::u64 flags = 0;
        asm volatile("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
        if (__atomic_load_n(&task->state, __ATOMIC_ACQUIRE) != TaskState::Running)
        {
            asm volatile("push %0; popfq" : : "r"(flags) : "memory", "cc");
            return false;
        }

        task->sleepUntil = now + milliseconds;
        __atomic_store_n(&task->state, TaskState::Sleeping, __ATOMIC_RELEASE);
        Scheduler::instance().schedule(cpuIndex);
        asm volatile("push %0; popfq" : : "r"(flags) : "memory", "cc");
        return true;
    }

    void TaskManager::wakeSleepingTasks(QC::u32 cpuIndex, QC::u64 nowMs)
    {
        const QC::u32 count = __atomic_load_n(&m_taskCount, __ATOMIC_ACQUIRE);
        for (QC::u32 i = 0; i < count; ++i)
        {
            Task &task = m_tasks[i];
            if (task.cpuIndex != cpuIndex ||
                __atomic_load_n(&task.state, __ATOMIC_ACQUIRE) != TaskState::Sleeping ||
                task.sleepUntil > nowMs)
                continue;

            TaskState expected = TaskState::Sleeping;
            (void)__atomic_compare_exchange_n(
                &task.state, &expected, TaskState::Ready, false,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
        }
    }

    QC::u32 TaskManager::reapTerminatedTasks(QC::u32 cpuIndex, TaskId activeTaskId)
    {
        if (cpuIndex >= kTaskManagerMaxCpus)
            return 0;

        QC::u32 reaped = 0;
        const QC::u32 count = __atomic_load_n(&m_taskCount, __ATOMIC_ACQUIRE);
        for (QC::u32 i = 0; i < count; ++i)
        {
            Task &task = m_tasks[i];
            if (task.id == activeTaskId || task.cpuIndex != cpuIndex ||
                __atomic_load_n(&task.state, __ATOMIC_ACQUIRE) != TaskState::Terminated)
                continue;

            const QC::VirtAddr stack =
                __atomic_exchange_n(&task.stackBase, static_cast<QC::VirtAddr>(0), __ATOMIC_ACQ_REL);
            if (stack == 0)
                continue;

            delete[] reinterpret_cast<QC::u8 *>(stack);
            ++reaped;
        }

        if (reaped != 0)
            __atomic_add_fetch(&m_reapedTaskCounts[cpuIndex], reaped, __ATOMIC_RELAXED);
        return reaped;
    }

    QC::u64 TaskManager::reapedTaskCount(QC::u32 cpuIndex) const
    {
        if (cpuIndex >= kTaskManagerMaxCpus)
            return 0;
        return __atomic_load_n(&m_reapedTaskCounts[cpuIndex], __ATOMIC_RELAXED);
    }

    QC::u32 TaskManager::copyTaskInfo(TaskInfo *out, QC::u32 capacity) const
    {
        if (!out || capacity == 0)
            return 0;

        // Task slots are append-only, so entries below the published count stay valid.
        const QC::u32 count = __atomic_load_n(&m_taskCount, __ATOMIC_ACQUIRE);
        QC::u32 written = 0;
        for (QC::u32 i = 0; i < count && written < capacity; ++i)
        {
            const Task &task = m_tasks[i];
            TaskInfo &info = out[written++];
            info.id = task.id;
            QC::String::strncpy(info.name, task.name, sizeof(info.name) - 1);
            info.name[sizeof(info.name) - 1] = '\0';
            info.state = __atomic_load_n(&task.state, __ATOMIC_ACQUIRE);
            info.cpuIndex = task.cpuIndex;
        }
        return written;
    }

    void TaskManager::yield(QC::u32 cpuIndex)
    {
        const TaskId id = getCurrentTaskId(cpuIndex);
        if (id != 0)
        {
            Task *task = getTask(id);
            if (task && __atomic_load_n(&task->state, __ATOMIC_ACQUIRE) == TaskState::Running)
                setTaskState(id, TaskState::Ready);
        }
        Scheduler::instance().schedule(cpuIndex);
    }

    [[noreturn]] void TaskManager::exit(QC::u32 cpuIndex)
    {
        const TaskId id = getCurrentTaskId(cpuIndex);
        if (id != 0)
            setTaskState(id, TaskState::Terminated);
        Scheduler::instance().schedule(cpuIndex);
        for (;;)
            asm volatile("pause");
    }

    [[noreturn]] void TaskManager::taskBootstrap(TaskId id)
    {
        TaskManager &manager = instance();
        Task *task = manager.getTask(id);
        if (!task)
        {
            QC_LOG_FATAL("QKTaskMgr", "Task bootstrap could not resolve task ID %u", id);
            QC::halt();
        }

        if (task->entryWithArgument)
            task->entryWithArgument(task->argument);
        else if (task->entry)
            task->entry();

        manager.exit(task->cpuIndex);
    }

} // namespace QK
