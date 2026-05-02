# CQL Database Engine - Citadel OS Porting Guide

> Note: This guide is kept for historical/contextual reference.
> For active implementation in the current repo layout, use `CITADEL_PORTING_GUIDE_V2_2026-04-15.md` as the source of truth.

## Overview

This guide provides step-by-step instructions for porting the CQL Database Engine from Windows to **Citadel OS** (a bare-metal C++ operating system using the Limine bootloader and tested with QEMU).

**Source**: Windows-based CQL implementation (~2000 lines of C++ code)  
**Target**: Citadel OS (bare-metal C++, QEMU/WSL testing environment)  
**Architecture**: Database as a Service (loads after file system, before user shell)

---

## Prerequisites

### What Citadel Must Have Before Porting:
1. ✅ **C++ Runtime**: Basic C++ support (constructors, destructors, new/delete)
2. ✅ **Memory Manager**: Heap allocation (malloc/free or custom allocator)
3. ✅ **File System**: VFS or direct disk I/O capability
4. ✅ **Console/Terminal**: Text output for debug messages
5. ⚠️ **Service Manager** (optional but recommended): Service lifecycle management

### What You'll Need to Implement:
- **Platform Abstraction Layer**: Wrappers for file I/O, strings, console
- **Service Registration**: Integrate CSQL with Citadel's service architecture
- **Memory Allocation**: Bridge between CQL and Citadel's heap

---

## Architecture Decision: Service vs Kernel Module

### ✅ **Recommended: Database as a Service**

**Boot Sequence**:
```
Limine Bootloader
    ↓
Citadel Kernel Init
    ↓
Hardware Drivers (Keyboard, Mouse, Video, Disk)
    ↓
File System
    ↓
Service Manager (if available)
    ↓
CSQL Service ← Load here!
    ↓
User Shell / Applications
```

**Why Service Architecture?**
1. **Fault Isolation**: Database crash doesn't crash OS
2. **Hot Reload**: Can restart service without rebooting
3. **Optional**: System boots even if database fails
4. **Debugging**: Easier to debug service than kernel code
5. **Security**: Can sandbox database operations

**Alternative: Kernel Module** (not recommended)
- Pros: Faster (no IPC overhead), direct memory access
- Cons: Crashes kill OS, hard to debug, bloats kernel, security risk

---

## Porting Strategy: Three Phases

### Phase 1: Platform Abstraction Layer (PAL)
Create wrappers to replace standard library dependencies.

### Phase 2: Core Engine Port
Copy CQL code and adapt to use PAL.

### Phase 3: Service Integration
Register CSQL with Citadel's service infrastructure.

---

## Phase 1: Platform Abstraction Layer

### File: `CitadelPAL.h` (Platform Abstraction Layer)

```cpp
// CitadelPAL.h - Platform Abstraction Layer for CQL on Citadel
#ifndef CITADEL_PAL_H
#define CITADEL_PAL_H

#include <stdint.h>
#include <stddef.h>

namespace Citadel::PAL {

    // ========================================
    // FILE I/O ABSTRACTION
    // ========================================
    
    enum class FileMode {
        Read,           // Open for reading (must exist)
        Write,          // Create/truncate for writing
        ReadWrite,      // Open for read/write (must exist)
        Create          // Create new file (truncate if exists)
    };
    
    class File {
    private:
        void* handle;     // Citadel file handle (opaque)
        bool isOpen;
        
    public:
        File();
        ~File();
        
        // Open file using Citadel's VFS
        bool Open(const char* path, FileMode mode);
        
        // Read bytes into buffer
        // Returns: Number of bytes read, 0 on EOF, -1 on error
        int64_t Read(void* buffer, size_t size);
        
        // Write bytes from buffer
        // Returns: Number of bytes written, -1 on error
        int64_t Write(const void* buffer, size_t size);
        
        // Seek to absolute position
        bool Seek(uint64_t offset);
        
        // Get current position
        uint64_t Tell();
        
        // Close file
        void Close();
        
        // Check if file is open
        bool IsOpen() const { return isOpen; }
        
        // Flush write buffer
        bool Flush();
        
        // Check for errors
        bool Good() const;
        void Clear();
    };
    
    // ========================================
    // MEMORY MANAGEMENT
    // ========================================
    
    // Allocate memory from Citadel's heap
    void* Allocate(size_t size);
    
    // Free memory to Citadel's heap
    void Free(void* ptr);
    
    // Reallocate memory
    void* Reallocate(void* ptr, size_t newSize);
    
    // ========================================
    // CONSOLE OUTPUT
    // ========================================
    
    // Print to Citadel's console/terminal
    void Print(const char* message);
    void PrintLine(const char* message);
    
    // Print error message
    void PrintError(const char* error);
    void PrintErrorLine(const char* error);
    
    // Formatted print (like printf)
    void PrintFormat(const char* format, ...);
    
    // ========================================
    // STRING UTILITIES
    // ========================================
    
    // Simple string class (if Citadel doesn't have std::string)
    class String {
    private:
        char* data;
        size_t length;
        size_t capacity;
        
        void Resize(size_t newCapacity);
        
    public:
        String();
        String(const char* str);
        String(const String& other);
        ~String();
        
        String& operator=(const String& other);
        String& operator=(const char* str);
        
        const char* CStr() const { return data ? data : ""; }
        size_t Length() const { return length; }
        bool IsEmpty() const { return length == 0; }
        
        void Append(const char* str);
        void Append(const String& str);
        void Clear();
        
        bool operator==(const String& other) const;
        bool operator==(const char* str) const;
        bool operator<(const String& other) const;
        
        char& operator[](size_t index) { return data[index]; }
        const char& operator[](size_t index) const { return data[index]; }
    };
    
    // ========================================
    // DYNAMIC ARRAY (if no std::vector)
    // ========================================
    
    template<typename T>
    class Vector {
    private:
        T* data;
        size_t size;
        size_t capacity;
        
        void Resize(size_t newCapacity) {
            T* newData = (T*)Allocate(newCapacity * sizeof(T));
            for (size_t i = 0; i < size; i++) {
                new (&newData[i]) T(data[i]);  // Placement new
                data[i].~T();  // Destroy old
            }
            Free(data);
            data = newData;
            capacity = newCapacity;
        }
        
    public:
        Vector() : data(nullptr), size(0), capacity(0) {}
        
        ~Vector() {
            for (size_t i = 0; i < size; i++) {
                data[i].~T();
            }
            Free(data);
        }
        
        void push_back(const T& value) {
            if (size >= capacity) {
                Resize(capacity == 0 ? 8 : capacity * 2);
            }
            new (&data[size]) T(value);
            size++;
        }
        
        T& operator[](size_t index) { return data[index]; }
        const T& operator[](size_t index) const { return data[index]; }
        
        size_t size() const { return size; }
        bool empty() const { return size == 0; }
        void clear() {
            for (size_t i = 0; i < size; i++) {
                data[i].~T();
            }
            size = 0;
        }
        
        T* begin() { return data; }
        T* end() { return data + size; }
        const T* begin() const { return data; }
        const T* end() const { return data + size; }
    };
    
    // ========================================
    // STRING CONVERSION
    // ========================================
    
    // String to integer
    int64_t StringToInt(const char* str);
    
    // String to double
    double StringToDouble(const char* str);
    
    // Integer to string
    void IntToString(int64_t value, char* buffer, size_t bufferSize);
    
    // Double to string
    void DoubleToString(double value, char* buffer, size_t bufferSize, int precision);
    
} // namespace Citadel::PAL

#endif // CITADEL_PAL_H
```

---

### Implementation Example: `CitadelPAL.cpp`

```cpp
// CitadelPAL.cpp - Implementation of Platform Abstraction Layer
#include "CitadelPAL.h"
#include <CitadelVFS.h>     // Your file system header
#include <CitadelHeap.h>    // Your memory manager header
#include <CitadelConsole.h> // Your console/terminal header

namespace Citadel::PAL {

    // ========================================
    // FILE I/O IMPLEMENTATION
    // ========================================
    
    File::File() : handle(nullptr), isOpen(false) {}
    
    File::~File() {
        Close();
    }
    
    bool File::Open(const char* path, FileMode mode) {
        if (isOpen) {
            Close();
        }
        
        // Map FileMode to Citadel's VFS flags
        uint32_t vfsMode = 0;
        switch (mode) {
            case FileMode::Read:
                vfsMode = VFS_READ;
                break;
            case FileMode::Write:
                vfsMode = VFS_WRITE | VFS_CREATE | VFS_TRUNCATE;
                break;
            case FileMode::ReadWrite:
                vfsMode = VFS_READ | VFS_WRITE;
                break;
            case FileMode::Create:
                vfsMode = VFS_WRITE | VFS_CREATE | VFS_TRUNCATE;
                break;
        }
        
        // Call Citadel's VFS open function
        handle = CitadelVFS::Open(path, vfsMode);
        isOpen = (handle != nullptr);
        return isOpen;
    }
    
    int64_t File::Read(void* buffer, size_t size) {
        if (!isOpen || !handle) return -1;
        return CitadelVFS::Read(handle, buffer, size);
    }
    
    int64_t File::Write(const void* buffer, size_t size) {
        if (!isOpen || !handle) return -1;
        return CitadelVFS::Write(handle, buffer, size);
    }
    
    bool File::Seek(uint64_t offset) {
        if (!isOpen || !handle) return false;
        return CitadelVFS::Seek(handle, offset, SEEK_SET);
    }
    
    uint64_t File::Tell() {
        if (!isOpen || !handle) return 0;
        return CitadelVFS::Tell(handle);
    }
    
    void File::Close() {
        if (isOpen && handle) {
            CitadelVFS::Close(handle);
            handle = nullptr;
            isOpen = false;
        }
    }
    
    bool File::Flush() {
        if (!isOpen || !handle) return false;
        return CitadelVFS::Flush(handle);
    }
    
    bool File::Good() const {
        return isOpen && handle != nullptr;
    }
    
    void File::Clear() {
        // Clear error state if Citadel VFS supports it
    }
    
    // ========================================
    // MEMORY MANAGEMENT IMPLEMENTATION
    // ========================================
    
    void* Allocate(size_t size) {
        return CitadelHeap::Allocate(size);
    }
    
    void Free(void* ptr) {
        CitadelHeap::Free(ptr);
    }
    
    void* Reallocate(void* ptr, size_t newSize) {
        return CitadelHeap::Reallocate(ptr, newSize);
    }
    
    // ========================================
    // CONSOLE OUTPUT IMPLEMENTATION
    // ========================================
    
    void Print(const char* message) {
        CitadelConsole::Write(message);
    }
    
    void PrintLine(const char* message) {
        CitadelConsole::WriteLine(message);
    }
    
    void PrintError(const char* error) {
        CitadelConsole::WriteError(error);
    }
    
    void PrintErrorLine(const char* error) {
        CitadelConsole::WriteErrorLine(error);
    }
    
    void PrintFormat(const char* format, ...) {
        char buffer[1024];
        va_list args;
        va_start(args, format);
        // Use your vsprintf implementation
        CitadelString::FormatV(buffer, sizeof(buffer), format, args);
        va_end(args);
        CitadelConsole::Write(buffer);
    }
    
    // ========================================
    // STRING CLASS IMPLEMENTATION
    // ========================================
    
    String::String() : data(nullptr), length(0), capacity(0) {}
    
    String::String(const char* str) : data(nullptr), length(0), capacity(0) {
        if (str) {
            length = CitadelString::Length(str);
            capacity = length + 1;
            data = (char*)Allocate(capacity);
            CitadelString::Copy(data, str, capacity);
        }
    }
    
    String::String(const String& other) : data(nullptr), length(other.length), capacity(other.capacity) {
        if (other.data) {
            data = (char*)Allocate(capacity);
            CitadelString::Copy(data, other.data, capacity);
        }
    }
    
    String::~String() {
        if (data) {
            Free(data);
        }
    }
    
    String& String::operator=(const String& other) {
        if (this != &other) {
            if (data) Free(data);
            length = other.length;
            capacity = other.capacity;
            if (other.data) {
                data = (char*)Allocate(capacity);
                CitadelString::Copy(data, other.data, capacity);
            } else {
                data = nullptr;
            }
        }
        return *this;
    }
    
    String& String::operator=(const char* str) {
        if (data) Free(data);
        if (str) {
            length = CitadelString::Length(str);
            capacity = length + 1;
            data = (char*)Allocate(capacity);
            CitadelString::Copy(data, str, capacity);
        } else {
            data = nullptr;
            length = 0;
            capacity = 0;
        }
        return *this;
    }
    
    void String::Append(const char* str) {
        if (!str) return;
        size_t addLen = CitadelString::Length(str);
        if (length + addLen + 1 > capacity) {
            Resize(length + addLen + 1);
        }
        CitadelString::Copy(data + length, str, capacity - length);
        length += addLen;
    }
    
    void String::Append(const String& str) {
        Append(str.CStr());
    }
    
    void String::Clear() {
        if (data) {
            Free(data);
            data = nullptr;
        }
        length = 0;
        capacity = 0;
    }
    
    void String::Resize(size_t newCapacity) {
        char* newData = (char*)Allocate(newCapacity);
        if (data) {
            CitadelString::Copy(newData, data, length + 1);
            Free(data);
        }
        data = newData;
        capacity = newCapacity;
    }
    
    bool String::operator==(const String& other) const {
        if (length != other.length) return false;
        if (!data && !other.data) return true;
        if (!data || !other.data) return false;
        return CitadelString::Compare(data, other.data) == 0;
    }
    
    bool String::operator==(const char* str) const {
        if (!data && !str) return true;
        if (!data || !str) return false;
        return CitadelString::Compare(data, str) == 0;
    }
    
    bool String::operator<(const String& other) const {
        if (!data && !other.data) return false;
        if (!data) return true;
        if (!other.data) return false;
        return CitadelString::Compare(data, other.data) < 0;
    }
    
    // ========================================
    // STRING CONVERSION IMPLEMENTATION
    // ========================================
    
    int64_t StringToInt(const char* str) {
        return CitadelString::ParseInt(str);
    }
    
    double StringToDouble(const char* str) {
        return CitadelString::ParseDouble(str);
    }
    
    void IntToString(int64_t value, char* buffer, size_t bufferSize) {
        CitadelString::FormatInt(buffer, bufferSize, value);
    }
    
    void DoubleToString(double value, char* buffer, size_t bufferSize, int precision) {
        CitadelString::FormatDouble(buffer, bufferSize, value, precision);
    }
    
} // namespace Citadel::PAL
```

---

## Phase 2: Core Engine Port

### File-by-File Porting Checklist

#### ✅ **Minimal Changes** (Copy as-is):
1. **FileHeader.h** - Pure data structures, no dependencies
2. **WhereClauseEvaluator.h/cpp** - Logic only, minimal string usage

#### ⚠️ **Minor Changes** (Replace I/O):
3. **Table.h/cpp** - Replace `std::string` with `PAL::String`
4. **SQLParser.h/cpp** - Replace `std::string` with `PAL::String`, `std::vector` with `PAL::Vector`
5. **QueryExecutor.h/cpp** - Replace string operations

#### 🔧 **Major Changes** (Replace file I/O and strings):
6. **Database.h/cpp** - Replace `std::fstream` with `PAL::File`, `std::string` with `PAL::String`
7. **FileManager.h/cpp** - Replace `std::fstream` with `PAL::File`
8. **PageManager.h/cpp** - Replace `std::fstream` with `PAL::File`
9. **RowSerializer.h/cpp** - Replace `std::vector<uint8_t>` with `PAL::Vector<uint8_t>`

---

### Example: Porting `Database.h`

**Original (Windows)**:
```cpp
#include <fstream>
#include <string>
#include <vector>
#include <memory>

class Database {
private:
    std::fstream file;
    std::string filePath;
    std::vector<std::shared_ptr<Table>> tables;
    
public:
    bool Create(const std::string& filepath);
    bool Open(const std::string& filepath);
    std::string ExecuteQuery(const std::string& query);
};
```

**Ported (Citadel)**:
```cpp
#include "CitadelPAL.h"
// #include <fstream>  ← Remove
// #include <string>   ← Remove
// #include <vector>   ← Remove
// #include <memory>   ← Remove (or keep if Citadel supports smart pointers)

using namespace Citadel::PAL;

class Database {
private:
    File file;              // ← Changed from std::fstream
    String filePath;        // ← Changed from std::string
    Vector<Table*> tables;  // ← Changed from std::vector<std::shared_ptr<Table>>
    
public:
    bool Create(const char* filepath);           // ← Changed parameter type
    bool Open(const char* filepath);             // ← Changed parameter type
    const char* ExecuteQuery(const char* query); // ← Changed return/parameter types
};
```

---

### Example: Porting File I/O Operations

**Original (Windows)**:
```cpp
// Database.cpp
bool Database::Open(const std::string& filepath) {
    file.open(filepath, std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open database file" << std::endl;
        return false;
    }
    // ...
}
```

**Ported (Citadel)**:
```cpp
// Database.cpp
bool Database::Open(const char* filepath) {
    if (!file.Open(filepath, FileMode::ReadWrite)) {
        PrintErrorLine("Failed to open database file");
        return false;
    }
    // ...
}
```

---

### Example: Porting String Operations

**Original (Windows)**:
```cpp
std::string Database::ExecuteQuery(const std::string& query) {
    if (!isOpen) {
        return "Error: Database not open";
    }
    
    std::vector<std::string> statements = SplitStatements(query);
    std::ostringstream result;
    
    for (const auto& stmt : statements) {
        result << ProcessStatement(stmt);
    }
    
    return result.str();
}
```

**Ported (Citadel)**:
```cpp
const char* Database::ExecuteQuery(const char* query) {
    if (!isOpen) {
        return "Error: Database not open";
    }
    
    Vector<String> statements = SplitStatements(query);
    static String result;  // Static buffer for return value
    result.Clear();
    
    for (size_t i = 0; i < statements.size(); i++) {
        result.Append(ProcessStatement(statements[i].CStr()));
    }
    
    return result.CStr();
}
```

---

## Phase 3: Service Integration

### Service Interface for Citadel

Create a clean API for applications to interact with CSQL:

```cpp
// CSQLService.h - Database Service for Citadel
#ifndef CSQL_SERVICE_H
#define CSQL_SERVICE_H

#include <stdint.h>

namespace Citadel::Services::CSQL {

    // ========================================
    // SERVICE LIFECYCLE
    // ========================================
    
    // Initialize CSQL service (called by ServiceManager at boot)
    // Returns: true on success, false on failure
    bool Initialize();
    
    // Shutdown CSQL service (called by ServiceManager at shutdown)
    void Shutdown();
    
    // Get service status
    enum class ServiceStatus {
        Uninitialized,
        Running,
        Error
    };
    ServiceStatus GetStatus();
    
    // ========================================
    // DATABASE OPERATIONS
    // ========================================
    
    // Database handle (opaque to caller)
    typedef int32_t DatabaseHandle;
    constexpr DatabaseHandle INVALID_HANDLE = -1;
    
    // Create new database file
    // Returns: Database handle on success, INVALID_HANDLE on failure
    DatabaseHandle CreateDatabase(const char* path);
    
    // Open existing database file
    // Returns: Database handle on success, INVALID_HANDLE on failure
    DatabaseHandle OpenDatabase(const char* path);
    
    // Execute SQL query on database
    // Returns: Result string (pointer valid until next query on this handle)
    const char* ExecuteQuery(DatabaseHandle db, const char* query);
    
    // Close database
    // Returns: true on success, false on failure
    bool CloseDatabase(DatabaseHandle db);
    
    // ========================================
    // SERVICE INFO
    // ========================================
    
    // Get CSQL version string
    const char* GetVersion();
    
    // Get memory usage in bytes
    size_t GetMemoryUsage();
    
    // Get number of active database connections
    int GetActiveConnections();
    
    // Get last error message
    const char* GetLastError();
    
} // namespace Citadel::Services::CSQL

#endif // CSQL_SERVICE_H
```

---

### Service Implementation Example

```cpp
// CSQLService.cpp - Implementation
#include "CSQLService.h"
#include "Database.h"
#include "CitadelPAL.h"

namespace Citadel::Services::CSQL {

    // Internal state
    static ServiceStatus currentStatus = ServiceStatus::Uninitialized;
    static Database* databases[16];  // Support up to 16 open databases
    static int nextHandle = 0;
    static char lastError[256] = {0};
    
    // ========================================
    // SERVICE LIFECYCLE
    // ========================================
    
    bool Initialize() {
        PrintLine("[CSQL] Initializing database service...");
        
        // Initialize all database slots
        for (int i = 0; i < 16; i++) {
            databases[i] = nullptr;
        }
        nextHandle = 0;
        
        currentStatus = ServiceStatus::Running;
        PrintLine("[CSQL] Service initialized successfully");
        return true;
    }
    
    void Shutdown() {
        PrintLine("[CSQL] Shutting down database service...");
        
        // Close all open databases
        for (int i = 0; i < 16; i++) {
            if (databases[i]) {
                databases[i]->Close();
                delete databases[i];
                databases[i] = nullptr;
            }
        }
        
        currentStatus = ServiceStatus::Uninitialized;
        PrintLine("[CSQL] Service shut down successfully");
    }
    
    ServiceStatus GetStatus() {
        return currentStatus;
    }
    
    // ========================================
    // DATABASE OPERATIONS
    // ========================================
    
    DatabaseHandle CreateDatabase(const char* path) {
        if (currentStatus != ServiceStatus::Running) {
            CitadelString::Copy(lastError, "Service not running", sizeof(lastError));
            return INVALID_HANDLE;
        }
        
        // Find free slot
        int handle = -1;
        for (int i = 0; i < 16; i++) {
            if (databases[i] == nullptr) {
                handle = i;
                break;
            }
        }
        
        if (handle == -1) {
            CitadelString::Copy(lastError, "Too many open databases", sizeof(lastError));
            return INVALID_HANDLE;
        }
        
        // Create database
        databases[handle] = new Database();
        if (!databases[handle]->Create(path)) {
            delete databases[handle];
            databases[handle] = nullptr;
            CitadelString::Copy(lastError, "Failed to create database", sizeof(lastError));
            return INVALID_HANDLE;
        }
        
        return handle;
    }
    
    DatabaseHandle OpenDatabase(const char* path) {
        if (currentStatus != ServiceStatus::Running) {
            CitadelString::Copy(lastError, "Service not running", sizeof(lastError));
            return INVALID_HANDLE;
        }
        
        // Find free slot
        int handle = -1;
        for (int i = 0; i < 16; i++) {
            if (databases[i] == nullptr) {
                handle = i;
                break;
            }
        }
        
        if (handle == -1) {
            CitadelString::Copy(lastError, "Too many open databases", sizeof(lastError));
            return INVALID_HANDLE;
        }
        
        // Open database
        databases[handle] = new Database();
        if (!databases[handle]->Open(path)) {
            delete databases[handle];
            databases[handle] = nullptr;
            CitadelString::Copy(lastError, "Failed to open database", sizeof(lastError));
            return INVALID_HANDLE;
        }
        
        return handle;
    }
    
    const char* ExecuteQuery(DatabaseHandle db, const char* query) {
        if (currentStatus != ServiceStatus::Running) {
            return "Error: Service not running";
        }
        
        if (db < 0 || db >= 16 || databases[db] == nullptr) {
            return "Error: Invalid database handle";
        }
        
        return databases[db]->ExecuteQuery(query);
    }
    
    bool CloseDatabase(DatabaseHandle db) {
        if (db < 0 || db >= 16 || databases[db] == nullptr) {
            return false;
        }
        
        databases[db]->Close();
        delete databases[db];
        databases[db] = nullptr;
        return true;
    }
    
    // ========================================
    // SERVICE INFO
    // ========================================
    
    const char* GetVersion() {
        return "CSQL 1.0 for Citadel OS";
    }
    
    size_t GetMemoryUsage() {
        // TODO: Track memory allocations
        return 0;
    }
    
    int GetActiveConnections() {
        int count = 0;
        for (int i = 0; i < 16; i++) {
            if (databases[i] != nullptr) {
                count++;
            }
        }
        return count;
    }
    
    const char* GetLastError() {
        return lastError;
    }
    
} // namespace Citadel::Services::CSQL
```

---

### Registering Service with Citadel

If Citadel has a Service Manager, register CSQL during boot:

```cpp
// In Citadel's boot sequence (after file system loads)
#include <CSQLService.h>

void InitializeServices() {
    // Initialize CSQL service
    if (Citadel::Services::CSQL::Initialize()) {
        PrintLine("[BOOT] CSQL service started successfully");
    } else {
        PrintErrorLine("[BOOT] Failed to start CSQL service");
    }
    
    // Initialize other services...
}

void ShutdownServices() {
    Citadel::Services::CSQL::Shutdown();
    // Shutdown other services...
}
```

---

## Testing Strategy

### Phase 1: Compile Test
1. Port PAL implementation
2. Port FileHeader.h and WhereClauseEvaluator (minimal deps)
3. Compile to verify headers work
4. Fix any compiler errors

### Phase 2: Basic Functionality
1. Port Database.cpp and dependencies
2. Test CREATE DATABASE
3. Test CREATE TABLE
4. Test INSERT
5. Test SELECT (single table)

### Phase 3: Advanced Features
1. Test JOINs
2. Test WHERE clauses
3. Test ORDER BY, LIMIT, OFFSET
4. Run full test suite from JOINS_TEST_QUERIES.sql

---

## QEMU Testing Setup

Since you're testing Citadel in QEMU via WSL:

```bash
# Create test database file
echo "CREATE TABLE Users (Id INT PRIMARY KEY, Name VARCHAR(50));" > test.sql
echo "INSERT INTO Users VALUES (1, 'Alice');" >> test.sql
echo "SELECT * FROM Users;" >> test.sql

# Copy to Citadel's file system (adjust path for your QEMU disk image)
# This depends on your Citadel VFS setup

# Run QEMU with your Citadel image
qemu-system-x86_64 -cdrom citadel.iso -m 512M

# In Citadel shell:
# > csql create /db/test.cdb
# > csql open /db/test.cdb
# > csql exec "CREATE TABLE Users (Id INT PRIMARY KEY, Name VARCHAR(50));"
# > csql exec "INSERT INTO Users VALUES (1, 'Alice');"
# > csql exec "SELECT * FROM Users;"
```

---

## File Structure in Citadel

Recommended directory structure:

```
/Citadel/
├── kernel/
│   ├── main.cpp
│   ├── memory.cpp
│   └── ...
├── drivers/
│   ├── keyboard.cpp
│   ├── video.cpp
│   └── ...
├── filesystem/
│   ├── vfs.cpp
│   ├── fat32.cpp
│   └── ...
├── services/
│   ├── service_manager.cpp
│   └── csql/               ← CSQL service here
│       ├── CSQLService.h
│       ├── CSQLService.cpp
│       ├── CitadelPAL.h
│       ├── CitadelPAL.cpp
│       ├── Database.h
│       ├── Database.cpp
│       ├── SQLParser.h
│       ├── SQLParser.cpp
│       ├── FileHeader.h
│       ├── Table.h
│       ├── Table.cpp
│       ├── RowSerializer.h
│       ├── RowSerializer.cpp
│       ├── PageManager.h
│       ├── PageManager.cpp
│       ├── FileManager.h
│       ├── FileManager.cpp
│       ├── QueryExecutor.h
│       ├── QueryExecutor.cpp
│       ├── WhereClauseEvaluator.h
│       └── WhereClauseEvaluator.cpp
└── shell/
    └── main.cpp
```

---

## Memory Considerations

### Heap Allocation
CSQL allocates memory for:
- Database metadata (~1-2 KB per database)
- Table structures (~500 bytes per table)
- Row data (4 KB pages)
- Query results (temporary, freed after query)

**Recommended**: Reserve at least **512 KB** for CSQL service.

### Stack Usage
CSQL uses recursion in some parsing functions.  
**Recommended**: Allocate at least **64 KB** stack per service thread.

---

## Performance Tips

1. **Disable Debug Output**: Remove `std::cout` replacements in production
2. **Use Native Types**: If Citadel has `std::string` or `std::vector`, use them instead of PAL implementations
3. **Buffer File I/O**: Implement buffering in PAL::File for better performance
4. **Optimize String Operations**: Use Citadel's optimized string functions if available

---

## Troubleshooting

### Problem: "Undefined reference to std::xxx"
**Solution**: You're using standard library types. Replace with PAL equivalents or implement missing features.

### Problem: "File operations fail silently"
**Solution**: Check PAL::File implementation. Ensure Citadel's VFS is initialized before CSQL service starts.

### Problem: "Memory corruption crashes"
**Solution**: Check PAL::Allocate/Free implementation. Ensure proper alignment and size tracking.

### Problem: "Queries return garbage data"
**Solution**: Check string conversion functions (StringToInt, StringToDouble). Ensure null-termination.

---

## Next Steps

After basic CSQL is working in Citadel:

1. **Add CSQL command to shell**: `csql open /db/test.cdb`, `csql exec "SELECT * FROM Users;"`
2. **Create test suite**: Port JOINS_TEST_QUERIES.sql to Citadel
3. **Optimize performance**: Profile and optimize hot paths
4. **Add network support**: Allow remote queries (if Citadel has networking)
5. **Implement caching**: Cache frequently-accessed pages in memory

---

## Summary Checklist

- [ ] Implement CitadelPAL.h/cpp (File, String, Vector, Console, Memory)
- [ ] Port FileHeader.h (no changes needed)
- [ ] Port WhereClauseEvaluator.h/cpp (minimal changes)
- [ ] Port Table.h/cpp (replace std::string)
- [ ] Port SQLParser.h/cpp (replace std::string, std::vector)
- [ ] Port Database.h/cpp (replace std::fstream, std::string)
- [ ] Port FileManager.h/cpp (replace std::fstream)
- [ ] Port PageManager.h/cpp (replace std::fstream)
- [ ] Port RowSerializer.h/cpp (replace std::vector)
- [ ] Port QueryExecutor.h/cpp (replace std::string)
- [ ] Implement CSQLService.h/cpp (service interface)
- [ ] Register service in Citadel boot sequence
- [ ] Test basic operations (CREATE, INSERT, SELECT)
- [ ] Test JOINs with JOINS_TEST_QUERIES.sql
- [ ] Add shell commands for CSQL interaction
- [ ] Document Citadel-specific usage

---

**Estimated Porting Time**: 8-16 hours (depending on Citadel's existing infrastructure)

**Difficulty**: Medium (mostly mechanical replacements, some debugging)

**Recommendation**: Start with PAL layer, get file I/O working first, then port database code incrementally.

Good luck with the port! This will be a powerful addition to Citadel OS. 🚀
