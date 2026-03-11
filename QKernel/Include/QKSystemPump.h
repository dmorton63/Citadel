#pragma once

// QKernel SystemPump - kernel-level poll hook abstraction
// Namespace: QK::System

namespace QK::System
{

    using PumpFn = void (*)();

    // Install a best-effort pump function (e.g., poll drivers / process IO).
    void setPumpFn(PumpFn fn);

    bool pumpAvailable();

    // Best-effort: if no provider installed, this is a no-op.
    void pump();

} // namespace QK::System
