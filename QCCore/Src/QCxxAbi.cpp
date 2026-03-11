/**
 * @file QCxxAbi.cpp
 * @brief C++ ABI runtime stubs for freestanding environment
 *
 * These functions are required by the C++ compiler for:
 * - Thread-safe static initialization (guard functions)
 * - Static object destruction (atexit)
 * - Dynamic shared object handle
 */

extern "C"
{

    // DSO handle for this executable
    void *__dso_handle = nullptr;

    // Guard variable for thread-safe static initialization
    // In a single-threaded kernel, we can use simple implementation
    int __cxa_guard_acquire(long long *guard)
    {
        // Check if already initialized
        if (*reinterpret_cast<char *>(guard))
        {
            return 0; // Already initialized
        }
        return 1; // Need to initialize
    }

    void __cxa_guard_release(long long *guard)
    {
        // Mark as initialized
        *reinterpret_cast<char *>(guard) = 1;
    }

    void __cxa_guard_abort(long long *guard)
    {
        // Initialization aborted - mark as not initialized
        *reinterpret_cast<char *>(guard) = 0;
    }

    // Static destructor registration
    // In kernel we typically don't call static destructors, but we need the symbol
    int __cxa_atexit(void (*destructor)(void *), void *arg, void *dso)
    {
        (void)destructor;
        (void)arg;
        (void)dso;
        // Not implemented - kernel doesn't exit normally
        return 0;
    }

    // Pure virtual function call handler
    void __cxa_pure_virtual()
    {
        // Kernel panic or halt
        while (true)
        {
            asm volatile("hlt");
        }
    }

    // Stack smashing detected handler
    void __stack_chk_fail()
    {
        // Kernel panic or halt
        while (true)
        {
            asm volatile("hlt");
        }
    }

} // extern "C"
