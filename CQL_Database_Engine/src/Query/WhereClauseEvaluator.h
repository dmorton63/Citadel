#pragma once
#include <string>
#include <vector>
#include "FileHeader.h"
#include "Table.h"
#include "SQLParser.h"  // For WhereCondition

namespace CQL {

    class WhereClauseEvaluator {
    public:
        // Evaluate a single WHERE condition for a row
        // Returns true if the row matches the WHERE clause, false otherwise
        // If whereColumn is empty, returns true (no WHERE clause)
        static bool Evaluate(
            const std::vector<std::string>& rowValues,
            const std::vector<Column>& columns,
            const std::string& whereColumn,
            const std::string& whereValue,
            const std::string& whereOperator = "="
        );

        // Evaluate compound WHERE clause (AND/OR support)
        // Returns true if the row matches the compound WHERE conditions
        static bool EvaluateCompound(
            const std::vector<std::string>& rowValues,
            const std::vector<Column>& columns,
            const std::vector<WhereCondition>& conditions,
            const std::vector<std::string>& logicalOps
        );

    private:
        // Type-specific comparison helpers
        static bool CompareNumeric(long long rowNum, long long whereNum, const std::string& op);
        static bool CompareFloat(double rowNum, double whereNum, const std::string& op);
        static bool CompareString(const std::string& rowVal, const std::string& whereVal, const std::string& op);
    };

} // namespace CQL
