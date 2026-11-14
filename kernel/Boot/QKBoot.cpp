#include "Boot/QKBoot.h"

#include "Boot/Limine/QKBootLimineRequests.h"
#include "Boot/Memory/QKBootAddressMapping.h"
#include "Boot/Memory/QKBootEarlyMemory.h"

#include "Boot/Config/QKBootJson.h"
#include "Boot/Config/QKBootRamdiskFile.h"
#include "Boot/Config/QKBootSysConfig.h"
#include "QKBootConfigTier.h"
#include "Boot/Config/QKBootStagedConfig.h"

#include "Boot/Security/QKBootJsonSignature.h"
#include "Boot/Security/QKBootSha256.h"

#include "QCMemUtil.h"
#include "QCBuiltins.h"

#include "Boot/ACPI/QKBootACPITables.h"
#include "Boot/Arch/QKBootArchInit.h"
#include "Boot/Desktop/QKBootDesktopSession.h"
#include "Boot/TPM/QKBootTPMSecureStore.h"

#include "QCJson.h"
#include "QCString.h"

#include "QKBootLog.h"
#include "QKBootEventLog.h"
#include "QKRuntimeRegistries.h"

#include "QArchIDT.h"
#include "QArchCPU.h"
#include "QKInterrupts.h"
#include "QKTaskManager.h"
#include "QKScheduler.h"
#include "QKTime.h"
#include "QDrvTimer.h"

namespace
{
    QKBoot::FLogFn g_Log = nullptr;
    QKBoot::FLogFn g_LogUpstream = nullptr;
    QKBoot::LimineRequests g_Req{};
    bool g_DesktopPrepared = false;

    struct ApBootSlot
    {
        volatile QC::u32 cpuIndex = 0;
        volatile QC::u32 lapicId = 0;
        volatile QC::u32 online = 0;
        volatile QC::u32 state = static_cast<QC::u32>(QKBoot::SmpCpuState::Offline);
        volatile QC::u64 idleHeartbeats = 0;
        volatile QC::u64 timerHookTicks = 0;
        volatile QC::u64 schedulerHookKicks = 0;
        volatile QC::u64 schedulerTaskTurns[3] = {};
        volatile QC::u64 schedulerTaskWakeups = 0;
        volatile QC::u64 selfTestSpins[2] = {};
        volatile QC::u32 workerIdle = 0;
        volatile QC::u64 workItemsCompleted = 0;
        volatile QC::u64 workChecksum = 0;
    };

    struct SmpSelfTestTaskArgument
    {
        ApBootSlot *slot = nullptr;
        QC::u32 taskIndex = 0;
    };

    static constexpr QC::usize kMaxApBootSlots = 256;
    static constexpr QC::u32 kSmpSelfTestTaskCount = 3;
    static constexpr QC::u64 kSmpSelfTestSpinTimeoutMs = 2000;
    static ApBootSlot g_ApBootSlots[kMaxApBootSlots]{};
    static QK::TaskId g_ApWorkerTaskIds[kMaxApBootSlots]{};
    static SmpSelfTestTaskArgument g_SmpSelfTestArguments[kMaxApBootSlots][kSmpSelfTestTaskCount]{};
    static volatile QC::u32 g_SmpSelfTestActive = 0;
    static volatile QC::u32 g_SmpSelfTestExpected = 0;
    static volatile QC::u32 g_SmpSelfTestFinished = 0;
    static volatile QC::u32 g_ApSlotCount = 0;
    static volatile QC::u32 g_ApOnlineCount = 0;
    static volatile QC::u64 g_SmpTotalCpus = 0;
    static volatile QC::u64 g_SmpTargetAps = 0;
    static volatile QC::u64 g_SmpWorkTarget = 0;
    static volatile QC::u64 g_SmpWorkCompleted = 0;
    static volatile QC::u64 g_SmpWorkChecksum = 0;
    static volatile QC::u32 g_SmpWorkBatchActive = 0;
    static volatile QC::u32 g_SmpTimerReady = 0;
    static volatile QC::u32 g_SmpTimerInitialCount = 0;
    static volatile QC::u32 g_SmpTimerQuantumTicks = 0;
    static volatile QC::u32 g_SmpTimerFrequency = 0;
    static volatile QC::u32 g_SmpSchedulerReady = 0;

    struct SmpWorkItem
    {
        QKBoot::SmpWorkFn work = nullptr;
        QC::u64 argument = 0;
        bool batchTracked = false;
    };

    static constexpr QC::usize kSmpWorkQueueCapacity = 256;
    static SmpWorkItem g_SmpWorkQueue[kSmpWorkQueueCapacity]{};
    static volatile QC::u32 g_SmpWorkQueueLock = 0;
    static QC::u32 g_SmpWorkQueueHead = 0;
    static QC::u32 g_SmpWorkQueueTail = 0;
    static QC::u32 g_SmpWorkQueueCount = 0;

    static QC::u64 LockSmpWorkQueue()
    {
        QC::u64 flags = 0;
        asm volatile("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
        while (__atomic_exchange_n(&g_SmpWorkQueueLock, 1u, __ATOMIC_ACQUIRE) != 0)
            QC::pause();
        return flags;
    }

    static void UnlockSmpWorkQueue(QC::u64 flags)
    {
        __atomic_store_n(&g_SmpWorkQueueLock, 0u, __ATOMIC_RELEASE);
        if (flags & (1ull << 9))
            asm volatile("sti" ::: "memory");
    }

    static bool EnqueueSmpWork(QKBoot::SmpWorkFn work, QC::u64 argument, bool batchTracked)
    {
        if (!work)
            return false;

        const QC::u64 flags = LockSmpWorkQueue();
        if (g_SmpWorkQueueCount >= kSmpWorkQueueCapacity)
        {
            UnlockSmpWorkQueue(flags);
            return false;
        }

        g_SmpWorkQueue[g_SmpWorkQueueTail] = SmpWorkItem{work, argument, batchTracked};
        g_SmpWorkQueueTail = (g_SmpWorkQueueTail + 1) % kSmpWorkQueueCapacity;
        ++g_SmpWorkQueueCount;
        UnlockSmpWorkQueue(flags);

        // Workers publish workerIdle under the queue lock, so this sees every worker that
        // blocked before the push; a worker blocking afterwards will see the queued item.
        const QC::usize slotCount = __atomic_load_n(&g_ApSlotCount, __ATOMIC_ACQUIRE);
        for (QC::usize i = 0; i < slotCount; ++i)
        {
            if (__atomic_exchange_n(&g_ApBootSlots[i].workerIdle, 0u, __ATOMIC_ACQ_REL) != 0)
                (void)QK::Scheduler::instance().unblockTask(g_ApWorkerTaskIds[i]);
        }
        return true;
    }

    static bool DequeueSmpWork(SmpWorkItem &out)
    {
        const QC::u64 flags = LockSmpWorkQueue();
        if (g_SmpWorkQueueCount == 0)
        {
            UnlockSmpWorkQueue(flags);
            return false;
        }

        out = g_SmpWorkQueue[g_SmpWorkQueueHead];
        g_SmpWorkQueue[g_SmpWorkQueueHead] = SmpWorkItem{};
        g_SmpWorkQueueHead = (g_SmpWorkQueueHead + 1) % kSmpWorkQueueCapacity;
        --g_SmpWorkQueueCount;
        UnlockSmpWorkQueue(flags);
        return true;
    }

    static void EnableFpuAndSseForCurrentCpu()
    {
        // Mirror the minimal CR0/CR4 setup that BSP applies so APs can safely run C++.
        QC::u64 cr0 = 0;
        QC::u64 cr4 = 0;

        asm volatile("clts" ::: "memory");
        asm volatile("mov %%cr0, %0" : "=r"(cr0));
        cr0 &= ~(1ull << 2); // EM = 0
        cr0 &= ~(1ull << 3); // TS = 0
        cr0 |= (1ull << 1);  // MP = 1
        cr0 |= (1ull << 5);  // NE = 1
        asm volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");

        asm volatile("mov %%cr4, %0" : "=r"(cr4));
        cr4 |= (1ull << 9);  // OSFXSR
        cr4 |= (1ull << 10); // OSXMMEXCPT
        asm volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
        asm volatile("fninit" ::: "memory");
    }

    static void LocalApicTimerInterrupt(QK::InterruptFrame *)
    {
        QC::u32 lapicId = 0;
        QC::u32 cpuIndex = 0;
        bool schedulerReady = false;
        if (QK::Boot::Acpi::ReadCurrentCpuLocalApicId(lapicId))
        {
            const QC::usize slotCount = __atomic_load_n(&g_ApSlotCount, __ATOMIC_RELAXED);
            for (QC::usize i = 0; i < slotCount; ++i)
            {
                ApBootSlot &slot = g_ApBootSlots[i];
                if (__atomic_load_n(&slot.lapicId, __ATOMIC_RELAXED) != lapicId ||
                    __atomic_load_n(&slot.online, __ATOMIC_RELAXED) == 0)
                    continue;

                const QC::u64 ticks = __atomic_add_fetch(&slot.timerHookTicks, 1ull, __ATOMIC_RELAXED);
                cpuIndex = __atomic_load_n(&slot.cpuIndex, __ATOMIC_RELAXED);
                const QC::u32 quantumTicks = __atomic_load_n(&g_SmpTimerQuantumTicks, __ATOMIC_RELAXED);
                if (quantumTicks != 0 && ticks % quantumTicks == 0)
                    __atomic_fetch_add(&slot.schedulerHookKicks, 1ull, __ATOMIC_RELAXED);
                schedulerReady = __atomic_load_n(&g_SmpSchedulerReady, __ATOMIC_ACQUIRE) != 0;
                break;
            }
        }

        QK::Boot::Acpi::SendCurrentCpuLocalApicEoi();
        if (schedulerReady)
            QK::Scheduler::instance().timerTick(cpuIndex);
    }

    static bool CalibrateLocalApicTimer(QC::u32 frequencyHz, QC::u32 &initialCount)
    {
        constexpr QC::u32 kCalibrationPitTicks = 50;
        constexpr QC::u64 kCalibrationSpinLimit = 100000000ull;
        QDrv::Timer &pitTimer = QDrv::Timer::instance();
        const QC::u32 pitFrequency = pitTimer.frequency();
        if (frequencyHz == 0 || pitFrequency == 0 || !QArch::CPU::instance().interruptsEnabled())
            return false;

        QK::Boot::Acpi::StopCurrentCpuLocalApicTimer();
        if (!QK::Boot::Acpi::ConfigureCurrentCpuLocalApicTimer(
                QK::IRQ_LOCAL_APIC_TIMER, 0xFFFFFFFFu, false))
            return false;

        const QC::u64 startTicks = pitTimer.ticks();
        QC::u64 spins = 0;
        while (pitTimer.ticks() - startTicks < kCalibrationPitTicks)
        {
            if (++spins >= kCalibrationSpinLimit)
            {
                QK::Boot::Acpi::StopCurrentCpuLocalApicTimer();
                return false;
            }
            QC::pause();
        }

        QC::u32 remainingCount = 0;
        const bool counterRead = QK::Boot::Acpi::ReadCurrentCpuLocalApicTimerCount(remainingCount);
        QK::Boot::Acpi::StopCurrentCpuLocalApicTimer();
        if (!counterRead || remainingCount >= 0xFFFFFFFFu)
            return false;

        const QC::u64 elapsedCounts = 0xFFFFFFFFull - remainingCount;
        const QC::u64 elapsedPitTicks = pitTimer.ticks() - startTicks;
        if (elapsedPitTicks == 0)
            return false;
        const QC::u64 countsPerSecond = (elapsedCounts * pitFrequency) / elapsedPitTicks;
        const QC::u64 count = countsPerSecond / frequencyHz;
        if (count == 0 || count > 0xFFFFFFFFull)
            return false;

        initialCount = static_cast<QC::u32>(count);
        return true;
    }

    static bool RunOneSmpWorkItem(ApBootSlot &slot)
    {
        SmpWorkItem item{};
        if (!DequeueSmpWork(item))
            return false;

        const QC::u64 value = item.work(item.argument);

        __atomic_fetch_add(&slot.workItemsCompleted, 1ull, __ATOMIC_RELAXED);
        __atomic_fetch_add(&slot.workChecksum, value, __ATOMIC_RELAXED);
        if (item.batchTracked)
        {
            __atomic_fetch_add(&g_SmpWorkChecksum, value, __ATOMIC_RELAXED);
            const QC::u64 completed = __atomic_add_fetch(&g_SmpWorkCompleted, 1ull, __ATOMIC_RELEASE);
            if (completed >= __atomic_load_n(&g_SmpWorkTarget, __ATOMIC_ACQUIRE))
                __atomic_store_n(&g_SmpWorkBatchActive, 0u, __ATOMIC_RELEASE);
        }
        return true;
    }

    static QC::u64 SmpWorkTestCallback(QC::u64 index)
    {
        QC::u64 value = index + 1;
        for (QC::u32 iteration = 0; iteration < 128; ++iteration)
            value = (value * 6364136223846793005ull) + 1442695040888963407ull;
        return value;
    }

    static void BootLogFanout(const char *msg)
    {
        // Always capture, even if upstream logging is disabled.
        QK::Boot::Log::Append(msg ? msg : "");

        if (g_LogUpstream)
        {
            g_LogUpstream(msg);
        }
    }

    static constexpr bool kProductionMode =
#if defined(CITADEL_PRODUCTION) && (CITADEL_PRODUCTION != 0)
    true;
#else
    false;
#endif

    struct CPUIDRegs
    {
        QC::u32 eax;
        QC::u32 ebx;
        QC::u32 ecx;
        QC::u32 edx;
    };

    static CPUIDRegs Cpuid(QC::u32 leaf, QC::u32 subleaf = 0)
    {
        CPUIDRegs r{};
        asm volatile("cpuid" : "=a"(r.eax), "=b"(r.ebx), "=c"(r.ecx), "=d"(r.edx) : "a"(leaf), "c"(subleaf));
        return r;
    }

    static bool CpuHasSse2()
    {
        const CPUIDRegs r = Cpuid(1);
        return (r.edx & (1u << 26)) != 0;
    }

    static bool CpuHasNx()
    {
        const CPUIDRegs maxExt = Cpuid(0x80000000u);
        if (maxExt.eax < 0x80000001u)
            return false;
        const CPUIDRegs r = Cpuid(0x80000001u);
        return (r.edx & (1u << 20)) != 0;
    }

    static bool CpuHasLongMode()
    {
        const CPUIDRegs maxExt = Cpuid(0x80000000u);
        if (maxExt.eax < 0x80000001u)
            return false;
        const CPUIDRegs r = Cpuid(0x80000001u);
        return (r.edx & (1u << 29)) != 0;
    }

    static void LogU64(QKBoot::FLogFn Log, QC::u64 value)
    {
        if (!Log)
            return;

        char buf[32];
        QC::usize pos = 0;

        if (value == 0)
        {
            buf[pos++] = '0';
        }
        else
        {
            while (value > 0 && pos < sizeof(buf) - 1)
            {
                const QC::u64 digit = value % 10;
                buf[pos++] = static_cast<char>('0' + digit);
                value /= 10;
            }

            // reverse in-place
            for (QC::usize i = 0; i < pos / 2; ++i)
            {
                const char tmp = buf[i];
                buf[i] = buf[pos - 1 - i];
                buf[pos - 1 - i] = tmp;
            }
        }

        buf[pos] = '\0';
        Log(buf);
    }

    static void LogBool(QKBoot::FLogFn Log, bool v)
    {
        if (!Log)
            return;
        Log(v ? "true" : "false");
    }

    // Blocks while the shared queue is empty; EnqueueSmpWork unblocks idle workers and the
    // AP idle loop reschedules after its next interrupt, so wakeups need no wall-clock time.
    static void RunApWorkerTask(void *argument)
    {
        auto *slot = static_cast<ApBootSlot *>(argument);
        if (!slot)
            return;

        QK::TaskManager &taskManager = QK::TaskManager::instance();
        const QC::u32 cpuIndex = slot->cpuIndex;
        const QK::TaskId self = taskManager.getCurrentTaskId(cpuIndex);
        for (;;)
        {
            if (RunOneSmpWorkItem(*slot))
                continue;

            const QC::u64 flags = LockSmpWorkQueue();
            const bool block = g_SmpWorkQueueCount == 0;
            if (block)
            {
                taskManager.setTaskState(self, QK::TaskState::Blocked);
                __atomic_store_n(&slot->workerIdle, 1u, __ATOMIC_RELEASE);
            }
            UnlockSmpWorkQueue(flags);
            if (block)
                QK::Scheduler::instance().schedule(cpuIndex);
        }
    }

    // The last finishing task reopens the gate; no task touches its argument afterwards.
    static void FinishSmpSelfTestTask()
    {
        const QC::u32 finished = __atomic_add_fetch(&g_SmpSelfTestFinished, 1u, __ATOMIC_ACQ_REL);
        if (finished == __atomic_load_n(&g_SmpSelfTestExpected, __ATOMIC_ACQUIRE))
            __atomic_store_n(&g_SmpSelfTestActive, 0u, __ATOMIC_RELEASE);
    }

    // Spinners 0 and 1 never yield; each passes only after observing the other advance
    // while it is still spinning, which requires timer preemption in both directions.
    static void RunSmpSelfTestTask(void *argument)
    {
        auto *task = static_cast<SmpSelfTestTaskArgument *>(argument);
        if (!task || !task->slot || task->taskIndex >= kSmpSelfTestTaskCount)
            return;

        ApBootSlot &slot = *task->slot;
        if (task->taskIndex == 2)
        {
            __atomic_store_n(&slot.schedulerTaskTurns[2], 1ull, __ATOMIC_RELAXED);
            if (QK::TaskManager::instance().sleep(5, slot.cpuIndex))
                __atomic_fetch_add(&slot.schedulerTaskWakeups, 1ull, __ATOMIC_RELAXED);
            FinishSmpSelfTestTask();
            return;
        }

        const QC::u32 self = task->taskIndex;
        const QC::u32 peer = 1 - self;
        const QC::u64 startMs = QK::Time::milliseconds();
        const QC::u64 peerStart = __atomic_load_n(&slot.selfTestSpins[peer], __ATOMIC_ACQUIRE);
        bool observed = false;
        for (;;)
        {
            const QC::u64 spins = __atomic_add_fetch(&slot.selfTestSpins[self], 1ull, __ATOMIC_RELEASE);
            if (!observed && __atomic_load_n(&slot.selfTestSpins[peer], __ATOMIC_ACQUIRE) != peerStart)
            {
                observed = true;
                __atomic_store_n(&slot.schedulerTaskTurns[self], spins, __ATOMIC_RELEASE);
            }
            if (observed && __atomic_load_n(&slot.schedulerTaskTurns[peer], __ATOMIC_ACQUIRE) != 0)
                break;
            if (QK::Time::milliseconds() - startMs >= kSmpSelfTestSpinTimeoutMs)
                break;
            QC::pause();
        }
        FinishSmpSelfTestTask();
    }

    extern "C" void QKBootApEntry(limine_mp_info *cpu)
    {
        asm volatile("cli" ::: "memory");

        auto *slot = static_cast<ApBootSlot *>(nullptr);
        if (cpu)
        {
            slot = reinterpret_cast<ApBootSlot *>(static_cast<QC::uptr>(cpu->extra_argument));
            if (slot)
            {
                slot->state = static_cast<QC::u32>(QKBoot::SmpCpuState::Initializing);
                slot->lapicId = cpu->lapic_id;
                slot->cpuIndex = cpu->processor_id;
                slot->idleHeartbeats = 0;
                slot->timerHookTicks = 0;
                slot->schedulerHookKicks = 0;
                for (QC::u32 taskIndex = 0; taskIndex < kSmpSelfTestTaskCount; ++taskIndex)
                    slot->schedulerTaskTurns[taskIndex] = 0;
                slot->schedulerTaskWakeups = 0;
                slot->selfTestSpins[0] = 0;
                slot->selfTestSpins[1] = 0;
                slot->workItemsCompleted = 0;
                slot->workChecksum = 0;
                slot->online = 1;
            }
        }

        // AP-local descriptor state and SIMD state are required before broader kernel work.
        QArch::IDT::instance().load();
        EnableFpuAndSseForCurrentCpu();

        if (slot)
        {
            slot->state = static_cast<QC::u32>(QKBoot::SmpCpuState::Online);
        }

        __atomic_fetch_add(&g_ApOnlineCount, 1u, __ATOMIC_SEQ_CST);

        bool localTimerAttempted = false;
        bool localTimerEnabled = false;
        for (;;)
        {
            if (slot)
            {
                __atomic_fetch_add(&slot->idleHeartbeats, 1ull, __ATOMIC_RELAXED);
                if (!localTimerAttempted && __atomic_load_n(&g_SmpTimerReady, __ATOMIC_ACQUIRE) != 0)
                {
                    localTimerAttempted = true;
                    const QC::u32 initialCount = __atomic_load_n(&g_SmpTimerInitialCount, __ATOMIC_RELAXED);
                    if (QK::Boot::Acpi::InitializeCurrentCpuLocalApic(nullptr) &&
                        QK::Boot::Acpi::ConfigureCurrentCpuLocalApicTimer(
                            QK::IRQ_LOCAL_APIC_TIMER, initialCount, true))
                    {
                        localTimerEnabled = true;
                        asm volatile("sti" ::: "memory");
                    }
                }
                if (__atomic_load_n(&g_SmpSchedulerReady, __ATOMIC_ACQUIRE) != 0)
                {
                    QK::Scheduler::instance().schedule(slot->cpuIndex);
                    // No ready task: sleep until the next local timer tick re-runs the scheduler.
                    if (localTimerEnabled)
                        asm volatile("sti; hlt" ::: "memory");
                    continue;
                }
                (void)RunOneSmpWorkItem(*slot);
            }
            QC::pause();
        }
    }

    static void ConfigureMpStartup(QKBoot::FLogFn Log)
    {
        const limine_mp_response *mp = QK::Boot::Limine::GetMpResponse(g_Req.mp);
        if (!mp || mp->cpu_count == 0 || !mp->cpus)
        {
            __atomic_store_n(&g_SmpTotalCpus, 0, __ATOMIC_SEQ_CST);
            __atomic_store_n(&g_SmpTargetAps, 0, __ATOMIC_SEQ_CST);
            __atomic_store_n(&g_ApSlotCount, 0, __ATOMIC_SEQ_CST);
            if (Log)
                Log("SMP: Limine MP response unavailable\r\n");
            return;
        }

        const QC::u64 totalCpus = mp->cpu_count;
        const QC::u32 bspLapicId = mp->bsp_lapic_id;
        __atomic_store_n(&g_SmpTotalCpus, totalCpus, __ATOMIC_SEQ_CST);
        __atomic_store_n(&g_ApOnlineCount, 0u, __ATOMIC_SEQ_CST);

        QC::usize slotCount = 0;
        QC::u64 targetAps = 0;
        for (QC::u64 i = 0; i < totalCpus; ++i)
        {
            limine_mp_info *cpu = mp->cpus[i];
            if (!cpu)
                continue;
            if (cpu->lapic_id == bspLapicId)
                continue;

            if (slotCount >= kMaxApBootSlots)
                continue;

            ApBootSlot &slot = g_ApBootSlots[slotCount++];
            slot.state = static_cast<QC::u32>(QKBoot::SmpCpuState::Launching);
            slot.lapicId = cpu->lapic_id;
            slot.cpuIndex = cpu->processor_id;
            slot.online = 0;
            slot.idleHeartbeats = 0;
            slot.timerHookTicks = 0;
            slot.schedulerHookKicks = 0;
            for (QC::u32 taskIndex = 0; taskIndex < kSmpSelfTestTaskCount; ++taskIndex)
                slot.schedulerTaskTurns[taskIndex] = 0;
            slot.schedulerTaskWakeups = 0;
            slot.workItemsCompleted = 0;
            slot.workChecksum = 0;
            cpu->extra_argument = static_cast<QC::u64>(reinterpret_cast<QC::uptr>(&slot));
            cpu->goto_address = &QKBootApEntry;
            ++targetAps;
        }

        __atomic_store_n(&g_SmpTargetAps, targetAps, __ATOMIC_SEQ_CST);
        __atomic_store_n(&g_ApSlotCount, static_cast<QC::u32>(slotCount), __ATOMIC_SEQ_CST);

        if (Log)
        {
            Log("SMP: Limine MP total CPUs = ");
            LogU64(Log, totalCpus);
            Log("\r\n");
            Log("SMP: AP startup targets = ");
            LogU64(Log, targetAps);
            Log("\r\n");
        }

        if (targetAps == 0)
            return;

        constexpr QC::u32 kWaitSpins = 8u * 1000u * 1000u;
        for (QC::u32 spin = 0; spin < kWaitSpins; ++spin)
        {
            const QC::u32 online = __atomic_load_n(&g_ApOnlineCount, __ATOMIC_SEQ_CST);
            if (online >= targetAps)
                break;
            QC::pause();
        }

        const QC::u32 onlineFinal = __atomic_load_n(&g_ApOnlineCount, __ATOMIC_SEQ_CST);
        if (Log)
        {
            Log("SMP: AP online = ");
            LogU64(Log, onlineFinal);
            Log("/");
            LogU64(Log, targetAps);
            Log("\r\n");

            QC::u64 onlineStates = 0;
            for (QC::usize i = 0; i < slotCount; ++i)
            {
                if (__atomic_load_n(&g_ApBootSlots[i].state, __ATOMIC_RELAXED) == static_cast<QC::u32>(QKBoot::SmpCpuState::Online))
                    ++onlineStates;
            }
            Log("SMP: AP state online slots = ");
            LogU64(Log, onlineStates);
            Log("\r\n");

            if (slotCount >= kMaxApBootSlots && targetAps < (totalCpus - 1))
                Log("SMP: AP slot capacity reached; extra APs deferred\r\n");
        }
    }

    static const QK::Boot::Config::SysConfigModule *FindSysConfigModuleById(const QK::Boot::Config::SysConfig &cfg, const char *id)
    {
        if (!id || !*id)
            return nullptr;

        for (QC::u32 i = 0; i < cfg.moduleCount && i < 32; ++i)
        {
            const auto &m = cfg.modules[i];
            if (m.id[0] == 0)
                continue;
            if (QC::String::strcmp(m.id, id) == 0)
                return &m;
        }

        return nullptr;
    }

    static bool EndsWithJsn(const char *path)
    {
        if (!path)
            return false;

        const QC::usize n = static_cast<QC::usize>(QC::String::strlen(path));
        if (n < 4)
            return false;

        const char a = path[n - 3];
        const char b = path[n - 2];
        const char c = path[n - 1];

        auto up = [](char ch) -> char {
            if (ch >= 'a' && ch <= 'z')
                return static_cast<char>(ch - 'a' + 'A');
            return ch;
        };

        return (up(a) == 'J' && up(b) == 'S' && up(c) == 'N');
    }

    static bool IsHexDigit(char c)
    {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    static bool IsSafeTokenChar(char c)
    {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-' || c == '.' ||
               c == ':';
    }

    static bool ValidateColorString(const char *s)
    {
        if (!s)
            return false;

        const QC::usize n = static_cast<QC::usize>(QC::String::strlen(s));
        if (n == 0 || n > 40)
            return false;

        // Accept CSS-ish hex colors: #RRGGBB or #RRGGBBAA
        if (s[0] == '#')
        {
            if (!(n == 7 || n == 9))
                return false;
            for (QC::usize i = 1; i < n; ++i)
            {
                if (!IsHexDigit(s[i]))
                    return false;
            }
            return true;
        }

        // Accept CSS-ish: rgba(r, g, b, a)
        {
            auto toLowerAscii = [](char c) -> char
            {
                return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
            };

            auto startsWithIgnoreCaseAscii = [&](const char *text, const char *prefix) -> bool
            {
                if (!text || !prefix)
                    return false;
                while (*prefix)
                {
                    if (*text == 0)
                        return false;
                    if (toLowerAscii(*text) != toLowerAscii(*prefix))
                        return false;
                    ++text;
                    ++prefix;
                }
                return true;
            };

            auto isSpace = [](char c) -> bool
            {
                return c == ' ' || c == '\t' || c == '\n' || c == '\r';
            };

            auto skipSpaces = [&](const char *&p) -> void
            {
                while (p && *p && isSpace(*p))
                    ++p;
            };

            auto parseUInt = [&](const char *&p, QC::u32 &out) -> bool
            {
                skipSpaces(p);
                if (!p || *p < '0' || *p > '9')
                    return false;
                QC::u32 v = 0;
                while (*p >= '0' && *p <= '9')
                {
                    v = v * 10 + static_cast<QC::u32>(*p - '0');
                    ++p;
                    if (v > 1000000)
                        return false;
                }
                out = v;
                return true;
            };

            auto expectChar = [&](const char *&p, char ch) -> bool
            {
                skipSpaces(p);
                if (!p || *p != ch)
                    return false;
                ++p;
                return true;
            };

            auto parseAlphaToken = [&](const char *&p) -> bool
            {
                skipSpaces(p);
                if (!p || (!(*p >= '0' && *p <= '9') && *p != '.'))
                    return false;

                bool hasDigits = false;
                bool hasDot = false;
                QC::u32 intPart = 0;
                while (*p >= '0' && *p <= '9')
                {
                    hasDigits = true;
                    intPart = intPart * 10 + static_cast<QC::u32>(*p - '0');
                    ++p;
                    if (intPart > 1000000)
                        return false;
                }

                QC::u32 frac = 0;
                if (*p == '.')
                {
                    hasDot = true;
                    ++p;
                    while (*p >= '0' && *p <= '9')
                    {
                        hasDigits = true;
                        frac = frac * 10 + static_cast<QC::u32>(*p - '0');
                        ++p;
                        if (frac > 1000000)
                            return false;
                    }
                }

                if (!hasDigits)
                    return false;

                skipSpaces(p);

                if (hasDot)
                {
                    if (intPart > 1)
                        return false;
                    if (intPart == 1 && frac > 0)
                        return false;
                    return true;
                }

                // int form: allow 0..255 (or 0/1 for 0..1 style)
                return intPart <= 255;
            };

            if (startsWithIgnoreCaseAscii(s, "rgba("))
            {
                const char *p = s + 5;
                QC::u32 r = 0;
                QC::u32 g = 0;
                QC::u32 b = 0;
                if (!parseUInt(p, r) || r > 255)
                    return false;
                if (!expectChar(p, ','))
                    return false;
                if (!parseUInt(p, g) || g > 255)
                    return false;
                if (!expectChar(p, ','))
                    return false;
                if (!parseUInt(p, b) || b > 255)
                    return false;
                if (!expectChar(p, ','))
                    return false;
                if (!parseAlphaToken(p))
                    return false;
                if (!expectChar(p, ')'))
                    return false;
                skipSpaces(p);
                return *p == 0;
            }
        }

        // Also allow a short token (for future theme tokens), but keep it very constrained.
        for (QC::usize i = 0; i < n; ++i)
        {
            if (!IsSafeTokenChar(s[i]))
                return false;
        }
        return true;
    }

    static bool ValidateThemeButtonOverrideObject(QKBoot::FLogFn Log, const QC::JSON::Value &v)
    {
        if (!v.isObject())
        {
            if (Log)
                Log("DesktopOverrides: theme.overrides.button entry must be an object\r\n");
            return false;
        }

        const QC::JSON::Object *obj = v.asObject();
        const QC::usize n = obj ? obj->size() : 0;
        for (QC::usize i = 0; i < n; ++i)
        {
            const auto &ent = (*obj)[i];
            if (!ent.key || !ent.value)
                return false;

            const char *k = ent.key;
            const QC::JSON::Value *val = ent.value;

            const bool isColorKey = (QC::String::strcmp(k, "fillNormal") == 0) || (QC::String::strcmp(k, "fillHover") == 0) ||
                                    (QC::String::strcmp(k, "fillPressed") == 0) || (QC::String::strcmp(k, "text") == 0) ||
                                    (QC::String::strcmp(k, "border") == 0);
            if (isColorKey)
            {
                if (!val->isString() || !ValidateColorString(val->asString(nullptr)))
                {
                    if (Log)
                        Log("DesktopOverrides: invalid theme button color\r\n");
                    return false;
                }
                continue;
            }

            if (QC::String::strcmp(k, "glass") == 0)
            {
                if (!val->isBool())
                {
                    if (Log)
                        Log("DesktopOverrides: theme button glass must be bool\r\n");
                    return false;
                }
                continue;
            }

            if (QC::String::strcmp(k, "shineIntensity") == 0)
            {
                if (!val->isNumber())
                {
                    if (Log)
                        Log("DesktopOverrides: theme button shineIntensity must be number\r\n");
                    return false;
                }
                continue;
            }

            if (QC::String::strcmp(k, "material") == 0)
            {
                if (!val->isString())
                {
                    if (Log)
                        Log("DesktopOverrides: theme button material must be string\r\n");
                    return false;
                }

                const char *s = val->asString(nullptr);
                const QC::usize len = s ? static_cast<QC::usize>(QC::String::strlen(s)) : 0;
                if (len == 0 || len >= 48)
                {
                    if (Log)
                        Log("DesktopOverrides: theme button material invalid length\r\n");
                    return false;
                }
                for (QC::usize j = 0; j < len; ++j)
                {
                    if (!IsSafeTokenChar(s[j]))
                    {
                        if (Log)
                            Log("DesktopOverrides: theme button material contains invalid characters\r\n");
                        return false;
                    }
                }
                continue;
            }

            if (Log)
            {
                Log("DesktopOverrides: unknown theme button key: ");
                Log(k);
                Log("\r\n");
            }
            return false;
        }

        return true;
    }

    static bool ValidateThemeMaterialLayerSet(QKBoot::FLogFn Log, const QC::JSON::Value &v)
    {
        if (!v.isObject())
        {
            if (Log)
                Log("DesktopOverrides: theme material layer set must be object\r\n");
            return false;
        }

        const QC::JSON::Object *obj = v.asObject();
        const QC::usize n = obj ? obj->size() : 0;
        for (QC::usize i = 0; i < n; ++i)
        {
            const auto &ent = (*obj)[i];
            if (!ent.key || !ent.value)
                return false;

            const char *k = ent.key;
            const QC::JSON::Value *val = ent.value;

            const bool allowed = (QC::String::strcmp(k, "glossTop") == 0) || (QC::String::strcmp(k, "glossBottom") == 0) ||
                                 (QC::String::strcmp(k, "shadeTop") == 0) || (QC::String::strcmp(k, "shadeBottom") == 0);
            if (!allowed)
            {
                if (Log)
                {
                    Log("DesktopOverrides: unknown theme material layer key: ");
                    Log(k);
                    Log("\r\n");
                }
                return false;
            }

            if (!val->isString() || !ValidateColorString(val->asString(nullptr)))
            {
                if (Log)
                    Log("DesktopOverrides: invalid theme material layer color\r\n");
                return false;
            }
        }

        return true;
    }

    static bool ValidateThemeMaterialObject(QKBoot::FLogFn Log, const QC::JSON::Value &v)
    {
        if (!v.isObject())
        {
            if (Log)
                Log("DesktopOverrides: theme material must be object\r\n");
            return false;
        }

        const QC::JSON::Object *obj = v.asObject();
        const QC::usize n = obj ? obj->size() : 0;
        for (QC::usize i = 0; i < n; ++i)
        {
            const auto &ent = (*obj)[i];
            if (!ent.key || !ent.value)
                return false;

            const char *k = ent.key;
            const QC::JSON::Value *val = ent.value;

            const bool isColorKey = (QC::String::strcmp(k, "fillNormal") == 0) || (QC::String::strcmp(k, "fillHover") == 0) ||
                                    (QC::String::strcmp(k, "fillPressed") == 0) || (QC::String::strcmp(k, "text") == 0) ||
                                    (QC::String::strcmp(k, "border") == 0);
            if (isColorKey)
            {
                if (!val->isString() || !ValidateColorString(val->asString(nullptr)))
                {
                    if (Log)
                        Log("DesktopOverrides: invalid theme material color\r\n");
                    return false;
                }
                continue;
            }

            if (QC::String::strcmp(k, "glass") == 0)
            {
                if (!val->isBool())
                {
                    if (Log)
                        Log("DesktopOverrides: theme material glass must be bool\r\n");
                    return false;
                }
                continue;
            }

            if (QC::String::strcmp(k, "shineIntensity") == 0)
            {
                if (!val->isNumber())
                {
                    if (Log)
                        Log("DesktopOverrides: theme material shineIntensity must be number\r\n");
                    return false;
                }
                continue;
            }

            if (QC::String::strcmp(k, "layers") == 0)
            {
                if (!val->isObject())
                {
                    if (Log)
                        Log("DesktopOverrides: theme material layers must be object\r\n");
                    return false;
                }

                const QC::JSON::Object *layersObj = val->asObject();
                const QC::usize ln = layersObj ? layersObj->size() : 0;
                for (QC::usize li = 0; li < ln; ++li)
                {
                    const auto &lent = (*layersObj)[li];
                    if (!lent.key || !lent.value)
                        return false;

                    const bool allowedState = (QC::String::strcmp(lent.key, "normal") == 0) || (QC::String::strcmp(lent.key, "hover") == 0) ||
                                              (QC::String::strcmp(lent.key, "pressed") == 0);
                    if (!allowedState)
                    {
                        if (Log)
                        {
                            Log("DesktopOverrides: unknown theme material layer state: ");
                            Log(lent.key);
                            Log("\r\n");
                        }
                        return false;
                    }

                    if (!ValidateThemeMaterialLayerSet(Log, *lent.value))
                        return false;
                }
                continue;
            }

            if (Log)
            {
                Log("DesktopOverrides: unknown theme material key: ");
                Log(k);
                Log("\r\n");
            }
            return false;
        }

        return true;
    }

    static bool ValidateThemeOverrides(QKBoot::FLogFn Log, const QC::JSON::Value &theme)
    {
        if (!theme.isObject())
        {
            if (Log)
                Log("DesktopOverrides: theme must be an object\r\n");
            return false;
        }

        const QC::JSON::Object *themeObj = theme.asObject();
        const QC::usize tn = themeObj ? themeObj->size() : 0;
        for (QC::usize i = 0; i < tn; ++i)
        {
            const auto &ent = (*themeObj)[i];
            if (!ent.key || !ent.value)
                return false;
            if (QC::String::strcmp(ent.key, "overrides") != 0)
            {
                if (Log)
                {
                    Log("DesktopOverrides: unknown theme key: ");
                    Log(ent.key);
                    Log("\r\n");
                }
                return false;
            }
        }

        const QC::JSON::Value *overrides = theme.find("overrides");
        if (!overrides)
            return true;

        if (!overrides->isObject())
        {
            if (Log)
                Log("DesktopOverrides: theme.overrides must be an object\r\n");
            return false;
        }

        const QC::JSON::Object *ovrObj = overrides->asObject();
        const QC::usize on = ovrObj ? ovrObj->size() : 0;
        for (QC::usize i = 0; i < on; ++i)
        {
            const auto &ent = (*ovrObj)[i];
            if (!ent.key || !ent.value)
                return false;

            const bool allowed = (QC::String::strcmp(ent.key, "materials") == 0) || (QC::String::strcmp(ent.key, "button") == 0);
            if (!allowed)
            {
                if (Log)
                {
                    Log("DesktopOverrides: unknown theme.overrides key: ");
                    Log(ent.key);
                    Log("\r\n");
                }
                return false;
            }
        }

        if (const QC::JSON::Value *mats = overrides->find("materials"); mats)
        {
            if (!mats->isObject())
            {
                if (Log)
                    Log("DesktopOverrides: theme.overrides.materials must be an object\r\n");
                return false;
            }

            const QC::JSON::Object *matsObj = mats->asObject();
            const QC::usize mn = matsObj ? matsObj->size() : 0;
            for (QC::usize i = 0; i < mn; ++i)
            {
                const auto &ent = (*matsObj)[i];
                if (!ent.key || !ent.value)
                    return false;

                const QC::usize len = static_cast<QC::usize>(QC::String::strlen(ent.key));
                if (len == 0 || len >= 48)
                {
                    if (Log)
                        Log("DesktopOverrides: theme material name invalid length\r\n");
                    return false;
                }
                for (QC::usize j = 0; j < len; ++j)
                {
                    if (!IsSafeTokenChar(ent.key[j]))
                    {
                        if (Log)
                            Log("DesktopOverrides: theme material name contains invalid characters\r\n");
                        return false;
                    }
                }

                if (!ValidateThemeMaterialObject(Log, *ent.value))
                    return false;
            }
        }

        if (const QC::JSON::Value *buttons = overrides->find("button"); buttons)
        {
            if (!buttons->isObject())
            {
                if (Log)
                    Log("DesktopOverrides: theme.overrides.button must be an object\r\n");
                return false;
            }

            const QC::JSON::Object *btnObj = buttons->asObject();
            const QC::usize bn = btnObj ? btnObj->size() : 0;
            for (QC::usize i = 0; i < bn; ++i)
            {
                const auto &ent = (*btnObj)[i];
                if (!ent.key || !ent.value)
                    return false;

                const bool roleAllowed = (QC::String::strcmp(ent.key, "default") == 0) || (QC::String::strcmp(ent.key, "accent") == 0) ||
                                         (QC::String::strcmp(ent.key, "sidebar") == 0) || (QC::String::strcmp(ent.key, "sidebarSelected") == 0) ||
                                         (QC::String::strcmp(ent.key, "taskbar") == 0) || (QC::String::strcmp(ent.key, "taskbarActive") == 0) ||
                                         (QC::String::strcmp(ent.key, "destructive") == 0);
                if (!roleAllowed)
                {
                    if (Log)
                    {
                        Log("DesktopOverrides: unknown theme button role: ");
                        Log(ent.key);
                        Log("\r\n");
                    }
                    return false;
                }

                if (!ValidateThemeButtonOverrideObject(Log, *ent.value))
                    return false;
            }
        }

        return true;
    }

    static bool ValidateLayoutEntry(const QC::JSON::Value &v)
    {
        if (!v.isObject())
            return false;

        const QC::JSON::Object *obj = v.asObject();
        const QC::usize n = obj ? obj->size() : 0;

        for (QC::usize i = 0; i < n; ++i)
        {
            const auto &ent = (*obj)[i];
            const char *k = ent.key;
            const QC::JSON::Value *val = ent.value;

            if (!k || !val)
                return false;

            const bool isAllowed = (QC::String::strcmp(k, "x") == 0) || (QC::String::strcmp(k, "y") == 0) ||
                                   (QC::String::strcmp(k, "width") == 0) || (QC::String::strcmp(k, "height") == 0);
            if (!isAllowed)
                return false;

            if (!(val->isNumber() || val->isString()))
                return false;
        }

        return true;
    }

    static char ToLowerAscii(char c)
    {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
    }

    static bool EqualsIgnoreCaseAscii(const char *a, const char *b)
    {
        if (!a || !b)
            return false;
        while (*a && *b)
        {
            if (ToLowerAscii(*a) != ToLowerAscii(*b))
                return false;
            ++a;
            ++b;
        }
        return *a == '\0' && *b == '\0';
    }

    static bool ValidateDesktopOverridesJson(QKBoot::FLogFn Log, const QC::JSON::Value &root)
    {
        if (!root.isObject())
        {
            if (Log)
                Log("DesktopOverrides: root is not an object\r\n");
            return false;
        }

        const QC::JSON::Object *obj = root.asObject();
        const QC::usize n = obj ? obj->size() : 0;

        for (QC::usize i = 0; i < n; ++i)
        {
            const auto &ent = (*obj)[i];
            const char *k = ent.key;
            const QC::JSON::Value *v = ent.value;

            if (!k || !v)
                return false;

            const bool allowed = (QC::String::strcmp(k, "version") == 0) || (QC::String::strcmp(k, "layout") == 0) ||
                                 (QC::String::strcmp(k, "colors") == 0) || (QC::String::strcmp(k, "banner_text") == 0) ||
                                 (QC::String::strcmp(k, "season") == 0) || (QC::String::strcmp(k, "theme") == 0);
            if (!allowed)
            {
                if (Log)
                {
                    Log("DesktopOverrides: unknown key: ");
                    Log(k);
                    Log("\r\n");
                }
                return false;
            }
        }

        if (const QC::JSON::Value *ver = root.find("version"); ver && !ver->isNumber())
        {
            if (Log)
                Log("DesktopOverrides: version must be a number\r\n");
            return false;
        }

        if (const QC::JSON::Value *banner = root.find("banner_text"); banner)
        {
            if (!banner->isString())
            {
                if (Log)
                    Log("DesktopOverrides: banner_text must be a string\r\n");
                return false;
            }
            const char *s = banner->asString(nullptr);
            const QC::usize len = s ? static_cast<QC::usize>(QC::String::strlen(s)) : 0;
            if (len > 160)
            {
                if (Log)
                    Log("DesktopOverrides: banner_text too long\r\n");
                return false;
            }
        }

        if (const QC::JSON::Value *season = root.find("season"); season)
        {
            if (!season->isString())
            {
                if (Log)
                    Log("DesktopOverrides: season must be a string\r\n");
                return false;
            }

            const char *s = season->asString(nullptr);
            const QC::usize len = s ? static_cast<QC::usize>(QC::String::strlen(s)) : 0;
            if (len == 0 || len > 16)
            {
                if (Log)
                    Log("DesktopOverrides: season invalid length\r\n");
                return false;
            }

            for (QC::usize i = 0; i < len; ++i)
            {
                if (!IsSafeTokenChar(s[i]))
                {
                    if (Log)
                        Log("DesktopOverrides: season contains invalid characters\r\n");
                    return false;
                }
            }

            const bool ok = EqualsIgnoreCaseAscii(s, "spring") || EqualsIgnoreCaseAscii(s, "summer") || EqualsIgnoreCaseAscii(s, "autumn") ||
                            EqualsIgnoreCaseAscii(s, "fall") || EqualsIgnoreCaseAscii(s, "winter") || EqualsIgnoreCaseAscii(s, "auto");
            if (!ok)
            {
                if (Log)
                    Log("DesktopOverrides: unknown season token\r\n");
                return false;
            }
        }

        if (const QC::JSON::Value *layout = root.find("layout"); layout)
        {
            if (!layout->isObject())
            {
                if (Log)
                    Log("DesktopOverrides: layout must be an object\r\n");
                return false;
            }

            const QC::JSON::Object *layoutObj = layout->asObject();
            const QC::usize ln = layoutObj ? layoutObj->size() : 0;
            for (QC::usize i = 0; i < ln; ++i)
            {
                const auto &ent = (*layoutObj)[i];
                if (!ent.key || !ent.value)
                    return false;
                if (!ValidateLayoutEntry(*ent.value))
                {
                    if (Log)
                    {
                        Log("DesktopOverrides: invalid layout entry for id: ");
                        Log(ent.key);
                        Log("\r\n");
                    }
                    return false;
                }
            }
        }

        if (const QC::JSON::Value *colors = root.find("colors"); colors)
        {
            if (!colors->isObject())
            {
                if (Log)
                    Log("DesktopOverrides: colors must be an object\r\n");
                return false;
            }

            const QC::JSON::Object *colorsObj = colors->asObject();
            const QC::usize cn = colorsObj ? colorsObj->size() : 0;
            for (QC::usize i = 0; i < cn; ++i)
            {
                const auto &ent = (*colorsObj)[i];
                if (!ent.key || !ent.value)
                    return false;

                const bool allowed = (QC::String::strcmp(ent.key, "background") == 0) || (QC::String::strcmp(ent.key, "accent") == 0) ||
                                     (QC::String::strcmp(ent.key, "text") == 0);
                if (!allowed)
                {
                    if (Log)
                    {
                        Log("DesktopOverrides: unknown colors key: ");
                        Log(ent.key);
                        Log("\r\n");
                    }
                    return false;
                }

                if (!ent.value->isString())
                {
                    if (Log)
                        Log("DesktopOverrides: color values must be strings\r\n");
                    return false;
                }

                const char *s = ent.value->asString(nullptr);
                if (!ValidateColorString(s))
                {
                    if (Log)
                    {
                        Log("DesktopOverrides: invalid color value for key: ");
                        Log(ent.key);
                        Log("\r\n");
                    }
                    return false;
                }
            }
        }

        if (const QC::JSON::Value *theme = root.find("theme"); theme)
        {
            if (!ValidateThemeOverrides(Log, *theme))
                return false;
        }

        return true;
    }

    static bool ValidateSimpleListManifest(QKBoot::FLogFn Log, const QC::JSON::Value &root, const char *ListKey, const char *Label)
    {
        if (!root.isObject())
        {
            if (Log)
            {
                Log(Label);
                Log(": root is not an object\r\n");
            }
            return false;
        }

        const QC::JSON::Object *obj = root.asObject();
        const QC::usize n = obj ? obj->size() : 0;

        for (QC::usize i = 0; i < n; ++i)
        {
            const auto &ent = (*obj)[i];
            const char *k = ent.key;
            const QC::JSON::Value *v = ent.value;
            if (!k || !v)
                return false;

            const bool allowed = (QC::String::strcmp(k, "version") == 0) || (QC::String::strcmp(k, ListKey) == 0);
            if (!allowed)
            {
                if (Log)
                {
                    Log(Label);
                    Log(": unknown key: ");
                    Log(k);
                    Log("\r\n");
                }
                return false;
            }
        }

        if (const QC::JSON::Value *ver = root.find("version"); !ver || !ver->isNumber())
        {
            if (Log)
            {
                Log(Label);
                Log(": missing/invalid version\r\n");
            }
            return false;
        }

        if (const QC::JSON::Value *lst = root.find(ListKey); !lst || !lst->isArray())
        {
            if (Log)
            {
                Log(Label);
                Log(": missing/invalid list: ");
                Log(ListKey);
                Log("\r\n");
            }
            return false;
        }

        // Shape-only: ensure list items are strings or objects (no semantics yet).
        const QC::JSON::Array *arr = root.find(ListKey)->asArray();
        const QC::usize an = arr ? arr->size() : 0;
        for (QC::usize i = 0; i < an; ++i)
        {
            const QC::JSON::Value *it = (*arr)[i];
            if (!it)
                return false;
            if (!(it->isString() || it->isObject()))
            {
                if (Log)
                {
                    Log(Label);
                    Log(": invalid list item type\r\n");
                }
                return false;
            }
        }

        return true;
    }

    static bool ValidateSecurityJson(QKBoot::FLogFn Log, const QC::JSON::Value &root)
    {
        if (!root.isObject())
        {
            if (Log)
                Log("Security: root is not an object\r\n");
            return false;
        }

        const QC::JSON::Object *obj = root.asObject();
        const QC::usize n = obj ? obj->size() : 0;

        for (QC::usize i = 0; i < n; ++i)
        {
            const auto &ent = (*obj)[i];
            const char *k = ent.key;
            const QC::JSON::Value *v = ent.value;
            if (!k || !v)
                return false;

            // Shape-only allowlist.
            const bool allowed = (QC::String::strcmp(k, "version") == 0) || (QC::String::strcmp(k, "mode") == 0) ||
                                 (QC::String::strcmp(k, "params") == 0);
            if (!allowed)
            {
                if (Log)
                {
                    Log("Security: unknown key: ");
                    Log(k);
                    Log("\r\n");
                }
                return false;
            }
        }

        if (const QC::JSON::Value *ver = root.find("version"); !ver || !ver->isNumber())
        {
            if (Log)
                Log("Security: missing/invalid version\r\n");
            return false;
        }

        if (const QC::JSON::Value *mode = root.find("mode"); mode && !mode->isString())
        {
            if (Log)
                Log("Security: mode must be a string\r\n");
            return false;
        }

        if (const QC::JSON::Value *params = root.find("params"); params && !params->isObject())
        {
            if (Log)
                Log("Security: params must be an object\r\n");
            return false;
        }

        return true;
    }

    static bool ResolveTieredModulePath(const char *tierRoot, const QK::Boot::Config::SysConfigModule &m, char outPath[], QC::usize outCap)
    {
        if (!outPath || outCap == 0)
            return false;
        outPath[0] = 0;

        if (m.path[0] == 0)
            return true;

        // Absolute paths bypass tiering.
        if (m.path[0] == '/')
        {
            QC::String::strncpy(outPath, m.path, outCap);
            outPath[outCap - 1] = 0;
            return true;
        }

        if (!tierRoot || tierRoot[0] == 0)
        {
            QC::String::strncpy(outPath, m.path, outCap);
            outPath[outCap - 1] = 0;
            return true;
        }

        // tierRoot + '/' + relative
        QC::usize rootLen = QC::String::strlen(tierRoot);
        QC::usize relLen = QC::String::strlen(m.path);
        const bool rootEndsWithSlash = (rootLen > 0 && tierRoot[rootLen - 1] == '/');
        const QC::usize extraSlash = rootEndsWithSlash ? 0 : 1;
        const QC::usize total = rootLen + extraSlash + relLen;

        if (total + 1 > outCap)
            return false;

        QC::String::memcpy(outPath, tierRoot, rootLen);
        QC::usize pos = rootLen;
        if (!rootEndsWithSlash)
            outPath[pos++] = '/';
        QC::String::memcpy(outPath + pos, m.path, relLen);
        outPath[pos + relLen] = 0;
        return true;
    }

    static bool ValidateDesktopConfigJson(QKBoot::FLogFn Log, const QC::JSON::Value &root)
    {
        if (!root.isObject())
        {
            if (Log)
                Log("DesktopConfig: root is not an object\r\n");
            return false;
        }

        const QC::JSON::Value *desktop = root.find("desktop");
        if (!desktop || !desktop->isObject())
        {
            if (Log)
                Log("DesktopConfig: missing 'desktop' object\r\n");
            return false;
        }

        const QC::JSON::Value *layout = desktop->find("layout");
        const QC::JSON::Value *controls = layout ? layout->find("controls") : nullptr;
        if (!controls || !controls->isArray())
        {
            if (Log)
                Log("DesktopConfig: missing layout.controls array\r\n");
            return false;
        }

        return true;
    }

    static void SetFirstFailureReason(char outReason[], QC::usize outCap, const char *prefix, const char *id, const char *path)
    {
        if (!outReason || outCap == 0)
            return;
        if (outReason[0] != 0)
            return;

        outReason[0] = 0;

        auto append = [&](const char *s) -> void
        {
            if (!s)
                return;
            QC::usize cur = QC::String::strlen(outReason);
            if (cur + 1 >= outCap)
                return;
            QC::String::strncpy(outReason + cur, s, outCap - cur);
            outReason[outCap - 1] = 0;
        };

        append(prefix);
        if (id && id[0])
        {
            append(" id='");
            append(id);
            append("'");
        }
        if (path && path[0])
        {
            append(" path='");
            append(path);
            append("'");
        }
    }

    static bool SysConfigEarlyStageForRoot(QKBoot::FLogFn Log, QC::u64 ModuleRequest[], const QK::Boot::Config::SysConfig &syscfg,
                                           const char *tierLabel, const char *tierRoot,
                                           bool &outTierOk,
                                           char outFirstFailure[], QC::usize outFirstFailureCap,
                                           QK::Boot::Config::StagedEarlyConfig &outStage)
    {
        outTierOk = true;

        outStage.clear();
        outStage.tier = QK::Boot::Config::ConfigTier::Unknown;
        if (tierRoot && tierRoot[0])
        {
            QC::String::strncpy(outStage.root, tierRoot, sizeof(outStage.root));
            outStage.root[sizeof(outStage.root) - 1] = 0;
        }

        if (outFirstFailure && outFirstFailureCap)
            outFirstFailure[0] = 0;

        if (!Log)
            return true;

        if (syscfg.earlyCount == 0)
        {
            Log("SysConfig: no boot.early entries\r\n");
            return true;
        }

        Log("SysConfig: early phase begin (tier='");
        Log(tierLabel ? tierLabel : "(none)");
        Log("' root='");
        Log(tierRoot && tierRoot[0] ? tierRoot : "(none)");
        Log("' count=");
        LogU64(Log, syscfg.earlyCount);
        Log(")\r\n");

        for (QC::u32 i = 0; i < syscfg.earlyCount && i < 16; ++i)
        {
            const char *id = syscfg.earlyIds[i];
            if (!id || id[0] == 0)
                continue;

            bool stagedThisModule = false;
            QC::u32 stagedIndex = 0;

            const auto *m = FindSysConfigModuleById(syscfg, id);
            if (!m)
            {
                Log("SysConfig: early missing module id='");
                Log(id);
                Log("'\r\n");
                SetFirstFailureReason(outFirstFailure, outFirstFailureCap, "missing module", id, nullptr);
                outTierOk = false;
                continue;
            }

            char resolvedPath[320] = {0};
            if (!ResolveTieredModulePath(tierRoot, *m, resolvedPath, sizeof(resolvedPath)))
            {
                Log("SysConfig: early path resolution failed for id='");
                Log(m->id);
                Log("'\r\n");
                outTierOk = false;
                SetFirstFailureReason(outFirstFailure, outFirstFailureCap, "path resolution failed", m->id, nullptr);
                continue;
            }

            Log("SysConfig: early load id='");
            Log(m->id);
            Log("' type='");
            Log(m->type);
            Log("' required=");
            LogBool(Log, m->required);
            Log(" path='");
            Log(resolvedPath);
            Log("'\r\n");

            if (resolvedPath[0] == 0)
            {
                Log("SysConfig: early module has empty path; skipping\r\n");
                SetFirstFailureReason(outFirstFailure, outFirstFailureCap, "empty path", m->id, nullptr);
                if (m->required)
                    outTierOk = false;
                continue;
            }

            char *buffer = nullptr;
            QC::usize len = 0;
            if (!QK::Boot::Config::ReadFileFromLimineRamdiskModule(Log, ModuleRequest, resolvedPath, buffer, len))
            {
                Log("SysConfig: early read failed for '");
                Log(resolvedPath);
                Log("'\r\n");
                if (m->required)
                    outTierOk = false;
                SetFirstFailureReason(outFirstFailure, outFirstFailureCap, "read failed", m->id, resolvedPath);
                continue;
            }

            // Stage metadata (even for non-JSON modules) for deterministic commit.
            // NOTE: This must happen before the Layer-2 trust gate so manifest-derived fields
            // can be persisted onto the staged slot.
            if (outStage.moduleCount < 16)
            {
                stagedIndex = outStage.moduleCount;
                auto &dst = outStage.modules[outStage.moduleCount++];
                QC::String::strncpy(dst.id, m->id, sizeof(dst.id));
                dst.id[sizeof(dst.id) - 1] = 0;
                QC::String::strncpy(dst.type, m->type, sizeof(dst.type));
                dst.type[sizeof(dst.type) - 1] = 0;
                QC::String::strncpy(dst.resolvedPath, resolvedPath, sizeof(dst.resolvedPath));
                dst.resolvedPath[sizeof(dst.resolvedPath) - 1] = 0;
                dst.required = m->required;
                stagedThisModule = true;
            }

            // Layer 2: kernel-side early modules trust gate.
            // Convention: manifest is adjacent to the module at "<modulePath>.module.json".
            // - contentValidationCode: SHA-256 hex of module bytes (required in production by default)
            // - signatureRequired/hashRequired: optional booleans
            // - status: trusted|quarantined|revoked (production refuses quarantined/revoked for required modules)
            {
                // This gate is intended for loadable kernel subsystems/modules.
                // Do NOT apply it to core boot policy/config JSON artifacts (BOOT.JSN, SYSCFG.JSN, etc.);
                // those are already handled by existing boot signature policy.
                const bool isJsonConfig = EndsWithJsn(resolvedPath);
                const bool isCoreBootConfig = (QC::String::strcmp(m->id, "boot") == 0) || (QC::String::strcmp(m->id, "sysconfig") == 0);
                if (isJsonConfig || isCoreBootConfig)
                    goto skip_layer2_module_trust_gate;

                char manifestPath[384] = {0};
                QC::String::strncpy(manifestPath, resolvedPath, sizeof(manifestPath) - 1);
                manifestPath[sizeof(manifestPath) - 1] = 0;

                const QC::usize baseLen = static_cast<QC::usize>(QC::String::strlen(manifestPath));
                if (baseLen + 12 < sizeof(manifestPath))
                {
                    QC::String::strncpy(manifestPath + baseLen, ".module.json", sizeof(manifestPath) - 1 - baseLen);
                    manifestPath[sizeof(manifestPath) - 1] = 0;

                    char *manifestBuf = nullptr;
                    QC::usize manifestLen = 0;
                    bool haveManifest = QK::Boot::Config::ReadFileFromLimineRamdiskModule(nullptr, ModuleRequest, manifestPath, manifestBuf, manifestLen);
                    (void)0;

                    bool requireHash = kProductionMode;
                    bool requireSig = kProductionMode;
                    const char *manifestRole = nullptr;
                    const char *manifestStatus = nullptr;
                    const char *manifestCvc = nullptr;

                    if (!haveManifest)
                    {
                        if (kProductionMode && m->required)
                        {
                            Log("SysConfig: early module manifest missing; refusing (production mode)\r\n");
                            outTierOk = false;
                            SetFirstFailureReason(outFirstFailure, outFirstFailureCap, "manifest missing", m->id, resolvedPath);
                            operator delete[](buffer);
                            continue;
                        }
                    }
                    else
                    {
                        QC::JSON::Value manifestRoot;
                        const bool parseOk = QC::JSON::parse(manifestBuf, manifestRoot);
                        operator delete[](manifestBuf);

                        if (!parseOk || !manifestRoot.isObject())
                        {
                            if (kProductionMode && m->required)
                            {
                                Log("SysConfig: early module manifest parse failed; refusing (production mode)\r\n");
                                outTierOk = false;
                                SetFirstFailureReason(outFirstFailure, outFirstFailureCap, "manifest parse failed", m->id, manifestPath);
                                operator delete[](buffer);
                                continue;
                            }
                        }
                        else
                        {
                            if (const auto *sigReq = manifestRoot.find("signatureRequired"); sigReq && sigReq->isBool())
                                requireSig = sigReq->asBool();
                            if (const auto *hashReq = manifestRoot.find("hashRequired"); hashReq && hashReq->isBool())
                                requireHash = hashReq->asBool();

                            if (const auto *r = manifestRoot.find("role"); r && r->isString())
                                manifestRole = r->asString();

                            if (const auto *st = manifestRoot.find("status"); st && st->isString())
                                manifestStatus = st->asString();

                            const bool quarantined = manifestStatus && QC::String::strcmp(manifestStatus, "quarantined") == 0;
                            const bool revoked = manifestStatus && QC::String::strcmp(manifestStatus, "revoked") == 0;
                            if (kProductionMode && (quarantined || revoked) && m->required)
                            {
                                Log("SysConfig: early module status not trusted; refusing (production mode)\r\n");
                                outTierOk = false;
                                SetFirstFailureReason(outFirstFailure, outFirstFailureCap, "status not trusted", m->id, resolvedPath);
                                operator delete[](buffer);
                                continue;
                            }

                            if (const auto *c = manifestRoot.find("contentValidationCode"); c && c->isString())
                                manifestCvc = c->asString();

                            if (requireHash)
                            {
                                if (!manifestCvc || !*manifestCvc)
                                {
                                    if (kProductionMode && m->required)
                                    {
                                        Log("SysConfig: early module missing contentValidationCode; refusing (production mode)\r\n");
                                        outTierOk = false;
                                        SetFirstFailureReason(outFirstFailure, outFirstFailureCap, "missing contentValidationCode", m->id, resolvedPath);
                                        operator delete[](buffer);
                                        continue;
                                    }
                                }
                                else
                                {
                                    QC::u8 digest[32];
                                    QK::Boot::Security::Sha256(reinterpret_cast<const QC::u8 *>(buffer), len, digest);
                                    if (!QK::Boot::Security::Sha256DigestEqualsHex(digest, manifestCvc))
                                    {
                                        if (kProductionMode && m->required)
                                        {
                                            Log("SysConfig: early module hash mismatch; refusing (production mode)\r\n");
                                            outTierOk = false;
                                            SetFirstFailureReason(outFirstFailure, outFirstFailureCap, "hash mismatch", m->id, resolvedPath);
                                            operator delete[](buffer);
                                            continue;
                                        }
                                    }
                                }
                            }

                            if (requireSig)
                            {
                                const auto policy = kProductionMode ? QK::Boot::Security::SigPolicy::RequireValid : QK::Boot::Security::SigPolicy::WarnOnly;
                                if (!QK::Boot::Security::VerifySignedFileFromLimineRamdiskPath(Log, ModuleRequest, resolvedPath, policy))
                                {
                                    if (kProductionMode && m->required)
                                    {
                                        Log("SysConfig: early module signature invalid; refusing (production mode)\r\n");
                                        outTierOk = false;
                                        SetFirstFailureReason(outFirstFailure, outFirstFailureCap, "signature invalid", m->id, resolvedPath);
                                        operator delete[](buffer);
                                        continue;
                                    }
                                }
                            }

                            // Persist manifest-derived trust metadata into the staged module snapshot.
                            if (stagedThisModule)
                            {
                                auto &dst = outStage.modules[stagedIndex];
                                dst.hashRequired = requireHash;
                                dst.signatureRequired = requireSig;
                                if (manifestRole && manifestRole[0])
                                {
                                    QC::String::strncpy(dst.role, manifestRole, sizeof(dst.role));
                                    dst.role[sizeof(dst.role) - 1] = 0;
                                }
                                if (manifestStatus && manifestStatus[0])
                                {
                                    QC::String::strncpy(dst.status, manifestStatus, sizeof(dst.status));
                                    dst.status[sizeof(dst.status) - 1] = 0;
                                }
                                if (manifestCvc && manifestCvc[0])
                                {
                                    QC::String::strncpy(dst.contentValidationCode, manifestCvc, sizeof(dst.contentValidationCode));
                                    dst.contentValidationCode[sizeof(dst.contentValidationCode) - 1] = 0;
                                }
                            }
                        }
                    }
                }
            }

        skip_layer2_module_trust_gate:
            (void)0;

            if (EndsWithJsn(resolvedPath))
            {
                QC::JSON::Value root;
                const bool ok = QC::JSON::parse(buffer, root);
                if (!ok)
                {
                    Log("SysConfig: early JSON parse failed for '");
                    Log(resolvedPath);
                    Log("'\r\n");
                    operator delete[](buffer);
                    if (m->required)
                        outTierOk = false;
                    SetFirstFailureReason(outFirstFailure, outFirstFailureCap, "JSON parse failed", m->id, resolvedPath);
                    continue;
                }

                // Minimal desktop config validation is required for tier selection.
                if (QC::String::strcmp(m->id, "desktop") == 0 && QC::String::strcmp(m->type, "config") == 0)
                {
                    if (!ValidateDesktopConfigJson(Log, root))
                    {
                        Log("SysConfig: desktop config validation failed\r\n");
                        operator delete[](buffer);
                        outTierOk = false;
                        SetFirstFailureReason(outFirstFailure, outFirstFailureCap, "desktop config invalid", m->id, resolvedPath);
                        continue;
                    }
                }

                if (QC::String::strcmp(m->type, "desktop_overrides") == 0)
                {
                    if (!ValidateDesktopOverridesJson(Log, root))
                    {
                        Log("SysConfig: desktop overrides validation failed\r\n");
                        operator delete[](buffer);

                        // Overrides are optional, but if present they must be valid in production.
                        outTierOk = false;
                        SetFirstFailureReason(outFirstFailure, outFirstFailureCap, "desktop overrides invalid", m->id, resolvedPath);
                        continue;
                    }
                }

                if (QC::String::strcmp(m->type, "security") == 0)
                {
                    if (!ValidateSecurityJson(Log, root))
                    {
                        Log("SysConfig: security manifest validation failed\r\n");
                        operator delete[](buffer);
                        outTierOk = false;
                        SetFirstFailureReason(outFirstFailure, outFirstFailureCap, "security invalid", m->id, resolvedPath);
                        continue;
                    }
                }

                if (QC::String::strcmp(m->type, "services") == 0)
                {
                    if (!ValidateSimpleListManifest(Log, root, "services", "Services"))
                    {
                        Log("SysConfig: services manifest validation failed\r\n");
                        operator delete[](buffer);
                        outTierOk = false;
                        SetFirstFailureReason(outFirstFailure, outFirstFailureCap, "services invalid", m->id, resolvedPath);
                        continue;
                    }
                }

                if (QC::String::strcmp(m->type, "drivers") == 0)
                {
                    if (!ValidateSimpleListManifest(Log, root, "drivers", "Drivers"))
                    {
                        Log("SysConfig: drivers manifest validation failed\r\n");
                        operator delete[](buffer);
                        outTierOk = false;
                        SetFirstFailureReason(outFirstFailure, outFirstFailureCap, "drivers invalid", m->id, resolvedPath);
                        continue;
                    }
                }

                if (QC::String::strcmp(m->type, "apps") == 0)
                {
                    if (!ValidateSimpleListManifest(Log, root, "apps", "Apps"))
                    {
                        Log("SysConfig: apps manifest validation failed\r\n");
                        operator delete[](buffer);
                        outTierOk = false;
                        SetFirstFailureReason(outFirstFailure, outFirstFailureCap, "apps invalid", m->id, resolvedPath);
                        continue;
                    }
                }

                // Commit parsed JSON into the stage snapshot.
                if (stagedThisModule)
                {
                    auto &dst = outStage.modules[stagedIndex];
                    dst.hasJson = true;
                    dst.json = static_cast<QC::JSON::Value &&>(root);
                }
            }

            Log("SysConfig: early loaded '");
            Log(m->id);
            Log("' bytes=");
            LogU64(Log, static_cast<QC::u64>(len));
            Log("\r\n");

            operator delete[](buffer);
        }

        Log("SysConfig: early phase done\r\n");
        return true;
    }

    static void SilentLog(const char *)
    {
    }

    static bool SysConfigSelectActiveTier(QKBoot::FLogFn Log, QC::u64 ModuleRequest[], const QK::Boot::Config::SysConfig &syscfg,
                                          QK::Boot::Config::ConfigTier &outTier, const char *&outRoot)
    {
        outTier = QK::Boot::Config::ConfigTier::Unknown;
        outRoot = "";

        // Stage validation should never spam serial logs; only the final outcome and/or
        // first-failure fallback reason should be surfaced.
        const auto StageLog = &SilentLog;

        // Clear any previous committed snapshot before selecting a tier.
        QK::Boot::Config::ClearCommittedEarlyConfig();

        const bool haveProdRoot = syscfg.productionRoot[0] != 0;
        const bool haveGoldenRoot = syscfg.goldenRoot[0] != 0;

        QK::Boot::Events::Emit("sysconfig", "select_begin", haveProdRoot ? (haveGoldenRoot ? "have_production=1 have_golden=1" : "have_production=1 have_golden=0")
                                           : (haveGoldenRoot ? "have_production=0 have_golden=1" : "have_production=0 have_golden=0"));

        // Default to production if configured; otherwise golden if configured.
        if (haveProdRoot)
        {
            bool prodOk = true;
            char prodReason[192] = {0};
            QK::Boot::Config::StagedEarlyConfig prodStage{};
            if (!SysConfigEarlyStageForRoot(StageLog, ModuleRequest, syscfg, "production", syscfg.productionRoot, prodOk, prodReason, sizeof(prodReason),
                                            prodStage))
                return false;

            if (prodOk)
            {
                outTier = QK::Boot::Config::ConfigTier::Production;
                outRoot = syscfg.productionRoot;
                prodStage.tier = QK::Boot::Config::ConfigTier::Production;
                QK::Boot::Config::CommitEarlyConfig(static_cast<QK::Boot::Config::StagedEarlyConfig &&>(prodStage));

                QK::Boot::Events::Emit("sysconfig", "select_ok", "active_tier=production");
                return true;
            }

            if (prodReason[0])
            {
                char details[192];
                QC::String::memset(details, 0, sizeof(details));
                QC::String::strncpy(details, "reason=", sizeof(details) - 1);
                const QC::usize cur = static_cast<QC::usize>(QC::String::strlen(details));
                if (cur < sizeof(details) - 1)
                    QC::String::strncpy(details + cur, prodReason, sizeof(details) - 1 - cur);
                QK::Boot::Events::Emit("sysconfig", "production_invalid", details);
            }
            else
            {
                QK::Boot::Events::Emit("sysconfig", "production_invalid", "");
            }

            if (haveGoldenRoot)
            {
                if (Log)
                {
                    Log("SysConfig: production tier invalid");
                    if (prodReason[0])
                    {
                        Log(": ");
                        Log(prodReason);
                    }
                    Log("; falling back to golden\r\n");
                }

                bool goldenOk = true;
                char goldenReason[192] = {0};
                QK::Boot::Config::StagedEarlyConfig goldenStage{};
                if (!SysConfigEarlyStageForRoot(StageLog, ModuleRequest, syscfg, "golden", syscfg.goldenRoot, goldenOk, goldenReason, sizeof(goldenReason),
                                                goldenStage))
                    return false;

                if (!goldenOk)
                {
                    if (Log)
                    {
                        Log("SysConfig: golden tier invalid");
                        if (goldenReason[0])
                        {
                            Log(": ");
                            Log(goldenReason);
                        }
                        Log("\r\n");
                    }
                    if (kProductionMode)
                        return false;
                }

                outTier = QK::Boot::Config::ConfigTier::Golden;
                outRoot = syscfg.goldenRoot;
                if (goldenOk)
                {
                    goldenStage.tier = QK::Boot::Config::ConfigTier::Golden;
                    QK::Boot::Config::CommitEarlyConfig(static_cast<QK::Boot::Config::StagedEarlyConfig &&>(goldenStage));
                }

                if (goldenOk)
                {
                    QK::Boot::Events::Emit("sysconfig", "fallback_ok", "active_tier=golden");
                }
                else
                {
                    if (goldenReason[0])
                    {
                        char details[192];
                        QC::String::memset(details, 0, sizeof(details));
                        QC::String::strncpy(details, "reason=", sizeof(details) - 1);
                        const QC::usize cur = static_cast<QC::usize>(QC::String::strlen(details));
                        if (cur < sizeof(details) - 1)
                            QC::String::strncpy(details + cur, goldenReason, sizeof(details) - 1 - cur);
                        QK::Boot::Events::Emit("sysconfig", "golden_invalid", details);
                    }
                    else
                    {
                        QK::Boot::Events::Emit("sysconfig", "golden_invalid", "");
                    }
                }
                return true;
            }

            // No golden tier configured; continue in dev with production selected.
            outTier = QK::Boot::Config::ConfigTier::Production;
            outRoot = syscfg.productionRoot;

            QK::Boot::Events::Emit("sysconfig", "select_ok", "active_tier=production dev_no_golden=1");
            return true;
        }

        if (haveGoldenRoot)
        {
            bool goldenOk = true;
            char goldenReason[192] = {0};
            QK::Boot::Config::StagedEarlyConfig goldenStage{};
            if (!SysConfigEarlyStageForRoot(StageLog, ModuleRequest, syscfg, "golden", syscfg.goldenRoot, goldenOk, goldenReason, sizeof(goldenReason),
                                            goldenStage))
                return false;

            if (!goldenOk)
            {
                if (Log)
                {
                    Log("SysConfig: golden tier invalid");
                    if (goldenReason[0])
                    {
                        Log(": ");
                        Log(goldenReason);
                    }
                    Log("\r\n");
                }
                if (kProductionMode)
                    return false;
            }

            outTier = QK::Boot::Config::ConfigTier::Golden;
            outRoot = syscfg.goldenRoot;
            if (goldenOk)
            {
                goldenStage.tier = QK::Boot::Config::ConfigTier::Golden;
                QK::Boot::Config::CommitEarlyConfig(static_cast<QK::Boot::Config::StagedEarlyConfig &&>(goldenStage));
            }

            if (goldenOk)
            {
                QK::Boot::Events::Emit("sysconfig", "select_ok", "active_tier=golden");
            }
        }

        return true;
    }

    static QK::Runtime::TierKind ToTierKind(QK::Boot::Config::ConfigTier t)
    {
        switch (t)
        {
        case QK::Boot::Config::ConfigTier::Production:
            return QK::Runtime::TierKind::Production;
        case QK::Boot::Config::ConfigTier::Golden:
            return QK::Runtime::TierKind::Golden;
        default:
            return QK::Runtime::TierKind::Unknown;
        }
    }

    static void RebuildRuntimeRegistriesFromCommittedEarlyConfig()
    {
        QK::Runtime::BootSeedConfig seed{};
        seed.tier = ToTierKind(QK::Boot::Config::GetActiveConfigTier());

        const char *tierName = QK::Boot::Config::GetActiveConfigTierName();
        const char *tierRoot = QK::Boot::Config::GetActiveConfigTierRoot();

        if (tierName)
            QC::String::strncpy(seed.tierName, tierName, sizeof(seed.tierName) - 1);
        if (tierRoot)
            QC::String::strncpy(seed.tierRoot, tierRoot, sizeof(seed.tierRoot) - 1);

        const auto *stage = QK::Boot::Config::GetCommittedEarlyConfig();
        if (stage)
        {
            QC::u32 n = stage->moduleCount;
            if (n > 16)
                n = 16;
            seed.moduleCount = n;
            for (QC::u32 i = 0; i < n; ++i)
            {
                const auto &m = stage->modules[i];

                if (m.id[0])
                    QC::String::strncpy(seed.modules[i].id, m.id, sizeof(seed.modules[i].id) - 1);
                if (m.type[0])
                    QC::String::strncpy(seed.modules[i].type, m.type, sizeof(seed.modules[i].type) - 1);
                if (m.resolvedPath[0])
                    QC::String::strncpy(seed.modules[i].resolvedPath, m.resolvedPath, sizeof(seed.modules[i].resolvedPath) - 1);

                seed.modules[i].required = m.required;
                seed.modules[i].hasJson = m.hasJson;

                if (m.role[0])
                    QC::String::strncpy(seed.modules[i].role, m.role, sizeof(seed.modules[i].role) - 1);
                if (m.status[0])
                    QC::String::strncpy(seed.modules[i].status, m.status, sizeof(seed.modules[i].status) - 1);
                seed.modules[i].hashRequired = m.hashRequired;
                seed.modules[i].signatureRequired = m.signatureRequired;
            }
        }

        QK::Runtime::Registries::instance().rebuildFromBootSeed(seed);

        // Debug visibility: dump early modules and their trust metadata.
        if (g_Log)
            QK::Runtime::Registries::instance().dumpBootSeedModules(g_Log);
    }

    static const char *firmwareTypeToString(QC::u64 t)
    {
        switch (t)
        {
        case LIMINE_FIRMWARE_TYPE_X86BIOS:
            return "x86 BIOS";
        case LIMINE_FIRMWARE_TYPE_UEFI32:
            return "UEFI32";
        case LIMINE_FIRMWARE_TYPE_UEFI64:
            return "UEFI64";
        case LIMINE_FIRMWARE_TYPE_SBI:
            return "SBI";
        default:
            return "UNKNOWN";
        }
    }

}

namespace QKBoot
{
    void setLogFn(FLogFn log)
    {
        g_LogUpstream = log;
        g_Log = &BootLogFanout;

        // Mirror structured events to the boot log + upstream serial.
        QK::Boot::Events::SetSerialSink(g_Log);
    }

    void setLimineRequests(const LimineRequests &req)
    {
        g_Req = req;
    }

    bool initializeSmpTimers(QC::u32 frequencyHz)
    {
        if (frequencyHz == 0)
            return false;
        if (__atomic_load_n(&g_SmpTargetAps, __ATOMIC_ACQUIRE) == 0)
            return true;
        if (__atomic_load_n(&g_SmpTimerReady, __ATOMIC_ACQUIRE) != 0)
            return __atomic_load_n(&g_SmpTimerFrequency, __ATOMIC_RELAXED) == frequencyHz &&
                   __atomic_load_n(&g_SmpSchedulerReady, __ATOMIC_ACQUIRE) != 0;
        if (!QK::Boot::Acpi::InitializeBspLocalApic(nullptr))
            return false;

        QK::InterruptManager::instance().registerHandler(
            QK::IRQ_LOCAL_APIC_TIMER, &LocalApicTimerInterrupt);

        QC::u32 initialCount = 0;
        if (!CalibrateLocalApicTimer(frequencyHz, initialCount))
        {
            if (g_Log)
                g_Log("SMP: local APIC timer calibration failed\r\n");
            return false;
        }

        const QC::u32 pitFrequency = QDrv::Timer::instance().frequency();
        QC::u32 quantumTicks = frequencyHz / 100;
        if (quantumTicks == 0)
            quantumTicks = 1;
        __atomic_store_n(&g_SmpTimerInitialCount, initialCount, __ATOMIC_RELAXED);
        __atomic_store_n(&g_SmpTimerQuantumTicks, quantumTicks, __ATOMIC_RELAXED);
        __atomic_store_n(&g_SmpTimerFrequency, frequencyHz, __ATOMIC_RELAXED);
        __atomic_store_n(&g_SmpTimerReady, 1u, __ATOMIC_RELEASE);

        const QC::usize slotCount = __atomic_load_n(&g_ApSlotCount, __ATOMIC_ACQUIRE);
        const QC::u64 startTicks = QDrv::Timer::instance().ticks();
        QC::u64 spins = 0;
        QC::usize timerOnline = 0;
        QC::usize schedulerOnline = 0;
        do
        {
            timerOnline = 0;
            schedulerOnline = 0;
            for (QC::usize i = 0; i < slotCount; ++i)
            {
                if (__atomic_load_n(&g_ApBootSlots[i].timerHookTicks, __ATOMIC_RELAXED) != 0)
                    ++timerOnline;
                if (__atomic_load_n(&g_ApBootSlots[i].schedulerHookKicks, __ATOMIC_RELAXED) != 0)
                    ++schedulerOnline;
            }
            if ((timerOnline == slotCount && schedulerOnline == slotCount) ||
                QDrv::Timer::instance().ticks() - startTicks >= 25 ||
                ++spins >= 100000000ull)
                break;
            QC::pause();
        } while (true);

        if (g_Log)
        {
            g_Log("SMP: AP-local LAPIC timer enabled at ");
            LogU64(g_Log, frequencyHz);
            g_Log(" Hz (");
            LogU64(g_Log, pitFrequency);
            g_Log(" Hz PIT calibration), APs ticking ");
            LogU64(g_Log, timerOnline);
            g_Log("/");
            LogU64(g_Log, slotCount);
            g_Log(", quanta ");
            LogU64(g_Log, schedulerOnline);
            g_Log("/");
            LogU64(g_Log, slotCount);
            g_Log("\r\n");
        }
        if (timerOnline != slotCount || schedulerOnline != slotCount)
            return false;

        QK::TaskManager &taskManager = QK::TaskManager::instance();
        for (QC::usize i = 0; i < slotCount; ++i)
        {
            ApBootSlot &slot = g_ApBootSlots[i];
            const QK::TaskId id = taskManager.createTask(
                "ap-worker", &RunApWorkerTask, &slot, QK::TaskPriority::Normal, slot.cpuIndex);
            if (id == 0)
            {
                for (QC::usize cleanupSlot = 0; cleanupSlot < i; ++cleanupSlot)
                {
                    taskManager.destroyTask(g_ApWorkerTaskIds[cleanupSlot]);
                    g_ApWorkerTaskIds[cleanupSlot] = 0;
                }
                if (g_Log)
                    g_Log("SMP: failed to create AP worker tasks\r\n");
                return false;
            }
            g_ApWorkerTaskIds[i] = id;
        }

        QK::Scheduler::instance().initialize();
        for (QC::usize i = 0; i < slotCount; ++i)
            QK::Scheduler::instance().addTask(g_ApWorkerTaskIds[i]);
        QK::Scheduler::instance().start();
        __atomic_store_n(&g_SmpSchedulerReady, 1u, __ATOMIC_RELEASE);
        if (g_Log)
            g_Log("SMP: timer-preemptive AP task scheduler started\r\n");
        return true;
    }

    bool getSmpStatusSnapshot(SmpStatusSnapshot &out)
    {
        out = SmpStatusSnapshot{};

        out.totalCpus = __atomic_load_n(&g_SmpTotalCpus, __ATOMIC_SEQ_CST);
        out.targetAps = __atomic_load_n(&g_SmpTargetAps, __ATOMIC_SEQ_CST);
        out.onlineAps = static_cast<QC::u64>(__atomic_load_n(&g_ApOnlineCount, __ATOMIC_SEQ_CST));

        QC::usize slotCount = static_cast<QC::usize>(__atomic_load_n(&g_ApSlotCount, __ATOMIC_SEQ_CST));
        if (slotCount > kSmpStatusMaxEntries)
            slotCount = kSmpStatusMaxEntries;
        out.slotCount = slotCount;

        for (QC::usize i = 0; i < slotCount; ++i)
        {
            const ApBootSlot &src = g_ApBootSlots[i];
            SmpCpuStatusEntry &dst = out.entries[i];
            dst.cpuIndex = __atomic_load_n(&src.cpuIndex, __ATOMIC_RELAXED);
            dst.lapicId = __atomic_load_n(&src.lapicId, __ATOMIC_RELAXED);
            dst.online = __atomic_load_n(&src.online, __ATOMIC_RELAXED) != 0;
            dst.state = static_cast<SmpCpuState>(__atomic_load_n(&src.state, __ATOMIC_RELAXED));
            dst.idleHeartbeats = __atomic_load_n(&src.idleHeartbeats, __ATOMIC_RELAXED);
            dst.timerHookTicks = __atomic_load_n(&src.timerHookTicks, __ATOMIC_RELAXED);
            dst.schedulerHookKicks = __atomic_load_n(&src.schedulerHookKicks, __ATOMIC_RELAXED);
            for (QC::u32 taskIndex = 0; taskIndex < kSmpSelfTestTaskCount; ++taskIndex)
                dst.schedulerTaskTurns[taskIndex] =
                    __atomic_load_n(&src.schedulerTaskTurns[taskIndex], __ATOMIC_RELAXED);
            dst.schedulerTaskWakeups =
                __atomic_load_n(&src.schedulerTaskWakeups, __ATOMIC_RELAXED);
            dst.schedulerTasksReaped =
                QK::TaskManager::instance().reapedTaskCount(dst.cpuIndex);
            dst.workItemsCompleted = __atomic_load_n(&src.workItemsCompleted, __ATOMIC_RELAXED);
            dst.workChecksum = __atomic_load_n(&src.workChecksum, __ATOMIC_RELAXED);
        }

        return out.totalCpus != 0 || out.slotCount != 0 || out.onlineAps != 0;
    }

    bool submitSmpWork(SmpWorkFn work, QC::u64 argument)
    {
        if (__atomic_load_n(&g_ApOnlineCount, __ATOMIC_ACQUIRE) == 0)
            return false;
        return EnqueueSmpWork(work, argument, false);
    }

    bool startSmpSelfTest()
    {
        if (__atomic_load_n(&g_SmpSchedulerReady, __ATOMIC_ACQUIRE) == 0 || !QK::Time::available())
            return false;

        const QC::usize slotCount = __atomic_load_n(&g_ApSlotCount, __ATOMIC_ACQUIRE);
        if (slotCount == 0)
            return false;

        QC::u32 expectedInactive = 0;
        if (!__atomic_compare_exchange_n(&g_SmpSelfTestActive, &expectedInactive, 1u, false,
                                         __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return false;

        __atomic_store_n(&g_SmpSelfTestExpected, 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&g_SmpSelfTestFinished, 0u, __ATOMIC_RELEASE);
        for (QC::usize i = 0; i < slotCount; ++i)
        {
            ApBootSlot &slot = g_ApBootSlots[i];
            for (QC::u32 taskIndex = 0; taskIndex < kSmpSelfTestTaskCount; ++taskIndex)
                __atomic_store_n(&slot.schedulerTaskTurns[taskIndex], 0ull, __ATOMIC_RELAXED);
            __atomic_store_n(&slot.schedulerTaskWakeups, 0ull, __ATOMIC_RELAXED);
            __atomic_store_n(&slot.selfTestSpins[0], 0ull, __ATOMIC_RELAXED);
            __atomic_store_n(&slot.selfTestSpins[1], 0ull, __ATOMIC_RELEASE);
        }

        // Create every task before scheduling any, so a creation failure can be undone safely.
        static QK::TaskId taskIds[kMaxApBootSlots][kSmpSelfTestTaskCount]{};
        QK::TaskManager &taskManager = QK::TaskManager::instance();
        for (QC::usize i = 0; i < slotCount; ++i)
        {
            for (QC::u32 taskIndex = 0; taskIndex < kSmpSelfTestTaskCount; ++taskIndex)
            {
                SmpSelfTestTaskArgument &argument = g_SmpSelfTestArguments[i][taskIndex];
                argument.slot = &g_ApBootSlots[i];
                argument.taskIndex = taskIndex;
                taskIds[i][taskIndex] = taskManager.createTask(
                    taskIndex == 2 ? "smp-selftest-sleep" : "smp-selftest-spin",
                    &RunSmpSelfTestTask, &argument, QK::TaskPriority::Normal,
                    g_ApBootSlots[i].cpuIndex);
                if (taskIds[i][taskIndex] != 0)
                    continue;

                for (QC::usize cleanupSlot = 0; cleanupSlot <= i; ++cleanupSlot)
                {
                    for (QC::u32 cleanupTask = 0; cleanupTask < kSmpSelfTestTaskCount; ++cleanupTask)
                    {
                        if (taskIds[cleanupSlot][cleanupTask] != 0)
                            taskManager.destroyTask(taskIds[cleanupSlot][cleanupTask]);
                        taskIds[cleanupSlot][cleanupTask] = 0;
                    }
                }
                __atomic_store_n(&g_SmpSelfTestActive, 0u, __ATOMIC_RELEASE);
                return false;
            }
        }

        const QC::u32 expectedFinished = static_cast<QC::u32>(slotCount) * kSmpSelfTestTaskCount;
        __atomic_store_n(&g_SmpSelfTestExpected, expectedFinished, __ATOMIC_RELEASE);
        for (QC::usize i = 0; i < slotCount; ++i)
        {
            for (QC::u32 taskIndex = 0; taskIndex < kSmpSelfTestTaskCount; ++taskIndex)
            {
                QK::Scheduler::instance().addTask(taskIds[i][taskIndex]);
                taskIds[i][taskIndex] = 0;
            }
        }
        return true;
    }

    bool getSmpSelfTestStatus(SmpSelfTestStatus &out)
    {
        out = SmpSelfTestStatus{};
        out.expectedTasks = __atomic_load_n(&g_SmpSelfTestExpected, __ATOMIC_ACQUIRE);
        out.finishedTasks = __atomic_load_n(&g_SmpSelfTestFinished, __ATOMIC_ACQUIRE);
        out.running = __atomic_load_n(&g_SmpSelfTestActive, __ATOMIC_ACQUIRE) != 0;
        if (out.expectedTasks == 0)
            return false;

        const QC::usize slotCount = __atomic_load_n(&g_ApSlotCount, __ATOMIC_ACQUIRE);
        out.totalAps = static_cast<QC::u32>(slotCount);
        for (QC::usize i = 0; i < slotCount; ++i)
        {
            const ApBootSlot &slot = g_ApBootSlots[i];
            if (__atomic_load_n(&slot.schedulerTaskTurns[0], __ATOMIC_ACQUIRE) != 0 &&
                __atomic_load_n(&slot.schedulerTaskTurns[1], __ATOMIC_ACQUIRE) != 0 &&
                __atomic_load_n(&slot.schedulerTaskTurns[2], __ATOMIC_ACQUIRE) != 0 &&
                __atomic_load_n(&slot.schedulerTaskWakeups, __ATOMIC_ACQUIRE) != 0)
                ++out.passedAps;
        }
        return true;
    }

    bool runSmpWork(QC::u32 workItems, QC::u64 &completed, QC::u64 &checksum)
    {
        completed = 0;
        checksum = 0;

        if (workItems == 0 || __atomic_load_n(&g_ApOnlineCount, __ATOMIC_ACQUIRE) == 0)
            return false;

        QC::u32 expectedInactive = 0;
        if (!__atomic_compare_exchange_n(&g_SmpWorkBatchActive, &expectedInactive, 1u, false,
                                         __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return false;

        const QC::usize slotCount = __atomic_load_n(&g_ApSlotCount, __ATOMIC_ACQUIRE);
        for (QC::usize i = 0; i < slotCount && i < kSmpStatusMaxEntries; ++i)
        {
            __atomic_store_n(&g_ApBootSlots[i].workItemsCompleted, 0ull, __ATOMIC_RELAXED);
            __atomic_store_n(&g_ApBootSlots[i].workChecksum, 0ull, __ATOMIC_RELAXED);
        }

        __atomic_store_n(&g_SmpWorkCompleted, 0ull, __ATOMIC_RELAXED);
        __atomic_store_n(&g_SmpWorkChecksum, 0ull, __ATOMIC_RELAXED);
        __atomic_store_n(&g_SmpWorkTarget, static_cast<QC::u64>(workItems), __ATOMIC_RELEASE);

        QC::u32 submitted = 0;
        QC::u64 enqueueSpins = 0;
        while (submitted < workItems)
        {
            if (EnqueueSmpWork(&SmpWorkTestCallback, submitted, true))
            {
                ++submitted;
                enqueueSpins = 0;
                continue;
            }

            if (++enqueueSpins >= 100000000ull)
                break;
            QC::pause();
        }

        __atomic_store_n(&g_SmpWorkTarget, static_cast<QC::u64>(submitted), __ATOMIC_RELEASE);
        if (submitted == 0)
        {
            __atomic_store_n(&g_SmpWorkBatchActive, 0u, __ATOMIC_RELEASE);
            return false;
        }

        constexpr QC::u64 kWaitSpins = 100000000ull;
        for (QC::u64 spin = 0; spin < kWaitSpins; ++spin)
        {
            if (__atomic_load_n(&g_SmpWorkCompleted, __ATOMIC_ACQUIRE) >= submitted)
                break;
            QC::pause();
        }

        completed = __atomic_load_n(&g_SmpWorkCompleted, __ATOMIC_ACQUIRE);
        checksum = __atomic_load_n(&g_SmpWorkChecksum, __ATOMIC_RELAXED);
        if (completed >= submitted)
            __atomic_store_n(&g_SmpWorkBatchActive, 0u, __ATOMIC_RELEASE);

        return completed >= workItems && submitted == workItems;
    }

    void initializeMemory()
    {
        QK::Boot::Memory::InitFromLimineRequests(g_Req.hhdm, g_Req.kernelAddress, g_Log);
    }

    void initializeDrivers()
    {
        if (g_Log)
        {
            const limine_firmware_type_response *FwResp = QK::Boot::Limine::GetFirmwareTypeResponse(g_Req.firmwareType);
            if (FwResp)
            {
                g_Log("Firmware: ");
                g_Log(firmwareTypeToString(FwResp->firmware_type));
                g_Log("\r\n");
            }
            else
            {
                g_Log("Firmware: unknown (no response)\r\n");
            }

            const limine_rsdp_response *RsdpResp = QK::Boot::Limine::GetRsdpResponse(g_Req.rsdp);
            if (RsdpResp && RsdpResp->address)
            {
                const QC::PhysAddr rsdpPhys = static_cast<QC::PhysAddr>(reinterpret_cast<QC::uptr>(RsdpResp->address));
                QK::Boot::Acpi::EnumerateTables(rsdpPhys, g_Log, &QK::Boot::Tpm::TryTpm2Startup);

                QK::Boot::Acpi::CpuTopologySnapshot topo{};
                if (QK::Boot::Acpi::GetCpuTopologySnapshot(topo))
                {
                    g_Log("SMP: ACPI MADT CPUs enabled/total = ");
                    LogU64(g_Log, static_cast<QC::u64>(topo.enabledEntries));
                    g_Log("/");
                    LogU64(g_Log, static_cast<QC::u64>(topo.totalEntries));
                    g_Log("\r\n");
                }
                else
                {
                    g_Log("SMP: ACPI MADT topology unavailable\r\n");
                }
            }
            else
            {
                g_Log("ACPI: no RSDP response\r\n");
            }
        }
        else
        {
            const limine_rsdp_response *RsdpResp = QK::Boot::Limine::GetRsdpResponse(g_Req.rsdp);
            if (RsdpResp && RsdpResp->address)
            {
                const QC::PhysAddr rsdpPhys = static_cast<QC::PhysAddr>(reinterpret_cast<QC::uptr>(RsdpResp->address));
                QK::Boot::Acpi::EnumerateTables(rsdpPhys, nullptr, &QK::Boot::Tpm::TryTpm2Startup);
            }
        }

        QK::Boot::Arch::InitCpuGdtIdtAndInterrupts(g_Log);

        if (g_Log)
        {
            if (QK::Boot::Acpi::InitializeBspLocalApic(g_Log))
                g_Log("SMP: BSP local APIC initialized\r\n");
            else
                g_Log("SMP: BSP local APIC initialization deferred\r\n");

            ConfigureMpStartup(g_Log);
        }
        else
        {
            (void)QK::Boot::Acpi::InitializeBspLocalApic(nullptr);
            ConfigureMpStartup(nullptr);
        }
    }

    [[noreturn]] static void RefusalHaltWithScreen(const char *title, const char *detailsLine1 = nullptr, const char *detailsLine2 = nullptr);

    void initializeBootPolicyAndGate()
    {
        if (!QK::Boot::Security::VerifyBootJsnSignatureFromLimineRamdiskModule(g_Log, g_Req.modules, g_Req.executableFile))
        {
            if (g_Log)
            {
                g_Log("Refusal Mode: boot config signature invalid\r\n");
                g_Log("Will not run in this configuration!  This machine does not meet the hardware requirements as listed above.\r\n");
            }
            RefusalHaltWithScreen("Refusal Mode", "boot config signature invalid", "Will not run in this configuration!");
        }

        QK::Boot::Config::BootPolicy policy{};
        (void)QK::Boot::Config::LoadBootPolicyFromLimineRamdiskModule(g_Log, g_Req.modules, policy);

        QK::Boot::Config::SysConfig syscfg{};
        const bool syscfgLoaded = QK::Boot::Config::LoadSysConfigFromLimineRamdiskModule(g_Log, g_Req.modules, syscfg);

        // Default until proven otherwise.
        QK::Boot::Config::SetActiveConfigTier(QK::Boot::Config::ConfigTier::Unknown, "");

        const auto &cpuReq = policy.requirements.cpu;
        if (cpuReq.requireSse2 && !CpuHasSse2())
        {
            if (g_Log)
            {
                g_Log("Refusal Mode: missing CPU feature\r\n");
                g_Log("Required: SSE2\r\n");
                g_Log("Will not run in this configuration!  This machine does not meet the hardware requirements as listed above.\r\n");
            }
            RefusalHaltWithScreen("Refusal Mode", "missing CPU feature", "Required: SSE2");
        }

        if (cpuReq.requireNx && !CpuHasNx())
        {
            if (g_Log)
            {
                g_Log("Refusal Mode: missing CPU feature\r\n");
                g_Log("Required: NX\r\n");
                g_Log("Will not run in this configuration!  This machine does not meet the hardware requirements as listed above.\r\n");
            }
            RefusalHaltWithScreen("Refusal Mode", "missing CPU feature", "Required: NX");
        }

        if ((cpuReq.require64bit || cpuReq.requireLongmode) && !CpuHasLongMode())
        {
            if (g_Log)
            {
                g_Log("Refusal Mode: missing CPU feature\r\n");
                g_Log("Required: Long Mode (x86_64)\r\n");
                g_Log("Will not run in this configuration!  This machine does not meet the hardware requirements as listed above.\r\n");
            }
            RefusalHaltWithScreen("Refusal Mode", "missing CPU feature", "Required: Long Mode (x86_64)");
        }

        const QC::u64 compiledMinMiB = QK::Boot::Config::kCompiledMinRamMiB;
        const QC::u64 compiledRecMiB = QK::Boot::Config::kCompiledRecommendedRamMiB;

        QC::u64 effectiveMinMiB = compiledMinMiB;
        QC::u64 effectiveRecMiB = compiledRecMiB;

        if (policy.requirements.minRamMiB > effectiveMinMiB)
            effectiveMinMiB = policy.requirements.minRamMiB;
        if (policy.requirements.recommendedRamMiB > effectiveRecMiB)
            effectiveRecMiB = policy.requirements.recommendedRamMiB;
        if (effectiveRecMiB < effectiveMinMiB)
            effectiveRecMiB = effectiveMinMiB;

        QC::u64 availBytes = 0;
        if (!QK::Boot::Limine::GetAvailableMemoryBytes(g_Req.memmap, availBytes))
        {
            if (g_Log)
                g_Log("BootGate: no memmap response; skipping RAM gate\r\n");
            return;
        }

        const QC::u64 availMiB = availBytes / (1024ull * 1024ull);

        if (g_Log)
        {
            g_Log("BootGate: available RAM MiB = ");
            LogU64(g_Log, availMiB);
            g_Log("\r\n");
        }

        if (availMiB < effectiveMinMiB)
        {
            const QC::u64 gap = effectiveMinMiB - availMiB;
            if (g_Log)
            {
                g_Log("Refusal Mode: insufficient RAM\r\n");
                g_Log("Required MiB: ");
                LogU64(g_Log, effectiveMinMiB);
                g_Log("\r\nDetected MiB: ");
                LogU64(g_Log, availMiB);
                g_Log("\r\nGap MiB: ");
                LogU64(g_Log, gap);
                g_Log("\r\n");
                g_Log("Will not run in this configuration!  This machine does not meet the hardware requirements as listed above.\r\n");
            }

            RefusalHaltWithScreen("Refusal Mode", "insufficient RAM", "Will not run in this configuration!");
        }

        if (syscfgLoaded)
        {
            QK::Boot::Config::ConfigTier activeTier = QK::Boot::Config::ConfigTier::Unknown;
            const char *activeRoot = "";
            char tierReason[192] = {0};
            {
                // Mirror the tier validation logic to capture the first failure reason for display.
                // Uses the same silent logging policy as selection.
                const auto StageLog = &SilentLog;
                bool prodOk = true;
                if (syscfg.productionRoot[0] != 0)
                {
                    QK::Boot::Config::StagedEarlyConfig prodStage{};
                    (void)SysConfigEarlyStageForRoot(StageLog, g_Req.modules, syscfg, "production", syscfg.productionRoot, prodOk, tierReason, sizeof(tierReason), prodStage);
                }
                if (!prodOk && syscfg.goldenRoot[0] != 0)
                {
                    bool goldenOk = true;
                    QK::Boot::Config::StagedEarlyConfig goldenStage{};
                    (void)SysConfigEarlyStageForRoot(StageLog, g_Req.modules, syscfg, "golden", syscfg.goldenRoot, goldenOk, tierReason, sizeof(tierReason), goldenStage);
                }
            }

            if (!SysConfigSelectActiveTier(g_Log, g_Req.modules, syscfg, activeTier, activeRoot))
            {
                if (g_Log)
                {
                    g_Log("Refusal Mode: sysconfig early load failed\r\n");
                    g_Log("Will not run in this configuration!\r\n");
                }
                RefusalHaltWithScreen("Refusal Mode", "sysconfig early load failed", tierReason[0] ? tierReason : "Will not run in this configuration!");
            }

            QK::Boot::Config::SetActiveConfigTier(activeTier, activeRoot);
            if (g_Log)
            {
                g_Log("SysConfig: active tier = '");
                g_Log(QK::Boot::Config::GetActiveConfigTierName());
                g_Log("' root='");
                g_Log(QK::Boot::Config::GetActiveConfigTierRoot());
                g_Log("'\r\n");
            }

            if (!QK::Boot::Security::VerifyDesktopCmlSignatureFromLimineRamdiskModule(g_Log, g_Req.modules))
            {
                if (g_Log)
                    g_Log("Refusal Mode: desktop signature invalid\r\n");
                RefusalHaltWithScreen("Refusal Mode", "desktop signature invalid", "Will not run in this configuration!");
            }

            RebuildRuntimeRegistriesFromCommittedEarlyConfig();
            if (g_Log)
                g_Log("Runtime registries: rebuilt\r\n");
        }
        else
        {
            QK::Boot::Events::Emit("sysconfig", "not_loaded", "");
        }

        if (availMiB < effectiveRecMiB)
        {
            if (g_Log)
                g_Log("Reduced Memory Mode: some services/features may be limited\r\n");

            QK::Boot::Events::Emit("bootpolicy", "reduced_memory_mode", "");
        }
    }

    void initializeGraphics()
    {
        const auto BootHeap = QK::Boot::Memory::GetEarlyHeap();
        QK::Boot::Desktop::EarlyHeap Heap{};
        Heap.Buffer = BootHeap.Buffer;
        Heap.Size = BootHeap.Size;

        g_DesktopPrepared = QK::Boot::Desktop::PrepareFromLimineRequests(g_Req.framebuffer, g_Req.modules, Heap, g_Log);
        if (g_Log)
        {
            if (g_DesktopPrepared)
                g_Log("Graphics: framebuffer present\r\n");
            else
                g_Log("Graphics: no framebuffer response\r\n");
        }
    }

    void initializeInput()
    {
        if (!g_DesktopPrepared)
        {
            if (g_Log)
                g_Log("Input: no framebuffer; skipping\r\n");
            return;
        }

        QK::Boot::Desktop::InitializeInput();
    }

    void initializeWindowSystem()
    {
        if (!g_DesktopPrepared)
        {
            if (g_Log)
                g_Log("WindowSystem: no framebuffer; skipping\r\n");
            return;
        }

        QK::Boot::Desktop::InitializeWindowSystem();
    }

    [[noreturn]] void initializeDesktop()
    {
        if (g_Log)
            g_Log("Desktop: starting session\r\n");

        if (!g_DesktopPrepared)
        {
            if (g_Log)
                g_Log("Desktop: no framebuffer response; halting\r\n");
            for (;;)
                asm volatile("hlt");
        }

        QK::Boot::Desktop::InitializeDesktopAndRunLoop();

        // If the desktop session ever returns, halt.
        for (;;)
        {
            asm volatile("hlt");
        }
    }

    struct EarlyFramebufferInfo
    {
        QC::uptr address = 0;
        QC::u32 width = 0;
        QC::u32 height = 0;
        QC::u32 pitch = 0;
    };

    static bool TryGetEarlyFramebufferInfo(QC::u64 FramebufferRequest[], EarlyFramebufferInfo &out)
    {
        out = {};
        if (!FramebufferRequest)
            return false;

        // Limine framebuffer response pointer is stored at index 5 in our request array.
        QC::u64 *fb_response = reinterpret_cast<QC::u64 *>(FramebufferRequest[5]);
        if (!fb_response)
            return false;

        const QC::u64 fb_count = fb_response[1];
        if (fb_count == 0)
            return false;

        QC::u64 **fb_array = reinterpret_cast<QC::u64 **>(fb_response[2]);
        if (!fb_array)
            return false;

        QC::u64 *fb = fb_array[0];
        if (!fb)
            return false;

        out.address = static_cast<QC::uptr>(fb[0]);
        out.width = static_cast<QC::u32>(fb[1]);
        out.height = static_cast<QC::u32>(fb[2]);
        out.pitch = static_cast<QC::u32>(fb[3]);
        return out.address != 0 && out.width != 0 && out.height != 0 && out.pitch != 0;
    }

    static void EarlyFbFill(const EarlyFramebufferInfo &fb, QC::u32 argb)
    {
        if (!fb.address)
            return;
        volatile QC::u8 *base = reinterpret_cast<volatile QC::u8 *>(fb.address);
        for (QC::u32 y = 0; y < fb.height; ++y)
        {
            volatile QC::u8 *rowBytes = base + static_cast<QC::usize>(y) * fb.pitch;
            volatile QC::u32 *row = reinterpret_cast<volatile QC::u32 *>(rowBytes);
            for (QC::u32 x = 0; x < fb.width; ++x)
                row[x] = argb;
        }
    }

    static void EarlyFbDrawRect(const EarlyFramebufferInfo &fb, QC::u32 x, QC::u32 y, QC::u32 w, QC::u32 h, QC::u32 argb)
    {
        if (!fb.address)
            return;
        if (x >= fb.width || y >= fb.height)
            return;
        if (x + w > fb.width)
            w = fb.width - x;
        if (y + h > fb.height)
            h = fb.height - y;

        volatile QC::u8 *base = reinterpret_cast<volatile QC::u8 *>(fb.address);
        for (QC::u32 yy = 0; yy < h; ++yy)
        {
            volatile QC::u8 *rowBytes = base + static_cast<QC::usize>(y + yy) * fb.pitch;
            volatile QC::u32 *row = reinterpret_cast<volatile QC::u32 *>(rowBytes);
            for (QC::u32 xx = 0; xx < w; ++xx)
                row[x + xx] = argb;
        }
    }

    static void EarlyFbDrawChar8x16(const EarlyFramebufferInfo &fb, QC::u32 x, QC::u32 y, char c, QC::u32 fg, QC::u32 bg)
    {
        // Tiny built-in 8x16 font for refusal mode.
        // Scope: enough glyphs to display our refusal strings cleanly (A-Z, 0-9, space and a few punctuations).
        // Unsupported chars fall back to a boxed glyph.

        const auto drawFallback = [&]() {
            EarlyFbDrawRect(fb, x, y, 8, 16, bg);
            EarlyFbDrawRect(fb, x, y, 8, 1, fg);
            EarlyFbDrawRect(fb, x, y + 15, 8, 1, fg);
            EarlyFbDrawRect(fb, x, y, 1, 16, fg);
            EarlyFbDrawRect(fb, x + 7, y, 1, 16, fg);
            EarlyFbDrawRect(fb, x + 3, y + 5, 2, 6, fg);
        };

        // Normalize to uppercase for simplicity.
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');

        // 8x8 glyphs, expanded vertically to 16 scanlines (each row doubled).
        // Bit 7 is left-most pixel.
        struct Glyph8x8
        {
            char ch;
            QC::u8 rows[8];
        };

        static const Glyph8x8 kGlyphs[] = {
            {' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
            {'?', {0x3C,0x66,0x06,0x0C,0x18,0x00,0x18,0x00}},
            {'\'',{0x18,0x18,0x10,0x00,0x00,0x00,0x00,0x00}},
            {':', {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00}},
            {'!', {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00}},
            {'-', {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}},
            {'(', {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}},
            {')', {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00}},
            {'.', {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}},
            {',', {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}},
            {'/', {0x06,0x0C,0x18,0x30,0x60,0x40,0x00,0x00}},
            {'_', {0x00,0x00,0x00,0x00,0x00,0x00,0x7E,0x00}},
            {'=', {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}},

            {'0', {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00}},
            {'1', {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}},
            {'2', {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00}},
            {'3', {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00}},
            {'4', {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00}},
            {'5', {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00}},
            {'6', {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00}},
            {'7', {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00}},
            {'8', {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}},
            {'9', {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00}},

            {'A', {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00}},
            {'B', {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00}},
            {'C', {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00}},
            {'D', {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00}},
            {'E', {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00}},
            {'F', {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00}},
            {'G', {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0x00}},
            {'H', {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}},
            {'I', {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}},
            {'J', {0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00}},
            {'K', {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00}},
            {'L', {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00}},
            {'M', {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00}},
            {'N', {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00}},
            {'O', {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}},
            {'P', {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00}},
            {'Q', {0x3C,0x66,0x66,0x66,0x6E,0x3C,0x0E,0x00}},
            {'R', {0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0x00}},
            {'S', {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00}},
            {'T', {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}},
            {'U', {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}},
            {'V', {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00}},
            {'W', {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}},
            {'X', {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00}},
            {'Y', {0x66,0x66,0x3C,0x18,0x18,0x18,0x18,0x00}},
            {'Z', {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00}},
        };

        const Glyph8x8 *glyph = nullptr;
        for (QC::usize i = 0; i < (sizeof(kGlyphs) / sizeof(kGlyphs[0])); ++i)
        {
            if (kGlyphs[i].ch == c)
            {
                glyph = &kGlyphs[i];
                break;
            }
        }

        if (!glyph)
        {
            drawFallback();
            return;
        }

        // Clear background.
        EarlyFbDrawRect(fb, x, y, 8, 16, bg);

        volatile QC::u8 *base = reinterpret_cast<volatile QC::u8 *>(fb.address);
        for (QC::u32 row8 = 0; row8 < 8; ++row8)
        {
            const QC::u8 bits = glyph->rows[row8];
            for (QC::u32 dy = 0; dy < 2; ++dy)
            {
                const QC::u32 yy = y + row8 * 2 + dy;
                if (yy >= fb.height)
                    continue;
                volatile QC::u8 *rowBytes = base + static_cast<QC::usize>(yy) * fb.pitch;
                volatile QC::u32 *row = reinterpret_cast<volatile QC::u32 *>(rowBytes);
                for (QC::u32 col = 0; col < 8; ++col)
                {
                    const bool on = (bits & (0x80u >> col)) != 0;
                    const QC::u32 xx = x + col;
                    if (xx < fb.width)
                        row[xx] = on ? fg : bg;
                }
            }
        }
    }

    static void EarlyFbDrawText(const EarlyFramebufferInfo &fb, QC::u32 x, QC::u32 y, const char *text, QC::u32 fg, QC::u32 bg)
    {
        if (!text)
            return;
        QC::u32 cx = x;
        QC::u32 cy = y;
        for (const char *p = text; *p; ++p)
        {
            unsigned char uch = static_cast<unsigned char>(*p);
            char ch = static_cast<char>(uch);

            // Sanitize to a small printable subset. This prevents odd bytes
            // (from encoding, or from invalid chars in reason strings) from
            // rendering as fallback boxes.
            if (ch == '\t')
                ch = ' ';
            if (ch >= 'a' && ch <= 'z')
                ch = static_cast<char>(ch - 'a' + 'A');
            const bool okPrintable =
                (ch == ' ') || (ch == '?') || (ch == '\'') || (ch == ':') || (ch == '!') || (ch == '-') ||
                (ch == '(') || (ch == ')') || (ch == '.') || (ch == ',') || (ch == '/') || (ch == '_') || (ch == '=') ||
                (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z');
            if (!okPrintable)
                ch = '?';
            if (ch == '\r')
                continue;
            if (ch == '\n')
            {
                cx = x;
                cy += 16;
                continue;
            }
            if (cx + 8 > fb.width)
            {
                cx = x;
                cy += 16;
            }
            if (cy + 16 > fb.height)
                break;
            EarlyFbDrawChar8x16(fb, cx, cy, ch, fg, bg);
            cx += 8;
        }
    }

    [[noreturn]] static void RefusalHaltWithScreen(const char *title, const char *detailsLine1, const char *detailsLine2)
    {
        EarlyFramebufferInfo fb{};
        const bool haveFb = TryGetEarlyFramebufferInfo(g_Req.framebuffer, fb);
        if (haveFb)
        {
            const QC::u32 kBg = 0xFF000000; // black
            const QC::u32 kFg = 0xFFFFFFFF; // white
            const QC::u32 kPanel = 0xFF202020; // dark gray
            EarlyFbFill(fb, kBg);

            // Center-ish panel
            const QC::u32 margin = 24;
            const QC::u32 px = margin;
            const QC::u32 py = margin;
            const QC::u32 pw = (fb.width > margin * 2) ? (fb.width - margin * 2) : fb.width;
            const QC::u32 ph = (fb.height > margin * 2) ? (fb.height - margin * 2) : fb.height;
            EarlyFbDrawRect(fb, px, py, pw, ph, kPanel);

            // Compose text lines (simple; avoids dynamic allocation)
            char line0[160] = {0};
            char line1[200] = {0};
            char line2[200] = {0};

            QC::String::strncpy(line0, title ? title : "Refusal Mode", sizeof(line0) - 1);
            if (detailsLine1)
                QC::String::strncpy(line1, detailsLine1, sizeof(line1) - 1);
            if (detailsLine2)
                QC::String::strncpy(line2, detailsLine2, sizeof(line2) - 1);

            EarlyFbDrawText(fb, px + 16, py + 16, line0, kFg, kPanel);
            if (line1[0])
                EarlyFbDrawText(fb, px + 16, py + 48, line1, kFg, kPanel);
            if (line2[0])
                EarlyFbDrawText(fb, px + 16, py + 80, line2, kFg, kPanel);

            EarlyFbDrawText(fb, px + 16, py + 128, "System halted.", kFg, kPanel);
        }

        for (;;)
            asm volatile("hlt");
    }
}
