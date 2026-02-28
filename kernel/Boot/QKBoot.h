#pragma once

#include "QCTypes.h"

namespace QKBoot
{
    using FLogFn = void (*)(const char *);

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
    };

    void setLogFn(FLogFn log);
    void setLimineRequests(const LimineRequests &req);

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
