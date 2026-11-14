#pragma once

#include "QCTypes.h"

namespace QK::Boot::Acpi
{
    using FLogFn = void (*)(const char *);
    using FTpm2StartupFn = void (*)(QC::u32 StartMethod, QC::PhysAddr ControlAreaPhys, QC::u64 EventLogPhys, QC::u32 EventLogLen, FLogFn Log);

    static constexpr QC::usize kCpuTopologyMaxEntries = 256;

    struct CpuTopologyEntry
    {
        QC::u32 acpiProcessorUid = 0;
        QC::u32 apicId = 0;
        bool enabled = false;
        bool onlineCapable = false;
        bool x2Apic = false;
    };

    struct CpuTopologySnapshot
    {
        bool madtPresent = false;
        QC::u32 localApicAddress = 0;
        QC::u32 madtFlags = 0;
        QC::u32 totalEntries = 0;
        QC::u32 enabledEntries = 0;
        QC::u32 onlineCapableEntries = 0;
        QC::u32 x2ApicEntries = 0;
        QC::usize entryCount = 0;
        CpuTopologyEntry entries[kCpuTopologyMaxEntries]{};
    };

    void EnumerateTables(QC::PhysAddr RsdpPhys, FLogFn Log, FTpm2StartupFn Tpm2Startup);
    bool GetCpuTopologySnapshot(CpuTopologySnapshot &Out);
    bool InitializeBspLocalApic(FLogFn Log = nullptr);
    bool InitializeCurrentCpuLocalApic(FLogFn Log = nullptr);
    bool ConfigureCurrentCpuLocalApicTimer(QC::u8 Vector, QC::u32 InitialCount, bool Periodic);
    bool ReadCurrentCpuLocalApicTimerCount(QC::u32 &Out);
    bool ReadCurrentCpuLocalApicId(QC::u32 &Out);
    void StopCurrentCpuLocalApicTimer();
    void SendCurrentCpuLocalApicEoi();
    bool TryAcpiShutdown(FLogFn Log = nullptr);
}
