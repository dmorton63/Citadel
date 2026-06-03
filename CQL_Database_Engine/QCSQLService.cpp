//
// QCSQLService.cpp - QCSQL Database Service Implementation
// Wraps CQL engine as a message-routed service for Citadel OS
//
// STATUS: STUB FILE FOR TESTING
// Phase 3: Service Integration
//

#include "QCSQLService.h"
#include "CitadelPAL.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>

#if defined(CITADEL_QCSQL_USE_CQL_ENGINE)
#include "Database.h"
#endif

#if defined(CITADEL_QCSQL_USE_QCQL_ENGINE)
#include "../QCQL/Include/QCQLEngine.h"
#endif

namespace QCQL {
namespace Svc {

namespace {
constexpr uint32_t kInvalidHandle = 0xFFFFFFFFu;

bool IsAbsolutePath(const char* path) {
    return path && path[0] == '/';
}

bool StartsWithIgnoreCase(const char* text, const char* prefix) {
    if (!text || !prefix) {
        return false;
    }
    while (*prefix) {
        if (*text == '\0') {
            return false;
        }
        if (std::tolower(static_cast<unsigned char>(*text)) !=
            std::tolower(static_cast<unsigned char>(*prefix))) {
            return false;
        }
        ++text;
        ++prefix;
    }
    return true;
}

enum class QueryKind {
    Unknown,
    Select,
    Insert,
    Update,
    Delete,
    Create,
    Drop,
    Alter,
    Dump,
    ShowTables
};

std::string TrimCopy(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    if (start >= s.size()) {
        return std::string();
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

QueryKind DetectQueryKind(const std::string& query) {
    const std::string trimmed = TrimCopy(query);
    if (trimmed.empty()) {
        return QueryKind::Unknown;
    }

    if (StartsWithIgnoreCase(trimmed.c_str(), "SELECT")) return QueryKind::Select;
    if (StartsWithIgnoreCase(trimmed.c_str(), "INSERT")) return QueryKind::Insert;
    if (StartsWithIgnoreCase(trimmed.c_str(), "UPDATE")) return QueryKind::Update;
    if (StartsWithIgnoreCase(trimmed.c_str(), "DELETE")) return QueryKind::Delete;
    if (StartsWithIgnoreCase(trimmed.c_str(), "CREATE")) return QueryKind::Create;
    if (StartsWithIgnoreCase(trimmed.c_str(), "DROP")) return QueryKind::Drop;
    if (StartsWithIgnoreCase(trimmed.c_str(), "ALTER")) return QueryKind::Alter;
    if (StartsWithIgnoreCase(trimmed.c_str(), "DUMP")) return QueryKind::Dump;
    if (StartsWithIgnoreCase(trimmed.c_str(), "SHOW TABLES")) return QueryKind::ShowTables;
    return QueryKind::Unknown;
}

uint32_t ExtractFirstUnsigned(const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
            continue;
        }
        uint64_t value = 0;
        size_t j = i;
        while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j]))) {
            value = (value * 10u) + static_cast<uint64_t>(text[j] - '0');
            if (value > 0xFFFFFFFFull) {
                return 0xFFFFFFFFu;
            }
            ++j;
        }
        return static_cast<uint32_t>(value);
    }
    return 0;
}

#if defined(CITADEL_QCSQL_USE_CQL_ENGINE)
const char* CqlErrorText() {
    return "CQL engine operation failed";
}
#endif

#if defined(CITADEL_QCSQL_USE_QCQL_ENGINE)
const char* EngineStatusToText(QCQL::Status status) {
    switch (status) {
        case QCQL::Status::Success: return "Success";
        case QCQL::Status::InvalidParam: return "Invalid parameter";
        case QCQL::Status::NotFound: return "Not found";
        case QCQL::Status::PermissionDenied: return "Permission denied";
        case QCQL::Status::AlreadyExists: return "Already exists";
        case QCQL::Status::OutOfMemory: return "Out of memory";
        case QCQL::Status::NotSupported: return "Not supported";
        case QCQL::Status::Corrupt: return "Corrupt database";
        default: return "Engine error";
    }
}

struct QCQLEngineAdapter {
    static bool EqualsIgnoreCase(const char* a, const char* b) {
        if (!a || !b) {
            return false;
        }
        while (*a && *b) {
            if (std::tolower(static_cast<unsigned char>(*a)) !=
                std::tolower(static_cast<unsigned char>(*b))) {
                return false;
            }
            ++a;
            ++b;
        }
        return *a == '\0' && *b == '\0';
    }

    static bool IsSpace(char ch) {
        return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
    }

    static const char* SkipSpaces(const char* text) {
        while (text && IsSpace(*text)) {
            ++text;
        }
        return text;
    }

    static bool ReadIdentifier(const char*& p, char* out, size_t outCap) {
        if (!out || outCap == 0) {
            return false;
        }

        out[0] = '\0';
        p = SkipSpaces(p);
        if (!p || *p == '\0') {
            return false;
        }

        const char first = *p;
        if (!std::isalpha(static_cast<unsigned char>(first)) && first != '_') {
            return false;
        }

        size_t i = 0;
        while (*p) {
            const unsigned char ch = static_cast<unsigned char>(*p);
            if (!std::isalnum(ch) && ch != '_') {
                break;
            }
            if (i + 1 >= outCap) {
                return false;
            }
            out[i++] = *p++;
        }
        out[i] = '\0';
        return i > 0;
    }

    static bool ConsumeKeyword(const char*& p, const char* keyword) {
        p = SkipSpaces(p);
        if (!p || !keyword) {
            return false;
        }

        const char* cursor = p;
        while (*keyword) {
            if (*cursor == '\0') {
                return false;
            }
            if (std::tolower(static_cast<unsigned char>(*cursor)) !=
                std::tolower(static_cast<unsigned char>(*keyword))) {
                return false;
            }
            ++cursor;
            ++keyword;
        }

        if (*cursor != '\0' && !IsSpace(*cursor) && *cursor != '(' && *cursor != ')' && *cursor != ',' && *cursor != '=') {
            return false;
        }

        p = cursor;
        return true;
    }

    static bool ConsumeChar(const char*& p, char token) {
        p = SkipSpaces(p);
        if (!p || *p != token) {
            return false;
        }
        ++p;
        return true;
    }

    static void SetError(ExecuteSQLResponse* resp, const char* message) {
        resp->success = false;
        resp->rowsAffected = 0;
        resp->resultLength = 0;
        Citadel::PAL::String::Copy(resp->result, "", MaxResultLength);
        Citadel::PAL::String::Copy(resp->errorMessage, message ? message : "Error", MaxErrorLength);
    }

    static void SetSuccess(ExecuteSQLResponse* resp, const std::string& text, uint32_t rowsAffected) {
        resp->success = true;
        resp->rowsAffected = rowsAffected;
        Citadel::PAL::String::Copy(resp->errorMessage, "", MaxErrorLength);
        Citadel::PAL::String::Copy(resp->result, text.c_str(), MaxResultLength);
        resp->resultLength = static_cast<uint32_t>(Citadel::PAL::String::Length(resp->result));
    }

    static const QCQL::Table* FindTable(QCQL::Database* database, const char* name) {
        if (!database || !name) {
            return nullptr;
        }
        for (size_t i = 0; i < database->tables.size(); ++i) {
            if (EqualsIgnoreCase(database->tables[i].name, name)) {
                return &database->tables[i];
            }
        }
        return nullptr;
    }

    static std::string FormatCell(const QCQL::Cell& cell) {
        if (cell.type == QCQL::ColumnType::Text) {
            std::string out;
            out.reserve(cell.bytes.size());
            for (size_t i = 0; i < cell.bytes.size(); ++i) {
                char ch = static_cast<char>(cell.bytes[i]);
                if (ch == '\0') {
                    continue;
                }
                if (ch < 32 || ch > 126) {
                    ch = ' ';
                }
                out.push_back(ch);
            }
            return out;
        }

        if (cell.type == QCQL::ColumnType::Bool) {
            return (!cell.bytes.empty() && cell.bytes[0] != 0) ? "true" : "false";
        }

        uint32_t value = 0;
        const size_t maxBytes = cell.bytes.size() < 4 ? cell.bytes.size() : 4;
        for (size_t i = 0; i < maxBytes; ++i) {
            value |= static_cast<uint32_t>(cell.bytes[i]) << (8 * i);
        }
        return std::to_string(value);
    }

    static bool ParseColumnType(const char*& p, QCQL::ColumnType& outType) {
        char typeName[32] = {};
        if (!ReadIdentifier(p, typeName, sizeof(typeName))) {
            return false;
        }

        if (EqualsIgnoreCase(typeName, "text") || EqualsIgnoreCase(typeName, "varchar") ||
            EqualsIgnoreCase(typeName, "char") || EqualsIgnoreCase(typeName, "string")) {
            outType = QCQL::ColumnType::Text;
        } else if (EqualsIgnoreCase(typeName, "int") || EqualsIgnoreCase(typeName, "integer") ||
                   EqualsIgnoreCase(typeName, "tinyint") || EqualsIgnoreCase(typeName, "smallint") ||
                   EqualsIgnoreCase(typeName, "bigint")) {
            outType = QCQL::ColumnType::Int;
        } else if (EqualsIgnoreCase(typeName, "bool") || EqualsIgnoreCase(typeName, "boolean")) {
            outType = QCQL::ColumnType::Bool;
        } else if (EqualsIgnoreCase(typeName, "datetime") || EqualsIgnoreCase(typeName, "timestamp")) {
            outType = QCQL::ColumnType::DateTime;
        } else {
            return false;
        }

        p = SkipSpaces(p);
        if (p && *p == '(') {
            ++p;
            while (*p && *p != ')') {
                ++p;
            }
            if (*p != ')') {
                return false;
            }
            ++p;
        }

        return true;
    }

    static bool ParseCreateTable(const char* definition, QCQL::TableSchema& outSchema) {
        const char* p = SkipSpaces(definition);
        if (!p) {
            return false;
        }

        outSchema = QCQL::TableSchema{};
        if (!ReadIdentifier(p, outSchema.tableName, sizeof(outSchema.tableName))) {
            return false;
        }
        if (!ConsumeChar(p, '(')) {
            return false;
        }

        uint32_t pkCount = 0;
        while (true) {
            QCQL::Column column{};
            if (!ReadIdentifier(p, column.name, sizeof(column.name))) {
                return false;
            }
            if (!ParseColumnType(p, column.type)) {
                return false;
            }

            const char* pkCursor = p;
            if (ConsumeKeyword(pkCursor, "PRIMARY")) {
                if (!ConsumeKeyword(pkCursor, "KEY")) {
                    return false;
                }
                column.isPrimaryKey = true;
                p = pkCursor;
                outSchema.primaryKeyIndex = static_cast<uint32_t>(outSchema.columns.size());
                ++pkCount;
            }

            outSchema.columns.push_back(static_cast<QCQL::Column&&>(column));
            if (outSchema.columns.size() > QCQL::kMaxColumnsPerTable) {
                return false;
            }

            p = SkipSpaces(p);
            if (!p) {
                return false;
            }
            if (*p == ',') {
                ++p;
                continue;
            }
            if (*p == ')') {
                ++p;
                break;
            }
            return false;
        }

        p = SkipSpaces(p);
        return p && *p == '\0' && !outSchema.columns.empty() && pkCount == 1;
    }

    static bool ParseLimitSuffix(const char* text, uint32_t& outLimit) {
        const char* p = SkipSpaces(text);
        if (!p || *p == '\0') {
            return true;
        }
        if (!ConsumeKeyword(p, "LIMIT")) {
            return false;
        }
        p = SkipSpaces(p);
        if (!p || !std::isdigit(static_cast<unsigned char>(*p))) {
            return false;
        }

        uint32_t value = 0;
        while (*p && std::isdigit(static_cast<unsigned char>(*p))) {
            value = (value * 10u) + static_cast<uint32_t>(*p - '0');
            ++p;
        }
        p = SkipSpaces(p);
        if (*p != '\0') {
            return false;
        }

        outLimit = value;
        return true;
    }

    static bool ParseQuotedString(const char*& p, std::string& out) {
        p = SkipSpaces(p);
        if (!p || (*p != '\'' && *p != '"')) {
            return false;
        }

        const char quote = *p++;
        out.clear();
        while (*p && *p != quote) {
            if (*p == '\\' && p[1] != '\0') {
                ++p;
            }
            out.push_back(*p++);
        }
        if (*p != quote) {
            return false;
        }
        ++p;
        return true;
    }

    static bool ParseSignedInteger(const char*& p, int64_t& outValue) {
        p = SkipSpaces(p);
        if (!p || *p == '\0') {
            return false;
        }

        char* end = nullptr;
        const long long value = std::strtoll(p, &end, 10);
        if (end == p) {
            return false;
        }
        outValue = static_cast<int64_t>(value);
        p = end;
        return true;
    }

    static bool ParseBool(const char*& p, bool& outValue) {
        p = SkipSpaces(p);
        if (!p || *p == '\0') {
            return false;
        }
        if (StartsWithIgnoreCase(p, "true")) {
            const char tail = p[4];
            if (tail == '\0' || IsSpace(tail) || tail == ',' || tail == ')') {
                p += 4;
                outValue = true;
                return true;
            }
        }
        if (StartsWithIgnoreCase(p, "false")) {
            const char tail = p[5];
            if (tail == '\0' || IsSpace(tail) || tail == ',' || tail == ')') {
                p += 5;
                outValue = false;
                return true;
            }
        }

        int64_t numeric = 0;
        if (!ParseSignedInteger(p, numeric)) {
            return false;
        }
        outValue = (numeric != 0);
        return true;
    }

    static bool ParseCellValue(const char*& p, QCQL::ColumnType type, QCQL::Cell& outCell) {
        outCell = QCQL::Cell{};
        outCell.type = type;

        if (type == QCQL::ColumnType::Text) {
            std::string text;
            if (ParseQuotedString(p, text)) {
                outCell.bytes.resize(text.size());
                for (size_t i = 0; i < text.size(); ++i) {
                    outCell.bytes[i] = static_cast<QC::u8>(text[i]);
                }
                return true;
            }

            char identifier[128] = {};
            if (!ReadIdentifier(p, identifier, sizeof(identifier))) {
                return false;
            }
            const size_t len = std::strlen(identifier);
            outCell.bytes.resize(len);
            for (size_t i = 0; i < len; ++i) {
                outCell.bytes[i] = static_cast<QC::u8>(identifier[i]);
            }
            return true;
        }

        if (type == QCQL::ColumnType::Bool) {
            bool value = false;
            if (!ParseBool(p, value)) {
                return false;
            }
            outCell.bytes.resize(1);
            outCell.bytes[0] = value ? 1u : 0u;
            return true;
        }

        int64_t value = 0;
        if (!ParseSignedInteger(p, value)) {
            return false;
        }
        outCell.bytes.resize(4);
        const uint32_t encoded = static_cast<uint32_t>(value);
        outCell.bytes[0] = static_cast<QC::u8>(encoded & 0xFFu);
        outCell.bytes[1] = static_cast<QC::u8>((encoded >> 8) & 0xFFu);
        outCell.bytes[2] = static_cast<QC::u8>((encoded >> 16) & 0xFFu);
        outCell.bytes[3] = static_cast<QC::u8>((encoded >> 24) & 0xFFu);
        return true;
    }

    static void ExecShowTables(QCQL::Database* database, ExecuteSQLResponse* resp) {
        std::string result;
        for (size_t i = 0; i < database->tables.size(); ++i) {
            if (!result.empty()) {
                result += '\n';
            }
            result += std::to_string(i + 1);
            result += ") ";
            result += database->tables[i].name;
            result += " columns=";
            result += std::to_string(database->tables[i].schema.columns.size());
        }
        if (result.empty()) {
            result = "No tables found";
        }
        SetSuccess(resp, result, static_cast<uint32_t>(database->tables.size()));
    }

    static void ExecDescribe(QCQL::Database* database, const char* name, ExecuteSQLResponse* resp) {
        const QCQL::Table* table = FindTable(database, name);
        if (!table) {
            SetError(resp, "Table not found");
            return;
        }

        std::string result;
        for (size_t i = 0; i < table->schema.columns.size(); ++i) {
            const QCQL::Column& col = table->schema.columns[i];
            if (!result.empty()) {
                result += '\n';
            }
            result += std::to_string(i + 1);
            result += ") ";
            result += col.name;
            result += " type=";
            switch (col.type) {
                case QCQL::ColumnType::Text: result += "text"; break;
                case QCQL::ColumnType::Int: result += "int"; break;
                case QCQL::ColumnType::Bool: result += "bool"; break;
                case QCQL::ColumnType::DateTime: result += "datetime"; break;
            }
            if (col.isPrimaryKey) {
                result += " pk=1";
            }
        }

        SetSuccess(resp, result, static_cast<uint32_t>(table->schema.columns.size()));
    }

    static void ExecSelect(QCQL::Database* database, const char* text, ExecuteSQLResponse* resp) {
        const char* p = text ? text : "";
        if (!ConsumeKeyword(p, "SELECT") || !ConsumeChar(p, '*') || !ConsumeKeyword(p, "FROM")) {
            SetError(resp, "Only SELECT * FROM <table> [LIMIT N] is supported");
            return;
        }

        char tableName[64] = {};
        if (!ReadIdentifier(p, tableName, sizeof(tableName))) {
            SetError(resp, "Missing table name");
            return;
        }

        uint32_t limit = 25;
        if (!ParseLimitSuffix(p, limit)) {
            SetError(resp, "Bad LIMIT clause");
            return;
        }

        const QCQL::Table* table = FindTable(database, tableName);
        if (!table) {
            SetError(resp, "Table not found");
            return;
        }

        std::string result;
        uint32_t emitted = 0;
        for (size_t pageIndex = 0; pageIndex < table->pages.size() && emitted < limit; ++pageIndex) {
            QCQL::Page page{};
            if (QCQL::Engine::instance().loadPage(*database, table->pages[pageIndex], page) != QCQL::Status::Success) {
                continue;
            }

            for (size_t rowIndex = 0; rowIndex < page.rowOffsets.size() && emitted < limit; ++rowIndex) {
                QCQL::Row row{};
                if (QCQL::Engine::instance().readRow(*database, page.header.pageId, page.rowOffsets[rowIndex], row) != QCQL::Status::Success) {
                    continue;
                }
                if (row.tombstone) {
                    continue;
                }

                if (!result.empty()) {
                    result += '\n';
                }
                for (size_t c = 0; c < row.cells.size(); ++c) {
                    if (c > 0) {
                        result += " | ";
                    }
                    result += FormatCell(row.cells[c]);
                }
                ++emitted;
            }
        }

        if (emitted == 0) {
            result = "No rows found";
        }
        SetSuccess(resp, result, emitted);
    }

    static void ExecCreateTable(QCQL::Database* database, const char* definition, ExecuteSQLResponse* resp) {
        QCQL::TableSchema schema{};
        if (!ParseCreateTable(definition, schema)) {
            SetError(resp, "Bad CREATE TABLE syntax");
            return;
        }

        const QCQL::Status status = QCQL::Engine::instance().createTable(*database, schema);
        if (status != QCQL::Status::Success) {
            switch (status) {
                case QCQL::Status::AlreadyExists: SetError(resp, "Table already exists"); break;
                case QCQL::Status::InvalidParam: SetError(resp, "Invalid schema"); break;
                default: SetError(resp, "Create table failed"); break;
            }
            return;
        }

        std::string result = "Created table ";
        result += schema.tableName;
        result += " columns=";
        result += std::to_string(schema.columns.size());
        SetSuccess(resp, result, 0);
    }

    static void ExecInsert(QCQL::Database* database, const char* text, ExecuteSQLResponse* resp) {
        const char* p = text ? text : "";
        if (!ConsumeKeyword(p, "INSERT") || !ConsumeKeyword(p, "INTO")) {
            SetError(resp, "Bad INSERT syntax");
            return;
        }

        char tableName[64] = {};
        if (!ReadIdentifier(p, tableName, sizeof(tableName))) {
            SetError(resp, "Missing table name");
            return;
        }
        if (!ConsumeKeyword(p, "VALUES") || !ConsumeChar(p, '(')) {
            SetError(resp, "Bad INSERT syntax");
            return;
        }

        const QCQL::Table* table = FindTable(database, tableName);
        if (!table) {
            SetError(resp, "Table not found");
            return;
        }

        QCQL::Row row{};
        for (size_t i = 0; i < table->schema.columns.size(); ++i) {
            QCQL::Cell cell{};
            if (!ParseCellValue(p, table->schema.columns[i].type, cell)) {
                SetError(resp, "Bad INSERT values");
                return;
            }
            row.cells.push_back(static_cast<QCQL::Cell&&>(cell));

            p = SkipSpaces(p);
            if (i + 1 < table->schema.columns.size()) {
                if (*p != ',') {
                    SetError(resp, "Bad INSERT values");
                    return;
                }
                ++p;
            }
        }

        p = SkipSpaces(p);
        if (*p != ')') {
            SetError(resp, "Bad INSERT values");
            return;
        }
        ++p;
        p = SkipSpaces(p);
        if (*p != '\0') {
            SetError(resp, "Bad INSERT syntax");
            return;
        }

        const QCQL::Status status = QCQL::Engine::instance().insertRowByName(*database, tableName, row);
        if (status != QCQL::Status::Success) {
            switch (status) {
                case QCQL::Status::AlreadyExists: SetError(resp, "Primary key already exists"); break;
                case QCQL::Status::InvalidParam: SetError(resp, "Row does not match schema"); break;
                default: SetError(resp, EngineStatusToText(status)); break;
            }
            return;
        }

        std::string result = "Inserted into ";
        result += tableName;
        SetSuccess(resp, result, 1);
    }

    static void Execute(QCQL::Database* database, const ExecuteSQLRequest* req, ExecuteSQLResponse* resp) {
        if (!database) {
            SetError(resp, "No database bound");
            return;
        }
        if (!req || req->queryLength == 0 || req->query[0] == '\0') {
            SetError(resp, "Query is empty");
            return;
        }

        const char* query = SkipSpaces(req->query);
        if (!query || *query == '\0') {
            SetError(resp, "Query is empty");
            return;
        }

        if (EqualsIgnoreCase(query, "SHOW TABLES") || EqualsIgnoreCase(query, "DUMP TABLES_LOADED")) {
            ExecShowTables(database, resp);
            return;
        }
        if (StartsWithIgnoreCase(query, "DESCRIBE ")) {
            ExecDescribe(database, SkipSpaces(query + 9), resp);
            return;
        }
        if (StartsWithIgnoreCase(query, "SHOW COLUMNS FROM ")) {
            ExecDescribe(database, SkipSpaces(query + 18), resp);
            return;
        }
        if (StartsWithIgnoreCase(query, "SELECT ")) {
            ExecSelect(database, query, resp);
            return;
        }
        if (StartsWithIgnoreCase(query, "CREATE TABLE ")) {
            ExecCreateTable(database, query + 13, resp);
            return;
        }
        if (StartsWithIgnoreCase(query, "INSERT INTO ")) {
            ExecInsert(database, query, resp);
            return;
        }

        SetError(resp, "Supported queries: SHOW TABLES, DESCRIBE, SHOW COLUMNS, SELECT * FROM, CREATE TABLE, INSERT INTO");
    }
};
#endif
} // namespace

#if defined(CITADEL_QCSQL_USE_CQL_ENGINE) && defined(CITADEL_QCSQL_USE_QCQL_ENGINE)
#error "Enable only one engine binding mode: CITADEL_QCSQL_USE_CQL_ENGINE or CITADEL_QCSQL_USE_QCQL_ENGINE"
#endif

namespace {
const char* ActiveBackendLabel() {
#if defined(CITADEL_QCSQL_USE_CQL_ENGINE)
    return "CQL engine";
#elif defined(CITADEL_QCSQL_USE_QCQL_ENGINE)
    return "QCQL engine";
#else
    return "stub backend";
#endif
}

const char* DefaultVersionString() {
#if defined(CITADEL_QCSQL_USE_CQL_ENGINE)
    return "1.0.0-Citadel-CQL";
#elif defined(CITADEL_QCSQL_USE_QCQL_ENGINE)
    return "1.0.0-Citadel-QCQL";
#else
    return "1.0.0-Citadel-Stub";
#endif
}

const char* InitModeMessage() {
#if defined(CITADEL_QCSQL_USE_CQL_ENGINE)
    return "QCSQL Service initialized (CQL engine backend)";
#elif defined(CITADEL_QCSQL_USE_QCQL_ENGINE)
    return "QCSQL Service initialized (QCQL engine backend)";
#else
    return "QCSQL Service initialized (stub backend)";
#endif
}
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

QCSQLService::QCSQLService()
    : running(false)
    , databaseCount(0)
    , activeConnections(0)
    , totalQueries(0)
    , uptime(0)
    , storageMode(StorageMode::Unknown)
{
    // Initialize database handle array
    for (uint32_t i = 0; i < config.maxDatabases; ++i) {
        databases[i].active = false;
        databases[i].databasePtr = nullptr;
        databases[i].refCount = 0;
    }

    storageBasePath[0] = '\0';
    statusMessage[0] = '\0';
}

QCSQLService::~QCSQLService() {
    Shutdown();
}

// ============================================================================
// Service Lifecycle
// ============================================================================

bool QCSQLService::Initialize(const ServiceConfig& cfg) {
    config = cfg;
    if (!config.version || config.version[0] == '\0') {
        config.version = DefaultVersionString();
    }
    
    LogInfo("QCSQL Service initializing...");

    // Choose persistence backend: /system first, then /shared, then in-memory fallback.
    SelectStorageBackend();
    
    running = true;
    LogInfo(statusMessage);
    LogInfo(InitModeMessage());
    return true;
}

void QCSQLService::Shutdown() {
    if (!running) return;
    
    LogInfo("QCSQL Service shutting down...");
    
    // STUB: Close all open databases
    for (uint32_t i = 0; i < config.maxDatabases; ++i) {
        if (databases[i].active) {
            FreeDatabaseHandle(i);
        }
    }
    
    running = false;
    storageMode = StorageMode::Unknown;
    storageBasePath[0] = '\0';
    Citadel::PAL::String::Copy(statusMessage, "Service is shut down", MaxErrorLength);
    LogInfo("QCSQL Service shut down");
}

// ============================================================================
// Message Router
// ============================================================================

void QCSQLService::HandleMessage(const void* request, size_t requestSize,
                                  void* response, size_t* responseSize) {
    if (!request || !response || !responseSize) {
        LogError("Invalid message parameters");
        return;
    }
    
    // Read message type from first field
    const MessageType* typePtr = static_cast<const MessageType*>(request);
    MessageType type = *typePtr;
    
    LogInfo("Received message");  // STUB: Would log actual type
    
    // Route to specific handler based on message type
    switch (type) {
        case MessageType::CreateDatabase:
            HandleCreateDatabase(
                static_cast<const CreateDatabaseRequest*>(request),
                static_cast<CreateDatabaseResponse*>(response)
            );
            *responseSize = sizeof(CreateDatabaseResponse);
            break;
            
        case MessageType::OpenDatabase:
            HandleOpenDatabase(
                static_cast<const OpenDatabaseRequest*>(request),
                static_cast<OpenDatabaseResponse*>(response)
            );
            *responseSize = sizeof(OpenDatabaseResponse);
            break;
            
        case MessageType::CloseDatabase:
            HandleCloseDatabase(
                static_cast<const CloseDatabaseRequest*>(request),
                static_cast<CloseDatabaseResponse*>(response)
            );
            *responseSize = sizeof(CloseDatabaseResponse);
            break;
            
        case MessageType::ExecuteSQL:
            HandleExecuteSQL(
                static_cast<const ExecuteSQLRequest*>(request),
                static_cast<ExecuteSQLResponse*>(response)
            );
            *responseSize = sizeof(ExecuteSQLResponse);
            break;
            
        case MessageType::GetVersion:
            HandleGetVersion(
                static_cast<const GetVersionRequest*>(request),
                static_cast<GetVersionResponse*>(response)
            );
            *responseSize = sizeof(GetVersionResponse);
            break;
            
        case MessageType::GetStatus:
            HandleGetStatus(
                static_cast<const GetStatusRequest*>(request),
                static_cast<GetStatusResponse*>(response)
            );
            *responseSize = sizeof(GetStatusResponse);
            break;
            
        case MessageType::GetDatabaseInfo:
            HandleGetDatabaseInfo(
                static_cast<const GetDatabaseInfoRequest*>(request),
                static_cast<GetDatabaseInfoResponse*>(response)
            );
            *responseSize = sizeof(GetDatabaseInfoResponse);
            break;
            
        default:
            LogError("Unknown message type");
            *responseSize = 0;
            break;
    }
    
    totalQueries++;
}

// ============================================================================
// Message Handlers (STUB implementations)
// ============================================================================

void QCSQLService::HandleCreateDatabase(const CreateDatabaseRequest* req,
                                         CreateDatabaseResponse* resp) {
    LogInfo("CreateDatabase request");

    resp->type = MessageType::Response;
    resp->success = false;
    resp->handle = kInvalidHandle;
    Citadel::PAL::String::Copy(resp->errorMessage, "", MaxErrorLength);

    if (!running) {
        Citadel::PAL::String::Copy(resp->errorMessage, "Service not running", MaxErrorLength);
        return;
    }

    char resolvedPath[MaxPathLength] = {0};
    if (!ResolveDatabasePath(req ? req->path : nullptr, resolvedPath, sizeof(resolvedPath))) {
        Citadel::PAL::String::Copy(resp->errorMessage, "Invalid or too-long database path", MaxErrorLength);
        return;
    }

    const uint32_t handle = AllocateDatabaseHandle();
    if (handle == kInvalidHandle) {
        Citadel::PAL::String::Copy(resp->errorMessage, "Too many open databases", MaxErrorLength);
        return;
    }

    if (storageMode != StorageMode::EphemeralMemory) {
        auto file = Citadel::PAL::FileHandle::Open(resolvedPath, Citadel::PAL::FileMode::Create);
        if (!file.IsValid()) {
            FreeDatabaseHandle(handle);
            Citadel::PAL::String::Copy(resp->errorMessage, "Failed to create database file", MaxErrorLength);
            return;
        }
        file.Close();
    }

    Citadel::PAL::String::Copy(databases[handle].path, resolvedPath, MaxPathLength);

#if defined(CITADEL_QCSQL_USE_CQL_ENGINE)
    auto* db = new CQL::Database();
    if (!db->Create(resolvedPath)) {
        delete db;
        FreeDatabaseHandle(handle);
        Citadel::PAL::String::Copy(resp->errorMessage, CqlErrorText(), MaxErrorLength);
        return;
    }
    databases[handle].databasePtr = db;
#endif

#if defined(CITADEL_QCSQL_USE_QCQL_ENGINE)
    auto* db = new QCQL::Database();
    const QCQL::Status createStatus = QCQL::Engine::instance().createDatabase(resolvedPath, *db);
    if (createStatus != QCQL::Status::Success) {
        delete db;
        FreeDatabaseHandle(handle);
        Citadel::PAL::String::Copy(resp->errorMessage, EngineStatusToText(createStatus), MaxErrorLength);
        return;
    }
    databases[handle].databasePtr = db;
#endif

    resp->success = true;
    resp->handle = handle;
    activeConnections = databaseCount;
}

void QCSQLService::HandleOpenDatabase(const OpenDatabaseRequest* req,
                                       OpenDatabaseResponse* resp) {
    LogInfo("OpenDatabase request");

    resp->type = MessageType::Response;
    resp->success = false;
    resp->handle = kInvalidHandle;
    Citadel::PAL::String::Copy(resp->errorMessage, "", MaxErrorLength);

    if (!running) {
        Citadel::PAL::String::Copy(resp->errorMessage, "Service not running", MaxErrorLength);
        return;
    }

    char resolvedPath[MaxPathLength] = {0};
    if (!ResolveDatabasePath(req ? req->path : nullptr, resolvedPath, sizeof(resolvedPath))) {
        Citadel::PAL::String::Copy(resp->errorMessage, "Invalid or too-long database path", MaxErrorLength);
        return;
    }

    if (storageMode != StorageMode::EphemeralMemory && !Citadel::PAL::FileHandle::Exists(resolvedPath)) {
        Citadel::PAL::String::Copy(resp->errorMessage, "Database file not found", MaxErrorLength);
        return;
    }

    const uint32_t handle = AllocateDatabaseHandle();
    if (handle == kInvalidHandle) {
        Citadel::PAL::String::Copy(resp->errorMessage, "Too many open databases", MaxErrorLength);
        return;
    }

    Citadel::PAL::String::Copy(databases[handle].path, resolvedPath, MaxPathLength);

#if defined(CITADEL_QCSQL_USE_CQL_ENGINE)
    auto* db = new CQL::Database();
    if (!db->Open(resolvedPath)) {
        delete db;
        FreeDatabaseHandle(handle);
        Citadel::PAL::String::Copy(resp->errorMessage, CqlErrorText(), MaxErrorLength);
        return;
    }
    databases[handle].databasePtr = db;
#endif

#if defined(CITADEL_QCSQL_USE_QCQL_ENGINE)
    auto* db = new QCQL::Database();
    const QCQL::Status openStatus = QCQL::Engine::instance().openDatabase(resolvedPath, *db);
    if (openStatus != QCQL::Status::Success) {
        delete db;
        FreeDatabaseHandle(handle);
        Citadel::PAL::String::Copy(resp->errorMessage, EngineStatusToText(openStatus), MaxErrorLength);
        return;
    }
    databases[handle].databasePtr = db;
#endif

    resp->success = true;
    resp->handle = handle;
    activeConnections = databaseCount;
}

void QCSQLService::HandleCloseDatabase(const CloseDatabaseRequest* req,
                                        CloseDatabaseResponse* resp) {
    LogInfo("CloseDatabase request");

    resp->type = MessageType::Response;
    resp->success = false;
    Citadel::PAL::String::Copy(resp->errorMessage, "", MaxErrorLength);

    if (!running) {
        Citadel::PAL::String::Copy(resp->errorMessage, "Service not running", MaxErrorLength);
        return;
    }

    const uint32_t handle = req ? req->handle : kInvalidHandle;
    if (!ValidateHandle(handle)) {
        Citadel::PAL::String::Copy(resp->errorMessage, "Invalid database handle", MaxErrorLength);
        return;
    }

    FreeDatabaseHandle(handle);
    activeConnections = databaseCount;
    resp->success = true;
}

void QCSQLService::HandleExecuteSQL(const ExecuteSQLRequest* req,
                                     ExecuteSQLResponse* resp) {
    LogInfo("ExecuteSQL request");

    resp->type = MessageType::Response;
    resp->success = false;
    resp->resultLength = 0;
    resp->rowsAffected = 0;
    Citadel::PAL::String::Copy(resp->result, "", MaxResultLength);
    Citadel::PAL::String::Copy(resp->errorMessage, "", MaxErrorLength);

    if (!running) {
        Citadel::PAL::String::Copy(resp->errorMessage, "Service not running", MaxErrorLength);
        return;
    }

    if (!req || !ValidateHandle(req->handle)) {
        Citadel::PAL::String::Copy(resp->errorMessage, "Invalid database handle", MaxErrorLength);
        return;
    }

#if defined(CITADEL_QCSQL_USE_QCQL_ENGINE)
    if (!databases[req->handle].databasePtr) {
        Citadel::PAL::String::Copy(resp->errorMessage, "Database handle is not engine-bound", MaxErrorLength);
        return;
    }
#endif

#if defined(CITADEL_QCSQL_USE_CQL_ENGINE)
    if (!databases[req->handle].databasePtr) {
        Citadel::PAL::String::Copy(resp->errorMessage, "Database handle is not CQL-bound", MaxErrorLength);
        return;
    }

    auto* db = static_cast<CQL::Database*>(databases[req->handle].databasePtr);
    std::string query(req->query, req->queryLength);
    const QueryKind kind = DetectQueryKind(query);

    // UX compatibility aliases for shell-style introspection commands.
    if (kind == QueryKind::ShowTables) {
        query = "DUMP TABLES_LOADED";
    }

    const std::string result = db->ExecuteQuery(query);

    if (StartsWithIgnoreCase(result.c_str(), "error:")) {
        resp->success = false;
        resp->rowsAffected = 0;
        Citadel::PAL::String::Copy(resp->errorMessage, result.c_str(), MaxErrorLength);
        Citadel::PAL::String::Copy(resp->result, "", MaxResultLength);
        resp->resultLength = 0;
        return;
    }

    // Deterministic-by-kind rowsAffected policy for API consumers.
    // SELECT keeps discovered row count; DDL/DUMP return 0.
    // DML uses discovered count when present, otherwise defaults to 1 on success.
    const uint32_t firstCount = ExtractFirstUnsigned(result);
    switch (kind) {
        case QueryKind::Select:
            resp->rowsAffected = firstCount;
            break;
        case QueryKind::Insert:
        case QueryKind::Update:
        case QueryKind::Delete:
            resp->rowsAffected = (firstCount > 0) ? firstCount : 1;
            break;
        case QueryKind::Create:
        case QueryKind::Drop:
        case QueryKind::Alter:
        case QueryKind::Dump:
        case QueryKind::ShowTables:
        case QueryKind::Unknown:
        default:
            resp->rowsAffected = 0;
            break;
    }

    Citadel::PAL::String::Copy(resp->result, result.c_str(), MaxResultLength);
    resp->resultLength = static_cast<uint32_t>(Citadel::PAL::String::Length(resp->result));
    resp->success = true;
    return;
#endif

#if defined(CITADEL_QCSQL_USE_QCQL_ENGINE)
    auto* db = static_cast<QCQL::Database*>(databases[req->handle].databasePtr);
    QCQLEngineAdapter::Execute(db, req, resp);
    return;
#endif

    if (req->queryLength == 0 || req->query[0] == '\0') {
        Citadel::PAL::String::Copy(resp->errorMessage, "Query is empty", MaxErrorLength);
        return;
    }
    
    // STUB: Echo query back as "executed"
    const char* stubResult = "STUB: Query would execute here";
    Citadel::PAL::String::Copy(resp->result, stubResult, MaxResultLength);
    resp->resultLength = static_cast<uint32_t>(Citadel::PAL::String::Length(stubResult));
    resp->success = true;
}

void QCSQLService::HandleGetVersion(const GetVersionRequest* req,
                                     GetVersionResponse* resp) {
    LogInfo("GetVersion request");

    resp->type = MessageType::Response;
    Citadel::PAL::String::Copy(resp->version, config.version, sizeof(resp->version));
}

void QCSQLService::HandleGetStatus(const GetStatusRequest* req,
                                    GetStatusResponse* resp) {
    LogInfo("GetStatus request");
    
    // STUB: Return current service status
    resp->type = MessageType::Response;
    resp->running = running;
    resp->activeConnections = activeConnections;
    resp->totalQueries = static_cast<uint32_t>(totalQueries);
    resp->uptime = uptime;
    resp->storageMode = storageMode;
    Citadel::PAL::String::Copy(resp->storageBasePath, storageBasePath, MaxPathLength);
    Citadel::PAL::String::Copy(resp->statusMessage, statusMessage, MaxErrorLength);
}

void QCSQLService::HandleGetDatabaseInfo(const GetDatabaseInfoRequest* req,
                                          GetDatabaseInfoResponse* resp) {
    LogInfo("GetDatabaseInfo request");

    resp->type = MessageType::Response;
    resp->success = false;
    Citadel::PAL::String::Copy(resp->path, "", MaxPathLength);
    resp->tableCount = 0;
    resp->totalSize = 0;
    Citadel::PAL::String::Copy(resp->errorMessage, "", MaxErrorLength);

    if (!running) {
        Citadel::PAL::String::Copy(resp->errorMessage, "Service not running", MaxErrorLength);
        return;
    }

    const uint32_t handle = req ? req->handle : kInvalidHandle;
    if (!ValidateHandle(handle)) {
        Citadel::PAL::String::Copy(resp->errorMessage, "Invalid database handle", MaxErrorLength);
        return;
    }

    Citadel::PAL::String::Copy(resp->path, databases[handle].path, MaxPathLength);
#if defined(CITADEL_QCSQL_USE_QCQL_ENGINE)
    auto* db = static_cast<QCQL::Database*>(databases[handle].databasePtr);
    if (db) {
        resp->tableCount = static_cast<uint32_t>(db->tables.size());
    }
#endif
#if defined(CITADEL_QCSQL_USE_CQL_ENGINE)
    auto* db = static_cast<CQL::Database*>(databases[handle].databasePtr);
    if (db) {
        resp->tableCount = static_cast<uint32_t>(db->GetTables().size());
    }
#endif
    resp->success = true;
}

// ============================================================================
// Helper Methods
// ============================================================================

uint32_t QCSQLService::AllocateDatabaseHandle() {
    // Find first free handle slot.
    for (uint32_t i = 0; i < config.maxDatabases; ++i) {
        if (!databases[i].active) {
            databases[i].active = true;
            databases[i].path[0] = '\0';
            databases[i].databasePtr = nullptr;
            databases[i].refCount = 1;
            databaseCount++;
            return i;
        }
    }
    return kInvalidHandle;
}

void QCSQLService::FreeDatabaseHandle(uint32_t handle) {
    // Mark handle as free.
    if (handle < config.maxDatabases && databases[handle].active) {
#if defined(CITADEL_QCSQL_USE_CQL_ENGINE)
        auto* db = static_cast<CQL::Database*>(databases[handle].databasePtr);
        if (db) {
            db->Close();
            delete db;
        }
#endif
#if defined(CITADEL_QCSQL_USE_QCQL_ENGINE)
        auto* db = static_cast<QCQL::Database*>(databases[handle].databasePtr);
        if (db) {
            (void)QCQL::Engine::instance().closeDatabase(*db);
            delete db;
        }
#endif
        databases[handle].active = false;
        databases[handle].path[0] = '\0';
        databases[handle].databasePtr = nullptr;
        databases[handle].refCount = 0;
        if (databaseCount > 0) {
            databaseCount--;
        }
    }
}

bool QCSQLService::ValidateHandle(uint32_t handle) const {
    // Check if handle is valid and active.
    return (handle < config.maxDatabases) && databases[handle].active;
}

bool QCSQLService::ResolveDatabasePath(const char* requestedPath, char* outPath, size_t outLen) const {
    if (!requestedPath || requestedPath[0] == '\0' || !outPath || outLen == 0) {
        return false;
    }

    outPath[0] = '\0';

    if (IsAbsolutePath(requestedPath)) {
        if (std::strlen(requestedPath) >= outLen) {
            return false;
        }
        Citadel::PAL::String::Copy(outPath, requestedPath, outLen);
        return true;
    }

    if (storageMode == StorageMode::EphemeralMemory) {
        if (std::strlen(requestedPath) >= outLen) {
            return false;
        }
        Citadel::PAL::String::Copy(outPath, requestedPath, outLen);
        return true;
    }

    if (storageBasePath[0] == '\0') {
        return false;
    }

    const size_t baseLen = std::strlen(storageBasePath);
    const size_t reqLen = std::strlen(requestedPath);
    const bool needsSlash = (baseLen > 0 && storageBasePath[baseLen - 1] != '/');
    const size_t total = baseLen + (needsSlash ? 1 : 0) + reqLen;
    if (total >= outLen) {
        return false;
    }

    Citadel::PAL::String::Copy(outPath, storageBasePath, outLen);
    if (needsSlash) {
        const size_t pos = std::strlen(outPath);
        outPath[pos] = '/';
        outPath[pos + 1] = '\0';
    }

    char* writePos = outPath + std::strlen(outPath);
    std::strncpy(writePos, requestedPath, outLen - std::strlen(outPath) - 1);
    outPath[outLen - 1] = '\0';
    return true;
}

bool QCSQLService::PrepareStorageBase(const char* path, StorageMode mode, const char* successMessage) {
    if (!path || path[0] == '\0') {
        return false;
    }

    if (!Citadel::PAL::FileHandle::EnsureDirectory(path)) {
        return false;
    }

    if (!Citadel::PAL::FileHandle::Exists(path)) {
        return false;
    }

    storageMode = mode;
    Citadel::PAL::String::Copy(storageBasePath, path, MaxPathLength);
    Citadel::PAL::String::Copy(statusMessage, successMessage, MaxErrorLength);
    return true;
}

void QCSQLService::SelectStorageBackend() {
    storageMode = StorageMode::Unknown;
    storageBasePath[0] = '\0';

    if (config.preferredSystemBasePath && Citadel::PAL::FileHandle::Exists("/system")) {
        if (PrepareStorageBase(config.preferredSystemBasePath,
                               StorageMode::PersistentSystem,
                               "Storage backend: persistent /system volume")) {
            return;
        }
    }

    if (config.allowSharedFallback && config.preferredSharedBasePath && Citadel::PAL::FileHandle::Exists("/shared")) {
        if (PrepareStorageBase(config.preferredSharedBasePath,
                               StorageMode::SharedHost,
                               "Storage backend: /shared host-mapped volume fallback")) {
            return;
        }
    }

    if (config.allowEphemeralFallback) {
        storageMode = StorageMode::EphemeralMemory;
        Citadel::PAL::String::Copy(storageBasePath, "(in-memory)", MaxPathLength);
        Citadel::PAL::String::Copy(statusMessage, "Storage backend: ephemeral in-memory fallback (persistent roots unavailable)", MaxErrorLength);
        return;
    }

    Citadel::PAL::String::Copy(statusMessage, "No usable storage backend found (and ephemeral fallback disabled)", MaxErrorLength);
}

// ============================================================================
// Logging (STUB - routes to CitadelPAL console)
// ============================================================================

void QCSQLService::LogInfo(const char* message) {
    if (config.enableDebugLogging) {
        Citadel::PAL::Console::Write("[QCSQL] INFO: ");
        Citadel::PAL::Console::WriteLine(message);
    }
}

void QCSQLService::LogError(const char* message) {
    Citadel::PAL::Console::Write("[QCSQL] ERROR: ");
    Citadel::PAL::Console::WriteError(message);
}

// ============================================================================
// Service Registration (Citadel Integration Point)
// ============================================================================

// Global service instance (STUB - would be managed by service registry)
static QCSQLService* g_QCSQLService = nullptr;

extern "C" QCSQLService* RegisterQCSQLService() {
    // STUB: Create and initialize service
    if (!g_QCSQLService) {
        g_QCSQLService = new QCSQLService();
        
        ServiceConfig config;
        config.serviceName = "QCSQL";
        config.version = "1.0.0-Citadel-Stub";
        config.maxDatabases = 16;
        config.enableDebugLogging = true;
        
        if (!g_QCSQLService->Initialize(config)) {
            delete g_QCSQLService;
            g_QCSQLService = nullptr;
            return nullptr;
        }
        
        // STUB: Would register with QKServiceRegistry here
        // QK::Svc::Registry::Register("QCSQL", g_QCSQLService->HandleMessage);
    }
    
    return g_QCSQLService;
}

} // namespace Svc
} // namespace QCQL
