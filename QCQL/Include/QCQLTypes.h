#pragma once

// QCQL Types - Core type and schema definitions
// Namespace: QCQL

#include "QCTypes.h"
#include "QCVector.h"

namespace QCQL
{

    static constexpr QC::u32 kPageSizeDefault = 4096;
    static constexpr QC::u32 kMaxTables = 64;
    static constexpr QC::u32 kMaxColumnsPerTable = 16;
    static constexpr QC::u32 kMaxForeignKeysPerTable = 8;
    static constexpr QC::u32 kFileVersion1 = 1;
    static constexpr QC::u32 kFileVersion2 = 2;
    static constexpr QC::u32 kCurrentFileVersion = kFileVersion2;

    enum class Status : QC::i32
    {
        Success = 0,
        Error = -1,
        InvalidParam = -2,
        NotFound = -3,
        PermissionDenied = -4,
        AlreadyExists = -5,
        OutOfMemory = -6,
        NotSupported = -7,
        Corrupt = -8,
        ConstraintViolation = -9
    };

    enum class ColumnType : QC::u8
    {
        Text = 0,
        Int = 1,
        Bool = 2,
        DateTime = 3
    };

    struct FileHeader
    {
        char magic[8] = {'C', 'Q', 'L', 'D', 'B', '\0', '\0', '\0'};
        QC::u32 version = kCurrentFileVersion;
        QC::u32 pageSize = kPageSizeDefault;
        QC::u32 tableCount = 0;
        QC::u64 tableDirOffset = 0;
        QC::u64 schemaOffset = 0;
        QC::u64 pageRegionOffset = 0;
        QC::u8 reserved[208] = {};
    } __attribute__((aligned(8)));

    struct TableEntry
    {
        char name[48] = {};
        QC::u64 schemaOffset = 0;
        QC::u64 rootPage = 0;
        QC::u32 flags = 0;
        QC::u8 reserved[32] = {};
    };

    struct ColumnDef
    {
        char name[48] = {};
        QC::u8 type = 0;
        QC::u8 isPrimaryKey = 0;
        QC::u8 reserved[14] = {};
    };

    struct TableSchemaDiskV1
    {
        char tableName[48] = {};
        QC::u32 tableId = 0;
        QC::u32 columnCount = 0;
        QC::u32 primaryKeyIndex = 0;
        ColumnDef columns[kMaxColumnsPerTable] = {};
    };

    struct ForeignKeyDef
    {
        char columnName[48] = {};
        char referencedTable[48] = {};
        char referencedColumn[48] = {};
        QC::u8 onDelete = 0;
        QC::u8 onUpdate = 0;
        QC::u8 reserved[14] = {};
    };

    struct TableSchemaDisk
    {
        char tableName[48] = {};
        QC::u32 tableId = 0;
        QC::u32 columnCount = 0;
        QC::u32 primaryKeyIndex = 0;
        QC::u32 foreignKeyCount = 0;
        QC::u8 reserved[12] = {};
        ColumnDef columns[kMaxColumnsPerTable] = {};
        ForeignKeyDef foreignKeys[kMaxForeignKeysPerTable] = {};
    };

    struct PageHeader
    {
        QC::u32 pageId = 0;
        QC::u32 tableId = 0;
        QC::u32 rowCount = 0;
        QC::u32 freeOffset = 0;
        QC::u8 flags = 0;
        QC::u8 reserved[15] = {};
    };

    struct TablePermission
    {
        char tableName[48] = {};
        bool canRead = false;
        bool canWrite = false;
        bool canDelete = false;
        bool canAdmin = false;
    };

    struct DbHandle
    {
        QC::u64 dbId = 0;
        QC::u32 callerProcessId = 0;
        QC::Vector<TablePermission> tablePermissions;
    };

    struct Cell
    {
        ColumnType type = ColumnType::Text;
        QC::Vector<QC::u8> bytes;
    };

    struct Row
    {
        QC::Vector<Cell> cells;
        bool tombstone = false;
    };

    struct Column
    {
        char name[48] = {};
        ColumnType type = ColumnType::Text;
        bool isPrimaryKey = false;
    };

    enum class ReferentialAction : QC::u8
    {
        Restrict = 0
    };

    struct ForeignKey
    {
        char columnName[48] = {};
        char referencedTable[48] = {};
        char referencedColumn[48] = {};
        ReferentialAction onDelete = ReferentialAction::Restrict;
        ReferentialAction onUpdate = ReferentialAction::Restrict;
    };

    struct TableSchema
    {
        char tableName[48] = {};
        QC::Vector<Column> columns;
        QC::Vector<ForeignKey> foreignKeys;
        QC::u32 primaryKeyIndex = 0;
    };

    struct Page
    {
        PageHeader header;
        QC::Vector<QC::u16> rowOffsets;
        QC::Vector<QC::u8> data;
        bool dirty = false;
    };

    struct PrimaryKeyIndexEntry
    {
        QC::Vector<QC::u8> key;
        QC::u32 pageId = 0;
        QC::u16 rowOffset = 0;
    };

    struct PrimaryKeyIndex
    {
        QC::Vector<PrimaryKeyIndexEntry> entries;
    };

    struct Table
    {
        char name[48] = {};
        TableSchema schema;
        QC::u64 rootPage = 0;
        QC::u32 tableId = 0;
        QC::u32 flags = 0;
        PrimaryKeyIndex primaryKeyIndex;
        QC::Vector<QC::u32> pages;
    };

    struct Database
    {
        char path[192] = {};
        FileHeader header;
        QC::Vector<Table> tables;
        QC::u32 pageSize = kPageSizeDefault;
        QC::u32 nextPageId = 1;
    };

    struct OpenStats
    {
        QC::u32 metadataTablesLoaded = 0;
        QC::u32 metadataPageHeadersScanned = 0;
        QC::u32 pkTablesRebuilt = 0;
        QC::u32 pkPagesLoaded = 0;
        QC::u32 pkRowsScanned = 0;
        QC::u32 pkRowsIndexed = 0;
    };

} // namespace QCQL
