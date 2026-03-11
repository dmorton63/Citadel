#pragma once

// QPR Thread - Thread management
// Namespace: QPR

#include "QCTypes.h"
#include "QPRProcess.h"

namespace QPR
{

    // Thread ID
    using ThreadId = QC::u32;
    constexpr ThreadId INVALID_TID = 0;

    // Thread state
    enum class ThreadState : QC::u8
    {
        Created,
        Ready,
        Running,
        Blocked,
        Sleeping,
        Terminated
    };

    // Thread priority (within process)
    enum class ThreadPriority : QC::u8
    {
        Low = 0,
        Normal = 1,
        High = 2
    };

    // Thread entry point
    using ThreadEntry = void (*)(void *arg);

    // CPU context
    struct CPUContext
    {
        QC::u64 rax, rbx, rcx, rdx;
        QC::u64 rsi, rdi, rbp, rsp;
        QC::u64 r8, r9, r10, r11;
        QC::u64 r12, r13, r14, r15;
        QC::u64 rip, rflags;
        QC::u64 cs, ss;
        QC::u64 cr3;

        // FPU/SSE state
        QC::u8 fpuState[512] __attribute__((aligned(16)));
    };

    // Thread control block
    struct ThreadControlBlock
    {
        ThreadId tid;
        ProcessId ownerPid;

        char name[32];
        ThreadState state;
        ThreadPriority priority;

        // Stack
        QC::uptr kernelStack;
        QC::uptr userStack;
        QC::usize stackSize;

        // Context
        CPUContext context;

        // Timing
        QC::u64 createTime;
        QC::u64 cpuTime;
        QC::u64 wakeTime; // For sleeping threads

        // Blocking
        void *blockReason;

        // Thread-local storage
        void *tlsBase;
        QC::usize tlsSize;
    };

    class Thread
    {
    public:
        static Thread &manager();

        void initialize();

        // Thread creation/destruction
        ThreadId create(ProcessId owner, const char *name, ThreadEntry entry, void *arg);
        void terminate(ThreadId tid);
        void exit(QC::i32 exitCode);

        // Thread lookup
        ThreadControlBlock *get(ThreadId tid);
        ThreadId current() const { return m_currentTid; }

        // Thread control
        void suspend(ThreadId tid);
        void resume(ThreadId tid);
        void yield();
        void sleep(QC::u64 milliseconds);
        void setPriority(ThreadId tid, ThreadPriority priority);

        // Blocking
        void block(ThreadId tid, void *reason);
        void unblock(ThreadId tid);
        void unblockAll(void *reason);

        // Context switching
        void switchTo(ThreadId tid);
        void saveContext(ThreadId tid, CPUContext *context);
        void restoreContext(ThreadId tid);

        // Waiting
        void join(ThreadId tid);

        // Enumeration
        QC::usize count() const;
        QC::usize countByProcess(ProcessId pid) const;

    private:
        Thread();
        ~Thread();
        Thread(const Thread &) = delete;
        Thread &operator=(const Thread &) = delete;

        ThreadId allocateTid();
        void setupStack(ThreadControlBlock *tcb, ThreadEntry entry, void *arg);

        static constexpr QC::usize MAX_THREADS = 4096;
        ThreadControlBlock *m_threads[MAX_THREADS];
        ThreadId m_currentTid;
        ThreadId m_nextTid;
    };

    // Synchronization primitives
    class Mutex
    {
    public:
        Mutex();
        ~Mutex();

        void lock();
        bool tryLock();
        void unlock();

        bool isLocked() const { return m_locked; }
        ThreadId owner() const { return m_owner; }

    private:
        volatile bool m_locked;
        ThreadId m_owner;
        // Wait queue would be here
    };

    class Semaphore
    {
    public:
        Semaphore(QC::i32 initial);
        ~Semaphore();

        void wait();
        bool tryWait();
        void signal();

        QC::i32 value() const { return m_value; }

    private:
        volatile QC::i32 m_value;
        // Wait queue would be here
    };

    class ConditionVariable
    {
    public:
        ConditionVariable();
        ~ConditionVariable();

        void wait(Mutex &mutex);
        bool waitTimeout(Mutex &mutex, QC::u64 milliseconds);
        void signal();
        void broadcast();

    private:
        // Wait queue would be here
    };

} // namespace QPR
