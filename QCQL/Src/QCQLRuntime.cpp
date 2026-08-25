// QCQL Runtime - Canonical handle-based execution surface
// Namespace: QCQL::Runtime

#include "QCQLRuntime.h"

#include "QCString.h"

namespace QCQL
{
    namespace Runtime
    {
        namespace
        {
            struct RuntimeSlot
            {
                bool active = false;
                bool enforceProcessBinding = false;
                bool enforceTablePermissions = false;
                QC::u32 ownerProcessId = 0;
                Database database = {};
                QC::Vector<TablePermission> tablePermissions;
                SchemaIntegrityReport integrityReport = {};
                char path[192] = {};
            };

            static constexpr QC::usize kMaxRuntimeSlots = 8;
            static RuntimeSlot s_slots[kMaxRuntimeSlots] = {};

            static const char *skipSpaces(const char *text)
            {
                while (text && (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n'))
                    ++text;
                return text;
            }

            static bool isSpace(char ch)
            {
                return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
            }

            static bool startsWithIgnoreCase(const char *text, const char *prefix)
            {
                if (!text || !prefix)
                    return false;

                while (*prefix)
                {
                    if (*text == '\0')
                        return false;
                    char a = *text;
                    char b = *prefix;
                    if (a >= 'A' && a <= 'Z')
                        a = static_cast<char>(a - 'A' + 'a');
                    if (b >= 'A' && b <= 'Z')
                        b = static_cast<char>(b - 'A' + 'a');
                    if (a != b)
                        return false;
                    ++text;
                    ++prefix;
                }
                return true;
            }

            static bool equalsIgnoreCase(const char *a, const char *b)
            {
                if (!a || !b)
                    return false;
                while (*a && *b)
                {
                    char ca = *a;
                    char cb = *b;
                    if (ca >= 'A' && ca <= 'Z')
                        ca = static_cast<char>(ca - 'A' + 'a');
                    if (cb >= 'A' && cb <= 'Z')
                        cb = static_cast<char>(cb - 'A' + 'a');
                    if (ca != cb)
                        return false;
                    ++a;
                    ++b;
                }
                return *a == '\0' && *b == '\0';
            }

            static bool parseU64(const char *text, QC::u64 &out)
            {
                if (!text)
                    return false;
                text = skipSpaces(text);
                if (!text || *text < '0' || *text > '9')
                    return false;

                QC::u64 value = 0;
                while (*text >= '0' && *text <= '9')
                {
                    value = (value * 10) + static_cast<QC::u64>(*text - '0');
                    ++text;
                }
                out = value;
                return true;
            }

            static bool readIdentifier(const char *&p, char *out, QC::usize outSize)
            {
                if (!out || outSize == 0)
                    return false;
                QC::String::memset(out, 0, outSize);

                p = p ? skipSpaces(p) : nullptr;
                if (!p)
                    return false;

                const char first = *p;
                const bool firstValid = (first >= 'A' && first <= 'Z') ||
                                        (first >= 'a' && first <= 'z') ||
                                        first == '_';
                if (!firstValid)
                    return false;

                QC::usize i = 0;
                while (*p)
                {
                    const char ch = *p;
                    const bool valid = (ch >= 'A' && ch <= 'Z') ||
                                       (ch >= 'a' && ch <= 'z') ||
                                       (ch >= '0' && ch <= '9') ||
                                       ch == '_';
                    if (!valid)
                        break;
                    if (i + 1 >= outSize)
                        return false;
                    out[i++] = *p++;
                }
                out[i] = '\0';
                return i > 0;
            }

            static bool consumeChar(const char *&p, char token)
            {
                p = p ? skipSpaces(p) : nullptr;
                if (!p || *p != token)
                    return false;
                ++p;
                return true;
            }

            static bool consumeKeyword(const char *&p, const char *keyword)
            {
                p = p ? skipSpaces(p) : nullptr;
                if (!p || !keyword)
                    return false;

                const char *cursor = p;
                while (*keyword)
                {
                    if (*cursor == '\0')
                        return false;
                    char a = *cursor;
                    char b = *keyword;
                    if (a >= 'A' && a <= 'Z')
                        a = static_cast<char>(a - 'A' + 'a');
                    if (b >= 'A' && b <= 'Z')
                        b = static_cast<char>(b - 'A' + 'a');
                    if (a != b)
                        return false;
                    ++cursor;
                    ++keyword;
                }

                if (*cursor != '\0' && !isSpace(*cursor) && *cursor != '(' && *cursor != ')' && *cursor != ',')
                    return false;

                p = cursor;
                return true;
            }

            static bool parseColumnType(const char *&p, ColumnType &outType)
            {
                char typeName[32];
                if (!readIdentifier(p, typeName, sizeof(typeName)))
                    return false;

                if (equalsIgnoreCase(typeName, "text") ||
                    equalsIgnoreCase(typeName, "varchar") ||
                    equalsIgnoreCase(typeName, "char") ||
                    equalsIgnoreCase(typeName, "string"))
                    outType = ColumnType::Text;
                else if (equalsIgnoreCase(typeName, "int") ||
                         equalsIgnoreCase(typeName, "integer") ||
                         equalsIgnoreCase(typeName, "tinyint") ||
                         equalsIgnoreCase(typeName, "smallint") ||
                         equalsIgnoreCase(typeName, "bigint"))
                    outType = ColumnType::Int;
                else if (equalsIgnoreCase(typeName, "bool") ||
                         equalsIgnoreCase(typeName, "boolean"))
                    outType = ColumnType::Bool;
                else if (equalsIgnoreCase(typeName, "datetime") ||
                         equalsIgnoreCase(typeName, "timestamp"))
                    outType = ColumnType::DateTime;
                else
                    return false;

                p = p ? skipSpaces(p) : nullptr;
                if (p && *p == '(')
                {
                    ++p;
                    while (*p && *p != ')')
                        ++p;
                    if (*p != ')')
                        return false;
                    ++p;
                }

                return true;
            }

            static bool parseCreateTableDefinition(const char *definition, TableSchema &outSchema)
            {
                const char *p = definition ? skipSpaces(definition) : nullptr;
                if (!p)
                    return false;

                outSchema = TableSchema{};
                if (!readIdentifier(p, outSchema.tableName, sizeof(outSchema.tableName)))
                    return false;
                if (!consumeChar(p, '('))
                    return false;

                QC::u32 pkCount = 0;
                while (true)
                {
                    Column column{};
                    if (!readIdentifier(p, column.name, sizeof(column.name)))
                        return false;
                    if (!parseColumnType(p, column.type))
                        return false;

                    const char *pkCursor = p;
                    if (consumeKeyword(pkCursor, "PRIMARY"))
                    {
                        if (!consumeKeyword(pkCursor, "KEY"))
                            return false;
                        column.isPrimaryKey = true;
                        p = pkCursor;
                        outSchema.primaryKeyIndex = static_cast<QC::u32>(outSchema.columns.size());
                        ++pkCount;
                    }

                    // REFERENCES <parentTable>(<parentColumn>) [ON DELETE RESTRICT] [ON UPDATE RESTRICT]
                    const char *refCursor = p;
                    if (consumeKeyword(refCursor, "REFERENCES"))
                    {
                        if (outSchema.foreignKeys.size() >= kMaxForeignKeysPerTable)
                            return false;

                        ForeignKey fk{};
                        QC::String::strncpy(fk.columnName, column.name, sizeof(fk.columnName) - 1);

                        if (!readIdentifier(refCursor, fk.referencedTable, sizeof(fk.referencedTable)))
                            return false;
                        if (!consumeChar(refCursor, '('))
                            return false;
                        if (!readIdentifier(refCursor, fk.referencedColumn, sizeof(fk.referencedColumn)))
                            return false;
                        if (!consumeChar(refCursor, ')'))
                            return false;

                        // Optional ON DELETE / ON UPDATE clauses — only RESTRICT is supported.
                        for (int clause = 0; clause < 2; ++clause)
                        {
                            const char *onCursor = refCursor;
                            if (!consumeKeyword(onCursor, "ON"))
                                break;
                            bool isDelete = false;
                            if (consumeKeyword(onCursor, "DELETE"))
                                isDelete = true;
                            else if (consumeKeyword(onCursor, "UPDATE"))
                                isDelete = false;
                            else
                                break;
                            if (!consumeKeyword(onCursor, "RESTRICT"))
                                return false;
                            // ReferentialAction::Restrict is the only supported policy — already the default.
                            refCursor = onCursor;
                        }

                        fk.onDelete = ReferentialAction::Restrict;
                        fk.onUpdate = ReferentialAction::Restrict;
                        outSchema.foreignKeys.push_back(static_cast<ForeignKey &&>(fk));
                        p = refCursor;
                    }

                    outSchema.columns.push_back(static_cast<Column &&>(column));
                    if (outSchema.columns.size() > kMaxColumnsPerTable)
                        return false;

                    p = p ? skipSpaces(p) : nullptr;
                    if (!p)
                        return false;
                    if (*p == ',')
                    {
                        ++p;
                        continue;
                    }
                    if (*p == ')')
                    {
                        ++p;
                        break;
                    }
                    return false;
                }

                p = p ? skipSpaces(p) : nullptr;
                if (!p || *p != '\0')
                    return false;

                return !outSchema.columns.empty() && pkCount == 1;
            }

            static RuntimeSlot *slotFromHandle(const DbHandle &handle)
            {
                if (handle.dbId == 0)
                    return nullptr;

                const QC::u64 idx64 = handle.dbId - 1;
                if (idx64 >= kMaxRuntimeSlots)
                    return nullptr;
                RuntimeSlot &slot = s_slots[static_cast<QC::usize>(idx64)];
                if (!slot.active)
                    return nullptr;
                return &slot;
            }

            enum class CapabilityOp
            {
                Read,
                Write,
                Delete,
                Admin
            };

            static bool permissionMatchesTable(const TablePermission &perm, const char *tableName)
            {
                if (perm.tableName[0] == '\0')
                    return false;
                if (perm.tableName[0] == '*' && perm.tableName[1] == '\0')
                    return true;
                return equalsIgnoreCase(perm.tableName, tableName ? tableName : "");
            }

            static bool hasPermission(const RuntimeSlot &slot, const char *tableName, CapabilityOp op)
            {
                if (!slot.enforceTablePermissions)
                    return true;

                const TablePermission *wildcard = nullptr;
                for (QC::usize i = 0; i < slot.tablePermissions.size(); ++i)
                {
                    const TablePermission &perm = slot.tablePermissions[i];
                    if (perm.tableName[0] == '*' && perm.tableName[1] == '\0')
                        wildcard = &perm;
                    if (!permissionMatchesTable(perm, tableName))
                        continue;

                    switch (op)
                    {
                    case CapabilityOp::Read: return perm.canRead;
                    case CapabilityOp::Write: return perm.canWrite;
                    case CapabilityOp::Delete: return perm.canDelete;
                    case CapabilityOp::Admin: return perm.canAdmin;
                    }
                }

                if (wildcard)
                {
                    switch (op)
                    {
                    case CapabilityOp::Read: return wildcard->canRead;
                    case CapabilityOp::Write: return wildcard->canWrite;
                    case CapabilityOp::Delete: return wildcard->canDelete;
                    case CapabilityOp::Admin: return wildcard->canAdmin;
                    }
                }

                return false;
            }

            static bool handleBindingValid(const RuntimeSlot &slot, const DbHandle &handle)
            {
                if (!slot.enforceProcessBinding)
                    return true;
                return handle.callerProcessId != 0 && handle.callerProcessId == slot.ownerProcessId;
            }

            static void appendText(char *dst, QC::usize cap, QC::usize &len, const char *text)
            {
                if (!dst || cap == 0 || !text)
                    return;
                while (*text && len + 1 < cap)
                    dst[len++] = *text++;
                dst[len] = '\0';
            }

            static void appendUnsigned(char *dst, QC::usize cap, QC::usize &len, QC::u64 value)
            {
                char tmp[32];
                QC::usize ti = 0;
                if (value == 0)
                {
                    appendText(dst, cap, len, "0");
                    return;
                }
                while (value > 0 && ti < sizeof(tmp))
                {
                    tmp[ti++] = static_cast<char>('0' + (value % 10));
                    value /= 10;
                }
                while (ti > 0)
                {
                    const char c[2] = {tmp[--ti], '\0'};
                    appendText(dst, cap, len, c);
                }
            }

            static void appendLine(QueryResult &outResult, const char *line)
            {
                QC::usize len = QC::String::strlen(outResult.output);
                appendText(outResult.output, sizeof(outResult.output), len, line ? line : "");
                appendText(outResult.output, sizeof(outResult.output), len, "\n");
            }

            static void setFailure(QueryResult &outResult, Status st, const char *message)
            {
                outResult.status = st;
                outResult.rowsAffected = 0;
                QC::String::memset(outResult.output, 0, sizeof(outResult.output));
                QC::String::memset(outResult.diagnostic, 0, sizeof(outResult.diagnostic));
                if (message)
                    QC::String::strncpy(outResult.diagnostic, message, sizeof(outResult.diagnostic) - 1);
            }

            static void appendCell(char *line, QC::usize cap, QC::usize &len, const Cell &cell)
            {
                if (cell.type == ColumnType::Text)
                {
                    for (QC::usize i = 0; i < cell.bytes.size(); ++i)
                    {
                        char ch = static_cast<char>(cell.bytes[i]);
                        if (ch == '\0')
                            continue;
                        if (ch < 32 || ch > 126)
                            ch = ' ';
                        if (len + 1 >= cap)
                            break;
                        line[len++] = ch;
                    }
                    line[len] = '\0';
                    return;
                }

                if (cell.type == ColumnType::Bool)
                {
                    appendText(line, cap, len, (!cell.bytes.empty() && cell.bytes[0] != 0) ? "true" : "false");
                    return;
                }

                if (cell.type == ColumnType::Int || cell.type == ColumnType::DateTime)
                {
                    QC::u64 value = 0;
                    const QC::usize maxBytes = cell.bytes.size() < 8 ? cell.bytes.size() : static_cast<QC::usize>(8);
                    for (QC::usize i = 0; i < maxBytes; ++i)
                        value |= (static_cast<QC::u64>(cell.bytes[i]) << (8 * i));
                    if (cell.type == ColumnType::DateTime)
                        appendText(line, cap, len, "ts=");
                    appendUnsigned(line, cap, len, value);
                    return;
                }

                appendText(line, cap, len, "<bin>");
            }
        }

        const char *statusName(Status st)
        {
            switch (st)
            {
            case Status::Success:
                return "Success";
            case Status::Error:
                return "Error";
            case Status::InvalidParam:
                return "InvalidParam";
            case Status::NotFound:
                return "NotFound";
            case Status::PermissionDenied:
                return "PermissionDenied";
            case Status::AlreadyExists:
                return "AlreadyExists";
            case Status::OutOfMemory:
                return "OutOfMemory";
            case Status::NotSupported:
                return "NotSupported";
            case Status::Corrupt:
                return "Corrupt";
            case Status::ConstraintViolation:
                return "ConstraintViolation";
            }
            return "Unknown";
        }

        Status openHandle(const char *path,
                          DbHandle &outHandle,
                          bool createIfMissing,
                          const HandleOpenOptions *options)
        {
            if (!path || path[0] == '\0')
                return Status::InvalidParam;

            Engine::instance().initialize();

            QC::usize slotIndex = kMaxRuntimeSlots;
            for (QC::usize i = 0; i < kMaxRuntimeSlots; ++i)
            {
                if (!s_slots[i].active)
                {
                    slotIndex = i;
                    break;
                }
            }
            if (slotIndex == kMaxRuntimeSlots)
                return Status::OutOfMemory;

            RuntimeSlot &slot = s_slots[slotIndex];
            slot.database = Database{};
            slot.tablePermissions.clear();
            slot.ownerProcessId = 0;
            slot.enforceProcessBinding = false;
            slot.enforceTablePermissions = false;

            Status st = createIfMissing
                            ? Engine::instance().createDatabase(path, slot.database)
                            : Engine::instance().openDatabase(path, slot.database);
            if (st != Status::Success)
            {
                slot = RuntimeSlot{};
                return st;
            }

            slot.active = true;
            QC::String::strncpy(slot.path, path, sizeof(slot.path) - 1);

            // Run schema integrity check; results are stored in the slot for later inspection.
            (void)Engine::instance().validateSchemaIntegrity(slot.database, slot.integrityReport);

            if (options)
            {
                slot.ownerProcessId = options->callerProcessId;
                slot.enforceProcessBinding = options->enforceProcessBinding;
                slot.enforceTablePermissions = options->enforceTablePermissions;

                if (options->tablePermissions && options->tablePermissionCount > 0)
                {
                    slot.tablePermissions.reserve(options->tablePermissionCount);
                    for (QC::usize i = 0; i < options->tablePermissionCount; ++i)
                        slot.tablePermissions.push_back(options->tablePermissions[i]);
                }

                if (slot.enforceProcessBinding && slot.ownerProcessId == 0)
                {
                    (void)Engine::instance().closeDatabase(slot.database);
                    slot = RuntimeSlot{};
                    return Status::InvalidParam;
                }

                if (slot.enforceTablePermissions && slot.tablePermissions.empty())
                {
                    (void)Engine::instance().closeDatabase(slot.database);
                    slot = RuntimeSlot{};
                    return Status::InvalidParam;
                }
            }

            outHandle = DbHandle{};
            outHandle.dbId = static_cast<QC::u64>(slotIndex + 1);
            outHandle.callerProcessId = options ? options->callerProcessId : 0;
            if (options && options->tablePermissions && options->tablePermissionCount > 0)
            {
                outHandle.tablePermissions.reserve(options->tablePermissionCount);
                for (QC::usize i = 0; i < options->tablePermissionCount; ++i)
                    outHandle.tablePermissions.push_back(options->tablePermissions[i]);
            }
            return Status::Success;
        }

        Status closeHandle(DbHandle &ioHandle)
        {
            RuntimeSlot *slot = slotFromHandle(ioHandle);
            if (!slot)
                return Status::NotFound;

            const Status st = Engine::instance().closeDatabase(slot->database);
            *slot = RuntimeSlot{};
            ioHandle = DbHandle{};
            return st;
        }

        Status insertRowByName(DbHandle &handle,
                               const char *tableName,
                               const Row &row,
                               QC::u32 *outPageId,
                               QC::u16 *outRowOffset)
        {
            RuntimeSlot *slot = slotFromHandle(handle);
            if (!slot)
                return Status::NotFound;
            if (!handleBindingValid(*slot, handle))
                return Status::PermissionDenied;
            if (!tableName || tableName[0] == '\0')
                return Status::InvalidParam;
            if (!hasPermission(*slot, tableName, CapabilityOp::Write))
                return Status::PermissionDenied;

            return Engine::instance().insertRowByName(slot->database,
                                                      tableName,
                                                      row,
                                                      outPageId,
                                                      outRowOffset);
        }

        Status borrowDatabase(DbHandle &handle, Database *&outDatabase)
        {
            RuntimeSlot *slot = slotFromHandle(handle);
            if (!slot)
                return Status::NotFound;
            if (!handleBindingValid(*slot, handle))
                return Status::PermissionDenied;
            outDatabase = &slot->database;
            return Status::Success;
        }

        Status getIntegrityReport(DbHandle &handle, SchemaIntegrityReport &outReport)
        {
            RuntimeSlot *slot = slotFromHandle(handle);
            if (!slot)
                return Status::NotFound;
            outReport = slot->integrityReport;
            return Status::Success;
        }

        Status execute(DbHandle &handle, const char *query, QueryResult &outResult)
        {
            QC::String::memset(&outResult, 0, sizeof(outResult));

            RuntimeSlot *slot = slotFromHandle(handle);
            if (!slot)
            {
                setFailure(outResult, Status::NotFound, "handle_not_open");
                return outResult.status;
            }
            if (!handleBindingValid(*slot, handle))
            {
                setFailure(outResult, Status::PermissionDenied, "process_binding_mismatch");
                return outResult.status;
            }

            const char *q = query ? skipSpaces(query) : nullptr;
            if (!q || *q == '\0')
            {
                setFailure(outResult, Status::InvalidParam, "query_empty");
                return outResult.status;
            }

            // SHOW TABLES
            if (equalsIgnoreCase(q, "SHOW TABLES") || equalsIgnoreCase(q, "DUMP TABLES_LOADED"))
            {
                if (!hasPermission(*slot, "*", CapabilityOp::Read))
                {
                    setFailure(outResult, Status::PermissionDenied, "permission_denied:read:*");
                    return outResult.status;
                }
                for (QC::usize i = 0; i < slot->database.tables.size(); ++i)
                {
                    appendLine(outResult, slot->database.tables[i].name[0] ? slot->database.tables[i].name : "(unnamed)");
                    outResult.rowsAffected++;
                }
                if (slot->database.tables.empty())
                    appendLine(outResult, "(no tables)");

                outResult.status = Status::Success;
                return outResult.status;
            }

            // CREATE TABLE ...
            if (startsWithIgnoreCase(q, "CREATE TABLE "))
            {
                const char *nameCursor = skipSpaces(q + 13);
                char tableName[64];
                QC::String::memset(tableName, 0, sizeof(tableName));
                if (nameCursor)
                    (void)readIdentifier(nameCursor, tableName, sizeof(tableName));
                if (!hasPermission(*slot, tableName[0] ? tableName : "*", CapabilityOp::Admin))
                {
                    setFailure(outResult, Status::PermissionDenied, "permission_denied:admin:create_table");
                    return outResult.status;
                }

                TableSchema schema{};
                if (!parseCreateTableDefinition(q + 13, schema))
                {
                    setFailure(outResult, Status::InvalidParam, "create_table_parse_failed");
                    return outResult.status;
                }

                const Status st = Engine::instance().createTable(slot->database, schema);
                if (st != Status::Success)
                {
                    setFailure(outResult, st, Engine::instance().lastDiagnostic());
                    return outResult.status;
                }

                outResult.status = Status::Success;
                appendLine(outResult, "ok");
                outResult.rowsAffected = 1;
                return outResult.status;
            }

            // DESCRIBE <table> and SHOW COLUMNS FROM <table>
            const bool describe = startsWithIgnoreCase(q, "DESCRIBE ");
            const bool showColumns = startsWithIgnoreCase(q, "SHOW COLUMNS FROM ");
            if (describe || showColumns)
            {
                const char *name = describe ? skipSpaces(q + 9) : skipSpaces(q + 18);
                char tableName[64];
                if (!readIdentifier(name, tableName, sizeof(tableName)))
                {
                    setFailure(outResult, Status::InvalidParam, "describe_table_missing");
                    return outResult.status;
                }

                const Table *table = nullptr;
                for (QC::usize i = 0; i < slot->database.tables.size(); ++i)
                {
                    if (equalsIgnoreCase(slot->database.tables[i].name, tableName))
                    {
                        table = &slot->database.tables[i];
                        break;
                    }
                }
                if (!table)
                {
                    setFailure(outResult, Status::NotFound, "table_not_found");
                    return outResult.status;
                }
                if (!hasPermission(*slot, tableName, CapabilityOp::Read))
                {
                    setFailure(outResult, Status::PermissionDenied, "permission_denied:read:table");
                    return outResult.status;
                }

                for (QC::usize i = 0; i < table->schema.columns.size(); ++i)
                {
                    const Column &col = table->schema.columns[i];
                    char line[256];
                    QC::String::memset(line, 0, sizeof(line));
                    QC::usize len = 0;
                    appendText(line, sizeof(line), len, col.name[0] ? col.name : "(unnamed)");
                    appendText(line, sizeof(line), len, " ");
                    appendText(line, sizeof(line), len, col.type == ColumnType::Text ? "text" :
                                                            col.type == ColumnType::Int ? "int" :
                                                            col.type == ColumnType::Bool ? "bool" : "datetime");
                    if (col.isPrimaryKey)
                        appendText(line, sizeof(line), len, " PRIMARY KEY");

                    // Annotate FK reference inline if this column is part of one.
                    for (QC::usize fki = 0; fki < table->schema.foreignKeys.size(); ++fki)
                    {
                        const ForeignKey &fk = table->schema.foreignKeys[fki];
                        if (!equalsIgnoreCase(fk.columnName, col.name))
                            continue;
                        appendText(line, sizeof(line), len, " REFERENCES ");
                        appendText(line, sizeof(line), len, fk.referencedTable);
                        appendText(line, sizeof(line), len, "(");
                        appendText(line, sizeof(line), len, fk.referencedColumn);
                        appendText(line, sizeof(line), len, ")");
                        break;
                    }

                    appendLine(outResult, line);
                    outResult.rowsAffected++;
                }

                // Emit FK relationship summary lines.
                for (QC::usize fki = 0; fki < table->schema.foreignKeys.size(); ++fki)
                {
                    const ForeignKey &fk = table->schema.foreignKeys[fki];
                    char line[256];
                    QC::String::memset(line, 0, sizeof(line));
                    QC::usize len = 0;
                    appendText(line, sizeof(line), len, "FK ");
                    appendText(line, sizeof(line), len, fk.columnName);
                    appendText(line, sizeof(line), len, " -> ");
                    appendText(line, sizeof(line), len, fk.referencedTable);
                    appendText(line, sizeof(line), len, "(");
                    appendText(line, sizeof(line), len, fk.referencedColumn);
                    appendText(line, sizeof(line), len, ") ON DELETE RESTRICT ON UPDATE RESTRICT");
                    appendLine(outResult, line);
                }

                outResult.status = Status::Success;
                return outResult.status;
            }

            // SELECT * FROM <table> [LIMIT N]
            if (startsWithIgnoreCase(q, "SELECT * FROM "))
            {
                const char *cursor = skipSpaces(q + 14);
                char tableName[64];
                if (!readIdentifier(cursor, tableName, sizeof(tableName)))
                {
                    setFailure(outResult, Status::InvalidParam, "select_table_missing");
                    return outResult.status;
                }

                const Table *table = nullptr;
                for (QC::usize i = 0; i < slot->database.tables.size(); ++i)
                {
                    if (equalsIgnoreCase(slot->database.tables[i].name, tableName))
                    {
                        table = &slot->database.tables[i];
                        break;
                    }
                }
                if (!table)
                {
                    setFailure(outResult, Status::NotFound, "table_not_found");
                    return outResult.status;
                }
                if (!hasPermission(*slot, tableName, CapabilityOp::Read))
                {
                    setFailure(outResult, Status::PermissionDenied, "permission_denied:read:table");
                    return outResult.status;
                }

                QC::u64 limit = 25;
                const char *tail = skipSpaces(cursor);
                if (tail && *tail && startsWithIgnoreCase(tail, "LIMIT "))
                {
                    const char *n = skipSpaces(tail + 6);
                    (void)parseU64(n, limit);
                }
                if (limit == 0)
                    limit = 25;

                QC::u64 emitted = 0;
                for (QC::usize p = 0; p < table->pages.size() && emitted < limit; ++p)
                {
                    Page page{};
                    if (Engine::instance().loadPage(slot->database, table->pages[p], page) != Status::Success)
                        continue;

                    for (QC::usize r = 0; r < page.rowOffsets.size() && emitted < limit; ++r)
                    {
                        Row row{};
                        if (Engine::instance().readRow(slot->database, page.header.pageId, page.rowOffsets[r], row) != Status::Success)
                            continue;
                        if (row.tombstone)
                            continue;

                        char line[384];
                        QC::String::memset(line, 0, sizeof(line));
                        QC::usize len = 0;
                        for (QC::usize c = 0; c < row.cells.size(); ++c)
                        {
                            if (c > 0)
                                appendText(line, sizeof(line), len, " | ");
                            appendCell(line, sizeof(line), len, row.cells[c]);
                        }

                        appendLine(outResult, line[0] ? line : "(empty row)");
                        ++emitted;
                    }
                }

                outResult.status = Status::Success;
                outResult.rowsAffected = static_cast<QC::u32>(emitted);
                return outResult.status;
            }

            // INSERT INTO <table> VALUES (<v1>, <v2>, ...)
            if (startsWithIgnoreCase(q, "INSERT INTO ") || startsWithIgnoreCase(q, "INSERT  INTO "))
            {
                const char *cursor = skipSpaces(q + 7); // past "INSERT"
                if (!consumeKeyword(cursor, "INTO"))
                {
                    setFailure(outResult, Status::InvalidParam, "insert_missing_into");
                    return outResult.status;
                }

                char tableName[64];
                if (!readIdentifier(cursor, tableName, sizeof(tableName)))
                {
                    setFailure(outResult, Status::InvalidParam, "insert_missing_table");
                    return outResult.status;
                }
                if (!hasPermission(*slot, tableName, CapabilityOp::Write))
                {
                    setFailure(outResult, Status::PermissionDenied, "permission_denied:write:table");
                    return outResult.status;
                }

                const Table *table = nullptr;
                for (QC::usize i = 0; i < slot->database.tables.size(); ++i)
                {
                    if (equalsIgnoreCase(slot->database.tables[i].name, tableName))
                    {
                        table = &slot->database.tables[i];
                        break;
                    }
                }
                if (!table)
                {
                    setFailure(outResult, Status::NotFound, "insert_table_not_found");
                    return outResult.status;
                }

                if (!consumeKeyword(cursor, "VALUES"))
                {
                    setFailure(outResult, Status::InvalidParam, "insert_missing_values");
                    return outResult.status;
                }
                if (!consumeChar(cursor, '('))
                {
                    setFailure(outResult, Status::InvalidParam, "insert_missing_lparen");
                    return outResult.status;
                }

                Row row{};
                for (QC::usize ci = 0; ci < table->schema.columns.size(); ++ci)
                {
                    cursor = cursor ? skipSpaces(cursor) : nullptr;
                    if (!cursor)
                    {
                        setFailure(outResult, Status::InvalidParam, "insert_values_truncated");
                        return outResult.status;
                    }

                    Cell cell{};
                    const ColumnType colType = table->schema.columns[ci].type;
                    if (*cursor == '\'' || *cursor == '"')
                    {
                        // Quoted string value.
                        const char quote = *cursor++;
                        cell.type = ColumnType::Text;
                        while (*cursor && *cursor != quote)
                        {
                            cell.bytes.push_back(static_cast<QC::u8>(*cursor));
                            ++cursor;
                        }
                        if (*cursor == quote)
                            ++cursor;
                    }
                    else if (colType == ColumnType::Bool ||
                             startsWithIgnoreCase(cursor, "true") ||
                             startsWithIgnoreCase(cursor, "false"))
                    {
                        cell.type = ColumnType::Bool;
                        QC::u8 bval = 0;
                        if (startsWithIgnoreCase(cursor, "true"))
                        {
                            bval = 1;
                            cursor += 4;
                        }
                        else if (startsWithIgnoreCase(cursor, "false"))
                        {
                            cursor += 5;
                        }
                        else
                        {
                            bval = (*cursor != '0') ? 1 : 0;
                            ++cursor;
                        }
                        cell.bytes.push_back(bval);
                    }
                    else if (*cursor == '-' || (*cursor >= '0' && *cursor <= '9'))
                    {
                        // Integer or datetime literal.
                        cell.type = (colType == ColumnType::DateTime) ? ColumnType::DateTime : ColumnType::Int;
                        bool negative = false;
                        if (*cursor == '-')
                        {
                            negative = true;
                            ++cursor;
                        }
                        QC::u64 uval = 0;
                        while (*cursor >= '0' && *cursor <= '9')
                        {
                            uval = uval * 10 + static_cast<QC::u64>(*cursor - '0');
                            ++cursor;
                        }
                        const QC::u64 stored = negative ? static_cast<QC::u64>(-static_cast<QC::i64>(uval)) : uval;
                        for (int bi = 0; bi < 8; ++bi)
                            cell.bytes.push_back(static_cast<QC::u8>((stored >> (8 * bi)) & 0xFF));
                    }
                    else if (*cursor == 'N' || *cursor == 'n')
                    {
                        // NULL literal → empty text cell.
                        if (startsWithIgnoreCase(cursor, "NULL"))
                        {
                            cursor += 4;
                            cell.type = ColumnType::Text;
                        }
                        else
                        {
                            setFailure(outResult, Status::InvalidParam, "insert_bad_value");
                            return outResult.status;
                        }
                    }
                    else
                    {
                        setFailure(outResult, Status::InvalidParam, "insert_bad_value");
                        return outResult.status;
                    }

                    row.cells.push_back(static_cast<Cell &&>(cell));

                    cursor = cursor ? skipSpaces(cursor) : nullptr;
                    if (!cursor)
                    {
                        setFailure(outResult, Status::InvalidParam, "insert_values_truncated");
                        return outResult.status;
                    }
                    if (ci + 1 < table->schema.columns.size())
                    {
                        if (*cursor != ',')
                        {
                            setFailure(outResult, Status::InvalidParam, "insert_values_separator");
                            return outResult.status;
                        }
                        ++cursor;
                    }
                }

                if (!consumeChar(cursor, ')'))
                {
                    setFailure(outResult, Status::InvalidParam, "insert_missing_rparen");
                    return outResult.status;
                }

                const Status insSt = Engine::instance().insertRowByName(slot->database, tableName, row);
                if (insSt != Status::Success)
                {
                    setFailure(outResult, insSt, Engine::instance().lastDiagnostic());
                    return outResult.status;
                }

                outResult.status = Status::Success;
                appendLine(outResult, "ok");
                outResult.rowsAffected = 1;
                return outResult.status;
            }

            // DELETE FROM <table> WHERE <pk_col> = <value>
            if (startsWithIgnoreCase(q, "DELETE FROM "))
            {
                const char *cursor = skipSpaces(q + 7); // past "DELETE"
                if (!consumeKeyword(cursor, "FROM"))
                {
                    setFailure(outResult, Status::InvalidParam, "delete_missing_from");
                    return outResult.status;
                }

                char tableName[64];
                if (!readIdentifier(cursor, tableName, sizeof(tableName)))
                {
                    setFailure(outResult, Status::InvalidParam, "delete_missing_table");
                    return outResult.status;
                }
                if (!hasPermission(*slot, tableName, CapabilityOp::Delete))
                {
                    setFailure(outResult, Status::PermissionDenied, "permission_denied:delete:table");
                    return outResult.status;
                }

                if (!consumeKeyword(cursor, "WHERE"))
                {
                    setFailure(outResult, Status::InvalidParam, "delete_missing_where");
                    return outResult.status;
                }

                char pkCol[64];
                if (!readIdentifier(cursor, pkCol, sizeof(pkCol)))
                {
                    setFailure(outResult, Status::InvalidParam, "delete_bad_where_col");
                    return outResult.status;
                }

                cursor = cursor ? skipSpaces(cursor) : nullptr;
                if (!cursor || *cursor != '=')
                {
                    setFailure(outResult, Status::InvalidParam, "delete_bad_where_eq");
                    return outResult.status;
                }
                ++cursor;
                cursor = cursor ? skipSpaces(cursor) : nullptr;

                // Build PK key bytes from the literal.
                QC::Vector<QC::u8> pkKey;
                if (cursor && (*cursor == '\'' || *cursor == '"'))
                {
                    const char q2 = *cursor++;
                    while (*cursor && *cursor != q2)
                    {
                        pkKey.push_back(static_cast<QC::u8>(*cursor));
                        ++cursor;
                    }
                    if (*cursor == q2)
                        ++cursor;
                }
                else if (cursor && (*cursor == '-' || (*cursor >= '0' && *cursor <= '9')))
                {
                    bool negative = false;
                    if (*cursor == '-') { negative = true; ++cursor; }
                    QC::u64 uval = 0;
                    while (*cursor >= '0' && *cursor <= '9')
                    {
                        uval = uval * 10 + static_cast<QC::u64>(*cursor - '0');
                        ++cursor;
                    }
                    const QC::u64 stored = negative ? static_cast<QC::u64>(-static_cast<QC::i64>(uval)) : uval;
                    for (int bi = 0; bi < 8; ++bi)
                        pkKey.push_back(static_cast<QC::u8>((stored >> (8 * bi)) & 0xFF));
                }
                else
                {
                    setFailure(outResult, Status::InvalidParam, "delete_bad_where_value");
                    return outResult.status;
                }

                const Status delSt = Engine::instance().removeRowByPrimaryKeyByName(slot->database, tableName, pkKey);
                if (delSt != Status::Success)
                {
                    setFailure(outResult, delSt, Engine::instance().lastDiagnostic());
                    return outResult.status;
                }

                outResult.status = Status::Success;
                appendLine(outResult, "ok");
                outResult.rowsAffected = 1;
                return outResult.status;
            }

            // UPDATE <table> SET <col> = <val> [, ...] WHERE <pk_col> = <pk_val>
            if (startsWithIgnoreCase(q, "UPDATE "))
            {
                const char *cursor = skipSpaces(q + 7);

                char tableName[64];
                if (!readIdentifier(cursor, tableName, sizeof(tableName)))
                {
                    setFailure(outResult, Status::InvalidParam, "update_missing_table");
                    return outResult.status;
                }
                if (!hasPermission(*slot, tableName, CapabilityOp::Write))
                {
                    setFailure(outResult, Status::PermissionDenied, "permission_denied:write:table");
                    return outResult.status;
                }
                if (!consumeKeyword(cursor, "SET"))
                {
                    setFailure(outResult, Status::InvalidParam, "update_missing_set");
                    return outResult.status;
                }

                const Table *table = nullptr;
                for (QC::usize i = 0; i < slot->database.tables.size(); ++i)
                {
                    if (equalsIgnoreCase(slot->database.tables[i].name, tableName))
                    {
                        table = &slot->database.tables[i];
                        break;
                    }
                }
                if (!table)
                {
                    setFailure(outResult, Status::NotFound, "update_table_not_found");
                    return outResult.status;
                }

                // Parse SET assignments into a column-value map.
                char setColNames[kMaxColumnsPerTable][64] = {};
                QC::Vector<QC::u8> setValues[kMaxColumnsPerTable];
                QC::usize setCount = 0;

                while (setCount < kMaxColumnsPerTable)
                {
                    char colName[64];
                    if (!readIdentifier(cursor, colName, sizeof(colName)))
                        break;

                    cursor = cursor ? skipSpaces(cursor) : nullptr;
                    if (!cursor || *cursor != '=')
                    {
                        setFailure(outResult, Status::InvalidParam, "update_bad_set_eq");
                        return outResult.status;
                    }
                    ++cursor;
                    cursor = cursor ? skipSpaces(cursor) : nullptr;

                    QC::Vector<QC::u8> val;
                    if (cursor && (*cursor == '\'' || *cursor == '"'))
                    {
                        const char q2 = *cursor++;
                        while (*cursor && *cursor != q2)
                        {
                            val.push_back(static_cast<QC::u8>(*cursor));
                            ++cursor;
                        }
                        if (*cursor == q2)
                            ++cursor;
                    }
                    else if (cursor && (*cursor == '-' || (*cursor >= '0' && *cursor <= '9')))
                    {
                        bool negative = false;
                        if (*cursor == '-') { negative = true; ++cursor; }
                        QC::u64 uval = 0;
                        while (*cursor >= '0' && *cursor <= '9')
                        {
                            uval = uval * 10 + static_cast<QC::u64>(*cursor - '0');
                            ++cursor;
                        }
                        const QC::u64 stored = negative ? static_cast<QC::u64>(-static_cast<QC::i64>(uval)) : uval;
                        for (int bi = 0; bi < 8; ++bi)
                            val.push_back(static_cast<QC::u8>((stored >> (8 * bi)) & 0xFF));
                    }
                    else if (cursor && startsWithIgnoreCase(cursor, "true"))
                    {
                        cursor += 4;
                        val.push_back(1);
                    }
                    else if (cursor && startsWithIgnoreCase(cursor, "false"))
                    {
                        cursor += 5;
                        val.push_back(0);
                    }
                    else
                    {
                        setFailure(outResult, Status::InvalidParam, "update_bad_set_value");
                        return outResult.status;
                    }

                    QC::String::strncpy(setColNames[setCount], colName, sizeof(setColNames[setCount]) - 1);
                    setValues[setCount] = static_cast<QC::Vector<QC::u8> &&>(val);
                    ++setCount;

                    cursor = cursor ? skipSpaces(cursor) : nullptr;
                    if (!cursor || *cursor != ',')
                        break;
                    ++cursor;
                }

                if (!consumeKeyword(cursor, "WHERE"))
                {
                    setFailure(outResult, Status::InvalidParam, "update_missing_where");
                    return outResult.status;
                }

                char pkCol[64];
                if (!readIdentifier(cursor, pkCol, sizeof(pkCol)))
                {
                    setFailure(outResult, Status::InvalidParam, "update_bad_where_col");
                    return outResult.status;
                }
                cursor = cursor ? skipSpaces(cursor) : nullptr;
                if (!cursor || *cursor != '=')
                {
                    setFailure(outResult, Status::InvalidParam, "update_bad_where_eq");
                    return outResult.status;
                }
                ++cursor;
                cursor = cursor ? skipSpaces(cursor) : nullptr;

                QC::Vector<QC::u8> pkKey;
                if (cursor && (*cursor == '\'' || *cursor == '"'))
                {
                    const char q2 = *cursor++;
                    while (*cursor && *cursor != q2)
                    {
                        pkKey.push_back(static_cast<QC::u8>(*cursor));
                        ++cursor;
                    }
                    if (*cursor == q2)
                        ++cursor;
                }
                else if (cursor && (*cursor == '-' || (*cursor >= '0' && *cursor <= '9')))
                {
                    bool negative = false;
                    if (*cursor == '-') { negative = true; ++cursor; }
                    QC::u64 uval = 0;
                    while (*cursor >= '0' && *cursor <= '9')
                    {
                        uval = uval * 10 + static_cast<QC::u64>(*cursor - '0');
                        ++cursor;
                    }
                    const QC::u64 stored = negative ? static_cast<QC::u64>(-static_cast<QC::i64>(uval)) : uval;
                    for (int bi = 0; bi < 8; ++bi)
                        pkKey.push_back(static_cast<QC::u8>((stored >> (8 * bi)) & 0xFF));
                }
                else
                {
                    setFailure(outResult, Status::InvalidParam, "update_bad_where_value");
                    return outResult.status;
                }

                // Fetch current row, apply SET changes, re-submit.
                const Table *liveTable = nullptr;
                for (QC::usize i = 0; i < slot->database.tables.size(); ++i)
                {
                    if (equalsIgnoreCase(slot->database.tables[i].name, tableName))
                    {
                        liveTable = &slot->database.tables[i];
                        break;
                    }
                }

                QC::u32 rowPageId = 0;
                QC::u16 rowPageOffset = 0;
                const Status lookupSt = Engine::instance().findByPrimaryKey(slot->database,
                                                                             liveTable ? liveTable->tableId : 0,
                                                                             pkKey,
                                                                             rowPageId,
                                                                             rowPageOffset);
                if (lookupSt != Status::Success)
                {
                    setFailure(outResult, lookupSt, "update_row_not_found");
                    return outResult.status;
                }

                Row existingRow{};
                if (Engine::instance().readRow(slot->database, rowPageId, rowPageOffset, existingRow) != Status::Success)
                {
                    setFailure(outResult, Status::Error, "update_read_row_failed");
                    return outResult.status;
                }

                // Apply SET assignments.
                for (QC::usize si = 0; si < setCount; ++si)
                {
                    const QC::i32 colIdx = liveTable
                                               ? [&]() -> QC::i32 {
                                                     for (QC::usize ci = 0; ci < liveTable->schema.columns.size(); ++ci)
                                                         if (equalsIgnoreCase(liveTable->schema.columns[ci].name, setColNames[si]))
                                                             return static_cast<QC::i32>(ci);
                                                     return -1;
                                                 }()
                                               : -1;
                    if (colIdx < 0 || static_cast<QC::usize>(colIdx) >= existingRow.cells.size())
                        continue;

                    Cell &cell = existingRow.cells[static_cast<QC::usize>(colIdx)];
                    cell.bytes = setValues[si];
                }

                const Status updSt = Engine::instance().updateRowByPrimaryKeyByName(slot->database,
                                                                                     tableName,
                                                                                     pkKey,
                                                                                     existingRow);
                if (updSt != Status::Success)
                {
                    setFailure(outResult, updSt, Engine::instance().lastDiagnostic());
                    return outResult.status;
                }

                outResult.status = Status::Success;
                appendLine(outResult, "ok");
                outResult.rowsAffected = 1;
                return outResult.status;
            }

            setFailure(outResult, Status::NotSupported, "query_not_supported");
            return outResult.status;
        }
    }
}
