#pragma once
//
// QCSQLServiceProtocol.h - Message protocol for QCSQL Database Service
// Defines request/response messages for inter-service communication
//

#include <cstdint>
#include <cstddef>

namespace QCQL {
namespace Svc {

// ============================================================================
// Message Types
// ============================================================================

enum class MessageType : uint32_t {
    // Database lifecycle
    CreateDatabase = 1,
    OpenDatabase = 2,
    CloseDatabase = 3,
    
    // SQL execution
    ExecuteSQL = 10,
    
    // Service info
    GetVersion = 20,
    GetStatus = 21,
    GetDatabaseInfo = 22,
    
    // Response type (used internally)
    Response = 100
};

// ============================================================================
// Constants
// ============================================================================

constexpr size_t MaxPathLength = 256;
constexpr size_t MaxQueryLength = 4096;
constexpr size_t MaxResultLength = 8192;
constexpr size_t MaxErrorLength = 256;

// ============================================================================
// Request Structures
// ============================================================================

struct CreateDatabaseRequest {
    MessageType type = MessageType::CreateDatabase;
    char path[MaxPathLength];
};

struct CreateDatabaseResponse {
    MessageType type = MessageType::Response;
    bool success;
    uint32_t handle;  // Database handle for subsequent operations
    char errorMessage[MaxErrorLength];
};

struct OpenDatabaseRequest {
    MessageType type = MessageType::OpenDatabase;
    char path[MaxPathLength];
};

struct OpenDatabaseResponse {
    MessageType type = MessageType::Response;
    bool success;
    uint32_t handle;
    char errorMessage[MaxErrorLength];
};

struct CloseDatabaseRequest {
    MessageType type = MessageType::CloseDatabase;
    uint32_t handle;
};

struct CloseDatabaseResponse {
    MessageType type = MessageType::Response;
    bool success;
    char errorMessage[MaxErrorLength];
};

struct ExecuteSQLRequest {
    MessageType type = MessageType::ExecuteSQL;
    uint32_t handle;
    uint32_t queryLength;
    char query[MaxQueryLength];
};

struct ExecuteSQLResponse {
    MessageType type = MessageType::Response;
    bool success;
    uint32_t resultLength;
    char result[MaxResultLength];  // Formatted result (e.g., CSV, JSON-like)
    char errorMessage[MaxErrorLength];
    uint32_t rowsAffected;
};

struct GetVersionRequest {
    MessageType type = MessageType::GetVersion;
};

struct GetVersionResponse {
    MessageType type = MessageType::Response;
    char version[64];  // e.g., "QCSQL v1.0.0-Citadel"
};

struct GetStatusRequest {
    MessageType type = MessageType::GetStatus;
};

enum class StorageMode : uint32_t {
    Unknown = 0,
    PersistentSystem = 1,
    SharedHost = 2,
    EphemeralMemory = 3
};

struct GetStatusResponse {
    MessageType type = MessageType::Response;
    bool running;
    uint32_t activeConnections;
    uint32_t totalQueries;
    uint64_t uptime;  // In milliseconds or ticks
    StorageMode storageMode;
    char storageBasePath[MaxPathLength];
    char statusMessage[MaxErrorLength];
};

struct GetDatabaseInfoRequest {
    MessageType type = MessageType::GetDatabaseInfo;
    uint32_t handle;
};

struct GetDatabaseInfoResponse {
    MessageType type = MessageType::Response;
    bool success;
    char path[MaxPathLength];
    uint32_t tableCount;
    uint64_t totalSize;  // In bytes
    char errorMessage[MaxErrorLength];
};

// ============================================================================
// Helper Functions
// ============================================================================

inline const char* MessageTypeToString(MessageType type) {
    switch (type) {
        case MessageType::CreateDatabase: return "CreateDatabase";
        case MessageType::OpenDatabase: return "OpenDatabase";
        case MessageType::CloseDatabase: return "CloseDatabase";
        case MessageType::ExecuteSQL: return "ExecuteSQL";
        case MessageType::GetVersion: return "GetVersion";
        case MessageType::GetStatus: return "GetStatus";
        case MessageType::GetDatabaseInfo: return "GetDatabaseInfo";
        case MessageType::Response: return "Response";
        default: return "Unknown";
    }
}

} // namespace Svc
} // namespace QCQL
