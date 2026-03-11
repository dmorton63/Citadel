#pragma once

// QPR Process - Process management
// Namespace: QPR

#include "QCTypes.h"
#include "QCVector.h"

namespace QPR
{

    class Thread;

    // Process ID
    using ProcessId = QC::u32;
    constexpr ProcessId INVALID_PID = 0;

    // Process state
    enum class ProcessState : QC::u8
    {
        Created,
        Ready,
        Running,
        Blocked,
        Zombie,
        Terminated
    };

    // Process priority
    enum class ProcessPriority : QC::u8
    {
        Idle = 0,
        Low = 1,
        Normal = 2,
        High = 3,
        Realtime = 4
    };

    // Memory region
    struct MemoryRegion
    {
        QC::uptr start;
        QC::usize size;
        QC::u32 flags; // Read, Write, Execute
    };

    // Process control block
    struct ProcessControlBlock
    {
        ProcessId pid;
        ProcessId parentPid;

        char name[64];
        ProcessState state;
        ProcessPriority priority;

        // Memory
        QC::uptr pageDirectory;
        QC::Vector<MemoryRegion> memoryRegions;
        QC::usize heapStart;
        QC::usize heapEnd;
        QC::usize stackTop;

        // Threads
        QC::Vector<Thread *> threads;

        // File descriptors
        static constexpr QC::usize MAX_FDS = 256;
        void *fileDescriptors[MAX_FDS];

        // Exit code
        QC::i32 exitCode;

        // Timing
        QC::u64 createTime;
        QC::u64 cpuTime;
    };

    class Process
    {
    public:
        static Process &manager();

        void initialize();

        // Process creation/destruction
        ProcessId create(const char *name, const void *executable, QC::usize size);
        ProcessId fork(ProcessId parent);
        QC::Status exec(ProcessId pid, const void *executable, QC::usize size);
        void terminate(ProcessId pid, QC::i32 exitCode);
        void kill(ProcessId pid);

        // Process lookup
        ProcessControlBlock *get(ProcessId pid);
        ProcessId current() const { return m_currentPid; }
        ProcessId parent(ProcessId pid);

        // Process control
        void suspend(ProcessId pid);
        void resume(ProcessId pid);
        void setPriority(ProcessId pid, ProcessPriority priority);

        // Memory
        void *allocate(ProcessId pid, QC::usize size);
        void free(ProcessId pid, void *address);
        QC::Status mmap(ProcessId pid, QC::uptr address, QC::usize size, QC::u32 flags);

        // Waiting
        QC::i32 waitpid(ProcessId pid);

        // Enumeration
        QC::usize count() const;
        void list(ProcessId *pids, QC::usize maxCount, QC::usize *actualCount);

    private:
        Process();
        ~Process();
        Process(const Process &) = delete;
        Process &operator=(const Process &) = delete;

        ProcessId allocatePid();

        static constexpr QC::usize MAX_PROCESSES = 1024;
        ProcessControlBlock *m_processes[MAX_PROCESSES];
        ProcessId m_currentPid;
        ProcessId m_nextPid;
    };

} // namespace QPR
