#pragma once

// QKernel Time - kernel-level time abstraction
// Namespace: QK::Time

#include "QCTypes.h"

namespace QK::Time
{

    using NowMsFn = QC::u64 (*)();
    using SleepMsFn = void (*)(QC::u64);

    // Install a provider (typically from a driver during boot).
    void setProvider(NowMsFn nowMsFn, SleepMsFn sleepMsFn);

    bool available();

    // Monotonic milliseconds since boot (best-effort: returns 0 if unavailable).
    QC::u64 milliseconds();

    // Sleep for N ms (no-op if unavailable).
    void sleep(QC::u64 ms);

} // namespace QK::Time
