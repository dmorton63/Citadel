#pragma once
//
// QCSQLService.h - QCSQL Database Service for Citadel OS
// Wraps CQL engine as a message-routed service
//
// STATUS: STUB FILE FOR TESTING
// Phase 3: Service Integration
//

#include "QCSQLServiceProtocol.h"
#include <cstdint>
#include <cstddef>

// Forward declarations for Citadel types (if available)
// Uncomment when integrating with actual Citadel service infrastructure
// #include "QKServiceRegistry.h"
// #include "QKMsgBus.h"

namespace QCQL {
namespace Svc {

// ============================================================================
// Service Configuration
// ============================================================================

struct ServiceConfig {
    const char* serviceName = "QCSQL";
    const char* version = "1.0.0-Citadel-Stub";
    uint32_t maxDatabases = 16;
    uint32_t maxConnections = 64;
    bool enableDebugLogging = true;
    const char* preferredSystemBasePath = "/system/db";
    const char* preferredSharedBasePath = "/shared/db";
    bool allowSharedFallback = true;
    bool allowEphemeralFallback = true;
};

// ============================================================================
// Database Handle Management
// ============================================================================

struct DatabaseHandle {
    bool active = false;
    char path[MaxPathLength] = {0};
    void* databasePtr = nullptr;  // Pointer to actual Database object
    uint32_t refCount = 0;
};

// ============================================================================
// QCSQL Service Class
// ============================================================================

class QCSQLService {
public:
    // Constructor / Destructor
    QCSQLService();
    ~QCSQLService();
    
    // Service Lifecycle
    bool Initialize(const ServiceConfig& config);
    void Shutdown();
    bool IsRunning() const { return running; }
    
    // Message Handler (called by QKServiceRegistry)
    // STUB: Returns placeholder responses
    void HandleMessage(const void* request, size_t requestSize, 
                      void* response, size_t* responseSize);
    
    // Specific Message Handlers (STUB implementations)
    void HandleCreateDatabase(const CreateDatabaseRequest* req, CreateDatabaseResponse* resp);
    void HandleOpenDatabase(const OpenDatabaseRequest* req, OpenDatabaseResponse* resp);
    void HandleCloseDatabase(const CloseDatabaseRequest* req, CloseDatabaseResponse* resp);
    void HandleExecuteSQL(const ExecuteSQLRequest* req, ExecuteSQLResponse* resp);
    void HandleGetVersion(const GetVersionRequest* req, GetVersionResponse* resp);
    void HandleGetStatus(const GetStatusRequest* req, GetStatusResponse* resp);
    void HandleGetDatabaseInfo(const GetDatabaseInfoRequest* req, GetDatabaseInfoResponse* resp);
    
    // Service Statistics
    uint32_t GetActiveConnections() const { return activeConnections; }
    uint64_t GetTotalQueries() const { return totalQueries; }
    uint64_t GetUptime() const { return uptime; }

private:
    // Service State
    bool running;
    ServiceConfig config;
    
    // Database Management
    DatabaseHandle databases[16];  // Max databases
    uint32_t databaseCount;
    
    // Statistics
    uint32_t activeConnections;
    uint64_t totalQueries;
    uint64_t uptime;

    // Storage policy state
    StorageMode storageMode;
    char storageBasePath[MaxPathLength];
    char statusMessage[MaxErrorLength];
    
    // Helper Methods
    uint32_t AllocateDatabaseHandle();
    void FreeDatabaseHandle(uint32_t handle);
    bool ValidateHandle(uint32_t handle) const;
    bool ResolveDatabasePath(const char* requestedPath, char* outPath, size_t outLen) const;
    bool PrepareStorageBase(const char* path, StorageMode mode, const char* successMessage);
    void SelectStorageBackend();
    
    // Logging (STUB)
    void LogInfo(const char* message);
    void LogError(const char* message);
};

// ============================================================================
// Service Registration Function (for Citadel Service Manager)
// ============================================================================

// Call this from Citadel's service initialization sequence
// STUB: Returns nullptr until fully implemented
extern "C" QCSQLService* RegisterQCSQLService();

} // namespace Svc
} // namespace QCQL
