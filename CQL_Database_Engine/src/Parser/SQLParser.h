#pragma once

#include "FileHeader.h"  // For ColumnType enum
#include <string>
#include <vector>

namespace CQL {

    // Column definition for in-memory representation
    struct ColumnInfo {
        std::string name;
        ColumnType type;
        bool isPrimaryKey;
        uint16_t size;  // For sized types (CHAR, VARCHAR, etc.)

        ColumnInfo(const std::string& n, ColumnType t, bool pk = false, uint16_t sz = 0)
            : name(n), type(t), isPrimaryKey(pk), size(sz) {}
    };

    // Command result structures
    struct CreateTableCommand {
        bool valid = false;
        std::string errorMessage;
        std::string tableName;
        std::vector<ColumnInfo> columns;
    };

    struct DropTableCommand {
        bool valid = false;
        std::string errorMessage;
        std::string tableName;
        bool ifExists = false;  // Support for DROP TABLE IF EXISTS
    };

    struct InsertCommand {
        bool valid = false;
        std::string errorMessage;
        std::string tableName;
        std::vector<std::string> values;
    };

    enum class JoinType {
        NONE,
        INNER,
        LEFT,
        RIGHT
    };

    // Single WHERE condition (for compound WHERE support)
    struct WhereCondition {
        std::string column;
        std::string value;
        std::string op; // =, !=, <>, <, >, <=, >=

        WhereCondition() : op("=") {}
        WhereCondition(const std::string& col, const std::string& val, const std::string& operation)
            : column(col), value(val), op(operation) {}
    };

    struct SelectCommand {
        bool valid = false;
        std::string errorMessage;
        std::string tableName;
        std::vector<std::string> columnNames;

        // Legacy single WHERE condition (kept for backward compatibility)
        std::string whereColumn;
        std::string whereValue;
        std::string whereOperator = "=";       // Comparison operator: =, !=, <>, <, >, <=, >=

        // Compound WHERE support (AND/OR logic)
        std::vector<WhereCondition> whereConditions;  // Multiple conditions
        std::vector<std::string> whereLogicalOps;     // "AND" or "OR" between conditions

        std::string orderByColumn;              // Column to sort by
        bool orderByAscending = true;           // true = ASC, false = DESC
        int limit = -1;                         // Max rows to return (-1 = no limit)
        int offset = 0;                         // Rows to skip (default 0)

        // JOIN support
        JoinType joinType = JoinType::NONE;
        std::string joinTableName;
        std::string leftTableAlias;
        std::string rightTableAlias;
        std::string joinOnLeftColumn;
        std::string joinOnRightColumn;
    };

    struct UpdateCommand {
        bool valid = false;
        std::string errorMessage;
        std::string tableName;
        std::vector<std::string> setColumns;    // Column names to update
        std::vector<std::string> setValues;     // New values for columns
        std::string whereColumn;                // WHERE clause column (typically primary key)
        std::string whereValue;                 // WHERE clause value
    };

    struct DeleteCommand {
        bool valid = false;
        std::string errorMessage;
        std::string tableName;
        std::string whereColumn;                // WHERE clause column
        std::string whereValue;                 // WHERE clause value
    };

    struct AlterTableCommand {
        bool valid = false;
        std::string errorMessage;
        std::string tableName;
        std::string columnName;
        ColumnType columnType = ColumnType::INT;
        uint16_t columnSize = 0;
        std::string defaultValue;               // Optional default value
    };

    // SQL Parser class
    class SQLParser {
    public:
        // Parse CREATE TABLE statement
        static CreateTableCommand ParseCreateTable(const std::string& query);

        // Parse DROP TABLE statement
        static DropTableCommand ParseDropTable(const std::string& query);

        // Parse ALTER TABLE ADD COLUMN statement
        static AlterTableCommand ParseAlterTableAddColumn(const std::string& query);

        // Parse INSERT INTO statement
        static InsertCommand ParseInsertInto(const std::string& query);

        // Parse SELECT statement
        static SelectCommand ParseSelect(const std::string& query);

        // Parse UPDATE statement
        static UpdateCommand ParseUpdate(const std::string& query);

        // Parse DELETE statement
        static DeleteCommand ParseDelete(const std::string& query);

        // Helper: Remove SQL comments (-- style) from query
        static std::string RemoveComments(const std::string& query);

    private:
        // Helper: Map type name string to ColumnType enum
        static bool MapColumnType(const std::string& typeName, ColumnType& outType, bool& outRequiresSize);

        // Helper: Find matching closing parenthesis with depth tracking
        static size_t FindMatchingParen(const std::string& str, size_t openParenPos);

        // Helper: Trim whitespace from string
        static std::string Trim(const std::string& str);

        // Helper: Parse comma-separated values (handles quotes)
        static std::vector<std::string> ParseValueList(const std::string& valueList);
    };

} // namespace CQL
