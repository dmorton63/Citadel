#include "QKBootTPMLock.h"
#include "QKBootTPMProbePolicy.h"

#include "QCBuiltins.h"

namespace QK::Boot::Tpm
{
    namespace
    {
        // Covers several back-to-back slow commands (e.g. seal = CreatePrimary + Create).
        constexpr QC::usize kLockSpinIterations = kTpmCommandCompleteSpinIterations * 4u;

        static QC::u32 g_OwnerTag = 0;
        static QC::u64 g_Acquisitions = 0;
        static QC::u64 g_Contended = 0;
        static QC::u64 g_SameCpuRejections = 0;
        static QC::u64 g_TimeoutRejections = 0;

        static QC::u32 CurrentCpuTag()
        {
            QC::u32 Eax = 1, Ebx = 0, Ecx = 0, Edx = 0;
            asm volatile("cpuid" : "+a"(Eax), "=b"(Ebx), "+c"(Ecx), "=d"(Edx));
            return (Ebx >> 24) + 1u;
        }
    }

    FTpmOpGuard::FTpmOpGuard()
        : FTpmOpGuard(kLockSpinIterations)
    {
    }

    FTpmOpGuard::FTpmOpGuard(QC::usize SpinIterations)
    {
        const QC::u32 Tag = CurrentCpuTag();
        bool bContended = false;
        for (QC::usize i = 0; i < SpinIterations; ++i)
        {
            QC::u32 Expected = 0;
            if (__atomic_compare_exchange_n(&g_OwnerTag, &Expected, Tag, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            {
                m_bAcquired = true;
                __atomic_fetch_add(&g_Acquisitions, 1ull, __ATOMIC_RELAXED);
                if (bContended)
                    __atomic_fetch_add(&g_Contended, 1ull, __ATOMIC_RELAXED);
                return;
            }

            if (Expected == Tag)
            {
                __atomic_fetch_add(&g_SameCpuRejections, 1ull, __ATOMIC_RELAXED);
                return;
            }

            bContended = true;
            QC::pause();
        }

        __atomic_fetch_add(&g_TimeoutRejections, 1ull, __ATOMIC_RELAXED);
    }

    FTpmOpGuard::~FTpmOpGuard()
    {
        if (m_bAcquired)
            __atomic_store_n(&g_OwnerTag, 0u, __ATOMIC_RELEASE);
    }

    void GetLockStats(FLockStats &Out)
    {
        Out.Acquisitions = __atomic_load_n(&g_Acquisitions, __ATOMIC_RELAXED);
        Out.ContendedAcquisitions = __atomic_load_n(&g_Contended, __ATOMIC_RELAXED);
        Out.SameCpuRejections = __atomic_load_n(&g_SameCpuRejections, __ATOMIC_RELAXED);
        Out.TimeoutRejections = __atomic_load_n(&g_TimeoutRejections, __ATOMIC_RELAXED);
        Out.OwnerTag = __atomic_load_n(&g_OwnerTag, __ATOMIC_RELAXED);
    }
}
