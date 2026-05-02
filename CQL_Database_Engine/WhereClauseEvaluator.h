#pragma once
#include <string>
#include <vector>
#include "FileHeader.h"
#include "Table.h"

namespace CQL {
    
    class WhereClauseEvaluator {
    public:
        // Evaluate a WHERE condition for a row
        // Returns true if the row matches the WHERE clause, false otherwise
        // If whereColumn is empty, returns true (no WHERE clause)
        static bool Evaluate(
            const std::vector<std::string>& rowValues,
            const std::vector<Column>& columns,
            const std::string& whereColumn,
            const std::string& whereValue,
            const std::string& whereOperator = "="
        );

    private:
        // Type-specific comparison helpers
        static bool CompareNumeric(long long rowNum, long long whereNum, const std::string& op);
        static bool CompareFloat(double rowNum, double whereNum, const std::string& op);
        static bool CompareString(const std::string& rowVal, const std::string& whereVal, const std::string& op);
    };

} // namespace CQL
