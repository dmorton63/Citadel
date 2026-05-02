# QCSQL Service Implementation for Citadel OS
## Implementation-Ready Code Using Actual Citadel APIs

---

## Overview

This implementation wraps your **Windows CQL engine** (SQLParser, Database, QueryExecutor with JOINs) as a Citadel service using the actual `QK::Svc::Registry` and `QK::Msg` infrastructure.

**Key Components:**
- `QCQL::Engine` - Low-level storage (already in Citadel)
- `SQLParser` + `Database` - SQL parsing and execution (from Windows, needs porting)
- `QCSQLService` - Service wrapper using `QK::Msg::Envelope`
- `QK::Svc::Registry` - Service registration and message routing

---

## Architecture Decision

The QCQL::Engine provides low-level storage operations (insertRow, readRow, etc.) but **no SQL parsing**. Your Windows implementation has:
- `SQLParser` - Parses SQL statements
- `Database` - Executes queries (SELECT, INSERT, UPDATE, DELETE, JOINs)
- `QueryExecutor` - Routes commands

**Strategy:**
1. Port the Windows CQL code to Citadel (replace std::string/vector with QC:: equivalents)
2. Wrap it as a service that accepts SQL queries via `QK::Msg::Envelope`
3. Use QCQL::Engine for storage backend

---

## File Structure

```
QCQL/
├── include/
│   ├── QCQLEngine.h              (exists - low-level storage)
│   ├── QCQLTypes.h               (exists - Status, Database, Row, etc.)
│   ├── QCSQLParser.h             (new - port from SQLParser.h)
│   ├── QCSQLDatabase.h           (new - port from Database.h)
│   ├── QCSQLQueryExecutor.h      (new - port from QueryExecutor.h)
│   ├── QCSQLService.h            (new - service wrapper)
│   └── QCSQLServiceProtocol.h    (new - message types)
└── src/
    ├── QCQLEngine.cpp            (exists)
    ├── QCSQLParser.cpp           (new - port from SQLParser.cpp)
    ├── QCSQLDatabase.cpp         (new - port from Database.cpp with JOINs)
    ├── QCSQLQueryExecutor.cpp    (new - port from QueryExecutor.cpp)
    └── QCSQLService.cpp          (new - service implementation)
```

---

## Step 1: Message Protocol

```cpp
// QCQL/include/QCSQLServiceProtocol.h
#pragma once

#include "QCTypes.h"
#include "QKMsgBus.h"

namespace QCQL
{
    namespace Svc
    {
        // Message types for QCSQL service
        enum class MessageType : QC::u32
        {
            // Database lifecycle
            CreateDatabase = 1,
            OpenDatabase = 2,
            CloseDatabase = 3,

            // SQL execution
            ExecuteSQL = 10,

            // Service info
            GetVersion = 20,
            GetStatus = 21,
            GetDatabaseInfo = 22
        };

        // Maximum sizes
        static constexpr QC::usize MaxPathLength = 256;
        static constexpr QC::usize MaxQueryLength = 4096;
        static constexpr QC::usize MaxResultLength = 8192;
        static constexpr QC::usize MaxErrorLength = 256;

        // Request: Create database
        struct CreateDatabaseRequest
        {
            char path[MaxPathLength];
        };

        struct CreateDatabaseResponse
        {
            bool success;
            QC::u32 handle;  // Database handle (index in service's array)
            char errorMessage[MaxErrorLength];
        };

        // Request: Open database
        struct OpenDatabaseRequest
        {
            char path[MaxPathLength];
        };

        struct OpenDatabaseResponse
        {
            bool success;
            QC::u32 handle;
            char errorMessage[MaxErrorLength];
        };

        // Request: Close database
        struct CloseDatabaseRequest
        {
            QC::u32 handle;
        };

        struct CloseDatabaseResponse
        {
            bool success;
            char errorMessage[MaxErrorLength];
        };

        // Request: Execute SQL
        struct ExecuteSQLRequest
        {
            QC::u32 handle;
            char sql[MaxQueryLength];
        };

        struct ExecuteSQLResponse
        {
            bool success;
            char result[MaxResultLength];
            char errorMessage[MaxErrorLength];
        };

        // Request: Get version
        struct GetVersionRequest
        {
            // Empty
        };

        struct GetVersionResponse
        {
            char version[64];
        };

        // Request: Get status
        struct GetStatusRequest
        {
            // Empty
        };

        struct GetStatusResponse
        {
            enum class Status : QC::u32
            {
                Uninitialized = 0,
                Running = 1,
                Error = 2
            };
            Status status;
            char statusMessage[128];
        };

        // Request: Get database info
        struct GetDatabaseInfoRequest
        {
            QC::u32 handle;
        };

        struct GetDatabaseInfoResponse
        {
            bool success;
            char path[MaxPathLength];
            QC::u32 tableCount;
            QC::u32 pageCount;
            char errorMessage[MaxErrorLength];
        };

        // Helper: Extract message type from envelope
        inline MessageType getMessageType(const QK::Msg::Envelope *env)
        {
            if (!env || !env->data || env->dataSize < sizeof(MessageType))
                return static_cast<MessageType>(0);
            return *reinterpret_cast<const MessageType *>(env->data);
        }

        // Helper: Copy string safely
        inline void copyString(char *dest, const char *src, QC::usize maxLen)
        {
            if (!dest || !src || maxLen == 0)
                return;
            QC::usize i = 0;
            while (i < maxLen - 1 && src[i] != '\0')
            {
                dest[i] = src[i];
                i++;
            }
            dest[i] = '\0';
        }

    } // namespace Svc
} // namespace QCQL
```

---

## Step 2: Service Handler

```cpp
// QCQL/include/QCSQLService.h
#pragma once

#include "QCTypes.h"
#include "QKMsgBus.h"
#include "QKServiceRegistry.h"
#include "QCSQLServiceProtocol.h"
#include "QCQLTypes.h"

// Forward declarations (will be ported from Windows code)
namespace QCQL
{
    class SQLDatabase;  // Port of Database.cpp with JOINs
}

namespace QCQL
{
    namespace Svc
    {
        class Service
        {
        public:
            static Service &instance();

            void initialize();
            void shutdown();

            // Service handler called by QK::Svc::Registry
            static void handleMessage(QK::Msg::Envelope *env, void *userData);

        private:
            Service() = default;

            // Database slots (up to 16 concurrent databases)
            struct DatabaseSlot
            {
                bool active = false;
                QCQL::Database database;  // QCQL::Engine handle
                QCQL::SQLDatabase *sqlDb = nullptr;  // SQL layer (from Windows port)
                char path[MaxPathLength] = {0};
            };

            static constexpr QC::usize MaxDatabases = 16;
            DatabaseSlot m_databases[MaxDatabases];
            bool m_initialized = false;

            // Message handlers
            void handleCreateDatabase(QK::Msg::Envelope *env);
            void handleOpenDatabase(QK::Msg::Envelope *env);
            void handleCloseDatabase(QK::Msg::Envelope *env);
            void handleExecuteSQL(QK::Msg::Envelope *env);
            void handleGetVersion(QK::Msg::Envelope *env);
            void handleGetStatus(QK::Msg::Envelope *env);
            void handleGetDatabaseInfo(QK::Msg::Envelope *env);

            // Helper functions
            QC::u32 allocateHandle();
            void releaseHandle(QC::u32 handle);
            bool validateHandle(QC::u32 handle);
        };

    } // namespace Svc
} // namespace QCQL
```

---

## Step 3: Service Implementation

```cpp
// QCQL/src/QCSQLService.cpp
#include "QCSQLService.h"
#include "QCQLEngine.h"
#include "QCSQLDatabase.h"  // Port of Database.h with JOINs
#include "QKLog.h"

namespace QCQL
{
    namespace Svc
    {
        Service &Service::instance()
        {
            static Service s_instance;
            return s_instance;
        }

        void Service::initialize()
        {
            if (m_initialized)
                return;

            QK::Log::info("[QCSQL] Initializing SQL service...");

            // Initialize all database slots
            for (QC::usize i = 0; i < MaxDatabases; i++)
            {
                m_databases[i].active = false;
                m_databases[i].sqlDb = nullptr;
                m_databases[i].path[0] = '\0';
            }

            m_initialized = true;
            QK::Log::info("[QCSQL] SQL service initialized successfully");
        }

        void Service::shutdown()
        {
            if (!m_initialized)
                return;

            QK::Log::info("[QCSQL] Shutting down SQL service...");

            // Close all active databases
            for (QC::usize i = 0; i < MaxDatabases; i++)
            {
                if (m_databases[i].active)
                {
                    if (m_databases[i].sqlDb)
                    {
                        delete m_databases[i].sqlDb;
                        m_databases[i].sqlDb = nullptr;
                    }
                    QCQL::Engine::instance().closeDatabase(m_databases[i].database);
                    m_databases[i].active = false;
                }
            }

            m_initialized = false;
            QK::Log::info("[QCSQL] SQL service shut down");
        }

        // Static handler for QK::Svc::Registry
        void Service::handleMessage(QK::Msg::Envelope *env, void *userData)
        {
            Service &svc = Service::instance();

            if (!env || !env->data)
            {
                QK::Log::error("[QCSQL] Received null envelope");
                return;
            }

            MessageType msgType = getMessageType(env);

            switch (msgType)
            {
            case MessageType::CreateDatabase:
                svc.handleCreateDatabase(env);
                break;
            case MessageType::OpenDatabase:
                svc.handleOpenDatabase(env);
                break;
            case MessageType::CloseDatabase:
                svc.handleCloseDatabase(env);
                break;
            case MessageType::ExecuteSQL:
                svc.handleExecuteSQL(env);
                break;
            case MessageType::GetVersion:
                svc.handleGetVersion(env);
                break;
            case MessageType::GetStatus:
                svc.handleGetStatus(env);
                break;
            case MessageType::GetDatabaseInfo:
                svc.handleGetDatabaseInfo(env);
                break;
            default:
                QK::Log::error("[QCSQL] Unknown message type: %u", static_cast<QC::u32>(msgType));
                break;
            }
        }

        // ========================================
        // MESSAGE HANDLERS
        // ========================================

        void Service::handleCreateDatabase(QK::Msg::Envelope *env)
        {
            if (env->dataSize < sizeof(CreateDatabaseRequest))
            {
                QK::Log::error("[QCSQL] CreateDatabase: Invalid message size");
                return;
            }

            auto *req = reinterpret_cast<const CreateDatabaseRequest *>(env->data);
            CreateDatabaseResponse resp = {};

            // Allocate handle
            QC::u32 handle = allocateHandle();
            if (handle == static_cast<QC::u32>(-1))
            {
                resp.success = false;
                resp.handle = static_cast<QC::u32>(-1);
                copyString(resp.errorMessage, "Too many open databases (max 16)", MaxErrorLength);
                QK::Msg::reply(env, &resp, sizeof(resp));
                return;
            }

            // Create database using QCQL::Engine
            QCQL::Status status = QCQL::Engine::instance().createDatabase(
                req->path,
                m_databases[handle].database);

            if (status != QCQL::Status::Success)
            {
                releaseHandle(handle);
                resp.success = false;
                resp.handle = static_cast<QC::u32>(-1);
                copyString(resp.errorMessage, "Failed to create database file", MaxErrorLength);
                QK::Msg::reply(env, &resp, sizeof(resp));
                return;
            }

            // Create SQL layer (port of Windows Database class)
            m_databases[handle].sqlDb = new QCQL::SQLDatabase(&m_databases[handle].database);
            copyString(m_databases[handle].path, req->path, MaxPathLength);
            m_databases[handle].active = true;

            // Success
            resp.success = true;
            resp.handle = handle;
            resp.errorMessage[0] = '\0';

            QK::Log::info("[QCSQL] Database created: %s (handle %u)", req->path, handle);
            QK::Msg::reply(env, &resp, sizeof(resp));
        }

        void Service::handleOpenDatabase(QK::Msg::Envelope *env)
        {
            if (env->dataSize < sizeof(OpenDatabaseRequest))
            {
                QK::Log::error("[QCSQL] OpenDatabase: Invalid message size");
                return;
            }

            auto *req = reinterpret_cast<const OpenDatabaseRequest *>(env->data);
            OpenDatabaseResponse resp = {};

            // Allocate handle
            QC::u32 handle = allocateHandle();
            if (handle == static_cast<QC::u32>(-1))
            {
                resp.success = false;
                resp.handle = static_cast<QC::u32>(-1);
                copyString(resp.errorMessage, "Too many open databases (max 16)", MaxErrorLength);
                QK::Msg::reply(env, &resp, sizeof(resp));
                return;
            }

            // Open database using QCQL::Engine
            QCQL::Status status = QCQL::Engine::instance().openDatabase(
                req->path,
                m_databases[handle].database);

            if (status != QCQL::Status::Success)
            {
                releaseHandle(handle);
                resp.success = false;
                resp.handle = static_cast<QC::u32>(-1);
                copyString(resp.errorMessage, "Failed to open database file", MaxErrorLength);
                QK::Msg::reply(env, &resp, sizeof(resp));
                return;
            }

            // Create SQL layer
            m_databases[handle].sqlDb = new QCQL::SQLDatabase(&m_databases[handle].database);
            copyString(m_databases[handle].path, req->path, MaxPathLength);
            m_databases[handle].active = true;

            // Success
            resp.success = true;
            resp.handle = handle;
            resp.errorMessage[0] = '\0';

            QK::Log::info("[QCSQL] Database opened: %s (handle %u)", req->path, handle);
            QK::Msg::reply(env, &resp, sizeof(resp));
        }

        void Service::handleCloseDatabase(QK::Msg::Envelope *env)
        {
            if (env->dataSize < sizeof(CloseDatabaseRequest))
            {
                QK::Log::error("[QCSQL] CloseDatabase: Invalid message size");
                return;
            }

            auto *req = reinterpret_cast<const CloseDatabaseRequest *>(env->data);
            CloseDatabaseResponse resp = {};

            if (!validateHandle(req->handle))
            {
                resp.success = false;
                copyString(resp.errorMessage, "Invalid database handle", MaxErrorLength);
                QK::Msg::reply(env, &resp, sizeof(resp));
                return;
            }

            // Close database
            if (m_databases[req->handle].sqlDb)
            {
                delete m_databases[req->handle].sqlDb;
                m_databases[req->handle].sqlDb = nullptr;
            }

            QCQL::Engine::instance().closeDatabase(m_databases[req->handle].database);
            releaseHandle(req->handle);

            resp.success = true;
            resp.errorMessage[0] = '\0';

            QK::Log::info("[QCSQL] Database closed (handle %u)", req->handle);
            QK::Msg::reply(env, &resp, sizeof(resp));
        }

        void Service::handleExecuteSQL(QK::Msg::Envelope *env)
        {
            if (env->dataSize < sizeof(ExecuteSQLRequest))
            {
                QK::Log::error("[QCSQL] ExecuteSQL: Invalid message size");
                return;
            }

            auto *req = reinterpret_cast<const ExecuteSQLRequest *>(env->data);
            ExecuteSQLResponse resp = {};

            if (!validateHandle(req->handle))
            {
                resp.success = false;
                copyString(resp.errorMessage, "Invalid database handle", MaxErrorLength);
                QK::Msg::reply(env, &resp, sizeof(resp));
                return;
            }

            // Execute SQL using the ported Database class
            // This will use your Windows code with JOINs, WHERE, ORDER BY, etc.
            QCQL::SQLDatabase *sqlDb = m_databases[req->handle].sqlDb;
            if (!sqlDb)
            {
                resp.success = false;
                copyString(resp.errorMessage, "Database not initialized", MaxErrorLength);
                QK::Msg::reply(env, &resp, sizeof(resp));
                return;
            }

            // Execute query (this calls your ported QueryExecutor logic)
            QC::String result = sqlDb->executeQuery(req->sql);

            // Check result size
            if (result.length() >= MaxResultLength)
            {
                resp.success = false;
                copyString(resp.errorMessage, "Result too large for response buffer", MaxErrorLength);
                QK::Msg::reply(env, &resp, sizeof(resp));
                return;
            }

            // Success
            resp.success = true;
            copyString(resp.result, result.c_str(), MaxResultLength);
            resp.errorMessage[0] = '\0';

            QK::Msg::reply(env, &resp, sizeof(resp));
        }

        void Service::handleGetVersion(QK::Msg::Envelope *env)
        {
            GetVersionResponse resp = {};
            copyString(resp.version, "QCQL 1.0 for Citadel OS (with JOINs)", 64);
            QK::Msg::reply(env, &resp, sizeof(resp));
        }

        void Service::handleGetStatus(QK::Msg::Envelope *env)
        {
            GetStatusResponse resp = {};

            if (m_initialized)
            {
                resp.status = GetStatusResponse::Status::Running;
                copyString(resp.statusMessage, "Service running", 128);
            }
            else
            {
                resp.status = GetStatusResponse::Status::Uninitialized;
                copyString(resp.statusMessage, "Service not initialized", 128);
            }

            QK::Msg::reply(env, &resp, sizeof(resp));
        }

        void Service::handleGetDatabaseInfo(QK::Msg::Envelope *env)
        {
            if (env->dataSize < sizeof(GetDatabaseInfoRequest))
            {
                QK::Log::error("[QCSQL] GetDatabaseInfo: Invalid message size");
                return;
            }

            auto *req = reinterpret_cast<const GetDatabaseInfoRequest *>(env->data);
            GetDatabaseInfoResponse resp = {};

            if (!validateHandle(req->handle))
            {
                resp.success = false;
                copyString(resp.errorMessage, "Invalid database handle", MaxErrorLength);
                QK::Msg::reply(env, &resp, sizeof(resp));
                return;
            }

            // Get database info
            resp.success = true;
            copyString(resp.path, m_databases[req->handle].path, MaxPathLength);
            resp.tableCount = m_databases[req->handle].database.header.tableCount;
            resp.pageCount = m_databases[req->handle].database.header.pageCount;
            resp.errorMessage[0] = '\0';

            QK::Msg::reply(env, &resp, sizeof(resp));
        }

        // ========================================
        // HELPER FUNCTIONS
        // ========================================

        QC::u32 Service::allocateHandle()
        {
            for (QC::usize i = 0; i < MaxDatabases; i++)
            {
                if (!m_databases[i].active)
                {
                    return static_cast<QC::u32>(i);
                }
            }
            return static_cast<QC::u32>(-1);
        }

        void Service::releaseHandle(QC::u32 handle)
        {
            if (handle < MaxDatabases)
            {
                m_databases[handle].active = false;
                m_databases[handle].path[0] = '\0';
            }
        }

        bool Service::validateHandle(QC::u32 handle)
        {
            return (handle < MaxDatabases && m_databases[handle].active);
        }

    } // namespace Svc
} // namespace QCQL
```

---

## Step 4: Service Registration

```cpp
// In your boot/initialization code (e.g., QCCore/src/QKServiceInit.cpp)

#include "QKServiceRegistry.h"
#include "QCSQLService.h"
#include "QKLog.h"

namespace QK
{
    namespace Svc
    {
        void registerQCSQLService()
        {
            QK::Log::info("[BOOT] Registering QCSQL service...");

            // Initialize service
            QCQL::Svc::Service::instance().initialize();

            // Register with service registry
            QK::Svc::Registry::instance().registerService(
                "qcsql",                                    // Service name
                &QCQL::Svc::Service::handleMessage,         // Handler function
                nullptr                                     // User data (not needed)
            );

            QK::Log::info("[BOOT] QCSQL service registered successfully");
        }

    } // namespace Svc
} // namespace QK

// Call this during boot sequence (after filesystem init):
// QK::Svc::registerQCSQLService();
```

---

## Step 5: Client Example (Terminal Command)

```cpp
// In your terminal command handler (e.g., QCMS/src/QDTerminal.cpp)

#include "QKServiceRegistry.h"
#include "QCSQLServiceProtocol.h"
#include "QKMsgBus.h"
#include "QKLog.h"

namespace QCMS
{
    using namespace QCQL::Svc;

    void handleCSQLCommand(const char *args)
    {
        // Parse command: "csql create /db/test.cdb" or "csql query 0 SELECT * FROM Users"

        // Example: Create database
        if (QC::strStartsWith(args, "create "))
        {
            const char *path = args + 7;

            // Prepare request
            CreateDatabaseRequest req = {};
            copyString(req.path, path, MaxPathLength);

            // Create envelope
            QK::Msg::Envelope env = {};
            env.data = &req;
            env.dataSize = sizeof(req);

            // Send to service
            if (!QK::Svc::Registry::instance().sendTo("qcsql", &env))
            {
                QK::Log::error("Failed to send message to qcsql service");
                return;
            }

            // Wait for response (synchronous for now)
            // TODO: Handle async responses properly
            if (env.data && env.dataSize >= sizeof(CreateDatabaseResponse))
            {
                auto *resp = reinterpret_cast<const CreateDatabaseResponse *>(env.data);
                if (resp->success)
                {
                    QK::Log::info("Database created successfully. Handle: %u", resp->handle);
                }
                else
                {
                    QK::Log::error("Error: %s", resp->errorMessage);
                }
            }
        }

        // Example: Execute query
        else if (QC::strStartsWith(args, "query "))
        {
            // Parse: "query <handle> <sql>"
            QC::u32 handle;
            char sql[MaxQueryLength];

            if (QC::sscanf(args + 6, "%u %[^\n]", &handle, sql) == 2)
            {
                // Prepare request
                ExecuteSQLRequest req = {};
                req.handle = handle;
                copyString(req.sql, sql, MaxQueryLength);

                // Create envelope
                QK::Msg::Envelope env = {};
                env.data = &req;
                env.dataSize = sizeof(req);

                // Send to service
                if (!QK::Svc::Registry::instance().sendTo("qcsql", &env))
                {
                    QK::Log::error("Failed to send message to qcsql service");
                    return;
                }

                // Handle response
                if (env.data && env.dataSize >= sizeof(ExecuteSQLResponse))
                {
                    auto *resp = reinterpret_cast<const ExecuteSQLResponse *>(env.data);
                    if (resp->success)
                    {
                        QK::Log::info("%s", resp->result);
                    }
                    else
                    {
                        QK::Log::error("Error: %s", resp->errorMessage);
                    }
                }
            }
            else
            {
                QK::Log::error("Usage: csql query <handle> <sql>");
            }
        }

        // Example: Close database
        else if (QC::strStartsWith(args, "close "))
        {
            QC::u32 handle;
            if (QC::sscanf(args + 6, "%u", &handle) == 1)
            {
                CloseDatabaseRequest req = {};
                req.handle = handle;

                QK::Msg::Envelope env = {};
                env.data = &req;
                env.dataSize = sizeof(req);

                if (!QK::Svc::Registry::instance().sendTo("qcsql", &env))
                {
                    QK::Log::error("Failed to send message to qcsql service");
                    return;
                }

                if (env.data && env.dataSize >= sizeof(CloseDatabaseResponse))
                {
                    auto *resp = reinterpret_cast<const CloseDatabaseResponse *>(env.data);
                    if (resp->success)
                    {
                        QK::Log::info("Database closed successfully");
                    }
                    else
                    {
                        QK::Log::error("Error: %s", resp->errorMessage);
                    }
                }
            }
        }

        // Add more commands: open, status, info, etc.
    }

} // namespace QCMS
```

---

## Next Steps: Porting Windows CQL to Citadel

Your Windows CQL implementation needs to be ported to use Citadel types:

### Port Checklist:

**Phase 1: Type Replacements**
- [ ] `std::string` → `QC::String`
- [ ] `std::vector` → `QC::Vector`
- [ ] `uint32_t` → `QC::u32`
- [ ] `uint64_t` → `QC::u64`
- [ ] File I/O → QCQL::Engine methods

**Phase 2: Core Classes**
- [ ] Port `SQLParser.h/.cpp` → `QCSQLParser.h/.cpp`
- [ ] Port `Database.h/.cpp` (with JOINs) → `QCSQLDatabase.h/.cpp`
- [ ] Port `QueryExecutor.h/.cpp` → `QCSQLQueryExecutor.h/.cpp`
- [ ] Port `Table.h/.cpp` → `QCSQLTable.h/.cpp`
- [ ] Port `WhereClauseEvaluator.h/.cpp` → `QCSQLWhereClause.h/.cpp`

**Phase 3: Integration**
- [ ] Replace `FileManager` with `QCQL::Engine` calls
- [ ] Replace `PageManager` with `QCQL::Engine::loadPage/flushPage`
- [ ] Replace `RowSerializer` with `QCQL::Row` serialization
- [ ] Test basic queries (CREATE, INSERT, SELECT)
- [ ] Test JOINs from `JOINS_TEST_QUERIES.sql`

---

## Summary

**What You Now Have:**
✅ Implementation-ready service code using actual `QK::Svc::Registry` API  
✅ Message protocol using `QK::Msg::Envelope`  
✅ Integration with `QCQL::Engine` for storage  
✅ Service registration example  
✅ Terminal client example  

**What You Need To Do:**
1. Port Windows CQL classes (SQLParser, Database, QueryExecutor) to Citadel types
2. Add files to QCQL/include and QCQL/src
3. Register service during boot
4. Add terminal commands
5. Test with `JOINS_TEST_QUERIES.sql`

**Estimated Time:**
- Type conversion: 2-4 hours
- Class porting: 4-8 hours
- Integration testing: 2-4 hours
- **Total: 8-16 hours**

Would you like me to start porting specific files (e.g., SQLParser.cpp → QCSQLParser.cpp) to show the exact type conversions needed?
