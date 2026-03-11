#pragma once

#include "QCTypes.h"

namespace QK
{

    namespace Entropy
    {

        // Mixes external entropy into the kernel entropy pool.
        // Safe to call early during boot.
        void addEntropy(const void *data, QC::usize size);

        // Fills `out` with best-effort cryptographic random bytes.
        // Returns Success once the pool has been seeded at least once.
        // May still return Success even if seeded state is weak; callers should
        // treat early-boot randomness as lower trust.
        QC::Status fillRandom(void *out, QC::usize size);

        // Returns whether the pool has been seeded via addEntropy().
        bool isSeeded();

    } // namespace Entropy

} // namespace QK
