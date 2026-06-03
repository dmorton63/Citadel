#pragma once
//
// CitadelPAL.h - Platform Abstraction Layer for Citadel OS
// Provides Windows API compatibility layer for porting CQL Database Engine
//
// STATUS: STUB FILE FOR TESTING
// Phase 1: Platform Abstraction Layer
//

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace Citadel {
namespace PAL {

// ============================================================================
// Basic Types (Citadel equivalents of Windows types)
// ============================================================================

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

// ============================================================================
// File I/O Abstraction
// ============================================================================

enum class FileMode : u8 {
    Read = 0,
    Write = 1,
    ReadWrite = 2,
    Create = 3,
    Append = 4
};

enum class SeekOrigin : u8 {
    Begin = 0,
    Current = 1,
    End = 2
};

struct FileResult {
    bool success;
    const char* errorMessage;
    u64 bytesTransferred;
};

class FileHandle {
public:
    FileHandle() : handle(0), isValid(false) {}
    explicit FileHandle(u64 h) : handle(h), isValid(true) {}
    
    bool IsValid() const { return isValid; }
    u64 GetNativeHandle() const { return handle; }
    
    // STUB: Minimal implementations
    static FileHandle Open(const char* path, FileMode mode) {
        // TODO: Call Citadel VFS
        return FileHandle(0xDEADBEEF); // Stub handle
    }
    
    FileResult Read(void* buffer, usize size) {
        // TODO: Call Citadel VFS read
        return FileResult{false, "STUB: Not implemented", 0};
    }
    
    FileResult Write(const void* buffer, usize size) {
        // TODO: Call Citadel VFS write
        return FileResult{false, "STUB: Not implemented", 0};
    }
    
    FileResult Seek(i64 offset, SeekOrigin origin) {
        // TODO: Call Citadel VFS seek
        return FileResult{false, "STUB: Not implemented", 0};
    }
    
    void Close() {
        // TODO: Call Citadel VFS close
        isValid = false;
    }
    
    bool Delete(const char* path) {
        // TODO: Call Citadel VFS delete
        return false;
    }
    
    static bool Exists(const char* path) {
        // TODO: Call Citadel VFS exists check
        return false;
    }

private:
    u64 handle;
    bool isValid;
};

// ============================================================================
// String Utilities (replacing Windows string functions)
// ============================================================================

class String {
public:
    // STUB: Basic string operations
    static usize Length(const char* str) {
        return str ? strlen(str) : 0;
    }
    
	static void Copy(char* dest, const char* src, usize maxLen) {
		if (dest && src && maxLen > 0) {
			// strncpy_s automatically null-terminates
			strncpy_s(dest, maxLen, src, maxLen - 1);
		}
	}
    
    static bool Equals(const char* a, const char* b) {
        if (!a || !b) return false;
        return strcmp(a, b) == 0;
    }
    
    static int Compare(const char* a, const char* b) {
        if (!a || !b) return 0;
        return strcmp(a, b);
    }
};

// ============================================================================
// Memory Management (replacing Windows heap functions)
// ============================================================================

class Memory {
public:
    // STUB: Memory allocation wrappers
    static void* Allocate(usize size) {
        // TODO: Call Citadel memory manager
        return nullptr; // STUB
    }
    
    static void* Reallocate(void* ptr, usize newSize) {
        // TODO: Call Citadel memory manager
        return nullptr; // STUB
    }
    
    static void Free(void* ptr) {
        // TODO: Call Citadel memory manager
        (void)ptr;
    }
    
    static void Zero(void* ptr, usize size) {
        if (ptr && size > 0) {
            memset(ptr, 0, size);
        }
    }
    
    static void Copy(void* dest, const void* src, usize size) {
        if (dest && src && size > 0) {
            memcpy(dest, src, size);
        }
    }
};

// ============================================================================
// Console/Debug Output (replacing Windows console functions)
// ============================================================================

class Console {
public:
    // STUB: Console output wrappers
    static void Write(const char* message) {
        // TODO: Call Citadel console driver
        (void)message;
    }
    
    static void WriteLine(const char* message) {
        // TODO: Call Citadel console driver
        (void)message;
    }
    
    static void WriteError(const char* message) {
        // TODO: Call Citadel console driver error output
        (void)message;
    }
    
    static void DebugPrint(const char* format, ...) {
        // TODO: Call Citadel debug output
        (void)format;
    }
};

// ============================================================================
// Error Handling (replacing Windows GetLastError)
// ============================================================================

enum class ErrorCode : u32 {
    Success = 0,
    FileNotFound = 2,
    AccessDenied = 5,
    InvalidHandle = 6,
    OutOfMemory = 14,
    InvalidParameter = 87,
    InsufficientBuffer = 122,
    NotImplemented = 0xFFFF0001,
    Unknown = 0xFFFFFFFF
};

class Error {
public:
    static ErrorCode GetLast() {
        return lastError;
    }
    
    static void SetLast(ErrorCode code) {
        lastError = code;
    }
    
    static const char* GetMessage(ErrorCode code) {
        // STUB: Error message lookup
        switch (code) {
            case ErrorCode::Success: return "Success";
            case ErrorCode::FileNotFound: return "File not found";
            case ErrorCode::AccessDenied: return "Access denied";
            case ErrorCode::InvalidHandle: return "Invalid handle";
            case ErrorCode::OutOfMemory: return "Out of memory";
            case ErrorCode::InvalidParameter: return "Invalid parameter";
            case ErrorCode::InsufficientBuffer: return "Insufficient buffer";
            case ErrorCode::NotImplemented: return "Not implemented (stub)";
            default: return "Unknown error";
        }
    }

private:
    static ErrorCode lastError;
};

// Static initialization (would be in .cpp file in full implementation)
// For stub, this is inline
inline ErrorCode Error::lastError = ErrorCode::Success;

} // namespace PAL
} // namespace Citadel
