#pragma once

#include <string>
#include <vector>

namespace CQL {

    // Forward declaration
    class Database;

    class QueryExecutor {
    public:
        // Split query by GO batch separator
        static std::vector<std::string> SplitIntoBatches(const std::string& query);

        // Split batch by semicolon into statements
        static std::vector<std::string> SplitIntoStatements(const std::string& batch);

        // Route a single statement to appropriate handler
        static std::string RouteStatement(Database& db, const std::string& statement);

    private:
        // Helper: Trim whitespace from string
        static std::string Trim(const std::string& str);
    };

} // namespace CQL
