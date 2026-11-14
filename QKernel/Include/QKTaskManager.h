#pragma once

// QKernel Task Manager - Task/thread management
// Namespace: QK

#include "QCTypes.h"

namespace QK
{

    using TaskId = QC::u32;
    using TaskEntry = void (*)(void *);

    static constexpr QC::u32 kTaskManagerMaxTasks = 512;
    static constexpr QC::u32 kTaskManagerMaxCpus = 256;
    static constexpr QC::usize kDefaultTaskStackSize = 16 * 1024;
    static constexpr QC::usize kMinimumTaskStackSize = 4 * 1024;

    enum class TaskState : QC::u8
    {
        Created,
        Ready,
        Running,
        Blocked,
        Sleeping,
        Terminated
    };

    enum class TaskPriority : QC::u8
    {
        Idle = 0,
        Low = 1,
        Normal = 2,
        High = 3,
        Realtime = 4
    };

    struct TaskContext
    {
        QC::u64 rax, rbx, rcx, rdx;
        QC::u64 rsi, rdi, rbp, rsp;
        QC::u64 r8, r9, r10, r11;
        QC::u64 r12, r13, r14, r15;
        QC::u64 rip, rflags;
        QC::u64 cs, ss;
        QC::u64 cr3; // Page table
        alignas(16) QC::u8 fxState[512];
    };

    struct Task
    {
        TaskId id;
        char name[64];
        TaskState state;
        TaskPriority priority;
        TaskContext context;
        void (*entry)();
        TaskEntry entryWithArgument;
        void *argument;
        QC::u32 cpuIndex;
        QC::VirtAddr stackBase;
        QC::usize stackSize;
        QC::u64 sleepUntil;
    };

    class TaskManager
    {
    public:
        static TaskManager &instance();

        TaskId createTask(const char *name, void (*entry)(), TaskPriority priority = TaskPriority::Normal);
        TaskId createTask(const char *name, TaskEntry entry, void *argument,
                          TaskPriority priority = TaskPriority::Normal, QC::u32 cpuIndex = 0,
                          QC::usize stackSize = kDefaultTaskStackSize);
        void destroyTask(TaskId id);

        Task *getTask(TaskId id);
        Task *getCurrentTask(QC::u32 cpuIndex = 0);
        TaskId getCurrentTaskId(QC::u32 cpuIndex) const;
        void setCurrentTaskId(QC::u32 cpuIndex, TaskId id);
        TaskId findNextReadyTask(QC::u32 cpuIndex, TaskId afterId);

        void setTaskState(TaskId id, TaskState state);
        void setTaskPriority(TaskId id, TaskPriority priority);

        bool sleep(QC::u64 milliseconds, QC::u32 cpuIndex = 0);
        void yield(QC::u32 cpuIndex = 0);
        [[noreturn]] void exit(QC::u32 cpuIndex = 0);
        void wakeSleepingTasks(QC::u32 cpuIndex, QC::u64 nowMs);
        QC::u32 reapTerminatedTasks(QC::u32 cpuIndex, TaskId activeTaskId);
        QC::u64 reapedTaskCount(QC::u32 cpuIndex) const;

    private:
        TaskManager();
        ~TaskManager();
        TaskManager(const TaskManager &) = delete;
        TaskManager &operator=(const TaskManager &) = delete;

        TaskId createTaskInternal(const char *name, void (*entry)(), TaskEntry entryWithArgument,
                                  void *argument, TaskPriority priority, QC::u32 cpuIndex,
                                  QC::usize stackSize);
        [[noreturn]] static void taskBootstrap(TaskId id);

        TaskId m_nextId;
        QC::u32 m_taskCount;
        volatile QC::u32 m_creationLock;
        QC::u64 m_reapedTaskCounts[kTaskManagerMaxCpus]{};
        Task m_tasks[kTaskManagerMaxTasks];
        TaskId m_currentTaskIds[kTaskManagerMaxCpus];
    };

} // namespace QK
