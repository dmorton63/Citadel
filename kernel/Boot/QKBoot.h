#pragma once

#include "QCTypes.h"

namespace QKBoot
{
    using FLogFn = void (*)(const char *);
    // AP callbacks must be bounded and nonblocking; interrupt state may vary.
    using SmpWorkFn = QC::u64 (*)(QC::u64);

    static constexpr QC::usize kSmpStatusMaxEntries = 256;

    enum class SmpCpuState : QC::u32
    {
        Offline = 0,
        Launching = 1,
        Initializing = 2,
        Online = 3,
    };

    struct SmpCpuStatusEntry
    {
        QC::u32 cpuIndex = 0;
        QC::u32 lapicId = 0;
        bool online = false;
        SmpCpuState state = SmpCpuState::Offline;
        QC::u64 idleHeartbeats = 0;
        QC::u64 timerHookTicks = 0;
        QC::u64 schedulerHookKicks = 0;
        QC::u64 schedulerTaskTurns[3] = {};
        QC::u64 schedulerTaskWakeups = 0;
        QC::u64 schedulerTasksReaped = 0;
        QC::u64 workItemsCompleted = 0;
        QC::u64 workChecksum = 0;
    };

    struct SmpStatusSnapshot
    {
        QC::u64 totalCpus = 0;
        QC::u64 targetAps = 0;
        QC::u64 onlineAps = 0;
        QC::usize slotCount = 0;
        SmpCpuStatusEntry entries[kSmpStatusMaxEntries]{};
    };

    struct LimineRequests
    {
        QC::u64 *framebuffer = nullptr;
        QC::u64 *hhdm = nullptr;
        QC::u64 *kernelAddress = nullptr;
        QC::u64 *executableFile = nullptr;
        QC::u64 *memmap = nullptr;
        QC::u64 *modules = nullptr;
        QC::u64 *firmwareType = nullptr;
        QC::u64 *rsdp = nullptr;
        QC::u64 *mp = nullptr;
        QC::u64 *efiSystemTable = nullptr;
        QC::u64 *efiMemmap = nullptr;
    };

    void setLogFn(FLogFn log);
    void setLimineRequests(const LimineRequests &req);
    bool initializeSmpTimers(QC::u32 frequencyHz);
    bool getSmpStatusSnapshot(SmpStatusSnapshot &out);
    bool submitSmpWork(SmpWorkFn work, QC::u64 argument);
    bool runSmpWork(QC::u32 workItems, QC::u64 &completed, QC::u64 &checksum);

    struct SmpSelfTestStatus
    {
        bool running = false;
        QC::u32 expectedTasks = 0;
        QC::u32 finishedTasks = 0;
        QC::u32 totalAps = 0;
        QC::u32 passedAps = 0;
    };

    // Starts bounded preemption/sleep/exit test tasks on every AP without waiting for them,
    // because terminal commands run in IRQ context where the PIT clock cannot advance.
    bool startSmpSelfTest();
    bool getSmpSelfTestStatus(SmpSelfTestStatus &out);

    // --- Early Boot ---
    void initializeMemory();
    void initializeDrivers();
    void initializeBootPolicyAndGate();
    void initializeGraphics();

    // --- Input Pipeline (QER / QM / QES) ---
    void initializeInput();

    // --- Window System ---
    void initializeWindowSystem();
    [[noreturn]] void initializeDesktop();
}
