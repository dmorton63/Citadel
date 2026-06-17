// QCQL Engine - Core database engine shell implementation
// Namespace: QCQL

#include "QCQLEngine.h"
#include "QCString.h"
#include "QFSFile.h"
#include "QFSVFS.h"

namespace QCQL
{

    namespace
    {
        static Engine *s_engine = nullptr;
        static char s_lastDiagnostic[256] = {};

        static void clearDiagnostic()
        {
            QC::String::memset(s_lastDiagnostic, 0, sizeof(s_lastDiagnostic));
        }

        static void setDiagnostic(const char *message)
        {
            QC::String::memset(s_lastDiagnostic, 0, sizeof(s_lastDiagnostic));
            if (message)
                QC::String::strncpy(s_lastDiagnostic, message, sizeof(s_lastDiagnostic) - 1);
        }

        static void appendDiag(char *dst, QC::usize cap, const char *src)
        {
            if (!dst || cap == 0 || !src || !*src)
                return;
            const QC::usize used = QC::String::strlen(dst);
            if (used + 1 >= cap)
                return;
            QC::String::strncpy(dst + used, src, cap - used - 1);
        }

        static void setForeignKeyDiagnostic(const char *code,
                                            const char *tableName,
                                            const char *columnName,
                                            const char *referencedTable,
                                            const char *referencedColumn)
        {
            char msg[256];
            QC::String::memset(msg, 0, sizeof(msg));
            appendDiag(msg, sizeof(msg), code ? code : "fk_error");
            appendDiag(msg, sizeof(msg), " table=");
            appendDiag(msg, sizeof(msg), tableName ? tableName : "?");
            appendDiag(msg, sizeof(msg), " column=");
            appendDiag(msg, sizeof(msg), columnName ? columnName : "?");
            appendDiag(msg, sizeof(msg), " parent_table=");
            appendDiag(msg, sizeof(msg), referencedTable ? referencedTable : "?");
            appendDiag(msg, sizeof(msg), " parent_column=");
            appendDiag(msg, sizeof(msg), referencedColumn ? referencedColumn : "?");
            setDiagnostic(msg);
        }

        static void appendU16(QC::Vector<QC::u8> &out, QC::u16 v)
        {
            out.push_back(static_cast<QC::u8>(v & 0xFF));
            out.push_back(static_cast<QC::u8>((v >> 8) & 0xFF));
        }

        static void appendU32(QC::Vector<QC::u8> &out, QC::u32 v)
        {
            out.push_back(static_cast<QC::u8>(v & 0xFF));
            out.push_back(static_cast<QC::u8>((v >> 8) & 0xFF));
            out.push_back(static_cast<QC::u8>((v >> 16) & 0xFF));
            out.push_back(static_cast<QC::u8>((v >> 24) & 0xFF));
        }

        static bool readU16(const QC::u8 *p, QC::usize len, QC::usize &off, QC::u16 &out)
        {
            if (!p || off + 2 > len)
                return false;
            out = static_cast<QC::u16>(p[off]) |
                  static_cast<QC::u16>(static_cast<QC::u16>(p[off + 1]) << 8);
            off += 2;
            return true;
        }

        static bool readU32(const QC::u8 *p, QC::usize len, QC::usize &off, QC::u32 &out)
        {
            if (!p || off + 4 > len)
                return false;
            out = static_cast<QC::u32>(p[off]) |
                  static_cast<QC::u32>(static_cast<QC::u32>(p[off + 1]) << 8) |
                  static_cast<QC::u32>(static_cast<QC::u32>(p[off + 2]) << 16) |
                  static_cast<QC::u32>(static_cast<QC::u32>(p[off + 3]) << 24);
            off += 4;
            return true;
        }

        static bool writeHeader(const Database &database)
        {
            QFS::File *f = QFS::VFS::instance().open(database.path,
                                                     QFS::OpenMode::Write | QFS::OpenMode::Create);
            if (!f)
                return false;

            const QC::isize n = f->write(&database.header, sizeof(FileHeader));
            QFS::VFS::instance().close(f);
            return n == static_cast<QC::isize>(sizeof(FileHeader));
        }

        static bool readAt(QFS::File *f, QC::u64 off, void *out, QC::usize size)
        {
            if (!f || !out || size == 0)
                return false;
            if (f->seek(static_cast<QC::i64>(off), QFS::SeekOrigin::Begin) < 0)
                return false;
            const QC::isize n = f->read(out, size);
            return n == static_cast<QC::isize>(size);
        }

        static bool writeAt(QFS::File *f, QC::u64 off, const void *src, QC::usize size)
        {
            if (!f || !src || size == 0)
                return false;
            if (f->seek(static_cast<QC::i64>(off), QFS::SeekOrigin::Begin) < 0)
                return false;
            const QC::isize n = f->write(src, size);
            return n == static_cast<QC::isize>(size);
        }

        static Status loadPageFromFile(const Database &database, QFS::File *f, QC::u32 pageId, Page &outPage)
        {
            if (!f || database.path[0] == '\0' || pageId == 0 || database.pageSize < sizeof(PageHeader))
                return Status::InvalidParam;

            QC::Vector<QC::u8> buf;
            buf.resize(database.pageSize);
            const QC::u64 off = database.header.pageRegionOffset +
                                static_cast<QC::u64>(pageId - 1) * static_cast<QC::u64>(database.pageSize);
            if (!readAt(f, off, buf.data(), database.pageSize))
                return Status::NotFound;

            Page page{};
            QC::String::memcpy(&page.header, buf.data(), sizeof(PageHeader));
            if (page.header.pageId == 0 || page.header.pageId != pageId)
                return Status::Corrupt;

            const QC::usize offsetsBytes = static_cast<QC::usize>(page.header.rowCount) * sizeof(QC::u16);
            const QC::usize offsetsStart = sizeof(PageHeader);
            const QC::usize dataStart = offsetsStart + offsetsBytes;
            if (dataStart > database.pageSize)
                return Status::Corrupt;
            if (page.header.freeOffset < dataStart || page.header.freeOffset > database.pageSize)
                return Status::Corrupt;

            page.rowOffsets.resize(page.header.rowCount);
            if (page.header.rowCount > 0)
            {
                QC::String::memcpy(page.rowOffsets.data(),
                                   buf.data() + offsetsStart,
                                   offsetsBytes);
            }

            const QC::usize payloadBytes = static_cast<QC::usize>(page.header.freeOffset) - dataStart;
            page.data.resize(payloadBytes);
            if (payloadBytes > 0)
            {
                QC::String::memcpy(page.data.data(),
                                   buf.data() + dataStart,
                                   payloadBytes);
            }

            page.dirty = false;
            outPage = static_cast<Page &&>(page);
            return Status::Success;
        }

        static QC::usize schemaDiskSize(QC::u32 version)
        {
            return (version <= kFileVersion1) ? sizeof(TableSchemaDiskV1) : sizeof(TableSchemaDisk);
        }

        static bool serializeRow(const Row &row, QC::Vector<QC::u8> &out)
        {
            if (row.cells.size() > 65535)
                return false;

            out.clear();

            QC::u32 bodyLen = 1 + 2; // tombstone + cell count
            for (QC::usize i = 0; i < row.cells.size(); ++i)
            {
                const Cell &c = row.cells[i];
                if (c.bytes.size() > 65535)
                    return false;
                bodyLen += 1 + 2 + static_cast<QC::u32>(c.bytes.size());
            }

            appendU32(out, bodyLen);
            out.push_back(row.tombstone ? 1 : 0);
            appendU16(out, static_cast<QC::u16>(row.cells.size()));

            for (QC::usize i = 0; i < row.cells.size(); ++i)
            {
                const Cell &c = row.cells[i];
                out.push_back(static_cast<QC::u8>(c.type));
                appendU16(out, static_cast<QC::u16>(c.bytes.size()));
                for (QC::usize j = 0; j < c.bytes.size(); ++j)
                    out.push_back(c.bytes[j]);
            }

            return true;
        }

        static bool deserializeRow(const QC::u8 *p, QC::usize len, Row &outRow)
        {
            if (!p || len < 7)
                return false;

            QC::usize off = 0;
            QC::u32 bodyLen = 0;
            if (!readU32(p, len, off, bodyLen))
                return false;
            if (off + bodyLen > len)
                return false;

            const QC::u8 tomb = p[off++];
            outRow = Row{};
            outRow.tombstone = (tomb != 0);

            QC::u16 cellCount = 0;
            if (!readU16(p, len, off, cellCount))
                return false;

            for (QC::u16 i = 0; i < cellCount; ++i)
            {
                if (off + 3 > len)
                    return false;

                Cell c{};
                c.type = static_cast<ColumnType>(p[off++]);
                QC::u16 cellLen = 0;
                if (!readU16(p, len, off, cellLen))
                    return false;
                if (off + cellLen > len)
                    return false;

                c.bytes.resize(cellLen);
                if (cellLen > 0)
                    QC::String::memcpy(c.bytes.data(), p + off, cellLen);
                off += cellLen;
                outRow.cells.push_back(static_cast<Cell &&>(c));
            }

            return true;
        }

        static bool tableNameEquals(const char *a, const char *b)
        {
            if (!a || !b)
                return false;
            return QC::String::strcmp(a, b) == 0;
        }

        static QC::i32 findColumnIndex(const TableSchema &schema, const char *columnName)
        {
            if (!columnName || columnName[0] == '\0')
                return -1;

            for (QC::usize i = 0; i < schema.columns.size(); ++i)
            {
                if (tableNameEquals(schema.columns[i].name, columnName))
                    return static_cast<QC::i32>(i);
            }

            return -1;
        }

        static Table *findTableByName(Database &database, const char *name)
        {
            if (!name)
                return nullptr;

            for (QC::usize i = 0; i < database.tables.size(); ++i)
            {
                if (tableNameEquals(database.tables[i].name, name))
                    return &database.tables[i];
            }

            return nullptr;
        }

        static const Table *findTableByName(const Database &database, const char *name)
        {
            if (!name)
                return nullptr;

            for (QC::usize i = 0; i < database.tables.size(); ++i)
            {
                if (tableNameEquals(database.tables[i].name, name))
                    return &database.tables[i];
            }

            return nullptr;
        }

        static bool hasTable(const Database &database, const char *name)
        {
            for (QC::usize i = 0; i < database.tables.size(); ++i)
            {
                if (tableNameEquals(database.tables[i].name, name))
                    return true;
            }
            return false;
        }

        static void setFixedName(char *dst, QC::usize cap, const char *src)
        {
            if (!dst || cap == 0)
                return;
            QC::String::memset(dst, 0, cap);
            if (src)
                QC::String::strncpy(dst, src, cap - 1);
        }

        static bool validateSchema(const Database &database,
                                   const TableSchema &schema,
                                   QC::u32 &outPrimaryKeyIndex)
        {
            if (schema.tableName[0] == '\0' || schema.columns.empty() || schema.columns.size() > kMaxColumnsPerTable)
            {
                setDiagnostic("schema_invalid_shape");
                return false;
            }
            if (schema.foreignKeys.size() > kMaxForeignKeysPerTable)
            {
                setDiagnostic("schema_too_many_foreign_keys");
                return false;
            }

            QC::u32 pkCount = 0;
            outPrimaryKeyIndex = 0;

            for (QC::usize i = 0; i < schema.columns.size(); ++i)
            {
                const Column &col = schema.columns[i];
                if (col.name[0] == '\0')
                {
                    setDiagnostic("schema_empty_column_name");
                    return false;
                }

                for (QC::usize j = i + 1; j < schema.columns.size(); ++j)
                {
                    if (tableNameEquals(col.name, schema.columns[j].name))
                    {
                        setDiagnostic("schema_duplicate_column_name");
                        return false;
                    }
                }

                if (col.isPrimaryKey)
                {
                    outPrimaryKeyIndex = static_cast<QC::u32>(i);
                    ++pkCount;
                }
            }

            if (pkCount != 1)
            {
                setDiagnostic("schema_primary_key_count_must_be_1");
                return false;
            }

            for (QC::usize i = 0; i < schema.foreignKeys.size(); ++i)
            {
                const ForeignKey &foreignKey = schema.foreignKeys[i];
                if (foreignKey.columnName[0] == '\0' ||
                    foreignKey.referencedTable[0] == '\0' ||
                    foreignKey.referencedColumn[0] == '\0')
                {
                    setDiagnostic("schema_foreign_key_missing_fields");
                    return false;
                }

                if (foreignKey.onDelete != ReferentialAction::Restrict ||
                    foreignKey.onUpdate != ReferentialAction::Restrict)
                {
                    setDiagnostic("schema_foreign_key_action_not_supported");
                    return false;
                }

                const QC::i32 childColumnIndex = findColumnIndex(schema, foreignKey.columnName);
                if (childColumnIndex < 0)
                {
                    setForeignKeyDiagnostic("schema_foreign_key_child_column_missing",
                                            schema.tableName,
                                            foreignKey.columnName,
                                            foreignKey.referencedTable,
                                            foreignKey.referencedColumn);
                    return false;
                }

                const Table *parentTable = findTableByName(database, foreignKey.referencedTable);
                if (!parentTable)
                {
                    setForeignKeyDiagnostic("schema_foreign_key_parent_table_missing",
                                            schema.tableName,
                                            foreignKey.columnName,
                                            foreignKey.referencedTable,
                                            foreignKey.referencedColumn);
                    return false;
                }

                const QC::i32 parentColumnIndex = findColumnIndex(parentTable->schema, foreignKey.referencedColumn);
                if (parentColumnIndex < 0)
                {
                    setForeignKeyDiagnostic("schema_foreign_key_parent_column_missing",
                                            schema.tableName,
                                            foreignKey.columnName,
                                            foreignKey.referencedTable,
                                            foreignKey.referencedColumn);
                    return false;
                }

                const Column &parentColumn = parentTable->schema.columns[static_cast<QC::usize>(parentColumnIndex)];
                const Column &childColumn = schema.columns[static_cast<QC::usize>(childColumnIndex)];
                if (!parentColumn.isPrimaryKey)
                {
                    setForeignKeyDiagnostic("schema_foreign_key_parent_not_primary_key",
                                            schema.tableName,
                                            foreignKey.columnName,
                                            foreignKey.referencedTable,
                                            foreignKey.referencedColumn);
                    return false;
                }
                if (parentColumn.type != childColumn.type)
                {
                    setForeignKeyDiagnostic("schema_foreign_key_type_mismatch",
                                            schema.tableName,
                                            foreignKey.columnName,
                                            foreignKey.referencedTable,
                                            foreignKey.referencedColumn);
                    return false;
                }
            }

            return true;
        }

        static bool validateRowAgainstSchema(const TableSchema &schema, const Row &row)
        {
            if (row.cells.size() != schema.columns.size())
            {
                setDiagnostic("row_column_count_mismatch");
                return false;
            }

            for (QC::usize i = 0; i < schema.columns.size(); ++i)
            {
                if (row.cells[i].type != schema.columns[i].type)
                {
                    setDiagnostic("row_column_type_mismatch");
                    return false;
                }
            }

            return true;
        }

        static bool hasForeignKey(const TableSchema &schema,
                                  const char *columnName,
                                  const char *referencedTable,
                                  const char *referencedColumn)
        {
            for (QC::usize i = 0; i < schema.foreignKeys.size(); ++i)
            {
                const ForeignKey &foreignKey = schema.foreignKeys[i];
                if (!tableNameEquals(foreignKey.columnName, columnName))
                    continue;
                if (!tableNameEquals(foreignKey.referencedTable, referencedTable))
                    continue;
                if (!tableNameEquals(foreignKey.referencedColumn, referencedColumn))
                    continue;
                return true;
            }

            return false;
        }

        static void addForeignKey(TableSchema &schema,
                                  const char *columnName,
                                  const char *referencedTable,
                                  const char *referencedColumn)
        {
            if (hasForeignKey(schema, columnName, referencedTable, referencedColumn))
                return;

            ForeignKey foreignKey{};
            setFixedName(foreignKey.columnName, sizeof(foreignKey.columnName), columnName);
            setFixedName(foreignKey.referencedTable, sizeof(foreignKey.referencedTable), referencedTable);
            setFixedName(foreignKey.referencedColumn, sizeof(foreignKey.referencedColumn), referencedColumn);
            schema.foreignKeys.push_back(static_cast<ForeignKey &&>(foreignKey));
        }

        static void applyBuiltInRelationshipMetadata(Database &database)
        {
            Table *themeTokens = findTableByName(database, "ThemeTokens");
            if (themeTokens)
                addForeignKey(themeTokens->schema, "themeId", "Themes", "id");
        }

        static QC::u32 nextTableId(const Database &database)
        {
            QC::u32 maxTableId = 3;
            for (QC::usize i = 0; i < database.tables.size(); ++i)
            {
                if (database.tables[i].tableId > maxTableId)
                    maxTableId = database.tables[i].tableId;
            }
            return maxTableId + 1;
        }

        static int compareByteVectors(const QC::Vector<QC::u8> &a, const QC::Vector<QC::u8> &b)
        {
            const QC::usize minLen = (a.size() < b.size()) ? a.size() : b.size();
            for (QC::usize i = 0; i < minLen; ++i)
            {
                if (a[i] < b[i])
                    return -1;
                if (a[i] > b[i])
                    return 1;
            }
            if (a.size() < b.size())
                return -1;
            if (a.size() > b.size())
                return 1;
            return 0;
        }

        static void insertPkEntrySorted(PrimaryKeyIndex &index, PrimaryKeyIndexEntry &&entry)
        {
            if (index.entries.empty())
            {
                index.entries.push_back(static_cast<PrimaryKeyIndexEntry &&>(entry));
                return;
            }

            QC::usize pos = 0;
            while (pos < index.entries.size() && compareByteVectors(index.entries[pos].key, entry.key) < 0)
                ++pos;

            index.entries.push_back(PrimaryKeyIndexEntry{});
            for (QC::usize i = index.entries.size() - 1; i > pos; --i)
                index.entries[i] = static_cast<PrimaryKeyIndexEntry &&>(index.entries[i - 1]);
            index.entries[pos] = static_cast<PrimaryKeyIndexEntry &&>(entry);
        }

        static Table *findTableById(Database &database, QC::u32 tableId)
        {
            for (QC::usize i = 0; i < database.tables.size(); ++i)
            {
                if (database.tables[i].tableId == tableId)
                    return &database.tables[i];
            }
            return nullptr;
        }

        static const Table *findTableById(const Database &database, QC::u32 tableId)
        {
            for (QC::usize i = 0; i < database.tables.size(); ++i)
            {
                if (database.tables[i].tableId == tableId)
                    return &database.tables[i];
            }
            return nullptr;
        }

        static bool removePkEntry(PrimaryKeyIndex &index,
                                  const QC::Vector<QC::u8> &key,
                                  QC::u32 pageId,
                                  QC::u16 rowOffset)
        {
            for (QC::usize i = 0; i < index.entries.size(); ++i)
            {
                const PrimaryKeyIndexEntry &e = index.entries[i];
                if (e.pageId != pageId || e.rowOffset != rowOffset)
                    continue;
                if (compareByteVectors(e.key, key) != 0)
                    continue;

                for (QC::usize j = i + 1; j < index.entries.size(); ++j)
                    index.entries[j - 1] = static_cast<PrimaryKeyIndexEntry &&>(index.entries[j]);
                index.entries.pop_back();
                return true;
            }
            return false;
        }

        static Status enforceForeignKeyConstraints(Engine &engine,
                                                   const Database &database,
                                                   const Table &table,
                                                   const Row &row)
        {
            for (QC::usize i = 0; i < table.schema.foreignKeys.size(); ++i)
            {
                const ForeignKey &foreignKey = table.schema.foreignKeys[i];
                const QC::i32 childColumnIndex = findColumnIndex(table.schema, foreignKey.columnName);
                if (childColumnIndex < 0)
                {
                    setForeignKeyDiagnostic("fk_child_column_missing",
                                            table.name,
                                            foreignKey.columnName,
                                            foreignKey.referencedTable,
                                            foreignKey.referencedColumn);
                    return Status::Corrupt;
                }

                const QC::usize childIndex = static_cast<QC::usize>(childColumnIndex);
                if (childIndex >= row.cells.size())
                {
                    setForeignKeyDiagnostic("fk_child_column_out_of_bounds",
                                            table.name,
                                            foreignKey.columnName,
                                            foreignKey.referencedTable,
                                            foreignKey.referencedColumn);
                    return Status::InvalidParam;
                }

                const Table *parentTable = findTableByName(database, foreignKey.referencedTable);
                if (!parentTable)
                {
                    setForeignKeyDiagnostic("fk_parent_table_missing",
                                            table.name,
                                            foreignKey.columnName,
                                            foreignKey.referencedTable,
                                            foreignKey.referencedColumn);
                    return Status::Corrupt;
                }

                QC::u32 parentPageId = 0;
                QC::u16 parentRowOffset = 0;
                const Status parentSt = engine.findByPrimaryKey(database,
                                                                parentTable->tableId,
                                                                row.cells[childIndex].bytes,
                                                                parentPageId,
                                                                parentRowOffset);
                if (parentSt == Status::Success)
                    continue;
                if (parentSt == Status::NotFound)
                {
                    setForeignKeyDiagnostic("fk_parent_row_missing",
                                            table.name,
                                            foreignKey.columnName,
                                            foreignKey.referencedTable,
                                            foreignKey.referencedColumn);
                    return Status::ConstraintViolation;
                }
                return parentSt;
            }

            return Status::Success;
        }

        static Status enforceDeleteConstraints(Engine &engine,
                                               const Database &database,
                                               const Table &parentTable,
                                               const QC::Vector<QC::u8> &parentKey)
        {
            for (QC::usize tableIndex = 0; tableIndex < database.tables.size(); ++tableIndex)
            {
                const Table &childTable = database.tables[tableIndex];
                for (QC::usize fkIndex = 0; fkIndex < childTable.schema.foreignKeys.size(); ++fkIndex)
                {
                    const ForeignKey &foreignKey = childTable.schema.foreignKeys[fkIndex];
                    if (!tableNameEquals(foreignKey.referencedTable, parentTable.name))
                        continue;
                    if (!tableNameEquals(foreignKey.referencedColumn,
                                         parentTable.schema.columns[parentTable.schema.primaryKeyIndex].name))
                    {
                        continue;
                    }

                    const QC::i32 childColumnIndex = findColumnIndex(childTable.schema, foreignKey.columnName);
                    if (childColumnIndex < 0)
                    {
                        setForeignKeyDiagnostic("fk_delete_child_column_missing",
                                                childTable.name,
                                                foreignKey.columnName,
                                                foreignKey.referencedTable,
                                                foreignKey.referencedColumn);
                        return Status::Corrupt;
                    }

                    for (QC::usize pageIndex = 0; pageIndex < childTable.pages.size(); ++pageIndex)
                    {
                        Page page{};
                        const Status loadSt = engine.loadPage(database, childTable.pages[pageIndex], page);
                        if (loadSt != Status::Success)
                            return loadSt;

                        for (QC::usize rowIndex = 0; rowIndex < page.rowOffsets.size(); ++rowIndex)
                        {
                            Row childRow{};
                            const Status readSt = engine.readRow(database,
                                                                 page.header.pageId,
                                                                 page.rowOffsets[rowIndex],
                                                                 childRow);
                            if (readSt != Status::Success)
                                return readSt;
                            if (childRow.tombstone)
                                continue;

                            const QC::usize childIndex = static_cast<QC::usize>(childColumnIndex);
                            if (childIndex >= childRow.cells.size())
                            {
                                setForeignKeyDiagnostic("fk_delete_child_column_out_of_bounds",
                                                        childTable.name,
                                                        foreignKey.columnName,
                                                        foreignKey.referencedTable,
                                                        foreignKey.referencedColumn);
                                return Status::Corrupt;
                            }
                            if (compareByteVectors(childRow.cells[childIndex].bytes, parentKey) == 0)
                            {
                                setForeignKeyDiagnostic("fk_delete_restrict_violation",
                                                        childTable.name,
                                                        foreignKey.columnName,
                                                        foreignKey.referencedTable,
                                                        foreignKey.referencedColumn);
                                return Status::ConstraintViolation;
                            }
                        }
                    }
                }
            }

            return Status::Success;
        }
    }

    Engine &Engine::instance()
    {
        if (!s_engine)
            s_engine = new Engine();
        return *s_engine;
    }

    void Engine::initialize()
    {
        m_initialized = true;
    }

    Status Engine::createDatabase(const char *path, Database &outDatabase)
    {
        if (!path || path[0] == '\0')
            return Status::InvalidParam;

        if (!m_initialized)
            initialize();

        Database db{};
        if (!copyPath(path, db.path, sizeof(db.path)))
            return Status::InvalidParam;

        const Status st = initializeDatabaseHeader(db);
        if (st != Status::Success)
            return st;

        QFS::File *f = QFS::VFS::instance().open(path,
                                                 QFS::OpenMode::Write | QFS::OpenMode::Create | QFS::OpenMode::Truncate);
        if (!f)
            return Status::Error;

        const QC::isize n = f->write(&db.header, sizeof(FileHeader));
        QFS::VFS::instance().close(f);
        if (n != static_cast<QC::isize>(sizeof(FileHeader)))
            return Status::Error;

        const Status metaSt = persistMetadata(db);
        if (metaSt != Status::Success)
            return metaSt;

        outDatabase = static_cast<Database &&>(db);
        return Status::Success;
    }

    Status Engine::openDatabase(const char *path, Database &outDatabase, OpenStats *outStats)
    {
        if (!path || path[0] == '\0')
            return Status::InvalidParam;

        if (!m_initialized)
            initialize();

        QFS::File *f = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
        if (!f)
            return Status::NotFound;

        FileHeader hdr{};
        const QC::isize n = f->read(&hdr, sizeof(FileHeader));
        const QC::u64 fileBytes = f->size();
        QFS::VFS::instance().close(f);
        if (n != static_cast<QC::isize>(sizeof(FileHeader)))
            return Status::Corrupt;

        if (hdr.magic[0] != 'C' || hdr.magic[1] != 'Q' || hdr.magic[2] != 'L' || hdr.magic[3] != 'D' || hdr.magic[4] != 'B')
            return Status::Corrupt;
        if ((hdr.version != kFileVersion1 && hdr.version != kFileVersion2) || hdr.pageSize == 0)
            return Status::NotSupported;

        Database db{};
        if (!copyPath(path, db.path, sizeof(db.path)))
            return Status::InvalidParam;

        db.header = hdr;
        db.pageSize = hdr.pageSize;
        if (fileBytes > db.header.pageRegionOffset)
        {
            const QC::u64 pageBytes = fileBytes - db.header.pageRegionOffset;
            const QC::u64 pageCount = pageBytes / db.pageSize;
            db.nextPageId = static_cast<QC::u32>(pageCount + 1);
        }
        else
        {
            db.nextPageId = 1;
        }

        OpenStats localStats{};
        OpenStats *stats = outStats ? outStats : &localStats;

        const Status metaSt = loadMetadata(db, stats);
        if (metaSt != Status::Success)
            return metaSt;

        for (QC::usize i = 0; i < db.tables.size(); ++i)
        {
            const Status idxSt = rebuildPrimaryKeyIndex(db, db.tables[i].tableId, stats);
            if (idxSt != Status::Success)
                return idxSt;
        }

        outDatabase = static_cast<Database &&>(db);
        return Status::Success;
    }

    Status Engine::closeDatabase(Database &database)
    {
        database = Database{};
        return Status::Success;
    }

    Status Engine::createTable(Database &database, const TableSchema &schema)
    {
        clearDiagnostic();
        if (database.path[0] == '\0')
            return Status::InvalidParam;
        if (database.tables.size() >= kMaxTables)
            return Status::OutOfMemory;
        if (hasTable(database, schema.tableName))
            return Status::AlreadyExists;

        QC::u32 primaryKeyIndex = 0;
        if (!validateSchema(database, schema, primaryKeyIndex))
            return Status::InvalidParam;

        Table table{};
        setFixedName(table.name, sizeof(table.name), schema.tableName);
        table.tableId = nextTableId(database);

        setFixedName(table.schema.tableName, sizeof(table.schema.tableName), schema.tableName);
        table.schema.primaryKeyIndex = primaryKeyIndex;

        for (QC::usize i = 0; i < schema.columns.size(); ++i)
        {
            Column col{};
            setFixedName(col.name, sizeof(col.name), schema.columns[i].name);
            col.type = schema.columns[i].type;
            col.isPrimaryKey = schema.columns[i].isPrimaryKey;
            table.schema.columns.push_back(static_cast<Column &&>(col));
        }

        for (QC::usize i = 0; i < schema.foreignKeys.size(); ++i)
        {
            table.schema.foreignKeys.push_back(schema.foreignKeys[i]);
        }

        Page root{};
        const Status pageSt = allocatePage(database, table.tableId, root);
        if (pageSt != Status::Success)
            return pageSt;

        table.rootPage = root.header.pageId;
        table.pages.push_back(root.header.pageId);
        database.tables.push_back(static_cast<Table &&>(table));
        database.header.tableCount = static_cast<QC::u32>(database.tables.size());

        if (!writeHeader(database))
            return Status::Error;

        return persistMetadata(database);
    }

    Status Engine::initializeDatabaseHeader(Database &database) const
    {
        database.header = FileHeader{};
        database.header.version = kCurrentFileVersion;
        database.pageSize = database.header.pageSize;
        database.header.tableDirOffset = sizeof(FileHeader);
        database.header.schemaOffset = database.header.tableDirOffset + static_cast<QC::u64>(sizeof(TableEntry) * kMaxTables);
        database.header.pageRegionOffset = database.header.schemaOffset + static_cast<QC::u64>(schemaDiskSize(database.header.version) * kMaxTables);
        database.nextPageId = 1;
        return Status::Success;
    }

    Status Engine::persistMetadata(const Database &database) const
    {
        if (database.path[0] == '\0')
            return Status::InvalidParam;
        if (database.tables.size() > kMaxTables)
            return Status::OutOfMemory;

        QFS::File *f = QFS::VFS::instance().open(database.path,
                                                 QFS::OpenMode::Write | QFS::OpenMode::Create);
        if (!f)
            return Status::Error;

        QC::Vector<TableEntry> tableEntries;
        tableEntries.resize(kMaxTables);
        QC::String::memset(tableEntries.data(), 0, tableEntries.size() * sizeof(TableEntry));

        const bool persistV1 = (database.header.version <= kFileVersion1);
        QC::Vector<TableSchemaDiskV1> schemasV1;
        QC::Vector<TableSchemaDisk> schemasV2;
        if (persistV1)
        {
            schemasV1.resize(kMaxTables);
            QC::String::memset(schemasV1.data(), 0, schemasV1.size() * sizeof(TableSchemaDiskV1));
        }
        else
        {
            schemasV2.resize(kMaxTables);
            QC::String::memset(schemasV2.data(), 0, schemasV2.size() * sizeof(TableSchemaDisk));
        }

        for (QC::usize i = 0; i < database.tables.size(); ++i)
        {
            const Table &t = database.tables[i];
            TableEntry &te = tableEntries[i];

            QC::String::strncpy(te.name, t.name, sizeof(te.name) - 1);
            te.rootPage = t.rootPage;
            te.flags = t.flags;
            te.schemaOffset = database.header.schemaOffset + static_cast<QC::u64>(i * schemaDiskSize(database.header.version));

            if (persistV1)
            {
                TableSchemaDiskV1 &sd = schemasV1[i];
                QC::String::strncpy(sd.tableName, t.schema.tableName, sizeof(sd.tableName) - 1);
                sd.tableId = t.tableId;
                sd.columnCount = static_cast<QC::u32>(t.schema.columns.size());
                if (sd.columnCount > kMaxColumnsPerTable)
                {
                    QFS::VFS::instance().close(f);
                    return Status::OutOfMemory;
                }
                sd.primaryKeyIndex = t.schema.primaryKeyIndex;

                for (QC::usize c = 0; c < t.schema.columns.size(); ++c)
                {
                    QC::String::strncpy(sd.columns[c].name, t.schema.columns[c].name, sizeof(sd.columns[c].name) - 1);
                    sd.columns[c].type = static_cast<QC::u8>(t.schema.columns[c].type);
                    sd.columns[c].isPrimaryKey = t.schema.columns[c].isPrimaryKey ? 1 : 0;
                }
            }
            else
            {
                TableSchemaDisk &sd = schemasV2[i];
                QC::String::strncpy(sd.tableName, t.schema.tableName, sizeof(sd.tableName) - 1);
                sd.tableId = t.tableId;
                sd.columnCount = static_cast<QC::u32>(t.schema.columns.size());
                if (sd.columnCount > kMaxColumnsPerTable)
                {
                    QFS::VFS::instance().close(f);
                    return Status::OutOfMemory;
                }
                sd.foreignKeyCount = static_cast<QC::u32>(t.schema.foreignKeys.size());
                if (sd.foreignKeyCount > kMaxForeignKeysPerTable)
                {
                    QFS::VFS::instance().close(f);
                    return Status::OutOfMemory;
                }
                sd.primaryKeyIndex = t.schema.primaryKeyIndex;

                for (QC::usize c = 0; c < t.schema.columns.size(); ++c)
                {
                    QC::String::strncpy(sd.columns[c].name, t.schema.columns[c].name, sizeof(sd.columns[c].name) - 1);
                    sd.columns[c].type = static_cast<QC::u8>(t.schema.columns[c].type);
                    sd.columns[c].isPrimaryKey = t.schema.columns[c].isPrimaryKey ? 1 : 0;
                }

                for (QC::usize fk = 0; fk < t.schema.foreignKeys.size(); ++fk)
                {
                    QC::String::strncpy(sd.foreignKeys[fk].columnName, t.schema.foreignKeys[fk].columnName, sizeof(sd.foreignKeys[fk].columnName) - 1);
                    QC::String::strncpy(sd.foreignKeys[fk].referencedTable, t.schema.foreignKeys[fk].referencedTable, sizeof(sd.foreignKeys[fk].referencedTable) - 1);
                    QC::String::strncpy(sd.foreignKeys[fk].referencedColumn, t.schema.foreignKeys[fk].referencedColumn, sizeof(sd.foreignKeys[fk].referencedColumn) - 1);
                    sd.foreignKeys[fk].onDelete = static_cast<QC::u8>(t.schema.foreignKeys[fk].onDelete);
                    sd.foreignKeys[fk].onUpdate = static_cast<QC::u8>(t.schema.foreignKeys[fk].onUpdate);
                }
            }
        }

        const bool wroteTables = writeAt(f, database.header.tableDirOffset, tableEntries.data(), tableEntries.size() * sizeof(TableEntry));
        const bool wroteSchemas = persistV1
                                      ? writeAt(f, database.header.schemaOffset, schemasV1.data(), schemasV1.size() * sizeof(TableSchemaDiskV1))
                                      : writeAt(f, database.header.schemaOffset, schemasV2.data(), schemasV2.size() * sizeof(TableSchemaDisk));
        if (!wroteTables || !wroteSchemas)
        {
            QFS::VFS::instance().close(f);
            return Status::Error;
        }

        QFS::VFS::instance().close(f);
        return Status::Success;
    }

    Status Engine::loadMetadata(Database &database, OpenStats *outStats) const
    {
        if (database.path[0] == '\0')
            return Status::InvalidParam;
        if (database.header.tableCount > kMaxTables)
            return Status::Corrupt;

        database.tables.clear();
        if (database.header.tableCount == 0)
            return Status::Success;

        OpenStats localStats{};
        OpenStats *stats = outStats ? outStats : &localStats;

        QFS::File *f = QFS::VFS::instance().open(database.path, QFS::OpenMode::Read);
        if (!f)
            return Status::NotFound;

        for (QC::u32 i = 0; i < database.header.tableCount; ++i)
        {
            TableEntry te{};
            const QC::u64 teOff = database.header.tableDirOffset + static_cast<QC::u64>(i * sizeof(TableEntry));
            if (!readAt(f, teOff, &te, sizeof(TableEntry)))
            {
                QFS::VFS::instance().close(f);
                return Status::Corrupt;
            }

            Table t{};
            QC::String::strncpy(t.name, te.name, sizeof(t.name) - 1);
            t.rootPage = te.rootPage;
            t.flags = te.flags;

            if (database.header.version <= kFileVersion1)
            {
                TableSchemaDiskV1 sd{};
                if (!readAt(f, te.schemaOffset, &sd, sizeof(TableSchemaDiskV1)))
                {
                    QFS::VFS::instance().close(f);
                    return Status::Corrupt;
                }

                if (sd.columnCount > kMaxColumnsPerTable)
                {
                    QFS::VFS::instance().close(f);
                    return Status::Corrupt;
                }

                t.tableId = sd.tableId;
                QC::String::strncpy(t.schema.tableName, sd.tableName, sizeof(t.schema.tableName) - 1);
                t.schema.primaryKeyIndex = sd.primaryKeyIndex;

                for (QC::u32 c = 0; c < sd.columnCount; ++c)
                {
                    Column col{};
                    QC::String::strncpy(col.name, sd.columns[c].name, sizeof(col.name) - 1);
                    col.type = static_cast<ColumnType>(sd.columns[c].type);
                    col.isPrimaryKey = (sd.columns[c].isPrimaryKey != 0);
                    t.schema.columns.push_back(static_cast<Column &&>(col));
                }
            }
            else
            {
                TableSchemaDisk sd{};
                if (!readAt(f, te.schemaOffset, &sd, sizeof(TableSchemaDisk)))
                {
                    QFS::VFS::instance().close(f);
                    return Status::Corrupt;
                }

                if (sd.columnCount > kMaxColumnsPerTable || sd.foreignKeyCount > kMaxForeignKeysPerTable)
                {
                    QFS::VFS::instance().close(f);
                    return Status::Corrupt;
                }

                t.tableId = sd.tableId;
                QC::String::strncpy(t.schema.tableName, sd.tableName, sizeof(t.schema.tableName) - 1);
                t.schema.primaryKeyIndex = sd.primaryKeyIndex;

                for (QC::u32 c = 0; c < sd.columnCount; ++c)
                {
                    Column col{};
                    QC::String::strncpy(col.name, sd.columns[c].name, sizeof(col.name) - 1);
                    col.type = static_cast<ColumnType>(sd.columns[c].type);
                    col.isPrimaryKey = (sd.columns[c].isPrimaryKey != 0);
                    t.schema.columns.push_back(static_cast<Column &&>(col));
                }

                for (QC::u32 fk = 0; fk < sd.foreignKeyCount; ++fk)
                {
                    ForeignKey foreignKey{};
                    QC::String::strncpy(foreignKey.columnName, sd.foreignKeys[fk].columnName, sizeof(foreignKey.columnName) - 1);
                    QC::String::strncpy(foreignKey.referencedTable, sd.foreignKeys[fk].referencedTable, sizeof(foreignKey.referencedTable) - 1);
                    QC::String::strncpy(foreignKey.referencedColumn, sd.foreignKeys[fk].referencedColumn, sizeof(foreignKey.referencedColumn) - 1);
                    foreignKey.onDelete = static_cast<ReferentialAction>(sd.foreignKeys[fk].onDelete);
                    foreignKey.onUpdate = static_cast<ReferentialAction>(sd.foreignKeys[fk].onUpdate);
                    t.schema.foreignKeys.push_back(static_cast<ForeignKey &&>(foreignKey));
                }
            }

            database.tables.push_back(static_cast<Table &&>(t));
            ++stats->metadataTablesLoaded;
        }

        applyBuiltInRelationshipMetadata(database);

        for (QC::usize i = 0; i < database.tables.size(); ++i)
            database.tables[i].pages.clear();

        for (QC::u32 pageId = 1; pageId < database.nextPageId; ++pageId)
        {
            PageHeader ph{};
            if (!readAt(f, pageOffset(database, pageId), &ph, sizeof(PageHeader)))
                continue;
            ++stats->metadataPageHeadersScanned;
            if (ph.pageId != pageId || ph.tableId == 0)
                continue;

            for (QC::usize i = 0; i < database.tables.size(); ++i)
            {
                if (database.tables[i].tableId != ph.tableId)
                    continue;

                database.tables[i].pages.push_back(pageId);
                if (database.tables[i].rootPage == 0)
                    database.tables[i].rootPage = pageId;
                break;
            }
        }

        QFS::VFS::instance().close(f);
        return Status::Success;
    }

    Status Engine::allocatePage(Database &database, QC::u32 tableId, Page &outPage)
    {
        if (database.path[0] == '\0' || database.pageSize == 0)
            return Status::InvalidParam;

        Page page{};
        page.header.pageId = database.nextPageId;
        page.header.tableId = tableId;
        page.header.rowCount = 0;
        page.header.freeOffset = static_cast<QC::u32>(sizeof(PageHeader));
        page.header.flags = 0;
        page.dirty = true;

        const Status st = flushPage(database, page);
        if (st != Status::Success)
            return st;

        ++database.nextPageId;
        outPage = static_cast<Page &&>(page);
        return Status::Success;
    }

    Status Engine::loadPage(const Database &database, QC::u32 pageId, Page &outPage)
    {
        if (database.path[0] == '\0' || pageId == 0 || database.pageSize < sizeof(PageHeader))
            return Status::InvalidParam;

        QFS::File *f = QFS::VFS::instance().open(database.path, QFS::OpenMode::Read);
        if (!f)
            return Status::NotFound;

        Page page{};
        const Status st = loadPageFromFile(database, f, pageId, page);
        QFS::VFS::instance().close(f);
        if (st != Status::Success)
            return st;

        outPage = static_cast<Page &&>(page);
        return Status::Success;
    }

    Status Engine::flushPage(const Database &database, const Page &page)
    {
        if (database.path[0] == '\0' || page.header.pageId == 0 || database.pageSize < sizeof(PageHeader))
            return Status::InvalidParam;

        const QC::usize offsetsBytes = page.rowOffsets.size() * sizeof(QC::u16);
        const QC::usize dataStart = sizeof(PageHeader) + offsetsBytes;
        const QC::usize end = dataStart + page.data.size();
        if (end > database.pageSize)
            return Status::OutOfMemory;

        QFS::File *f = QFS::VFS::instance().open(database.path,
                                                 QFS::OpenMode::Write | QFS::OpenMode::Create);
        if (!f)
            return Status::Error;

        const QC::u64 off = pageOffset(database, page.header.pageId);
        if (f->seek(static_cast<QC::i64>(off), QFS::SeekOrigin::Begin) < 0)
        {
            QFS::VFS::instance().close(f);
            return Status::Error;
        }

        QC::Vector<QC::u8> buf;
        buf.resize(database.pageSize);
        QC::String::memset(buf.data(), 0, database.pageSize);

        PageHeader hdr = page.header;
        hdr.rowCount = static_cast<QC::u32>(page.rowOffsets.size());
        hdr.freeOffset = static_cast<QC::u32>(end);

        QC::String::memcpy(buf.data(), &hdr, sizeof(PageHeader));
        if (offsetsBytes > 0)
        {
            QC::String::memcpy(buf.data() + sizeof(PageHeader),
                               page.rowOffsets.data(),
                               offsetsBytes);
        }
        if (!page.data.empty())
        {
            QC::String::memcpy(buf.data() + dataStart,
                               page.data.data(),
                               page.data.size());
        }

        const QC::isize n = f->write(buf.data(), database.pageSize);
        QFS::VFS::instance().close(f);
        if (n != static_cast<QC::isize>(database.pageSize))
            return Status::Error;

        return Status::Success;
    }

    Status Engine::initializeSystemTables(Database &database)
    {
        struct SystemTableDef
        {
            const char *name;
            QC::u32 tableId;
        };

        static const SystemTableDef defs[] = {
            {"Themes", 1},
            {"ThemeTokens", 2},
            {"Capabilities", 3},
        };

        for (QC::usize i = 0; i < sizeof(defs) / sizeof(defs[0]); ++i)
        {
            if (hasTable(database, defs[i].name))
                continue;

            Table t{};
            setFixedName(t.name, sizeof(t.name), defs[i].name);
            t.tableId = defs[i].tableId;
            t.flags = 0;

            setFixedName(t.schema.tableName, sizeof(t.schema.tableName), defs[i].name);
            t.schema.primaryKeyIndex = 0;

            auto addColumn = [&](const char *name, ColumnType type, bool pk) {
                Column c{};
                setFixedName(c.name, sizeof(c.name), name);
                c.type = type;
                c.isPrimaryKey = pk;
                t.schema.columns.push_back(static_cast<Column &&>(c));
            };

            if (tableNameEquals(defs[i].name, "Themes"))
            {
                addColumn("id", ColumnType::Text, true);
                addColumn("name", ColumnType::Text, false);
                addColumn("payload", ColumnType::Text, false);
            }
            else if (tableNameEquals(defs[i].name, "ThemeTokens"))
            {
                addColumn("id", ColumnType::Text, true);
                addColumn("themeId", ColumnType::Text, false);
                addColumn("tokenKey", ColumnType::Text, false);
                addColumn("tokenValue", ColumnType::Text, false);
                addForeignKey(t.schema, "themeId", "Themes", "id");
            }
            else
            {
                addColumn("id", ColumnType::Text, true);
                addColumn("processId", ColumnType::Int, false);
                addColumn("tableName", ColumnType::Text, false);
                addColumn("canRead", ColumnType::Bool, false);
                addColumn("canWrite", ColumnType::Bool, false);
                addColumn("canDelete", ColumnType::Bool, false);
                addColumn("canAdmin", ColumnType::Bool, false);
            }

            Page root{};
            const Status pageSt = allocatePage(database, t.tableId, root);
            if (pageSt != Status::Success)
                return pageSt;

            t.rootPage = root.header.pageId;
            t.pages.push_back(root.header.pageId);
            database.tables.push_back(static_cast<Table &&>(t));
            database.header.tableCount = static_cast<QC::u32>(database.tables.size());
        }

        Table *themeTokens = findTableByName(database, "ThemeTokens");
        if (themeTokens)
            addForeignKey(themeTokens->schema, "themeId", "Themes", "id");

        if (!writeHeader(database))
            return Status::Error;

        const Status metaSt = persistMetadata(database);
        if (metaSt != Status::Success)
            return metaSt;

        return Status::Success;
    }

    Status Engine::findPageForInsert(Database &database, QC::u32 tableId, QC::usize rowSizeNeeded, QC::u32 &outPageId)
    {
        if (tableId == 0 || rowSizeNeeded == 0)
            return Status::InvalidParam;

        Table *table = nullptr;
        for (QC::usize i = 0; i < database.tables.size(); ++i)
        {
            if (database.tables[i].tableId == tableId)
            {
                table = &database.tables[i];
                break;
            }
        }
        if (!table)
            return Status::NotFound;

        for (QC::usize i = 0; i < table->pages.size(); ++i)
        {
            Page page{};
            const Status st = loadPage(database, table->pages[i], page);
            if (st != Status::Success)
                continue;

            const QC::usize nextOffsetsBytes = (page.rowOffsets.size() + 1) * sizeof(QC::u16);
            const QC::usize predictedEnd = sizeof(PageHeader) + nextOffsetsBytes + page.data.size() + rowSizeNeeded;
            if (predictedEnd <= database.pageSize)
            {
                outPageId = table->pages[i];
                return Status::Success;
            }
        }

        Page newPage{};
        const Status allocSt = allocatePage(database, tableId, newPage);
        if (allocSt != Status::Success)
            return allocSt;

        table->pages.push_back(newPage.header.pageId);
        if (table->rootPage == 0)
            table->rootPage = newPage.header.pageId;

        const Status metaSt = persistMetadata(database);
        if (metaSt != Status::Success)
            return metaSt;

        outPageId = newPage.header.pageId;
        return Status::Success;
    }

    Status Engine::insertRow(Database &database, QC::u32 tableId, const Row &row,
                             QC::u32 *outPageId, QC::u16 *outRowOffset)
    {
        clearDiagnostic();
        Table *table = findTableById(database, tableId);
        if (!table)
            return Status::NotFound;
        if (!validateRowAgainstSchema(table->schema, row))
            return Status::InvalidParam;

        QC::Vector<QC::u8> encoded;
        if (!serializeRow(row, encoded))
            return Status::InvalidParam;

        const Status fkSt = enforceForeignKeyConstraints(*this, database, *table, row);
        if (fkSt != Status::Success)
            return fkSt;

        if (table->schema.primaryKeyIndex < row.cells.size())
        {
            const Cell &pkCell = row.cells[table->schema.primaryKeyIndex];
            QC::u32 existingPageId = 0;
            QC::u16 existingRowOffset = 0;
            const Status pkSt = findByPrimaryKey(database, tableId, pkCell.bytes, existingPageId, existingRowOffset);
            if (pkSt == Status::Success)
                return Status::AlreadyExists;
            if (pkSt != Status::NotFound)
                return pkSt;
        }

        QC::u32 pageId = 0;
        const Status findSt = findPageForInsert(database, tableId, encoded.size(), pageId);
        if (findSt != Status::Success)
            return findSt;

        QC::u16 insertedRowOffset = 0;
        const Status insSt = insertRow(database, pageId, row, &insertedRowOffset);
        if (insSt != Status::Success)
            return insSt;

        if (table->schema.primaryKeyIndex < row.cells.size())
        {
            const Cell &pkCell = row.cells[table->schema.primaryKeyIndex];
            PrimaryKeyIndexEntry idx{};
            idx.key = pkCell.bytes;
            idx.pageId = pageId;
            idx.rowOffset = insertedRowOffset;
            insertPkEntrySorted(table->primaryKeyIndex, static_cast<PrimaryKeyIndexEntry &&>(idx));
        }

        if (outPageId)
            *outPageId = pageId;
        if (outRowOffset)
            *outRowOffset = insertedRowOffset;
        return Status::Success;
    }

    Status Engine::insertRow(Database &database, QC::u32 pageId, const Row &row, QC::u16 *outRowOffset)
    {
        Page page{};
        const Status loadSt = loadPage(database, pageId, page);
        if (loadSt != Status::Success)
            return loadSt;

        QC::Vector<QC::u8> encoded;
        if (!serializeRow(row, encoded))
            return Status::InvalidParam;
        if (page.data.size() > 65535)
            return Status::OutOfMemory;

        const QC::usize nextOffsetsBytes = (page.rowOffsets.size() + 1) * sizeof(QC::u16);
        const QC::usize predictedEnd = sizeof(PageHeader) + nextOffsetsBytes + page.data.size() + encoded.size();
        if (predictedEnd > database.pageSize)
            return Status::OutOfMemory;

        const QC::u16 rowOffset = static_cast<QC::u16>(page.data.size());
        const QC::usize oldSize = page.data.size();
        page.data.resize(oldSize + encoded.size());
        if (!encoded.empty())
            QC::String::memcpy(page.data.data() + oldSize, encoded.data(), encoded.size());

        page.rowOffsets.push_back(rowOffset);
        page.header.rowCount = static_cast<QC::u32>(page.rowOffsets.size());
        page.dirty = true;

        const Status flushSt = flushPage(database, page);
        if (flushSt != Status::Success)
            return flushSt;

        if (outRowOffset)
            *outRowOffset = rowOffset;
        return Status::Success;
    }

    Status Engine::readRow(const Database &database, QC::u32 pageId, QC::u16 rowOffset, Row &outRow)
    {
        Page page{};
        const Status loadSt = loadPage(database, pageId, page);
        if (loadSt != Status::Success)
            return loadSt;

        if (rowOffset >= page.data.size())
            return Status::NotFound;

        if (!deserializeRow(page.data.data() + rowOffset,
                            page.data.size() - rowOffset,
                            outRow))
        {
            return Status::Corrupt;
        }

        return Status::Success;
    }

    Status Engine::rebuildPrimaryKeyIndex(Database &database, QC::u32 tableId, OpenStats *outStats)
    {
        Table *table = nullptr;
        for (QC::usize i = 0; i < database.tables.size(); ++i)
        {
            if (database.tables[i].tableId == tableId)
            {
                table = &database.tables[i];
                break;
            }
        }
        if (!table)
            return Status::NotFound;

        OpenStats localStats{};
        OpenStats *stats = outStats ? outStats : &localStats;

        table->primaryKeyIndex.entries.clear();

        QFS::File *f = QFS::VFS::instance().open(database.path, QFS::OpenMode::Read);
        if (!f)
            return Status::NotFound;

        for (QC::usize p = 0; p < table->pages.size(); ++p)
        {
            Page page{};
            const Status st = loadPageFromFile(database, f, table->pages[p], page);
            if (st != Status::Success)
            {
                QFS::VFS::instance().close(f);
                return st;
            }
            ++stats->pkPagesLoaded;

            for (QC::usize r = 0; r < page.rowOffsets.size(); ++r)
            {
                Row row{};
                ++stats->pkRowsScanned;
                const QC::u16 rowOffset = page.rowOffsets[r];
                if (rowOffset >= page.data.size())
                    continue;
                if (!deserializeRow(page.data.data() + rowOffset,
                                    page.data.size() - rowOffset,
                                    row) || row.tombstone)
                    continue;
                if (table->schema.primaryKeyIndex >= row.cells.size())
                    continue;

                PrimaryKeyIndexEntry idx{};
                idx.key = row.cells[table->schema.primaryKeyIndex].bytes;
                idx.pageId = page.header.pageId;
                idx.rowOffset = rowOffset;
                insertPkEntrySorted(table->primaryKeyIndex, static_cast<PrimaryKeyIndexEntry &&>(idx));
                ++stats->pkRowsIndexed;
            }
        }

        QFS::VFS::instance().close(f);
        ++stats->pkTablesRebuilt;

        return Status::Success;
    }

    Status Engine::findByPrimaryKey(const Database &database, QC::u32 tableId,
                                    const QC::Vector<QC::u8> &key,
                                    QC::u32 &outPageId,
                                    QC::u16 &outRowOffset) const
    {
        const Table *table = findTableById(database, tableId);
        if (!table)
            return Status::NotFound;
        if (table->primaryKeyIndex.entries.empty())
            return Status::NotFound;

        QC::usize lo = 0;
        QC::usize hi = table->primaryKeyIndex.entries.size();
        while (lo < hi)
        {
            const QC::usize mid = lo + ((hi - lo) / 2);
            const int cmp = compareByteVectors(table->primaryKeyIndex.entries[mid].key, key);
            if (cmp == 0)
            {
                outPageId = table->primaryKeyIndex.entries[mid].pageId;
                outRowOffset = table->primaryKeyIndex.entries[mid].rowOffset;
                return Status::Success;
            }
            if (cmp < 0)
                lo = mid + 1;
            else
                hi = mid;
        }

        return Status::NotFound;
    }

    Status Engine::selectRowByPrimaryKey(const Database &database,
                                         QC::u32 tableId,
                                         const QC::Vector<QC::u8> &key,
                                         Row &outRow) const
    {
        QC::u32 pageId = 0;
        QC::u16 rowOffset = 0;
        const Status st = findByPrimaryKey(database, tableId, key, pageId, rowOffset);
        if (st != Status::Success)
            return st;

        return const_cast<Engine *>(this)->readRow(database, pageId, rowOffset, outRow);
    }

    Status Engine::removeRowByPrimaryKey(Database &database,
                                         QC::u32 tableId,
                                         const QC::Vector<QC::u8> &key)
    {
        clearDiagnostic();
        Table *table = findTableById(database, tableId);
        if (!table)
            return Status::NotFound;

        const Status restrictSt = enforceDeleteConstraints(*this, database, *table, key);
        if (restrictSt != Status::Success)
            return restrictSt;

        QC::u32 pageId = 0;
        QC::u16 rowOffset = 0;
        const Status lookupSt = findByPrimaryKey(database, tableId, key, pageId, rowOffset);
        if (lookupSt != Status::Success)
            return lookupSt;

        Page page{};
        const Status loadSt = loadPage(database, pageId, page);
        if (loadSt != Status::Success)
            return loadSt;

        if (rowOffset + 5 > page.data.size())
            return Status::Corrupt;
        page.data[rowOffset + 4] = 1; // Tombstone flag.
        page.dirty = true;

        const Status flushSt = flushPage(database, page);
        if (flushSt != Status::Success)
            return flushSt;

        (void)removePkEntry(table->primaryKeyIndex, key, pageId, rowOffset);
        return Status::Success;
    }

    Status Engine::updateRowByPrimaryKey(Database &database,
                                         QC::u32 tableId,
                                         const QC::Vector<QC::u8> &key,
                                         const Row &updatedRow)
    {
        clearDiagnostic();
        Table *table = findTableById(database, tableId);
        if (!table)
            return Status::NotFound;
        if (!validateRowAgainstSchema(table->schema, updatedRow))
            return Status::InvalidParam;

        if (table->schema.primaryKeyIndex >= updatedRow.cells.size())
            return Status::InvalidParam;
        if (compareByteVectors(updatedRow.cells[table->schema.primaryKeyIndex].bytes, key) != 0)
            return Status::InvalidParam;

        const Status fkSt = enforceForeignKeyConstraints(*this, database, *table, updatedRow);
        if (fkSt != Status::Success)
            return fkSt;

        QC::u32 oldPageId = 0;
        QC::u16 oldRowOffset = 0;
        const Status lookupSt = findByPrimaryKey(database, tableId, key, oldPageId, oldRowOffset);
        if (lookupSt != Status::Success)
            return lookupSt;

        QC::Vector<QC::u8> encoded;
        if (!serializeRow(updatedRow, encoded))
            return Status::InvalidParam;

        QC::u32 newPageId = 0;
        const Status findSt = findPageForInsert(database, tableId, encoded.size(), newPageId);
        if (findSt != Status::Success)
            return findSt;

        QC::u16 newRowOffset = 0;
        const Status insSt = insertRow(database, newPageId, updatedRow, &newRowOffset);
        if (insSt != Status::Success)
            return insSt;

        Page oldPage{};
        const Status loadSt = loadPage(database, oldPageId, oldPage);
        if (loadSt != Status::Success)
            return loadSt;

        if (oldRowOffset + 5 > oldPage.data.size())
            return Status::Corrupt;
        oldPage.data[oldRowOffset + 4] = 1;
        oldPage.dirty = true;

        const Status flushSt = flushPage(database, oldPage);
        if (flushSt != Status::Success)
            return flushSt;

        if (!removePkEntry(table->primaryKeyIndex, key, oldPageId, oldRowOffset))
            return Status::Corrupt;

        PrimaryKeyIndexEntry idx{};
        idx.key = key;
        idx.pageId = newPageId;
        idx.rowOffset = newRowOffset;
        insertPkEntrySorted(table->primaryKeyIndex, static_cast<PrimaryKeyIndexEntry &&>(idx));

        return Status::Success;
    }

    // -------------------------------------------------------------------------
    // Named-table helpers
    // -------------------------------------------------------------------------

    Status Engine::lookupTableId(const Database &database,
                                 const char *tableName,
                                 QC::u32 &outTableId) const
    {
        if (!tableName)
            return Status::InvalidParam;
        for (QC::usize i = 0; i < database.tables.size(); ++i)
        {
            if (QC::String::strcmp(database.tables[i].name, tableName) == 0)
            {
                outTableId = database.tables[i].tableId;
                return Status::Success;
            }
        }
        return Status::NotFound;
    }

    Status Engine::insertRowByName(Database &database, const char *tableName,
                                   const Row &row,
                                   QC::u32 *outPageId, QC::u16 *outRowOffset)
    {
        QC::u32 tableId = 0;
        const Status st = lookupTableId(database, tableName, tableId);
        if (st != Status::Success)
            return st;
        return insertRow(database, tableId, row, outPageId, outRowOffset);
    }

    Status Engine::selectRowByPrimaryKeyByName(const Database &database,
                                               const char *tableName,
                                               const QC::Vector<QC::u8> &key,
                                               Row &outRow) const
    {
        QC::u32 tableId = 0;
        const Status st = lookupTableId(database, tableName, tableId);
        if (st != Status::Success)
            return st;
        return selectRowByPrimaryKey(database, tableId, key, outRow);
    }

    Status Engine::updateRowByPrimaryKeyByName(Database &database,
                                               const char *tableName,
                                               const QC::Vector<QC::u8> &key,
                                               const Row &updatedRow)
    {
        QC::u32 tableId = 0;
        const Status st = lookupTableId(database, tableName, tableId);
        if (st != Status::Success)
            return st;
        return updateRowByPrimaryKey(database, tableId, key, updatedRow);
    }

    Status Engine::removeRowByPrimaryKeyByName(Database &database,
                                               const char *tableName,
                                               const QC::Vector<QC::u8> &key)
    {
        QC::u32 tableId = 0;
        const Status st = lookupTableId(database, tableName, tableId);
        if (st != Status::Success)
            return st;
        return removeRowByPrimaryKey(database, tableId, key);
    }

    bool Engine::copyPath(const char *path, char *outPath, QC::usize outLen)
    {
        if (!path || !outPath || outLen == 0)
            return false;

        const QC::usize len = QC::String::strlen(path);
        if (len == 0 || len + 1 > outLen)
            return false;

        QC::String::memcpy(outPath, path, len);
        outPath[len] = '\0';
        return true;
    }

    QC::u64 Engine::pageOffset(const Database &database, QC::u32 pageId)
    {
        if (pageId == 0)
            return database.header.pageRegionOffset;
        return database.header.pageRegionOffset + (static_cast<QC::u64>(pageId - 1) * static_cast<QC::u64>(database.pageSize));
    }

    const char *Engine::lastDiagnostic() const
    {
        return s_lastDiagnostic;
    }

} // namespace QCQL
