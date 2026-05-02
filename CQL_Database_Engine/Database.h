#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <memory>
#include "FileHeader.h"
#include "Table.h"
#include "SQLParser.h"

namespace CQL {

    class Database {
        friend class QueryExecutor;  // Allow QueryExecutor to access private Parse methods

    public:
        Database();
        ~Database();

        // Lifecycle
        bool Create(const std::string& filepath);
        bool Open(const std::string& filepath);
        void Close();
        bool Reopen(); // Close and reopen current database for clean state
        bool IsOpen() const { return isOpen; }

        // File info
        const std::string& GetPath() const { return filePath; }
        const FileHeader& GetHeader() const { return header; }
        const std::vector<std::shared_ptr<Table>>& GetTables() const { return tables; }

        // DDL Operations
        bool CreateTable(const std::string& tableName, const std::vector<ColumnInfo>& columns);
        bool DropTable(const std::string& tableName);
        bool AlterTableAddColumn(const std::string& tableName, const std::string& columnName,
                                 ColumnType columnType, uint16_t columnSize, const std::string& defaultValue);

        // Query Execution
        std::string ExecuteQuery(const std::string& query);

        // Diagnostic Commands
        std::string DumpFileHeader();
        std::string DumpTableDir();
        std::string DumpSchema();
        std::string DumpPageRegion();
        std::string DumpTable(const std::string& tableName);
        std::string DumpTablesLoaded();

    private:
        std::string filePath;
        std::fstream file;
        FileHeader header;
        bool isOpen = false;
        std::vector<std::shared_ptr<Table>> tables;

        // Helper for table creation
        bool ValidateTableSchema(const std::string& tableName, const std::vector<ColumnInfo>& columns);
        std::string ParseCreateTable(const std::string& query);
        std::string ParseDropTable(const std::string& query);
        std::string ParseAlterTable(const std::string& query);

        // Helper for data insertion
        std::string ParseInsertInto(const std::string& query);
        bool InsertRow(const std::string& tableName, const std::vector<std::string>& values);
        std::shared_ptr<Table> FindTable(const std::string& tableName);

        // Helper for data retrieval
        std::string ParseSelect(const std::string& query);
        std::string SelectRows(const std::string& tableName, const std::vector<std::string>& columnNames, 
                               const std::string& whereColumn, const std::string& whereValue,
                               const std::string& whereOperator,
                               const std::string& orderByColumn, bool orderByAscending,
                               int limit, int offset);
        std::string SelectJoinRows(const std::string& leftTableName, const std::string& rightTableName,
                                   const std::vector<std::string>& columnNames,
                                   JoinType joinType, const std::string& leftTableAlias, const std::string& rightTableAlias,
                                   const std::string& joinOnLeftColumn, const std::string& joinOnRightColumn,
                                   const std::string& whereColumn, const std::string& whereValue,
                                   const std::string& whereOperator,
                                   const std::string& orderByColumn, bool orderByAscending,
                                   int limit, int offset);

        // Helper for data update
        std::string ParseUpdate(const std::string& query);
        bool UpdateRow(const std::string& tableName, const std::vector<std::string>& setColumns,
                       const std::vector<std::string>& setValues, const std::string& whereColumn,
                       const std::string& whereValue);

        // Helper for data deletion
        std::string ParseDelete(const std::string& query);
        bool DeleteRow(const std::string& tableName, const std::string& whereColumn,
                       const std::string& whereValue);
    };
} // namespace CQL