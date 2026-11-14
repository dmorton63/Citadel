#pragma once

#include "QCTypes.h"

namespace QK::Boot::Tpm
{
    struct FLockStats
    {
        QC::u64 Acquisitions = 0;
        QC::u64 ContendedAcquisitions = 0;
        QC::u64 SameCpuRejections = 0;
        QC::u64 TimeoutRejections = 0;
        QC::u32 OwnerTag = 0;
    };

    // Operation-level TPM lock. A whole TPM operation (including multi-command
    // sessions) runs under one guard so commands from different CPUs can never
    // interleave. Re-entry from the owning CPU (IRQ or another task) fails
    // immediately instead of deadlocking; other CPUs spin with a bound.
    class FTpmOpGuard
    {
    public:
        FTpmOpGuard();
        explicit FTpmOpGuard(QC::usize SpinIterations);
        ~FTpmOpGuard();

        FTpmOpGuard(const FTpmOpGuard &) = delete;
        FTpmOpGuard &operator=(const FTpmOpGuard &) = delete;

        bool Acquired() const { return m_bAcquired; }

    private:
        bool m_bAcquired = false;
    };

    void GetLockStats(FLockStats &Out);
}
