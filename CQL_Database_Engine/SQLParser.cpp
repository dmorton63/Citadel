#include "SQLParser.h"
#include <cctype>
#include <algorithm>

namespace CQL {

    // ========== PUBLIC PARSER METHODS ==========

    CreateTableCommand SQLParser::ParseCreateTable(const std::string& query) {
        // Parse: CREATE TABLE name ( col1 type [PRIMARY KEY], col2 type, ... );

        size_t tableNameStart = query.find("TABLE");
        if (tableNameStart == std::string::npos) {
            tableNameStart = query.find("table");
        }
        if (tableNameStart == std::string::npos) {
            return {false, "Invalid CREATE TABLE syntax", "", {}};
        }

        tableNameStart += 5; // Skip "TABLE"

        // Find table name
        while (tableNameStart < query.length() && isspace(query[tableNameStart])) {
            tableNameStart++;
        }

        size_t tableNameEnd = tableNameStart;
        while (tableNameEnd < query.length() && 
               (isalnum(query[tableNameEnd]) || query[tableNameEnd] == '_')) {
            tableNameEnd++;
        }

        if (tableNameStart == tableNameEnd) {
            return {false, "No table name found", "", {}};
        }

        std::string tableName = query.substr(tableNameStart, tableNameEnd - tableNameStart);

        // Skip whitespace/newlines after table name to find opening parenthesis
        size_t parenStart = tableNameEnd;
        while (parenStart < query.length() && isspace(query[parenStart])) {
            parenStart++;
        }

        // Find column definitions between ( )
        if (parenStart >= query.length() || query[parenStart] != '(') {
            return {false, "Missing opening parenthesis for column definitions", "", {}};
        }

        // Find matching closing parenthesis (handle nested parens like VARCHAR(100))
        size_t parenEnd = FindMatchingParen(query, parenStart);
        if (parenEnd == std::string::npos) {
            return {false, "Missing closing parenthesis for column definitions", "", {}};
        }

        std::string colDefs = query.substr(parenStart + 1, parenEnd - parenStart - 1);

        // Parse column definitions (split by comma)
        std::vector<ColumnInfo> columns;
        size_t pos = 0;

        while (pos < colDefs.length()) {
            // Skip whitespace
            while (pos < colDefs.length() && isspace(colDefs[pos])) pos++;

            // Find column name
            size_t colNameStart = pos;
            while (pos < colDefs.length() && (isalnum(colDefs[pos]) || colDefs[pos] == '_')) pos++;

            if (colNameStart == pos) break; // No more columns

            std::string colName = colDefs.substr(colNameStart, pos - colNameStart);

            // Skip whitespace
            while (pos < colDefs.length() && isspace(colDefs[pos])) pos++;

            // Find column type
            size_t typeStart = pos;
            while (pos < colDefs.length() && isalpha(colDefs[pos])) pos++;

            if (typeStart == pos) {
                return {false, "Missing type for column " + colName, "", {}};
            }

            std::string typeName = colDefs.substr(typeStart, pos - typeStart);

            // Convert to uppercase for comparison
            for (auto& c : typeName) c = toupper(c);

            // Check for size specification like CHAR(50)
            uint16_t typeSize = 0;
            while (pos < colDefs.length() && isspace(colDefs[pos])) pos++;

            if (pos < colDefs.length() && colDefs[pos] == '(') {
                pos++; // Skip '('
                size_t sizeStart = pos;
                while (pos < colDefs.length() && isdigit(colDefs[pos])) pos++;

                if (pos > sizeStart) {
                    std::string sizeStr = colDefs.substr(sizeStart, pos - sizeStart);
                    typeSize = static_cast<uint16_t>(std::stoi(sizeStr));
                }

                // Skip to closing paren
                while (pos < colDefs.length() && colDefs[pos] != ')') pos++;
                if (pos < colDefs.length() && colDefs[pos] == ')') pos++;
            }

            // Map type name to ColumnType
            ColumnType colType;
            bool requiresSize = false;
            if (!MapColumnType(typeName, colType, requiresSize)) {
                return {false, "Unknown type '" + typeName + "' for column " + colName, "", {}};
            }

            // Validate size for types that require it
            if (requiresSize && typeSize == 0) {
                return {false, "Type " + typeName + " requires a size specification for column " + colName, "", {}};
            }
            if (!requiresSize && typeSize > 0) {
                return {false, "Type " + typeName + " does not accept a size specification for column " + colName, "", {}};
            }

            // Check for PRIMARY KEY constraint
            bool isPK = false;
            while (pos < colDefs.length() && isspace(colDefs[pos])) pos++;

            if (pos < colDefs.length() - 11) {
                std::string pkCheck = colDefs.substr(pos, 11);
                for (auto& c : pkCheck) c = toupper(c);

                if (pkCheck == "PRIMARY KEY") {
                    isPK = true;
                    pos += 11;
                }
            }

            // Add column
            columns.push_back(ColumnInfo(colName, colType, isPK, typeSize));

            // Skip to next column (find comma)
            while (pos < colDefs.length() && colDefs[pos] != ',') pos++;
            if (pos < colDefs.length() && colDefs[pos] == ',') pos++; // Skip comma
        }

        if (columns.empty()) {
            return {false, "No columns defined", "", {}};
        }

        return {true, "", tableName, columns};
    }

    DropTableCommand SQLParser::ParseDropTable(const std::string& query) {
        // Parse: DROP TABLE TableName

        size_t tableNameStart = query.find("TABLE");
        if (tableNameStart == std::string::npos) {
            tableNameStart = query.find("table");
        }
        if (tableNameStart == std::string::npos) {
            return {false, "Invalid DROP TABLE syntax", ""};
        }

        tableNameStart += 5; // Skip "TABLE"

        // Find table name
        while (tableNameStart < query.length() && isspace(query[tableNameStart])) {
            tableNameStart++;
        }

        size_t tableNameEnd = tableNameStart;
        while (tableNameEnd < query.length() && 
               (isalnum(query[tableNameEnd]) || query[tableNameEnd] == '_')) {
            tableNameEnd++;
        }

        if (tableNameStart == tableNameEnd) {
            return {false, "No table name found", ""};
        }

        std::string tableName = query.substr(tableNameStart, tableNameEnd - tableNameStart);

        return {true, "", tableName};
    }

    AlterTableCommand SQLParser::ParseAlterTableAddColumn(const std::string& query) {
        // Parse: ALTER TABLE TableName ADD COLUMN ColumnName TYPE [DEFAULT value]

        AlterTableCommand cmd;

        // Find TABLE keyword
        size_t tablePos = query.find("TABLE");
        if (tablePos == std::string::npos) {
            tablePos = query.find("table");
        }
        if (tablePos == std::string::npos) {
            cmd.errorMessage = "TABLE keyword not found";
            return cmd;
        }

        // Find table name
        size_t tableNameStart = tablePos + 5; // Skip "TABLE"
        while (tableNameStart < query.length() && isspace(query[tableNameStart])) {
            tableNameStart++;
        }

        size_t tableNameEnd = tableNameStart;
        while (tableNameEnd < query.length() && 
               (isalnum(query[tableNameEnd]) || query[tableNameEnd] == '_')) {
            tableNameEnd++;
        }

        if (tableNameStart == tableNameEnd) {
            cmd.errorMessage = "No table name found";
            return cmd;
        }

        cmd.tableName = query.substr(tableNameStart, tableNameEnd - tableNameStart);

        // Find ADD COLUMN keywords
        size_t addPos = query.find("ADD", tableNameEnd);
        if (addPos == std::string::npos) {
            addPos = query.find("add", tableNameEnd);
        }
        if (addPos == std::string::npos) {
            cmd.errorMessage = "ADD keyword not found";
            return cmd;
        }

        size_t columnPos = query.find("COLUMN", addPos);
        if (columnPos == std::string::npos) {
            columnPos = query.find("column", addPos);
        }
        if (columnPos == std::string::npos) {
            cmd.errorMessage = "COLUMN keyword not found";
            return cmd;
        }

        // Find column name
        size_t colNameStart = columnPos + 6; // Skip "COLUMN"
        while (colNameStart < query.length() && isspace(query[colNameStart])) {
            colNameStart++;
        }

        size_t colNameEnd = colNameStart;
        while (colNameEnd < query.length() && 
               (isalnum(query[colNameEnd]) || query[colNameEnd] == '_')) {
            colNameEnd++;
        }

        if (colNameStart == colNameEnd) {
            cmd.errorMessage = "No column name found";
            return cmd;
        }

        cmd.columnName = query.substr(colNameStart, colNameEnd - colNameStart);

        // Find column type
        size_t typeStart = colNameEnd;
        while (typeStart < query.length() && isspace(query[typeStart])) {
            typeStart++;
        }

        size_t typeEnd = typeStart;
        while (typeEnd < query.length() && 
               (isalpha(query[typeEnd]) || isdigit(query[typeEnd]))) {
            typeEnd++;
        }

        if (typeStart == typeEnd) {
            cmd.errorMessage = "No column type found";
            return cmd;
        }

        std::string typeStr = query.substr(typeStart, typeEnd - typeStart);

        // Convert to uppercase for comparison
        for (auto& c : typeStr) c = toupper(c);

        // Check for size in parentheses (e.g., VARCHAR(50))
        bool requiresSize = false;
        if (!MapColumnType(typeStr, cmd.columnType, requiresSize)) {
            cmd.errorMessage = "Unknown column type: " + typeStr;
            return cmd;
        }

        // Parse size if required (VARCHAR, CHAR, etc.)
        if (requiresSize) {
            size_t sizeStart = query.find('(', typeEnd);
            if (sizeStart == std::string::npos) {
                cmd.errorMessage = "Column type " + typeStr + " requires a size, e.g., " + typeStr + "(50)";
                return cmd;
            }

            size_t sizeEnd = query.find(')', sizeStart);
            if (sizeEnd == std::string::npos) {
                cmd.errorMessage = "Missing closing parenthesis for column size";
                return cmd;
            }

            std::string sizeStr = query.substr(sizeStart + 1, sizeEnd - sizeStart - 1);
            try {
                cmd.columnSize = static_cast<uint16_t>(std::stoi(sizeStr));
            } catch (...) {
                cmd.errorMessage = "Invalid column size: " + sizeStr;
                return cmd;
            }

            typeEnd = sizeEnd + 1;
        }

        // Parse optional DEFAULT value
        size_t defaultPos = query.find("DEFAULT", typeEnd);
        if (defaultPos == std::string::npos) {
            defaultPos = query.find("default", typeEnd);
        }

        if (defaultPos != std::string::npos) {
            size_t defaultStart = defaultPos + 7; // Skip "DEFAULT"
            while (defaultStart < query.length() && isspace(query[defaultStart])) {
                defaultStart++;
            }

            // Check for quoted string
            bool inQuotes = false;
            if (defaultStart < query.length() && 
                (query[defaultStart] == '\'' || query[defaultStart] == '"')) {
                inQuotes = true;
                defaultStart++;
            }

            size_t defaultEnd = defaultStart;
            if (inQuotes) {
                while (defaultEnd < query.length() && 
                       query[defaultEnd] != '\'' && query[defaultEnd] != '"') {
                    defaultEnd++;
                }
            } else {
                while (defaultEnd < query.length() && 
                       !isspace(query[defaultEnd]) && query[defaultEnd] != ';') {
                    defaultEnd++;
                }
            }

            cmd.defaultValue = query.substr(defaultStart, defaultEnd - defaultStart);
        }

        cmd.valid = true;
        return cmd;
    }

    InsertCommand SQLParser::ParseInsertInto(const std::string& query) {
        // Parse: INSERT INTO TableName VALUES (val1, val2, ...)

        size_t intoPos = query.find("INTO");
        if (intoPos == std::string::npos) {
            intoPos = query.find("into");
        }
        if (intoPos == std::string::npos) {
            return {false, "Invalid INSERT INTO syntax", "", {}};
        }

        // Find table name
        size_t tableNameStart = intoPos + 4; // Skip "INTO"
        while (tableNameStart < query.length() && isspace(query[tableNameStart])) {
            tableNameStart++;
        }

        size_t tableNameEnd = tableNameStart;
        while (tableNameEnd < query.length() && 
               (isalnum(query[tableNameEnd]) || query[tableNameEnd] == '_')) {
            tableNameEnd++;
        }

        if (tableNameStart == tableNameEnd) {
            return {false, "No table name found", "", {}};
        }

        std::string tableName = query.substr(tableNameStart, tableNameEnd - tableNameStart);

        // Find VALUES keyword
        size_t valuesPos = query.find("VALUES", tableNameEnd);
        if (valuesPos == std::string::npos) {
            valuesPos = query.find("values", tableNameEnd);
        }
        if (valuesPos == std::string::npos) {
            return {false, "VALUES keyword not found", "", {}};
        }

        // Find value list between ( )
        size_t parenStart = query.find('(', valuesPos);
        if (parenStart == std::string::npos) {
            return {false, "Missing opening parenthesis for value list", "", {}};
        }

        // Find matching closing parenthesis (handle quotes)
        size_t parenEnd = std::string::npos;
        int parenDepth = 1;
        bool inQuotes = false;
        for (size_t i = parenStart + 1; i < query.length(); i++) {
            if (query[i] == '\'' || query[i] == '"') {
                inQuotes = !inQuotes;
            }
            if (!inQuotes) {
                if (query[i] == '(') parenDepth++;
                else if (query[i] == ')') {
                    parenDepth--;
                    if (parenDepth == 0) {
                        parenEnd = i;
                        break;
                    }
                }
            }
        }

        if (parenEnd == std::string::npos) {
            return {false, "Missing closing parenthesis for value list", "", {}};
        }

        std::string valueList = query.substr(parenStart + 1, parenEnd - parenStart - 1);

        // Parse values (comma-separated, handle quoted strings)
        std::vector<std::string> values = ParseValueList(valueList);

        return {true, "", tableName, values};
    }

    SelectCommand SQLParser::ParseSelect(const std::string& query) {
        // Parse: SELECT * FROM TableName
        //        SELECT col1, col2 FROM TableName
        //        SELECT * FROM TableName WHERE PrimaryKey = value

        size_t selectPos = 6; // Skip "SELECT"
        while (selectPos < query.length() && isspace(query[selectPos])) selectPos++;

        // Parse column list
        std::vector<std::string> columnNames;
        size_t fromPos = query.find("FROM");
        if (fromPos == std::string::npos) {
            fromPos = query.find("from");
        }
        if (fromPos == std::string::npos) {
            return {false, "FROM keyword not found", "", {}, "", ""};
        }

        std::string columnList = query.substr(selectPos, fromPos - selectPos);

        // Trim whitespace
        columnList = Trim(columnList);

        if (columnList == "*") {
            columnNames.push_back("*");
        } else {
            // Parse comma-separated column names
            size_t pos = 0;
            std::string currentCol;
            while (pos < columnList.length()) {
                char c = columnList[pos];
                if (c == ',') {
                    currentCol = Trim(currentCol);
                    if (!currentCol.empty()) {
                        columnNames.push_back(currentCol);
                    }
                    currentCol.clear();
                } else {
                    currentCol += c;
                }
                pos++;
            }
            // Add last column
            if (!currentCol.empty()) {
                currentCol = Trim(currentCol);
                if (!currentCol.empty()) {
                    columnNames.push_back(currentCol);
                }
            }
        }

        // Parse table name
        size_t tableNameStart = fromPos + 4; // Skip "FROM"
        while (tableNameStart < query.length() && isspace(query[tableNameStart])) {
            tableNameStart++;
        }

        size_t tableNameEnd = tableNameStart;
        while (tableNameEnd < query.length() && 
               (isalnum(query[tableNameEnd]) || query[tableNameEnd] == '_')) {
            tableNameEnd++;
        }

        if (tableNameStart == tableNameEnd) {
            return {false, "No table name found", "", {}, "", ""};
        }

        std::string tableName = query.substr(tableNameStart, tableNameEnd - tableNameStart);

        // Parse optional AS alias for first table
        std::string leftTableAlias;
        size_t afterTablePos = tableNameEnd;
        while (afterTablePos < query.length() && isspace(query[afterTablePos])) {
            afterTablePos++;
        }

        // Check for AS keyword
        if (afterTablePos + 2 < query.length()) {
            std::string maybeAS = query.substr(afterTablePos, 2);
            for (auto& c : maybeAS) c = toupper(c);
            if (maybeAS == "AS") {
                afterTablePos += 2;
                while (afterTablePos < query.length() && isspace(query[afterTablePos])) {
                    afterTablePos++;
                }
                // Parse alias
                size_t aliasStart = afterTablePos;
                while (afterTablePos < query.length() && 
                       (isalnum(query[afterTablePos]) || query[afterTablePos] == '_')) {
                    afterTablePos++;
                }
                leftTableAlias = query.substr(aliasStart, afterTablePos - aliasStart);
            }
        }

        // Parse optional JOIN clause
        JoinType joinType = JoinType::NONE;
        std::string joinTableName;
        std::string rightTableAlias;
        std::string joinOnLeftColumn;
        std::string joinOnRightColumn;

        size_t joinPos = afterTablePos;
        while (joinPos < query.length() && isspace(query[joinPos])) {
            joinPos++;
        }

        // Check for JOIN keyword (INNER JOIN, LEFT JOIN, RIGHT JOIN, or just JOIN)
        std::string upperQuery = query;
        for (auto& c : upperQuery) c = toupper(c);

        size_t innerJoinPos = upperQuery.find("INNER JOIN", joinPos);
        size_t leftJoinPos = upperQuery.find("LEFT JOIN", joinPos);
        size_t rightJoinPos = upperQuery.find("RIGHT JOIN", joinPos);
        size_t simpleJoinPos = upperQuery.find("JOIN", joinPos);

        // Make sure simple JOIN is not part of INNER/LEFT/RIGHT JOIN
        if (simpleJoinPos != std::string::npos) {
            if (simpleJoinPos == innerJoinPos + 6 || 
                simpleJoinPos == leftJoinPos + 5 || 
                simpleJoinPos == rightJoinPos + 6) {
                // This JOIN is part of a compound keyword
            } else if (innerJoinPos != std::string::npos || leftJoinPos != std::string::npos || rightJoinPos != std::string::npos) {
                // Use the specific join type instead
                simpleJoinPos = std::string::npos;
            }
        }

        size_t actualJoinPos = std::string::npos;
        size_t skipLength = 0;

        if (innerJoinPos != std::string::npos && 
            (actualJoinPos == std::string::npos || innerJoinPos < actualJoinPos)) {
            actualJoinPos = innerJoinPos;
            joinType = JoinType::INNER;
            skipLength = 10; // "INNER JOIN"
        }
        if (leftJoinPos != std::string::npos && 
            (actualJoinPos == std::string::npos || leftJoinPos < actualJoinPos)) {
            actualJoinPos = leftJoinPos;
            joinType = JoinType::LEFT;
            skipLength = 9; // "LEFT JOIN"
        }
        if (rightJoinPos != std::string::npos && 
            (actualJoinPos == std::string::npos || rightJoinPos < actualJoinPos)) {
            actualJoinPos = rightJoinPos;
            joinType = JoinType::RIGHT;
            skipLength = 10; // "RIGHT JOIN"
        }
        if (simpleJoinPos != std::string::npos && 
            (actualJoinPos == std::string::npos || simpleJoinPos < actualJoinPos)) {
            actualJoinPos = simpleJoinPos;
            joinType = JoinType::INNER; // Default JOIN to INNER JOIN
            skipLength = 4; // "JOIN"
        }

        if (actualJoinPos != std::string::npos) {
            // Parse second table name
            size_t joinTableStart = actualJoinPos + skipLength;
            while (joinTableStart < query.length() && isspace(query[joinTableStart])) {
                joinTableStart++;
            }

            size_t joinTableEnd = joinTableStart;
            while (joinTableEnd < query.length() && 
                   (isalnum(query[joinTableEnd]) || query[joinTableEnd] == '_')) {
                joinTableEnd++;
            }

            if (joinTableStart == joinTableEnd) {
                return {false, "No table name found after JOIN keyword", "", {}, "", ""};
            }

            joinTableName = query.substr(joinTableStart, joinTableEnd - joinTableStart);

            // Parse optional AS alias for second table
            size_t afterJoinTablePos = joinTableEnd;
            while (afterJoinTablePos < query.length() && isspace(query[afterJoinTablePos])) {
                afterJoinTablePos++;
            }

            if (afterJoinTablePos + 2 < query.length()) {
                std::string maybeAS = query.substr(afterJoinTablePos, 2);
                for (auto& c : maybeAS) c = toupper(c);
                if (maybeAS == "AS") {
                    afterJoinTablePos += 2;
                    while (afterJoinTablePos < query.length() && isspace(query[afterJoinTablePos])) {
                        afterJoinTablePos++;
                    }
                    // Parse alias
                    size_t aliasStart = afterJoinTablePos;
                    while (afterJoinTablePos < query.length() && 
                           (isalnum(query[afterJoinTablePos]) || query[afterJoinTablePos] == '_')) {
                        afterJoinTablePos++;
                    }
                    rightTableAlias = query.substr(aliasStart, afterJoinTablePos - aliasStart);
                }
            }

            // Parse ON condition
            size_t onPos = upperQuery.find("ON", afterJoinTablePos);
            if (onPos == std::string::npos) {
                return {false, "ON keyword not found after JOIN", "", {}, "", ""};
            }

            size_t onStart = onPos + 2; // Skip "ON"
            while (onStart < query.length() && isspace(query[onStart])) {
                onStart++;
            }

            // Parse left column (may include table/alias prefix)
            size_t leftColStart = onStart;
            while (onStart < query.length() && 
                   (isalnum(query[onStart]) || query[onStart] == '_' || query[onStart] == '.')) {
                onStart++;
            }
            joinOnLeftColumn = query.substr(leftColStart, onStart - leftColStart);

            // Skip whitespace and expect '='
            while (onStart < query.length() && isspace(query[onStart])) {
                onStart++;
            }

            if (onStart >= query.length() || query[onStart] != '=') {
                return {false, "Expected '=' in JOIN ON condition", "", {}, "", ""};
            }
            onStart++; // Skip '='

            while (onStart < query.length() && isspace(query[onStart])) {
                onStart++;
            }

            // Parse right column (may include table/alias prefix)
            size_t rightColStart = onStart;
            while (onStart < query.length() && 
                   (isalnum(query[onStart]) || query[onStart] == '_' || query[onStart] == '.')) {
                onStart++;
            }
            joinOnRightColumn = query.substr(rightColStart, onStart - rightColStart);

            // Update tableNameEnd to continue parsing WHERE/ORDER BY/etc from after ON clause
            tableNameEnd = onStart;
        }

        // Parse optional WHERE clause
        std::string whereColumn;
        std::string whereValue;
        std::string whereOperator = "=";

        size_t wherePos = query.find("WHERE", tableNameEnd);
        if (wherePos == std::string::npos) {
            wherePos = query.find("where", tableNameEnd);
        }

        if (wherePos != std::string::npos) {
            size_t condStart = wherePos + 5; // Skip "WHERE"
            while (condStart < query.length() && isspace(query[condStart])) condStart++;

            // Find column name (may include table/alias prefix with dot)
            size_t colStart = condStart;
            while (condStart < query.length() && 
                   (isalnum(query[condStart]) || query[condStart] == '_' || query[condStart] == '.')) {
                condStart++;
            }

            whereColumn = query.substr(colStart, condStart - colStart);

            // Skip whitespace and parse operator
            while (condStart < query.length() && isspace(query[condStart])) condStart++;

            // Parse comparison operator (=, !=, <>, <, >, <=, >=)
            if (condStart < query.length()) {
                if (query[condStart] == '=') {
                    whereOperator = "=";
                    condStart++;
                }
                else if (condStart + 1 < query.length() && query[condStart] == '!' && query[condStart + 1] == '=') {
                    whereOperator = "!=";
                    condStart += 2;
                }
                else if (condStart + 1 < query.length() && query[condStart] == '<' && query[condStart + 1] == '>') {
                    whereOperator = "<>";
                    condStart += 2;
                }
                else if (condStart + 1 < query.length() && query[condStart] == '<' && query[condStart + 1] == '=') {
                    whereOperator = "<=";
                    condStart += 2;
                }
                else if (condStart + 1 < query.length() && query[condStart] == '>' && query[condStart + 1] == '=') {
                    whereOperator = ">=";
                    condStart += 2;
                }
                else if (query[condStart] == '<') {
                    whereOperator = "<";
                    condStart++;
                }
                else if (query[condStart] == '>') {
                    whereOperator = ">";
                    condStart++;
                }
            }

            while (condStart < query.length() && isspace(query[condStart])) condStart++;

            // Parse value (handle quotes)
            bool inQuotes = false;
            if (condStart < query.length() && (query[condStart] == '\'' || query[condStart] == '"')) {
                inQuotes = true;
                condStart++;
            }

            size_t valStart = condStart;
            while (condStart < query.length()) {
                if (inQuotes && (query[condStart] == '\'' || query[condStart] == '"')) {
                    break;
                }
                if (!inQuotes && (isspace(query[condStart]) || query[condStart] == ';')) {
                    break;
                }
                condStart++;
            }

            whereValue = query.substr(valStart, condStart - valStart);
        }

        // Parse ORDER BY clause (optional)
        std::string orderByColumn;
        bool orderByAscending = true;

        size_t orderByPos = query.find("ORDER BY", wherePos != std::string::npos ? wherePos : tableNameEnd);
        if (orderByPos == std::string::npos) {
            orderByPos = query.find("order by", wherePos != std::string::npos ? wherePos : tableNameEnd);
        }

        if (orderByPos != std::string::npos) {
            size_t orderStart = orderByPos + 8; // Skip "ORDER BY"
            while (orderStart < query.length() && isspace(query[orderStart])) {
                orderStart++;
            }

            // Parse column name
            size_t orderColStart = orderStart;
            while (orderStart < query.length() && 
                   (isalnum(query[orderStart]) || query[orderStart] == '_' || query[orderStart] == '.')) {
                orderStart++;
            }

            if (orderColStart != orderStart) {
                orderByColumn = query.substr(orderColStart, orderStart - orderColStart);

                // Check for ASC/DESC
                while (orderStart < query.length() && isspace(query[orderStart])) {
                    orderStart++;
                }

                std::string remaining = query.substr(orderStart);
                std::string upperRemaining = remaining;
                for (auto& c : upperRemaining) c = toupper(c);

                if (upperRemaining.find("DESC") == 0) {
                    orderByAscending = false;
                } else if (upperRemaining.find("ASC") == 0) {
                    orderByAscending = true;
                }
                // Default is ASC if neither specified
            }
        }

        // Parse LIMIT and OFFSET clauses (optional)
        int limit = -1;
        int offset = 0;

        // Start searching after WHERE or ORDER BY or table name
        size_t searchPos = tableNameEnd;
        if (wherePos != std::string::npos) searchPos = wherePos;
        if (orderByPos != std::string::npos) searchPos = orderByPos;

        // Look for LIMIT
        size_t limitPos = query.find("LIMIT", searchPos);
        if (limitPos == std::string::npos) {
            limitPos = query.find("limit", searchPos);
        }

        if (limitPos != std::string::npos) {
            size_t limitStart = limitPos + 5; // Skip "LIMIT"
            while (limitStart < query.length() && isspace(query[limitStart])) {
                limitStart++;
            }

            // Parse limit number
            size_t limitEnd = limitStart;
            while (limitEnd < query.length() && isdigit(query[limitEnd])) {
                limitEnd++;
            }

            if (limitEnd > limitStart) {
                std::string limitStr = query.substr(limitStart, limitEnd - limitStart);
                try {
                    limit = std::stoi(limitStr);
                    if (limit < 0) limit = -1; // Invalid, treat as no limit
                } catch (...) {
                    limit = -1;
                }
            }
        }

        // Look for OFFSET
        size_t offsetPos = query.find("OFFSET", searchPos);
        if (offsetPos == std::string::npos) {
            offsetPos = query.find("offset", searchPos);
        }

        if (offsetPos != std::string::npos) {
            size_t offsetStart = offsetPos + 6; // Skip "OFFSET"
            while (offsetStart < query.length() && isspace(query[offsetStart])) {
                offsetStart++;
            }

            // Parse offset number
            size_t offsetEnd = offsetStart;
            while (offsetEnd < query.length() && isdigit(query[offsetEnd])) {
                offsetEnd++;
            }

            if (offsetEnd > offsetStart) {
                std::string offsetStr = query.substr(offsetStart, offsetEnd - offsetStart);
                try {
                    offset = std::stoi(offsetStr);
                    if (offset < 0) offset = 0; // Invalid, treat as no offset
                } catch (...) {
                    offset = 0;
                }
            }
        }

        SelectCommand cmd;
        cmd.valid = true;
        cmd.tableName = tableName;
        cmd.columnNames = columnNames;
        cmd.whereColumn = whereColumn;
        cmd.whereValue = whereValue;
        cmd.whereOperator = whereOperator;
        cmd.orderByColumn = orderByColumn;
        cmd.orderByAscending = orderByAscending;
        cmd.limit = limit;
        cmd.offset = offset;
        cmd.joinType = joinType;
        cmd.joinTableName = joinTableName;
        cmd.leftTableAlias = leftTableAlias;
        cmd.rightTableAlias = rightTableAlias;
        cmd.joinOnLeftColumn = joinOnLeftColumn;
        cmd.joinOnRightColumn = joinOnRightColumn;

        return cmd;
    }

    // ========== PRIVATE HELPER METHODS ==========

    bool SQLParser::MapColumnType(const std::string& typeName, ColumnType& outType, bool& outRequiresSize) {
        if (typeName == "TINYINT") {
            outType = ColumnType::TINYINT;
            outRequiresSize = false;
        } else if (typeName == "SMALLINT") {
            outType = ColumnType::SMALLINT;
            outRequiresSize = false;
        } else if (typeName == "INT" || typeName == "INTEGER") {
            outType = ColumnType::INT;
            outRequiresSize = false;
        } else if (typeName == "BIGINT") {
            outType = ColumnType::BIGINT;
            outRequiresSize = false;
        } else if (typeName == "CHAR") {
            outType = ColumnType::CHAR;
            outRequiresSize = true;
        } else if (typeName == "VARCHAR") {
            outType = ColumnType::VARCHAR;
            outRequiresSize = true;
        } else if (typeName == "NCHAR") {
            outType = ColumnType::NCHAR;
            outRequiresSize = true;
        } else if (typeName == "NVARCHAR") {
            outType = ColumnType::NVARCHAR;
            outRequiresSize = true;
        } else if (typeName == "TEXT") {
            outType = ColumnType::TEXT;
            outRequiresSize = false;
        } else if (typeName == "DATETIME") {
            outType = ColumnType::DATETIME;
            outRequiresSize = false;
        } else if (typeName == "DATETIME2") {
            outType = ColumnType::DATETIME2;
            outRequiresSize = false;
        } else if (typeName == "DATE") {
            outType = ColumnType::DATE;
            outRequiresSize = false;
        } else if (typeName == "TIME") {
            outType = ColumnType::TIME;
            outRequiresSize = false;
        } else if (typeName == "BOOL" || typeName == "BOOLEAN" || typeName == "BIT") {
            outType = ColumnType::BOOL;
            outRequiresSize = false;
        } else if (typeName == "FLOAT") {
            outType = ColumnType::FLOAT;
            outRequiresSize = false;
        } else if (typeName == "REAL") {
            outType = ColumnType::REAL;
            outRequiresSize = false;
        } else {
            return false; // Unknown type
        }
        return true;
    }

    size_t SQLParser::FindMatchingParen(const std::string& str, size_t openParenPos) {
        int parenDepth = 1;
        for (size_t i = openParenPos + 1; i < str.length(); i++) {
            if (str[i] == '(') parenDepth++;
            else if (str[i] == ')') {
                parenDepth--;
                if (parenDepth == 0) {
                    return i;
                }
            }
        }
        return std::string::npos; // No matching paren found
    }

    std::string SQLParser::Trim(const std::string& str) {
        size_t start = str.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) {
            return ""; // All whitespace
        }
        size_t end = str.find_last_not_of(" \t\n\r");
        return str.substr(start, end - start + 1);
    }

    std::vector<std::string> SQLParser::ParseValueList(const std::string& valueList) {
        std::vector<std::string> values;
        size_t pos = 0;
        bool inQuotes = false;
        std::string currentValue;

        while (pos < valueList.length()) {
            char c = valueList[pos];

            if (c == '\'' || c == '"') {
                inQuotes = !inQuotes;
                pos++;
                continue;
            }

            if (c == ',' && !inQuotes) {
                // Trim whitespace from value
                currentValue = Trim(currentValue);
                values.push_back(currentValue);
                currentValue.clear();
                pos++;
                continue;
            }

            currentValue += c;
            pos++;
        }

        // Add last value
        if (!currentValue.empty() || values.size() > 0) {
            currentValue = Trim(currentValue);
            values.push_back(currentValue);
        }

        return values;
    }

    UpdateCommand SQLParser::ParseUpdate(const std::string& query) {
        UpdateCommand result;
        result.valid = false;

        // Find UPDATE keyword
        size_t updatePos = query.find("UPDATE");
        if (updatePos == std::string::npos) {
            updatePos = query.find("update");
        }
        if (updatePos == std::string::npos) {
            result.errorMessage = "UPDATE keyword not found";
            return result;
        }

        // Parse table name
        size_t tableNameStart = updatePos + 6; // Skip "UPDATE"
        while (tableNameStart < query.length() && isspace(query[tableNameStart])) {
            tableNameStart++;
        }

        size_t tableNameEnd = tableNameStart;
        while (tableNameEnd < query.length() && 
               (isalnum(query[tableNameEnd]) || query[tableNameEnd] == '_')) {
            tableNameEnd++;
        }

        if (tableNameStart == tableNameEnd) {
            result.errorMessage = "No table name found";
            return result;
        }

        result.tableName = query.substr(tableNameStart, tableNameEnd - tableNameStart);

        // Find SET keyword
        size_t setPos = query.find("SET", tableNameEnd);
        if (setPos == std::string::npos) {
            setPos = query.find("set", tableNameEnd);
        }
        if (setPos == std::string::npos) {
            result.errorMessage = "SET keyword not found";
            return result;
        }

        // Parse SET clause (column=value pairs)
        size_t setStart = setPos + 3; // Skip "SET"
        while (setStart < query.length() && isspace(query[setStart])) {
            setStart++;
        }

        // Find WHERE or end of query
        size_t wherePos = query.find("WHERE", setStart);
        if (wherePos == std::string::npos) {
            wherePos = query.find("where", setStart);
        }
        size_t setEnd = (wherePos != std::string::npos) ? wherePos : query.length();

        std::string setClause = query.substr(setStart, setEnd - setStart);
        setClause = Trim(setClause);

        // Parse column=value pairs
        size_t pos = 0;
        while (pos < setClause.length()) {
            // Skip whitespace
            while (pos < setClause.length() && isspace(setClause[pos])) {
                pos++;
            }

            // Parse column name
            size_t colStart = pos;
            while (pos < setClause.length() && 
                   (isalnum(setClause[pos]) || setClause[pos] == '_')) {
                pos++;
            }

            if (colStart == pos) break;

            std::string columnName = setClause.substr(colStart, pos - colStart);

            // Skip whitespace and find '='
            while (pos < setClause.length() && isspace(setClause[pos])) {
                pos++;
            }

            if (pos >= setClause.length() || setClause[pos] != '=') {
                result.errorMessage = "Expected '=' after column name in SET clause";
                return result;
            }
            pos++; // Skip '='

            // Skip whitespace
            while (pos < setClause.length() && isspace(setClause[pos])) {
                pos++;
            }

            // Parse value (handle quoted strings)
            std::string value;
            bool inQuotes = false;
            if (pos < setClause.length() && setClause[pos] == '\'') {
                inQuotes = true;
                pos++; // Skip opening quote
                while (pos < setClause.length() && setClause[pos] != '\'') {
                    value += setClause[pos];
                    pos++;
                }
                if (pos < setClause.length() && setClause[pos] == '\'') {
                    pos++; // Skip closing quote
                }
            } else {
                // Parse unquoted value (until comma or end)
                while (pos < setClause.length() && setClause[pos] != ',') {
                    value += setClause[pos];
                    pos++;
                }
                value = Trim(value);
            }

            result.setColumns.push_back(columnName);
            result.setValues.push_back(value);

            // Skip whitespace
            while (pos < setClause.length() && isspace(setClause[pos])) {
                pos++;
            }

            // Skip comma if present
            if (pos < setClause.length() && setClause[pos] == ',') {
                pos++;
            }
        }

        if (result.setColumns.empty()) {
            result.errorMessage = "No columns found in SET clause";
            return result;
        }

        // Parse WHERE clause
        if (wherePos != std::string::npos) {
            size_t condStart = wherePos + 5; // Skip "WHERE"
            while (condStart < query.length() && isspace(query[condStart])) {
                condStart++;
            }

            // Parse column name
            size_t colStart = condStart;
            while (condStart < query.length() && 
                   (isalnum(query[condStart]) || query[condStart] == '_')) {
                condStart++;
            }

            if (colStart == condStart) {
                result.errorMessage = "No column name in WHERE clause";
                return result;
            }

            result.whereColumn = query.substr(colStart, condStart - colStart);

            // Skip whitespace and find '='
            while (condStart < query.length() && isspace(query[condStart])) {
                condStart++;
            }

            if (condStart >= query.length() || query[condStart] != '=') {
                result.errorMessage = "Expected '=' in WHERE clause";
                return result;
            }
            condStart++; // Skip '='

            // Skip whitespace
            while (condStart < query.length() && isspace(query[condStart])) {
                condStart++;
            }

            // Parse value (handle quoted strings)
            if (condStart < query.length() && query[condStart] == '\'') {
                condStart++; // Skip opening quote
                size_t valueStart = condStart;
                while (condStart < query.length() && query[condStart] != '\'') {
                    condStart++;
                }
                result.whereValue = query.substr(valueStart, condStart - valueStart);
            } else {
                // Parse unquoted value
                size_t valueStart = condStart;
                while (condStart < query.length() && !isspace(query[condStart])) {
                    condStart++;
                }
                result.whereValue = query.substr(valueStart, condStart - valueStart);
                result.whereValue = Trim(result.whereValue);
            }
        }

        result.valid = true;
        return result;
    }

    DeleteCommand SQLParser::ParseDelete(const std::string& query) {
        DeleteCommand result;
        result.valid = false;

        // Find DELETE keyword
        size_t deletePos = query.find("DELETE");
        if (deletePos == std::string::npos) {
            deletePos = query.find("delete");
        }
        if (deletePos == std::string::npos) {
            result.errorMessage = "DELETE keyword not found";
            return result;
        }

        // Find FROM keyword
        size_t fromPos = query.find("FROM", deletePos);
        if (fromPos == std::string::npos) {
            fromPos = query.find("from", deletePos);
        }
        if (fromPos == std::string::npos) {
            result.errorMessage = "FROM keyword not found";
            return result;
        }

        // Parse table name
        size_t tableNameStart = fromPos + 4; // Skip "FROM"
        while (tableNameStart < query.length() && isspace(query[tableNameStart])) {
            tableNameStart++;
        }

        size_t tableNameEnd = tableNameStart;
        while (tableNameEnd < query.length() && 
               (isalnum(query[tableNameEnd]) || query[tableNameEnd] == '_')) {
            tableNameEnd++;
        }

        if (tableNameStart == tableNameEnd) {
            result.errorMessage = "No table name found";
            return result;
        }

        result.tableName = query.substr(tableNameStart, tableNameEnd - tableNameStart);

        // Parse WHERE clause (required for safety - no DELETE without WHERE)
        size_t wherePos = query.find("WHERE", tableNameEnd);
        if (wherePos == std::string::npos) {
            wherePos = query.find("where", tableNameEnd);
        }

        if (wherePos == std::string::npos) {
            result.errorMessage = "WHERE clause required for DELETE (use DROP TABLE to delete all data)";
            return result;
        }

        size_t condStart = wherePos + 5; // Skip "WHERE"
        while (condStart < query.length() && isspace(query[condStart])) {
            condStart++;
        }

        // Parse column name
        size_t colStart = condStart;
        while (condStart < query.length() && 
               (isalnum(query[condStart]) || query[condStart] == '_')) {
            condStart++;
        }

        if (colStart == condStart) {
            result.errorMessage = "No column name in WHERE clause";
            return result;
        }

        result.whereColumn = query.substr(colStart, condStart - colStart);

        // Skip whitespace and find '='
        while (condStart < query.length() && isspace(query[condStart])) {
            condStart++;
        }

        if (condStart >= query.length() || query[condStart] != '=') {
            result.errorMessage = "Expected '=' in WHERE clause";
            return result;
        }
        condStart++; // Skip '='

        // Skip whitespace
        while (condStart < query.length() && isspace(query[condStart])) {
            condStart++;
        }

        // Parse value (handle quoted strings)
        if (condStart < query.length() && query[condStart] == '\'') {
            condStart++; // Skip opening quote
            size_t valueStart = condStart;
            while (condStart < query.length() && query[condStart] != '\'') {
                condStart++;
            }
            result.whereValue = query.substr(valueStart, condStart - valueStart);
        } else {
            // Parse unquoted value
            size_t valueStart = condStart;
            while (condStart < query.length() && !isspace(query[condStart])) {
                condStart++;
            }
            result.whereValue = query.substr(valueStart, condStart - valueStart);
            result.whereValue = Trim(result.whereValue);
        }

        result.valid = true;
        return result;
    }

    std::string SQLParser::RemoveComments(const std::string& query) {
        std::string result;
        bool inSingleQuote = false;
        size_t pos = 0;

        while (pos < query.length()) {
            // Toggle quote state
            if (query[pos] == '\'') {
                inSingleQuote = !inSingleQuote;
                result += query[pos];
                pos++;
                continue;
            }

            // Check for comment start (only if not in quotes)
            if (!inSingleQuote && pos + 1 < query.length() && 
                query[pos] == '-' && query[pos + 1] == '-') {
                // Skip until end of line or end of string
                while (pos < query.length() && query[pos] != '\n' && query[pos] != '\r') {
                    pos++;
                }
                // Keep the newline character if present
                if (pos < query.length() && (query[pos] == '\n' || query[pos] == '\r')) {
                    result += query[pos];
                    pos++;
                }
                continue;
            }

            // Regular character
            result += query[pos];
            pos++;
        }

        return result;
    }

} // namespace CQL
