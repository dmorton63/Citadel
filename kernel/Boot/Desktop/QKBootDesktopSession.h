#pragma once

#include "QCTypes.h"

namespace QK::Boot::Desktop
{
    using FLogFn = void (*)(const char *);

    struct EarlyHeap
    {
        QC::VirtAddr Buffer = 0;
        QC::usize Size = 0;
    };

    // Attempts to bring up the desktop UI and enter the main loop.
    // Returns false only when no framebuffer is available.
    bool RunFromLimineRequests(QC::u64 FramebufferRequest[], QC::u64 ModuleRequest[], const EarlyHeap &Heap, FLogFn Log);

    // Staged desktop bring-up (used by QKBoot pipeline).
    bool PrepareFromLimineRequests(QC::u64 FramebufferRequest[], QC::u64 ModuleRequest[], const EarlyHeap &Heap, FLogFn Log);

    // Lightweight state queries (useful for console-only startup flows).
    bool IsPrepared();
    bool IsInputInitialized();
    bool IsWindowSystemInitialized();
    bool IsDesktopInitialized();

    void InitializeInput();
    void InitializeWindowSystem();
    bool RequestStopDesktop();
    [[noreturn]] void InitializeDesktopAndRunLoop();
}
