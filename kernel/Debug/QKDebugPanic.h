#pragma once

// Kernel-wide panic entry point.
// Implemented in Debug/Panic.cpp so QKMain.cpp stays orchestration-only.

#ifdef __cplusplus
extern "C"
{
#endif

    [[noreturn]] void kernel_panic(const char *message);

#ifdef __cplusplus
}
#endif
