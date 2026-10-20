#include "QKBootSmp.h"

#include "QArchIDT.h"
#include "QCLogger.h"

namespace
{
    struct LimineMpInfo
    {
        QC::u32 processorId;
        QC::u32 lapicId;
        QC::u64 reserved;
        void (*gotoAddress)(LimineMpInfo *);
        QC::u64 extraArgument;
    };

    struct LimineMpResponse
    {
        QC::u64 revision;
        QC::u32 flags;
        QC::u32 bspLapicId;
        QC::u64 cpuCount;
        LimineMpInfo **cpus;
    };

    static_assert(sizeof(LimineMpInfo) == 32);
    static_assert(sizeof(LimineMpResponse) == 32);

    constexpr QC::u64 AP_STARTUP_SPIN_LIMIT = 100000000;
    QC::u64 g_startedApplicationProcessors = 0;
}

extern "C" void citadel_ap_entry(LimineMpInfo *Info);

extern "C" void citadel_ap_main(LimineMpInfo *)
{
    QArch::IDT::instance().load();
    __atomic_add_fetch(&g_startedApplicationProcessors, 1, __ATOMIC_RELEASE);

    for (;;)
    {
        asm volatile("cli; hlt" ::: "memory");
    }
}

namespace QK::Boot::Smp
{
    bool Initialize(QC::u64 MpRequest[], FLogFn Log)
    {
        if (!MpRequest || !MpRequest[5])
        {
            if (Log)
                Log("SMP: Limine did not provide a multiprocessor response\r\n");
            return false;
        }

        const auto *Response = reinterpret_cast<const LimineMpResponse *>(MpRequest[5]);
        if (Response->cpuCount == 0 || !Response->cpus)
        {
            if (Log)
                Log("SMP: Limine returned an invalid processor list\r\n");
            return false;
        }

        QC::u64 expectedApplicationProcessors = 0;
        for (QC::u64 i = 0; i < Response->cpuCount; ++i)
        {
            const LimineMpInfo *Info = Response->cpus[i];
            if (Info && Info->lapicId != Response->bspLapicId)
                ++expectedApplicationProcessors;
        }

        if (expectedApplicationProcessors == 0)
        {
            if (Log)
                Log("SMP: Limine reports one processor; no APs to start\r\n");
            return true;
        }

        QC::u64 launchedApplicationProcessors = 0;
        for (QC::u64 i = 0; i < Response->cpuCount; ++i)
        {
            LimineMpInfo *Info = Response->cpus[i];
            if (!Info || Info->lapicId == Response->bspLapicId)
                continue;

            if (__atomic_load_n(&Info->gotoAddress, __ATOMIC_ACQUIRE) != nullptr)
                continue;

            __atomic_store_n(&Info->gotoAddress, &citadel_ap_entry, __ATOMIC_SEQ_CST);
            ++launchedApplicationProcessors;

            bool started = false;
            for (QC::u64 spin = 0; spin < AP_STARTUP_SPIN_LIMIT; ++spin)
            {
                if (__atomic_load_n(&g_startedApplicationProcessors, __ATOMIC_ACQUIRE) >=
                    launchedApplicationProcessors)
                {
                    started = true;
                    break;
                }
                asm volatile("pause");
            }

            if (!started)
            {
                if (Log)
                    Log("SMP: timed out waiting for an application processor\r\n");
                break;
            }
        }

        const QC::u64 startedApplicationProcessors =
            __atomic_load_n(&g_startedApplicationProcessors, __ATOMIC_ACQUIRE);
        QC_LOG_INFO("SMP", "Started %llu of %llu application processors",
                    static_cast<unsigned long long>(startedApplicationProcessors),
                    static_cast<unsigned long long>(expectedApplicationProcessors));

        return startedApplicationProcessors == expectedApplicationProcessors;
    }
}
