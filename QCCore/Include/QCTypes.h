#pragma once

// QCommon Types - Core type definitions for QAIOS
// Namespace: QC

#include <cstdint>
#include <cstddef>

#if defined(__has_include)
#if __has_include(<new>)
#include <new>
#define QC_COMMON_HAS_STDLIB_NEW 1
#endif
#endif

#ifndef QC_COMMON_HAS_STDLIB_NEW
#define QC_COMMON_HAS_STDLIB_NEW 0
#endif

namespace QC
{

    // Fixed-width integer types
    using u8 = uint8_t;
    using u16 = uint16_t;
    using u32 = uint32_t;
    using u64 = uint64_t;

    using i8 = int8_t;
    using i16 = int16_t;
    using i32 = int32_t;
    using i64 = int64_t;

    using usize = size_t;
    using isize = ptrdiff_t;

    // Pointer-sized integer types
    using uptr = uintptr_t;
    using iptr = intptr_t;

    // Floating-point types
    using f32 = float;
    using f64 = double;

    // Physical and virtual address types
    using PhysAddr = u64;
    using VirtAddr = u64;

    // Status codes
    enum class Status : i32
    {
        Success = 0,
        Error = -1,
        InvalidParam = -2,
        OutOfMemory = -3,
        NotFound = -4,
        Timeout = -5,
        Busy = -6,
        NotSupported = -7
    };

    // Result type for error handling
    template <typename T>
    struct Result
    {
        T value;
        Status status;

        bool ok() const { return status == Status::Success; }
        operator bool() const { return ok(); }
    };

} // namespace QC

// Placement new operators for freestanding environment (only if std::new not available)
#if !QC_COMMON_HAS_STDLIB_NEW
inline void *operator new(QC::usize, void *ptr) noexcept { return ptr; }
inline void *operator new[](QC::usize, void *ptr) noexcept { return ptr; }
inline void operator delete(void *, void *) noexcept {}
inline void operator delete[](void *, void *) noexcept {}
#endif
