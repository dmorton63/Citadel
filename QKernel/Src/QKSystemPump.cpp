// QKernel SystemPump - kernel-level poll hook abstraction
// Namespace: QK::System

#include "QKSystemPump.h"

namespace QK::System
{

    static PumpFn g_pump = nullptr;

    void setPumpFn(PumpFn fn)
    {
        g_pump = fn;
    }

    bool pumpAvailable()
    {
        return g_pump != nullptr;
    }

    void pump()
    {
        if (g_pump)
            g_pump();
    }

} // namespace QK::System
