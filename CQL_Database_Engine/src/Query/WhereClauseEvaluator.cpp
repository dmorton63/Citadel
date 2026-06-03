#include "WhereClauseEvaluator.h"
#include <algorithm>
#include <cctype>

namespace CQL {

    bool WhereClauseEvaluator::Evaluate(
        const std::vector<std::string>& rowValues,
        const std::vector<Column>& columns,
        const std::string& whereColumn,
        const std::string& whereValue,
        const std::string& whereOperator) {
        
        // If no WHERE clause specified, match all rows
        if (whereColumn.empty()) {
            return true;
        }

        // Find the WHERE column index
        int whereColumnIndex = -1;
        for (size_t i = 0; i < columns.size(); i++) {
            if (columns[i].name == whereColumn) {
                whereColumnIndex = static_cast<int>(i);
                break;
            }
        }

        // Column not found - don't match
        if (whereColumnIndex == -1 || whereColumnIndex >= static_cast<int>(rowValues.size())) {
            return false;
        }

        const std::string& rowValue = rowValues[whereColumnIndex];
        ColumnType whereColType = columns[whereColumnIndex].type;

        // Perform type-aware comparison
        if (whereColType == ColumnType::TINYINT || whereColType == ColumnType::SMALLINT ||
            whereColType == ColumnType::INT || whereColType == ColumnType::BIGINT) {
            // Numeric comparison
            try {
                long long rowNum = std::stoll(rowValue);
                long long whereNum = std::stoll(whereValue);
                return CompareNumeric(rowNum, whereNum, whereOperator);
            } catch (...) {
                return false; // Conversion failed
            }
        }
        else if (whereColType == ColumnType::FLOAT || whereColType == ColumnType::REAL) {
            // Floating-point comparison
            try {
                double rowNum = std::stod(rowValue);
                double whereNum = std::stod(whereValue);
                return CompareFloat(rowNum, whereNum, whereOperator);
            } catch (...) {
                return false; // Conversion failed
            }
        }
        else {
            // String comparison for all other types
            return CompareString(rowValue, whereValue, whereOperator);
        }
    }

    bool WhereClauseEvaluator::CompareNumeric(long long rowNum, long long whereNum, const std::string& op) {
        if (op == "=") return rowNum == whereNum;
        if (op == "!=" || op == "<>") return rowNum != whereNum;
        if (op == "<") return rowNum < whereNum;
        if (op == ">") return rowNum > whereNum;
        if (op == "<=") return rowNum <= whereNum;
        if (op == ">=") return rowNum >= whereNum;
        return false; // Unknown operator
    }

    bool WhereClauseEvaluator::CompareFloat(double rowNum, double whereNum, const std::string& op) {
        if (op == "=") return rowNum == whereNum;
        if (op == "!=" || op == "<>") return rowNum != whereNum;
        if (op == "<") return rowNum < whereNum;
        if (op == ">") return rowNum > whereNum;
        if (op == "<=") return rowNum <= whereNum;
        if (op == ">=") return rowNum >= whereNum;
        return false; // Unknown operator
    }

    bool WhereClauseEvaluator::CompareString(const std::string& rowVal, const std::string& whereVal, const std::string& op) {
        if (op == "=") return rowVal == whereVal;
        if (op == "!=" || op == "<>") return rowVal != whereVal;
        if (op == "<") return rowVal < whereVal;
        if (op == ">") return rowVal > whereVal;
        if (op == "<=") return rowVal <= whereVal;
        if (op == ">=") return rowVal >= whereVal;
        return false; // Unknown operator
    }

    bool WhereClauseEvaluator::EvaluateCompound(
        const std::vector<std::string>& rowValues,
        const std::vector<Column>& columns,
        const std::vector<WhereCondition>& conditions,
        const std::vector<std::string>& logicalOps) {

        // No conditions = match all
        if (conditions.empty()) {
            return true;
        }

        // Helper to strip table/alias prefix from column name (e.g., "u.Age" -> "Age")
        auto stripPrefix = [](const std::string& qualifiedName) -> std::string {
            size_t dotPos = qualifiedName.find('.');
            if (dotPos != std::string::npos) {
                return qualifiedName.substr(dotPos + 1);
            }
            return qualifiedName;
        };

        // Evaluate first condition
        std::string firstCol = stripPrefix(conditions[0].column);
        bool result = Evaluate(rowValues, columns, 
                              firstCol, 
                              conditions[0].value, 
                              conditions[0].op);

        // Process remaining conditions with AND/OR logic
        for (size_t i = 0; i < logicalOps.size() && i + 1 < conditions.size(); i++) {
            std::string nextCol = stripPrefix(conditions[i + 1].column);
            bool nextResult = Evaluate(rowValues, columns,
                                      nextCol,
                                      conditions[i + 1].value,
                                      conditions[i + 1].op);

            if (logicalOps[i] == "AND") {
                result = result && nextResult;
            } else if (logicalOps[i] == "OR") {
                result = result || nextResult;
            }
        }

        return result;
    }

} // namespace CQL
