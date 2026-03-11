// QKernel Task Manager - Implementation
// Namespace: QK

#include "QKTaskManager.h"
#include "QCLogger.h"
#include "QCString.h"

namespace QK
{

    TaskManager &TaskManager::instance()
    {
        static TaskManager instance;
        return instance;
    }

    TaskManager::TaskManager() : m_nextId(1), m_currentTaskId(0)
    {
    }

    TaskManager::~TaskManager()
    {
    }

    TaskId TaskManager::createTask(const char *name, void (*entry)(), TaskPriority priority)
    {
        Task task{};
        task.id = m_nextId++;
        QC::String::strncpy(task.name, name, sizeof(task.name) - 1);
        task.state = TaskState::Created;
        task.priority = priority;
        task.stackSize = 4096 * 4; // 16KB stack

        // Initialize context
        task.context.rip = reinterpret_cast<QC::u64>(entry);
        task.context.rflags = 0x202; // IF set
        task.context.cs = 0x08;      // Kernel code segment
        task.context.ss = 0x10;      // Kernel data segment

        // TODO: Allocate stack and set rsp
        // task.stackBase = allocateStack(task.stackSize);
        // task.context.rsp = task.stackBase + task.stackSize;

        m_tasks.push_back(task);

        QC_LOG_DEBUG("QKTaskMgr", "Created task '%s' with ID %u", name, task.id);

        return task.id;
    }

    void TaskManager::destroyTask(TaskId id)
    {
        for (QC::usize i = 0; i < m_tasks.size(); ++i)
        {
            if (m_tasks[i].id == id)
            {
                QC_LOG_DEBUG("QKTaskMgr", "Destroying task '%s'", m_tasks[i].name);
                // TODO: Free stack memory
                // TODO: Remove from vector
                m_tasks[i].state = TaskState::Terminated;
                return;
            }
        }
    }

    Task *TaskManager::getTask(TaskId id)
    {
        for (QC::usize i = 0; i < m_tasks.size(); ++i)
        {
            if (m_tasks[i].id == id)
            {
                return &m_tasks[i];
            }
        }
        return nullptr;
    }

    Task *TaskManager::getCurrentTask()
    {
        return getTask(m_currentTaskId);
    }

    void TaskManager::setTaskState(TaskId id, TaskState state)
    {
        if (Task *task = getTask(id))
        {
            task->state = state;
        }
    }

    void TaskManager::setTaskPriority(TaskId id, TaskPriority priority)
    {
        if (Task *task = getTask(id))
        {
            task->priority = priority;
        }
    }

    void TaskManager::sleep(QC::u64 milliseconds)
    {
        if (Task *task = getCurrentTask())
        {
            // TODO: Get current time and set wake time
            // task->sleepUntil = getCurrentTime() + milliseconds;
            task->state = TaskState::Sleeping;
            yield();
        }
    }

    void TaskManager::yield()
    {
        // TODO: Trigger scheduler to pick next task
    }

    void TaskManager::exit()
    {
        if (Task *task = getCurrentTask())
        {
            task->state = TaskState::Terminated;
            yield();
        }
    }

} // namespace QK
