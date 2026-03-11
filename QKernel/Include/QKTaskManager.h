#pragma once

// QKernel Task Manager - Task/thread management
// Namespace: QK

#include "QCTypes.h"
#include "QCVector.h"

namespace QK
{

    using TaskId = QC::u32;

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
    };

    struct Task
    {
        TaskId id;
        char name[64];
        TaskState state;
        TaskPriority priority;
        TaskContext context;
        QC::VirtAddr stackBase;
        QC::usize stackSize;
        QC::u64 sleepUntil;
    };

    class TaskManager
    {
    public:
        static TaskManager &instance();

        TaskId createTask(const char *name, void (*entry)(), TaskPriority priority = TaskPriority::Normal);
        void destroyTask(TaskId id);

        Task *getTask(TaskId id);
        Task *getCurrentTask();

        void setTaskState(TaskId id, TaskState state);
        void setTaskPriority(TaskId id, TaskPriority priority);

        void sleep(QC::u64 milliseconds);
        void yield();
        void exit();

    private:
        TaskManager();
        ~TaskManager();
        TaskManager(const TaskManager &) = delete;
        TaskManager &operator=(const TaskManager &) = delete;

        TaskId m_nextId;
        TaskId m_currentTaskId;
        QC::Vector<Task> m_tasks;
    };

} // namespace QK
