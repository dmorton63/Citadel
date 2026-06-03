#include "QueryExecutor.h"
#include "Database.h"
#include "SQLParser.h"
#include <cctype>
#include <sstream>
#include <algorithm>

namespace CQL {

    std::string QueryExecutor::Trim(const std::string& str) {
        std::string result = str;
        result.erase(0, result.find_first_not_of(" \t\n\r"));
        result.erase(result.find_last_not_of(" \t\n\r") + 1);
        return result;
    }

    std::vector<std::string> QueryExecutor::SplitIntoBatches(const std::string& query) {
        std::vector<std::string> batches;
        size_t batchPos = 0;
        std::string currentBatch;

        while (batchPos < query.length()) {
            // Check for GO keyword (case-insensitive, must be on its own line or at start/end)
            if (batchPos == 0 || query[batchPos - 1] == '\n' || query[batchPos - 1] == '\r') {
                // Check if we have "GO" at this position
                if (batchPos + 2 <= query.length()) {
                    std::string possibleGo = query.substr(batchPos, 2);
                    for (auto& c : possibleGo) c = toupper(c);

                    if (possibleGo == "GO") {
                        // Check that it's followed by whitespace, newline, or end of string
                        bool isGo = false;
                        if (batchPos + 2 == query.length()) {
                            isGo = true; // GO at end of query
                        } else {
                            char nextChar = query[batchPos + 2];
                            if (isspace(nextChar) || nextChar == ';') {
                                isGo = true;
                            }
                        }

                        if (isGo) {
                            // Found GO - save current batch and start new one
                            std::string trimmed = Trim(currentBatch);

                            if (!trimmed.empty()) {
                                batches.push_back(trimmed);
                            }
                            currentBatch.clear();
                            batchPos += 2; // Skip "GO"

                            // Skip any whitespace/newlines after GO
                            while (batchPos < query.length() && isspace(query[batchPos])) {
                                batchPos++;
                            }
                            continue;
                        }
                    }
                }
            }

            currentBatch += query[batchPos];
            batchPos++;
        }

        // Add last batch if no trailing GO
        if (!currentBatch.empty()) {
            std::string trimmed = Trim(currentBatch);

            if (!trimmed.empty()) {
                batches.push_back(trimmed);
            }
        }

        return batches;
    }

    std::vector<std::string> QueryExecutor::SplitIntoStatements(const std::string& batch) {
        std::vector<std::string> statements;
        size_t pos = 0;
        bool inQuotes = false;
        std::string currentStatement;

        while (pos < batch.length()) {
            char c = batch[pos];

            if (c == '\'' || c == '"') {
                inQuotes = !inQuotes;
            }

            if (c == ';' && !inQuotes) {
                // End of statement - trim and add if not empty
                std::string trimmed = Trim(currentStatement);

                if (!trimmed.empty()) {
                    statements.push_back(trimmed);
                }
                currentStatement.clear();
            } else {
                currentStatement += c;
            }
            pos++;
        }

        // Add last statement if no trailing semicolon
        if (!currentStatement.empty()) {
            std::string trimmed = Trim(currentStatement);

            if (!trimmed.empty()) {
                statements.push_back(trimmed);
            }
        }

        return statements;
    }

    std::string QueryExecutor::RouteStatement(Database& db, const std::string& statement) {
        // Remove comments from statement
        std::string stmt = SQLParser::RemoveComments(statement);

        // Trim again after comment removal
        stmt = Trim(stmt);

        // Skip empty statements
        if (stmt.empty()) {
            return "";
        }

        // Parse query type
        std::string upperStmt = stmt;
        for (auto& c : upperStmt) c = toupper(c);

        std::string result;

        // CREATE TABLE
        if (upperStmt.find("CREATE TABLE") == 0) {
            result = db.ParseCreateTable(stmt);
            // Reopen database after CREATE TABLE for clean state
            if (result.find("Success") == 0) {
                if (!db.Reopen()) {
                    result += "\nWarning: Failed to reopen database after CREATE TABLE";
                }
            }
        }
        // DROP TABLE
        else if (upperStmt.find("DROP TABLE") == 0) {
            result = db.ParseDropTable(stmt);
            // Reopen database after DROP TABLE for clean state
            if (result.find("Success") == 0) {
                if (!db.Reopen()) {
                    result += "\nWarning: Failed to reopen database after DROP TABLE";
                }
            }
        }
        // ALTER TABLE
        else if (upperStmt.find("ALTER TABLE") == 0) {
            result = db.ParseAlterTable(stmt);
            // Reopen database after ALTER TABLE for clean state
            if (result.find("Success") == 0) {
                if (!db.Reopen()) {
                    result += "\nWarning: Failed to reopen database after ALTER TABLE";
                }
            }
        }
        // INSERT INTO
        else if (upperStmt.find("INSERT INTO") == 0) {
            result = db.ParseInsertInto(stmt);
        }
        // SELECT
        else if (upperStmt.find("SELECT") == 0) {
            result = db.ParseSelect(stmt);
        }
        // UPDATE
        else if (upperStmt.find("UPDATE") == 0) {
            result = db.ParseUpdate(stmt);
        }
        // DELETE
        else if (upperStmt.find("DELETE") == 0) {
            result = db.ParseDelete(stmt);
        }
        // DUMP commands
        else if (upperStmt.find("DUMP FILE_HEADER") == 0) {
            result = db.DumpFileHeader();
        }
        else if (upperStmt.find("DUMP TABLE_DIR") == 0) {
            result = db.DumpTableDir();
        }
        else if (upperStmt.find("DUMP SCHEMA") == 0) {
            result = db.DumpSchema();
        }
        else if (upperStmt.find("DUMP PAGE_REGION") == 0) {
            result = db.DumpPageRegion();
        }
        else if (upperStmt.find("DUMP TABLES_LOADED") == 0) {
            result = db.DumpTablesLoaded();
        }
        else if (upperStmt.find("DUMP TABLE") == 0) {
            // Extract table name
            size_t tablePos = upperStmt.find("TABLE") + 5;
            while (tablePos < stmt.length() && isspace(stmt[tablePos])) tablePos++;
            size_t tableEnd = tablePos;
            while (tableEnd < stmt.length() && (isalnum(stmt[tableEnd]) || stmt[tableEnd] == '_')) tableEnd++;
            std::string tableName = stmt.substr(tablePos, tableEnd - tablePos);
            result = db.DumpTable(tableName);
        }
        else {
            result = "Error: Unsupported query type (CREATE TABLE, DROP TABLE, INSERT INTO, SELECT, UPDATE, DELETE, and DUMP supported)";
        }

        return result;
    }

} // namespace CQL
