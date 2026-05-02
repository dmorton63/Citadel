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
#include <cstdlib>

#if defined(CITADEL_PAL_USE_QFS_VFS)
#include "QFSVFS.h"
#include "QFSFile.h"
#else
#include <cstdio>
#include <sys/types.h>
#include <sys/stat.h>
#include <cerrno>
#endif

// Enable this when building inside Citadel with QFileSystem linked.
// The service can then query real mount/file existence through QFS::VFS.
#if defined(CITADEL_PAL_USE_QFS_VFS)
#define CITADEL_PAL_HAS_QFS 1
#else
#define CITADEL_PAL_HAS_QFS 0
#endif

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
    explicit FileHandle(u64 h) : handle(h), isValid(h != 0) {}
    
    bool IsValid() const { return isValid; }
    u64 GetNativeHandle() const { return handle; }
    
    // STUB: Minimal implementations
    static FileHandle Open(const char* path, FileMode mode) {
#if CITADEL_PAL_HAS_QFS
        if (!path || path[0] == '\0') {
            return FileHandle();
        }

        QFS::OpenMode qfsMode = QFS::OpenMode::Binary;
        switch (mode) {
            case FileMode::Read:
                qfsMode = qfsMode | QFS::OpenMode::Read;
                break;
            case FileMode::Write:
                qfsMode = qfsMode | QFS::OpenMode::Write | QFS::OpenMode::Create | QFS::OpenMode::Truncate;
                break;
            case FileMode::ReadWrite:
                qfsMode = qfsMode | QFS::OpenMode::Read | QFS::OpenMode::Write;
                break;
            case FileMode::Create:
                qfsMode = qfsMode | QFS::OpenMode::Read | QFS::OpenMode::Write | QFS::OpenMode::Create | QFS::OpenMode::Truncate;
                break;
            case FileMode::Append:
                qfsMode = qfsMode | QFS::OpenMode::Write | QFS::OpenMode::Append | QFS::OpenMode::Create;
                break;
        }

        QFS::File* file = QFS::VFS::instance().open(path, qfsMode);
        return FileHandle(reinterpret_cast<u64>(file));
#else
        if (!path || path[0] == '\0') {
            return FileHandle();
        }

        const char* fopenMode = "rb";
        switch (mode) {
            case FileMode::Read:
                fopenMode = "rb";
                break;
            case FileMode::Write:
                fopenMode = "wb";
                break;
            case FileMode::ReadWrite:
                fopenMode = "r+b";
                break;
            case FileMode::Create:
                fopenMode = "w+b";
                break;
            case FileMode::Append:
                fopenMode = "ab+";
                break;
        }

        FILE* fp = fopen(path, fopenMode);
        if (!fp && mode == FileMode::ReadWrite) {
            // Read/write should create on first use in host fallback mode.
            fp = fopen(path, "w+b");
        }
        return FileHandle(reinterpret_cast<u64>(fp));
#endif
    }
    
    FileResult Read(void* buffer, usize size) {
        if (!isValid || handle == 0 || !buffer) {
            return FileResult{false, "Invalid file handle or buffer", 0};
        }

#if CITADEL_PAL_HAS_QFS
        auto* file = reinterpret_cast<QFS::File*>(handle);
        const auto bytesRead = file->read(buffer, size);
        if (bytesRead < 0) {
            return FileResult{false, "VFS read failed", 0};
        }
        return FileResult{true, "", static_cast<u64>(bytesRead)};
#else
        auto* fp = reinterpret_cast<FILE*>(handle);
        const size_t bytesRead = fread(buffer, 1, size, fp);
        if (ferror(fp)) {
            clearerr(fp);
            return FileResult{false, "Host file read failed", 0};
        }
        return FileResult{true, "", static_cast<u64>(bytesRead)};
#endif
    }
    
    FileResult Write(const void* buffer, usize size) {
        if (!isValid || handle == 0 || !buffer) {
            return FileResult{false, "Invalid file handle or buffer", 0};
        }

#if CITADEL_PAL_HAS_QFS
        auto* file = reinterpret_cast<QFS::File*>(handle);
        const auto bytesWritten = file->write(buffer, size);
        if (bytesWritten < 0) {
            return FileResult{false, "VFS write failed", 0};
        }
        return FileResult{true, "", static_cast<u64>(bytesWritten)};
#else
        auto* fp = reinterpret_cast<FILE*>(handle);
        const size_t bytesWritten = fwrite(buffer, 1, size, fp);
        if (bytesWritten < size && ferror(fp)) {
            clearerr(fp);
            return FileResult{false, "Host file write failed", static_cast<u64>(bytesWritten)};
        }
        return FileResult{true, "", static_cast<u64>(bytesWritten)};
#endif
    }
    
    FileResult Seek(i64 offset, SeekOrigin origin) {
        if (!isValid || handle == 0) {
            return FileResult{false, "Invalid file handle", 0};
        }

#if CITADEL_PAL_HAS_QFS
        auto* file = reinterpret_cast<QFS::File*>(handle);
        QFS::SeekOrigin qfsOrigin = QFS::SeekOrigin::Begin;
        switch (origin) {
            case SeekOrigin::Begin:
                qfsOrigin = QFS::SeekOrigin::Begin;
                break;
            case SeekOrigin::Current:
                qfsOrigin = QFS::SeekOrigin::Current;
                break;
            case SeekOrigin::End:
                qfsOrigin = QFS::SeekOrigin::End;
                break;
        }

        const auto pos = file->seek(offset, qfsOrigin);
        if (pos < 0) {
            return FileResult{false, "VFS seek failed", 0};
        }
        return FileResult{true, "", static_cast<u64>(pos)};
#else
        auto* fp = reinterpret_cast<FILE*>(handle);
        int whence = SEEK_SET;
        switch (origin) {
            case SeekOrigin::Begin:
                whence = SEEK_SET;
                break;
            case SeekOrigin::Current:
                whence = SEEK_CUR;
                break;
            case SeekOrigin::End:
                whence = SEEK_END;
                break;
        }

        if (fseek(fp, static_cast<long>(offset), whence) != 0) {
            return FileResult{false, "Host file seek failed", 0};
        }
        const long pos = ftell(fp);
        if (pos < 0) {
            return FileResult{false, "Host file tell failed", 0};
        }
        return FileResult{true, "", static_cast<u64>(pos)};
#endif
    }
    
    void Close() {
        if (!isValid || handle == 0) {
            return;
        }

    #if CITADEL_PAL_HAS_QFS
        auto* file = reinterpret_cast<QFS::File*>(handle);
        QFS::VFS::instance().close(file);
    #else
        auto* fp = reinterpret_cast<FILE*>(handle);
        fclose(fp);
    #endif

        handle = 0;
        isValid = false;
    }
    
    bool Delete(const char* path) {
        if (!path || path[0] == '\0') {
            return false;
        }

    #if CITADEL_PAL_HAS_QFS
        return QFS::VFS::instance().remove(path) == QC::Status::Success;
    #else
        return remove(path) == 0;
    #endif
    }
    
    static bool Exists(const char* path) {
        if (!path || path[0] == '\0') {
            return false;
        }

#if defined(CITADEL_PAL_USE_QFS_VFS)
        // Citadel runtime path: check directly against mounted VFS state.
        return QFS::VFS::instance().exists(path);
#else
        // Host fallback for stub tests. These env flags let tests simulate
        // mount availability without requiring kernel/VFS linkage.
        if (strncmp(path, "/system", 7) == 0 && (path[7] == '\0' || path[7] == '/')) {
            const char* mounted = std::getenv("CITADEL_PAL_MOUNT_SYSTEM");
            return mounted && mounted[0] == '1';
        }
        if (strncmp(path, "/shared", 7) == 0 && (path[7] == '\0' || path[7] == '/')) {
            const char* mounted = std::getenv("CITADEL_PAL_MOUNT_SHARED");
            return mounted && mounted[0] == '1';
        }

        FILE* fp = fopen(path, "rb");
        if (!fp) {
            return false;
        }
        fclose(fp);
        return true;
#endif
    }

    static bool EnsureDirectory(const char* path) {
        if (!path || path[0] == '\0') {
            return false;
        }

#if defined(CITADEL_PAL_USE_QFS_VFS)
        if (QFS::VFS::instance().exists(path)) {
            return true;
        }
        if (QFS::VFS::instance().createDir(path) == QC::Status::Success) {
            return true;
        }
        return QFS::VFS::instance().exists(path);
#else
        // Host test mode: when mount simulation is enabled for virtual roots,
        // treat them as writable-ready without touching host root paths.
        if (strncmp(path, "/system", 7) == 0) {
            const char* mounted = std::getenv("CITADEL_PAL_MOUNT_SYSTEM");
            if (mounted && mounted[0] == '1') {
                return true;
            }
        }
        if (strncmp(path, "/shared", 7) == 0) {
            const char* mounted = std::getenv("CITADEL_PAL_MOUNT_SHARED");
            if (mounted && mounted[0] == '1') {
                return true;
            }
        }

        if (Exists(path)) {
            return true;
        }

        char scratch[512];
        const size_t len = strlen(path);
        if (len == 0 || len >= sizeof(scratch)) {
            return false;
        }
        strncpy(scratch, path, sizeof(scratch) - 1);
        scratch[sizeof(scratch) - 1] = '\0';

        // Create each component progressively for nested paths.
        for (char* p = scratch + 1; *p; ++p) {
            if (*p != '/') {
                continue;
            }
            *p = '\0';
            if (mkdir(scratch, 0775) != 0 && errno != EEXIST) {
                *p = '/';
                return false;
            }
            *p = '/';
        }

        if (mkdir(scratch, 0775) != 0 && errno != EEXIST) {
            return false;
        }
        return true;
#endif
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
            strncpy(dest, src, maxLen - 1);
            dest[maxLen - 1] = '\0';
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
