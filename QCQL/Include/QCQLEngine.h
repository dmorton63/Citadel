#pragma once

// QCQL Engine - Core database engine shell
// Namespace: QCQL

#include "QCQLTypes.h"

namespace QCQL
{

    class Engine
    {
    public:
        static Engine &instance();

        void initialize();

        Status createDatabase(const char *path, Database &outDatabase);
        Status openDatabase(const char *path, Database &outDatabase);
        Status closeDatabase(Database &database);
        Status createTable(Database &database, const TableSchema &schema);

        Status allocatePage(Database &database, QC::u32 tableId, Page &outPage);
        Status loadPage(const Database &database, QC::u32 pageId, Page &outPage);
        Status flushPage(const Database &database, const Page &page);
        Status findPageForInsert(Database &database, QC::u32 tableId, QC::usize rowSizeNeeded, QC::u32 &outPageId);
        Status initializeSystemTables(Database &database);
        Status insertRow(Database &database, QC::u32 tableId, const Row &row,
                 QC::u32 *outPageId, QC::u16 *outRowOffset = nullptr);
        Status insertRow(Database &database, QC::u32 pageId, const Row &row, QC::u16 *outRowOffset = nullptr);
        Status readRow(const Database &database, QC::u32 pageId, QC::u16 rowOffset, Row &outRow);
        Status rebuildPrimaryKeyIndex(Database &database, QC::u32 tableId);
        Status findByPrimaryKey(const Database &database, QC::u32 tableId,
                    const QC::Vector<QC::u8> &key,
                    QC::u32 &outPageId,
                    QC::u16 &outRowOffset) const;
        Status selectRowByPrimaryKey(const Database &database,
                                     QC::u32 tableId,
                                     const QC::Vector<QC::u8> &key,
                                     Row &outRow) const;
        Status updateRowByPrimaryKey(Database &database,
                                     QC::u32 tableId,
                                     const QC::Vector<QC::u8> &key,
                                     const Row &updatedRow);
        Status removeRowByPrimaryKey(Database &database,
                                     QC::u32 tableId,
                                     const QC::Vector<QC::u8> &key);

        // Named-table helpers — resolve tableName → tableId, then forward
        Status lookupTableId(const Database &database,
                             const char *tableName,
                             QC::u32 &outTableId) const;
        Status insertRowByName(Database &database,
                               const char *tableName,
                               const Row &row,
                               QC::u32 *outPageId = nullptr,
                               QC::u16 *outRowOffset = nullptr);
        Status selectRowByPrimaryKeyByName(const Database &database,
                                           const char *tableName,
                                           const QC::Vector<QC::u8> &key,
                                           Row &outRow) const;
        Status updateRowByPrimaryKeyByName(Database &database,
                                           const char *tableName,
                                           const QC::Vector<QC::u8> &key,
                                           const Row &updatedRow);
        Status removeRowByPrimaryKeyByName(Database &database,
                                           const char *tableName,
                                           const QC::Vector<QC::u8> &key);

        Engine() = default;
        Engine(const Engine &) = delete;
        Engine &operator=(const Engine &) = delete;

        bool m_initialized = false;

        Status initializeDatabaseHeader(Database &database) const;
        Status persistMetadata(const Database &database) const;
        Status loadMetadata(Database &database) const;
        static bool copyPath(const char *path, char *outPath, QC::usize outLen);
        static QC::u64 pageOffset(const Database &database, QC::u32 pageId);
    };

} // namespace QCQL
