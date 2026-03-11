// QKernel Time - kernel-level time abstraction
// Namespace: QK::Time

#include "QKTime.h"

namespace QK::Time
{

    static NowMsFn g_nowMs = nullptr;
    static SleepMsFn g_sleepMs = nullptr;

    void setProvider(NowMsFn nowMsFn, SleepMsFn sleepMsFn)
    {
        g_nowMs = nowMsFn;
        g_sleepMs = sleepMsFn;
    }

    bool available()
    {
        return g_nowMs != nullptr;
    }

    QC::u64 milliseconds()
    {
        if (!g_nowMs)
            return 0;
        return g_nowMs();
    }

    void sleep(QC::u64 ms)
    {
        if (!g_sleepMs)
            return;
        g_sleepMs(ms);
    }

} // namespace QK::Time
