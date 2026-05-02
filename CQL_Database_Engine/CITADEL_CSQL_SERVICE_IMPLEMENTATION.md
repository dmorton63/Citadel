# CQL Database Service Implementation for Citadel OS

## Overview

This guide provides **concrete implementation steps** for wrapping the existing QCQL engine (QCQLEngine.h) as a message-routed service using Citadel's **QKServiceRegistry** infrastructure.

**Current State**: QCQL is a linked library (QCQLEngine.h)  
**Target State**: QCQL becomes a registered service with request/response protocol  
**Architecture**: In-process message-routed service (not fully isolated yet)

---

## Citadel Service Architecture (Existing)

### Components Already Available:
1. ✅ **QKServiceRegistry.h/cpp**: Service registration and message routing
2. ✅ **services.json**: Service manifest (currently empty, ready for CQL)
3. ✅ **Working Example**: Terminal → QDTerminal.cpp:1354 (client) + QDCommandProcessor.cpp:87 (service)
4. ✅ **QCQLEngine.h**: Existing CQL engine API (needs wrapping)

### What We're Building:
- **QCSQLService**: Message handler wrapping QCQLEngine.h
- **Service Protocol**: Request/response message format for CQL operations
- **Registration**: Add service entry to services.json
- **Client Example**: Terminal command or CMS panel integration

---

## Implementation Steps

### Step 1: Define CQL Service Protocol

Create message types for CQL operations:

```cpp
// QCSQLServiceProtocol.h - Message definitions for CQL service
#ifndef QCSQL_SERVICE_PROTOCOL_H
#define QCSQL_SERVICE_PROTOCOL_H

#include <stdint.h>

namespace Citadel::Services::QCSQL {

    // ========================================
    // MESSAGE TYPES
    // ========================================
    
    enum class MessageType : uint32_t {
        // Database lifecycle
        CreateDatabase = 1,
        OpenDatabase = 2,
        CloseDatabase = 3,
        
        // Query execution
        ExecuteQuery = 10,
        
        // Service info
        GetVersion = 20,
        GetStatus = 21,
        GetConnections = 22
    };
    
    // ========================================
    // REQUEST STRUCTURES
    // ========================================
    
    struct CreateDatabaseRequest {
        MessageType type = MessageType::CreateDatabase;
        char path[256];
    };
    
    struct OpenDatabaseRequest {
        MessageType type = MessageType::OpenDatabase;
        char path[256];
    };
    
    struct CloseDatabaseRequest {
        MessageType type = MessageType::CloseDatabase;
        int32_t handle;
    };
    
    struct ExecuteQueryRequest {
        MessageType type = MessageType::ExecuteQuery;
        int32_t handle;
        uint32_t queryLength;
        char query[4096];  // Max query size
    };
    
    struct GetVersionRequest {
        MessageType type = MessageType::GetVersion;
    };
    
    struct GetStatusRequest {
        MessageType type = MessageType::GetStatus;
    };
    
    struct GetConnectionsRequest {
        MessageType type = MessageType::GetConnections;
    };
    
    // ========================================
    // RESPONSE STRUCTURES
    // ========================================
    
    struct CreateDatabaseResponse {
        bool success;
        int32_t handle;  // -1 on failure
        char errorMessage[256];
    };
    
    struct OpenDatabaseResponse {
        bool success;
        int32_t handle;  // -1 on failure
        char errorMessage[256];
    };
    
    struct CloseDatabaseResponse {
        bool success;
        char errorMessage[256];
    };
    
    struct ExecuteQueryResponse {
        bool success;
        uint32_t resultLength;
        char result[8192];  // Max result size (may need dynamic allocation)
        char errorMessage[256];
    };
    
    struct GetVersionResponse {
        char version[64];
    };
    
    struct GetStatusResponse {
        enum class Status : uint32_t {
            Uninitialized = 0,
            Running = 1,
            Error = 2
        };
        Status status;
        char statusMessage[128];
    };
    
    struct GetConnectionsResponse {
        int32_t activeConnections;
        uint64_t memoryUsage;
    };
    
} // namespace Citadel::Services::QCSQL

#endif // QCSQL_SERVICE_PROTOCOL_H
```

---

### Step 2: Create Service Handler

Wrap QCQLEngine.h with message handling:

```cpp
// QCSQLService.h - Service handler for CQL database
#ifndef QCSQL_SERVICE_H
#define QCSQL_SERVICE_H

#include "QKServiceRegistry.h"
#include "QCSQLServiceProtocol.h"
#include "QCQLEngine.h"

namespace Citadel::Services::QCSQL {

    class QCSQLService {
    private:
        // Database handles (up to 16 concurrent databases)
        struct DatabaseSlot {
            QCQLEngine* engine;
            bool active;
            char path[256];
        };
        
        DatabaseSlot databases[16];
        bool initialized;
        
        // Message handlers
        void HandleCreateDatabase(const void* request, void* response);
        void HandleOpenDatabase(const void* request, void* response);
        void HandleCloseDatabase(const void* request, void* response);
        void HandleExecuteQuery(const void* request, void* response);
        void HandleGetVersion(const void* request, void* response);
        void HandleGetStatus(const void* request, void* response);
        void HandleGetConnections(const void* request, void* response);
        
        // Helper functions
        int32_t AllocateHandle();
        void ReleaseHandle(int32_t handle);
        bool ValidateHandle(int32_t handle);
        
    public:
        QCSQLService();
        ~QCSQLService();
        
        // Service lifecycle
        bool Initialize();
        void Shutdown();
        
        // Message router (called by QKServiceRegistry)
        void HandleMessage(uint32_t messageType, const void* request, void* response);
        
        // Static instance for registration
        static QCSQLService& GetInstance();
    };
    
} // namespace Citadel::Services::QCSQL

#endif // QCSQL_SERVICE_H
```

---

### Step 3: Implement Service Handler

```cpp
// QCSQLService.cpp - Implementation
#include "QCSQLService.h"
#include <cstring>

namespace Citadel::Services::QCSQL {

    // Singleton instance
    static QCSQLService* g_instance = nullptr;
    
    QCSQLService& QCSQLService::GetInstance() {
        if (!g_instance) {
            g_instance = new QCSQLService();
        }
        return *g_instance;
    }
    
    QCSQLService::QCSQLService() : initialized(false) {
        // Initialize all database slots
        for (int i = 0; i < 16; i++) {
            databases[i].engine = nullptr;
            databases[i].active = false;
            databases[i].path[0] = '\0';
        }
    }
    
    QCSQLService::~QCSQLService() {
        Shutdown();
    }
    
    bool QCSQLService::Initialize() {
        if (initialized) {
            return true;
        }
        
        // Log initialization
        // TODO: Use Citadel's logging system
        // Log::Info("[QCSQL] Initializing database service...");
        
        initialized = true;
        return true;
    }
    
    void QCSQLService::Shutdown() {
        if (!initialized) {
            return;
        }
        
        // Close all open databases
        for (int i = 0; i < 16; i++) {
            if (databases[i].active && databases[i].engine) {
                databases[i].engine->Close();
                delete databases[i].engine;
                databases[i].engine = nullptr;
                databases[i].active = false;
            }
        }
        
        initialized = false;
    }
    
    // ========================================
    // MESSAGE ROUTING
    // ========================================
    
    void QCSQLService::HandleMessage(uint32_t messageType, const void* request, void* response) {
        MessageType type = static_cast<MessageType>(messageType);
        
        switch (type) {
            case MessageType::CreateDatabase:
                HandleCreateDatabase(request, response);
                break;
            case MessageType::OpenDatabase:
                HandleOpenDatabase(request, response);
                break;
            case MessageType::CloseDatabase:
                HandleCloseDatabase(request, response);
                break;
            case MessageType::ExecuteQuery:
                HandleExecuteQuery(request, response);
                break;
            case MessageType::GetVersion:
                HandleGetVersion(request, response);
                break;
            case MessageType::GetStatus:
                HandleGetStatus(request, response);
                break;
            case MessageType::GetConnections:
                HandleGetConnections(request, response);
                break;
            default:
                // Unknown message type
                break;
        }
    }
    
    // ========================================
    // MESSAGE HANDLERS
    // ========================================
    
    void QCSQLService::HandleCreateDatabase(const void* request, void* response) {
        auto* req = static_cast<const CreateDatabaseRequest*>(request);
        auto* resp = static_cast<CreateDatabaseResponse*>(response);
        
        // Allocate handle
        int32_t handle = AllocateHandle();
        if (handle == -1) {
            resp->success = false;
            resp->handle = -1;
            strcpy(resp->errorMessage, "Too many open databases (max 16)");
            return;
        }
        
        // Create database engine
        databases[handle].engine = new QCQLEngine();
        if (!databases[handle].engine->Create(req->path)) {
            delete databases[handle].engine;
            databases[handle].engine = nullptr;
            ReleaseHandle(handle);
            
            resp->success = false;
            resp->handle = -1;
            strcpy(resp->errorMessage, "Failed to create database file");
            return;
        }
        
        // Success
        strcpy(databases[handle].path, req->path);
        databases[handle].active = true;
        
        resp->success = true;
        resp->handle = handle;
        resp->errorMessage[0] = '\0';
    }
    
    void QCSQLService::HandleOpenDatabase(const void* request, void* response) {
        auto* req = static_cast<const OpenDatabaseRequest*>(request);
        auto* resp = static_cast<OpenDatabaseResponse*>(response);
        
        // Allocate handle
        int32_t handle = AllocateHandle();
        if (handle == -1) {
            resp->success = false;
            resp->handle = -1;
            strcpy(resp->errorMessage, "Too many open databases (max 16)");
            return;
        }
        
        // Open database engine
        databases[handle].engine = new QCQLEngine();
        if (!databases[handle].engine->Open(req->path)) {
            delete databases[handle].engine;
            databases[handle].engine = nullptr;
            ReleaseHandle(handle);
            
            resp->success = false;
            resp->handle = -1;
            strcpy(resp->errorMessage, "Failed to open database file");
            return;
        }
        
        // Success
        strcpy(databases[handle].path, req->path);
        databases[handle].active = true;
        
        resp->success = true;
        resp->handle = handle;
        resp->errorMessage[0] = '\0';
    }
    
    void QCSQLService::HandleCloseDatabase(const void* request, void* response) {
        auto* req = static_cast<const CloseDatabaseRequest*>(request);
        auto* resp = static_cast<CloseDatabaseResponse*>(response);
        
        if (!ValidateHandle(req->handle)) {
            resp->success = false;
            strcpy(resp->errorMessage, "Invalid database handle");
            return;
        }
        
        // Close database
        databases[req->handle].engine->Close();
        delete databases[req->handle].engine;
        databases[req->handle].engine = nullptr;
        ReleaseHandle(req->handle);
        
        resp->success = true;
        resp->errorMessage[0] = '\0';
    }
    
    void QCSQLService::HandleExecuteQuery(const void* request, void* response) {
        auto* req = static_cast<const ExecuteQueryRequest*>(request);
        auto* resp = static_cast<ExecuteQueryResponse*>(response);
        
        if (!ValidateHandle(req->handle)) {
            resp->success = false;
            strcpy(resp->errorMessage, "Invalid database handle");
            strcpy(resp->result, "");
            resp->resultLength = 0;
            return;
        }
        
        // Execute query
        std::string result = databases[req->handle].engine->ExecuteQuery(req->query);
        
        // Check if result fits in response buffer
        if (result.length() >= sizeof(resp->result)) {
            resp->success = false;
            strcpy(resp->errorMessage, "Result too large for response buffer");
            strcpy(resp->result, "");
            resp->resultLength = 0;
            return;
        }
        
        // Success
        strcpy(resp->result, result.c_str());
        resp->resultLength = static_cast<uint32_t>(result.length());
        resp->success = true;
        resp->errorMessage[0] = '\0';
    }
    
    void QCSQLService::HandleGetVersion(const void* request, void* response) {
        auto* resp = static_cast<GetVersionResponse*>(response);
        strcpy(resp->version, "QCQL 1.0 for Citadel OS");
    }
    
    void QCSQLService::HandleGetStatus(const void* request, void* response) {
        auto* resp = static_cast<GetStatusResponse*>(response);
        
        if (initialized) {
            resp->status = GetStatusResponse::Status::Running;
            strcpy(resp->statusMessage, "Service running");
        } else {
            resp->status = GetStatusResponse::Status::Uninitialized;
            strcpy(resp->statusMessage, "Service not initialized");
        }
    }
    
    void QCSQLService::HandleGetConnections(const void* request, void* response) {
        auto* resp = static_cast<GetConnectionsResponse*>(response);
        
        int32_t activeCount = 0;
        for (int i = 0; i < 16; i++) {
            if (databases[i].active) {
                activeCount++;
            }
        }
        
        resp->activeConnections = activeCount;
        resp->memoryUsage = 0; // TODO: Track actual memory usage
    }
    
    // ========================================
    // HELPER FUNCTIONS
    // ========================================
    
    int32_t QCSQLService::AllocateHandle() {
        for (int i = 0; i < 16; i++) {
            if (!databases[i].active) {
                return i;
            }
        }
        return -1;
    }
    
    void QCSQLService::ReleaseHandle(int32_t handle) {
        if (handle >= 0 && handle < 16) {
            databases[handle].active = false;
            databases[handle].path[0] = '\0';
        }
    }
    
    bool QCSQLService::ValidateHandle(int32_t handle) {
        return (handle >= 0 && handle < 16 && databases[handle].active);
    }
    
} // namespace Citadel::Services::QCSQL
```

---

### Step 4: Register Service with QKServiceRegistry

Follow the Terminal/CommandProcessor pattern:

```cpp
// In your boot/initialization code (similar to QDCommandProcessor.cpp:87)

#include "QKServiceRegistry.h"
#include "QCSQLService.h"

namespace Citadel::Services {

    void RegisterQCSQLService() {
        // Get service instance
        auto& service = QCSQL::QCSQLService::GetInstance();
        
        // Initialize service
        if (!service.Initialize()) {
            // Log error
            // Log::Error("[BOOT] Failed to initialize QCSQL service");
            return;
        }
        
        // Register with service registry
        // Pattern from QDCommandProcessor.cpp:87
        QKServiceRegistry::RegisterService(
            "qcsql",  // Service name
            [](uint32_t messageType, const void* request, void* response) {
                // Message router lambda
                auto& svc = QCSQL::QCSQLService::GetInstance();
                svc.HandleMessage(messageType, request, response);
            }
        );
        
        // Log success
        // Log::Info("[BOOT] QCSQL service registered successfully");
    }
    
} // namespace Citadel::Services
```

---

### Step 5: Add Entry to services.json

```json
{
    "services": [
        {
            "name": "qcsql",
            "displayName": "CQL Database Service",
            "description": "Provides SQL database functionality for Citadel applications",
            "version": "1.0.0",
            "capabilities": [
                "database.create",
                "database.open",
                "database.query",
                "sql.ddl",
                "sql.dml",
                "sql.join"
            ],
            "dependencies": [
                "filesystem"
            ],
            "autoStart": true,
            "maxConnections": 16,
            "memoryLimit": "512KB"
        }
    ]
}
```

---

### Step 6: Create Client Example (Terminal Command)

Follow the pattern from QDTerminal.cpp:1354:

```cpp
// In your terminal command handler

#include "QKServiceRegistry.h"
#include "QCSQLServiceProtocol.h"

namespace Citadel::Terminal {

    using namespace Services::QCSQL;
    
    void HandleCSQLCommand(const char* args) {
        // Parse command: "csql create /db/test.cdb" or "csql query 0 SELECT * FROM Users"
        
        // Example: Create database
        if (strncmp(args, "create ", 7) == 0) {
            const char* path = args + 7;
            
            // Prepare request
            CreateDatabaseRequest request;
            strcpy(request.path, path);
            
            // Send message to service
            CreateDatabaseResponse response;
            QKServiceRegistry::SendMessage(
                "qcsql",
                static_cast<uint32_t>(MessageType::CreateDatabase),
                &request,
                &response
            );
            
            // Display result
            if (response.success) {
                printf("Database created successfully. Handle: %d\n", response.handle);
            } else {
                printf("Error: %s\n", response.errorMessage);
            }
        }
        
        // Example: Execute query
        else if (strncmp(args, "query ", 6) == 0) {
            // Parse: "query <handle> <sql>"
            int32_t handle;
            char query[4096];
            if (sscanf(args + 6, "%d %[^\n]", &handle, query) == 2) {
                // Prepare request
                ExecuteQueryRequest request;
                request.handle = handle;
                strcpy(request.query, query);
                request.queryLength = strlen(query);
                
                // Send message to service
                ExecuteQueryResponse response;
                QKServiceRegistry::SendMessage(
                    "qcsql",
                    static_cast<uint32_t>(MessageType::ExecuteQuery),
                    &request,
                    &response
                );
                
                // Display result
                if (response.success) {
                    printf("%s\n", response.result);
                } else {
                    printf("Error: %s\n", response.errorMessage);
                }
            } else {
                printf("Usage: csql query <handle> <sql>\n");
            }
        }
        
        // Add more command handlers (open, close, status, etc.)
    }
    
} // namespace Citadel::Terminal
```

---

## Integration Checklist

### Phase 1: Core Service
- [ ] Create QCSQLServiceProtocol.h (message definitions)
- [ ] Create QCSQLService.h (service handler interface)
- [ ] Implement QCSQLService.cpp (wrap QCQLEngine.h)
- [ ] Test compilation

### Phase 2: Registration
- [ ] Add RegisterQCSQLService() function
- [ ] Call during boot sequence (after filesystem init)
- [ ] Add entry to services.json
- [ ] Verify service appears in registry

### Phase 3: Client Integration
- [ ] Add Terminal command handler (csql create/open/query/close)
- [ ] Test basic operations (create, insert, select)
- [ ] Test error handling (invalid handles, bad SQL)
- [ ] Add status/info commands

### Phase 4: Testing
- [ ] Create test database via terminal
- [ ] Run basic queries (CREATE TABLE, INSERT, SELECT)
- [ ] Test JOINs from JOINS_TEST_QUERIES.sql
- [ ] Test concurrent database access (multiple handles)
- [ ] Test error recovery (close database mid-query)

### Phase 5: Optional Enhancements
- [ ] CMS panel for database management
- [ ] Query history/favorites
- [ ] Result export (CSV, JSON)
- [ ] Database browser UI
- [ ] Performance monitoring

---

## Key Differences from Generic Guide

### What's Different:
1. **Use QKServiceRegistry**: Don't implement service manager from scratch
2. **Follow Existing Pattern**: Terminal → CommandProcessor is the template
3. **In-Process Messages**: Not isolated processes (yet), just message routing
4. **Use QCQLEngine.h**: Wrap existing API, don't port from Windows code
5. **services.json**: Use existing manifest format

### What Stays Same:
- Platform Abstraction Layer (if needed for Windows → Citadel compatibility)
- Database file format (unchanged)
- SQL parsing logic (unchanged)
- Message protocol design (adapted to QKServiceRegistry format)

---

## Memory Layout

### Service Memory Footprint:
```
QCSQLService Instance:           ~1 KB
Database Slots (16 × ~50 bytes): ~800 bytes
QCQLEngine per database:         ~50 KB
Query result buffer:             ~8 KB (per query)
-------------------------------------------
Total (1 active DB):             ~60 KB
Total (16 active DBs):           ~810 KB
```

**Recommendation**: Reserve **1 MB** for QCSQL service to allow headroom for query results and temporary allocations.

---

## Next Steps

1. **Immediate**: Create QCSQLServiceProtocol.h and QCSQLService.h/cpp
2. **Integration**: Add RegisterQCSQLService() to boot sequence
3. **Testing**: Add `csql` command to Terminal for basic operations
4. **Validation**: Run JOINS_TEST_QUERIES.sql through service interface
5. **Documentation**: Update Citadel's service documentation with QCSQL examples

---

## Questions to Resolve

1. **QKServiceRegistry API**: What's the exact signature for `RegisterService()` and `SendMessage()`?
2. **Error Handling**: Does QKServiceRegistry have error codes or just success/failure?
3. **Async Messages**: Are messages synchronous request-response, or async with callbacks?
4. **Service Discovery**: How do clients enumerate available services?
5. **Capability Checking**: Does services.json enforce capability restrictions, or is it informational?

---

## Summary

**Current Path**: Wrap QCQLEngine.h with message handlers → Register with QKServiceRegistry → Add Terminal commands

**Estimated Time**: 4-8 hours (much faster than full port!)

**Benefits**:
- Reuse existing QCQLEngine.h (no porting needed)
- Follow proven pattern (Terminal/CommandProcessor)
- Service architecture already validated
- Can iterate quickly (compile → test → refine)

**Next Document**: Should I create specific code for the registration function using your actual QKServiceRegistry API? I'll need to see the actual function signatures to make it exact.
