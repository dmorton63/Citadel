//
// QCSQLServiceTest.cpp - Quick test program for QCSQL Service
// Demonstrates message protocol usage against the selected backend.
//
// Compile with the service and engine sources, for example via build_stubs.sh.
//

#include "QCSQLService.h"
#include "QCSQLServiceProtocol.h"
#include "CitadelPAL.h"
#include <cstdio>
#include <cstring>

using namespace QCQL::Svc;
using namespace Citadel::PAL;

#if defined(CITADEL_QCSQL_USE_CQL_ENGINE)
static const char* kBackendLabel = "CQL engine";
#elif defined(CITADEL_QCSQL_USE_QCQL_ENGINE)
static const char* kBackendLabel = "QCQL engine";
#else
static const char* kBackendLabel = "stub backend";
#endif

const char* StorageModeToString(StorageMode mode) {
    switch (mode) {
        case StorageMode::PersistentSystem: return "PersistentSystem";
        case StorageMode::SharedHost: return "SharedHost";
        case StorageMode::EphemeralMemory: return "EphemeralMemory";
        default: return "Unknown";
    }
}

// Test helper: Print separator
void PrintSeparator() {
    printf("\n==================================================\n");
}

// Test 1: Service Lifecycle
void TestServiceLifecycle() {
    PrintSeparator();
    printf("TEST 1: Service Lifecycle\n");
    PrintSeparator();
    
    QCSQLService service;
    ServiceConfig config;
    config.serviceName = "QCSQL-Test";
    config.version = "1.0.0-Test";
    config.maxDatabases = 16;
    config.enableDebugLogging = true;
    
    bool initSuccess = service.Initialize(config);
    printf("Service initialized: %s\n", initSuccess ? "SUCCESS" : "FAILED");
    printf("Service running: %s\n", service.IsRunning() ? "YES" : "NO");
    
    service.Shutdown();
    printf("Service shut down\n");
}

// Test 2: Create Database
void TestCreateDatabase() {
    PrintSeparator();
    printf("TEST 2: Create Database\n");
    PrintSeparator();
    
    QCSQLService service;
    ServiceConfig config;
    service.Initialize(config);
    
    CreateDatabaseRequest req;
    CreateDatabaseResponse resp;
    
    String::Copy(req.path, "test_database.db", MaxPathLength);
    
    size_t respSize = 0;
    service.HandleMessage(&req, sizeof(req), &resp, &respSize);
    
    printf("Request: CreateDatabase('%s')\n", req.path);
    printf("Response:\n");
    printf("  Success: %s\n", resp.success ? "YES" : "NO");
    printf("  Handle: %u\n", resp.handle);
    printf("  Message: %s\n", resp.errorMessage);
    printf("  Response Size: %zu bytes\n", respSize);
    
    service.Shutdown();
}

// Test 3: Execute SQL Query
void TestExecuteSQL() {
    PrintSeparator();
    printf("TEST 3: Execute SQL Query\n");
    PrintSeparator();
    
    QCSQLService service;
    ServiceConfig config;
    service.Initialize(config);

    CreateDatabaseRequest createReq;
    CreateDatabaseResponse createResp;
    String::Copy(createReq.path, "exec_test.db", MaxPathLength);
    size_t respSize = 0;
    service.HandleMessage(&createReq, sizeof(createReq), &createResp, &respSize);
    if (!createResp.success) {
        printf("Setup failed: %s\n", createResp.errorMessage);
        service.Shutdown();
        return;
    }
    
    ExecuteSQLRequest req;
    ExecuteSQLResponse resp;
    
    req.handle = createResp.handle;
    const char* testQuery = "SELECT * FROM users WHERE age > 18";
    String::Copy(req.query, testQuery, MaxQueryLength);
    req.queryLength = static_cast<uint32_t>(String::Length(testQuery));
    
    service.HandleMessage(&req, sizeof(req), &resp, &respSize);
    
    printf("Request: ExecuteSQL(handle=%u, query='%s')\n", req.handle, req.query);
    printf("Response:\n");
    printf("  Success: %s\n", resp.success ? "YES" : "NO");
    printf("  Result: %s\n", resp.result);
    printf("  Result Length: %u bytes\n", resp.resultLength);
    printf("  Rows Affected: %u\n", resp.rowsAffected);
    printf("  Error: %s\n", resp.errorMessage);
    
    service.Shutdown();
}

// Test 4: Get Service Status
void TestGetStatus() {
    PrintSeparator();
    printf("TEST 4: Get Service Status\n");
    PrintSeparator();
    
    QCSQLService service;
    ServiceConfig config;
    service.Initialize(config);
    
    // Execute a few dummy queries to increment stats
    ExecuteSQLRequest sqlReq;
    ExecuteSQLResponse sqlResp;
    size_t respSize = 0;
    
    for (int i = 0; i < 5; ++i) {
        service.HandleMessage(&sqlReq, sizeof(sqlReq), &sqlResp, &respSize);
    }
    
    GetStatusRequest req;
    GetStatusResponse resp;
    
    service.HandleMessage(&req, sizeof(req), &resp, &respSize);
    
    printf("Request: GetStatus()\n");
    printf("Response:\n");
    printf("  Running: %s\n", resp.running ? "YES" : "NO");
    printf("  Active Connections: %u\n", resp.activeConnections);
    printf("  Total Queries: %u\n", resp.totalQueries);
    printf("  Uptime: %llu ticks\n", (unsigned long long)resp.uptime);
    printf("  Storage Mode: %s\n", StorageModeToString(resp.storageMode));
    printf("  Storage Base Path: %s\n", resp.storageBasePath);
    printf("  Status Message: %s\n", resp.statusMessage);
    
    service.Shutdown();
}

// Test 5: Get Version
void TestGetVersion() {
    PrintSeparator();
    printf("TEST 5: Get Version\n");
    PrintSeparator();
    
    QCSQLService service;
    ServiceConfig config;
    config.version = nullptr;
    service.Initialize(config);
    
    GetVersionRequest req;
    GetVersionResponse resp;
    
    size_t respSize = 0;
    service.HandleMessage(&req, sizeof(req), &resp, &respSize);
    
    printf("Request: GetVersion()\n");
    printf("Response:\n");
    printf("  Version: %s\n", resp.version);
    
    service.Shutdown();
}

// Test 6: Multiple Messages (Simulating Client-Server)
void TestMultipleMessages() {
    PrintSeparator();
    printf("TEST 6: Multiple Messages (Client-Server Simulation)\n");
    PrintSeparator();
    
    QCSQLService service;
    ServiceConfig config;
    service.Initialize(config);
    
    printf("Simulating client session:\n\n");
    
    // 1. Create database
    printf("1. Creating database...\n");
    CreateDatabaseRequest createReq;
    CreateDatabaseResponse createResp;
    String::Copy(createReq.path, "users.db", MaxPathLength);
    size_t respSize = 0;
    service.HandleMessage(&createReq, sizeof(createReq), &createResp, &respSize);
    printf("   Created with handle: %u\n\n", createResp.handle);
    
    // 2. Execute CREATE TABLE
    printf("2. Creating table...\n");
    ExecuteSQLRequest execReq1;
    ExecuteSQLResponse execResp1;
    execReq1.handle = createResp.handle;
    const char* q1 = "CREATE TABLE users (id INT PRIMARY KEY, name TEXT)";
    String::Copy(execReq1.query, q1, MaxQueryLength);
    execReq1.queryLength = static_cast<uint32_t>(String::Length(q1));
    service.HandleMessage(&execReq1, sizeof(execReq1), &execResp1, &respSize);
    printf("   Result: %s\n\n", execResp1.result);
    
    // 3. Execute INSERT
    printf("3. Inserting data...\n");
    ExecuteSQLRequest execReq2;
    ExecuteSQLResponse execResp2;
    execReq2.handle = createResp.handle;
    const char* q2 = "INSERT INTO users VALUES (1, 'Alice')";
    String::Copy(execReq2.query, q2, MaxQueryLength);
    execReq2.queryLength = static_cast<uint32_t>(String::Length(q2));
    service.HandleMessage(&execReq2, sizeof(execReq2), &execResp2, &respSize);
    printf("   Result: %s\n\n", execResp2.result);
    
    // 4. Execute SELECT
    printf("4. Querying data...\n");
    ExecuteSQLRequest execReq3;
    ExecuteSQLResponse execResp3;
    execReq3.handle = createResp.handle;
    const char* q3 = "SELECT * FROM users";
    String::Copy(execReq3.query, q3, MaxQueryLength);
    execReq3.queryLength = static_cast<uint32_t>(String::Length(q3));
    service.HandleMessage(&execReq3, sizeof(execReq3), &execResp3, &respSize);
    printf("   Result: %s\n\n", execResp3.result);
    
    // 5. Close database
    printf("5. Closing database...\n");
    CloseDatabaseRequest closeReq;
    CloseDatabaseResponse closeResp;
    closeReq.handle = createResp.handle;
    service.HandleMessage(&closeReq, sizeof(closeReq), &closeResp, &respSize);
    printf("   Closed: %s\n\n", closeResp.success ? "SUCCESS" : "FAILED");
    
    // 6. Get final status
    printf("6. Getting final status...\n");
    GetStatusRequest statusReq;
    GetStatusResponse statusResp;
    service.HandleMessage(&statusReq, sizeof(statusReq), &statusResp, &respSize);
    printf("   Total queries executed: %u\n", statusResp.totalQueries);
    
    service.Shutdown();
}

// Main test runner
int main() {
    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║  QCSQL Service Test Suite                       ║\n");
    printf("║  Testing Platform Abstraction Layer & Service   ║\n");
    printf("╚══════════════════════════════════════════════════╝\n");
    
    TestServiceLifecycle();
    TestCreateDatabase();
    TestExecuteSQL();
    TestGetStatus();
    TestGetVersion();
    TestMultipleMessages();
    
    PrintSeparator();
    printf("ALL TESTS COMPLETED\n");
    printf("Backend: %s\n", kBackendLabel);
    PrintSeparator();
    printf("\n");
    
    return 0;
}
