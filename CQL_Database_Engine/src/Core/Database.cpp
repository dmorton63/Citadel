#include "Database.h"
#include "SQLParser.h"
#include "RowSerializer.h"
#include "PageManager.h"
#include "FileManager.h"
#include "DiagnosticDumper.h"
#include "QueryExecutor.h"
#include "WhereClauseEvaluator.h"
#include "BTree.h"
#include <iostream>
#include <unordered_set>
#include <cctype>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace CQL {

    Database::Database() : isOpen(false) {}

    Database::~Database() {
        Close();
    }

    bool Database::Create(const std::string& filepath) {
        filePath = filepath;
        
        // Open in binary write mode
        file.open(filepath, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            std::cerr << "Failed to create database file: " << filepath << std::endl;
            return false;
        }

        // Initialize header with defaults
        header = FileHeader();
        header.tableDirOffset = 256;     // Right after header
        header.schemaOffset = 2304;      // After table directory (256 + 16*128 = room for 16 tables)
        header.pageRegionOffset = 4096;  // Start pages at 4KB boundary

        if (!FileManager::WriteHeader(file, header)) {
            file.close();
            return false;
        }

        isOpen = true;
        return true;
    }

    bool Database::Open(const std::string& filepath) {
        filePath = filepath;

        file.open(filepath, std::ios::in | std::ios::out | std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open database file: " << filepath << std::endl;
            return false;
        }

        if (!FileManager::ReadHeader(file, header)) {
            file.close();
            return false;
        }

        // Validate magic
        if (strncmp(header.magic, "CQLDB", 5) != 0) {
            std::cerr << "Invalid database file (bad magic)" << std::endl;
            file.close();
            return false;
        }

        // Load table metadata
        if (!FileManager::LoadTables(file, header, tables)) {
            std::cerr << "Warning: Failed to load some table metadata" << std::endl;
        }

        isOpen = true;
        return true;
    }

    void Database::Close() {
        if (file.is_open()) {
            file.close();
        }
        tables.clear();
        isOpen = false;
    }

    bool Database::Reopen() {
        if (!isOpen || filePath.empty()) {
            std::cerr << "Cannot reopen: database not open or no path" << std::endl;
            return false;
        }

        std::string savedPath = filePath;
        Close();
        bool success = Open(savedPath);

        if (success) {
            std::cout << "Database reopened successfully" << std::endl;
        } else {
            std::cerr << "Failed to reopen database" << std::endl;
        }

        return success;
    }

    bool Database::ValidateTableSchema(const std::string& tableName, const std::vector<ColumnInfo>& columns) {
        // Check table name length
        if (tableName.empty() || tableName.length() >= 48) {
            std::cerr << "Invalid table name length (must be 1-47 chars)" << std::endl;
            return false;
        }

        // Check for columns
        if (columns.empty()) {
            std::cerr << "Table must have at least one column" << std::endl;
            return false;
        }

        // Check max columns (limited by file format)
        if (columns.size() > 64) {
            std::cerr << "Too many columns (max 64)" << std::endl;
            return false;
        }

        // Validate column names and count primary keys
        int pkCount = 0;
        std::unordered_set<std::string> columnNames;

        for (const auto& col : columns) {
            // Check name length
            if (col.name.empty() || col.name.length() >= 48) {
                std::cerr << "Invalid column name length: " << col.name << std::endl;
                return false;
            }

            // Check for duplicates
            if (columnNames.count(col.name) > 0) {
                std::cerr << "Duplicate column name: " << col.name << std::endl;
                return false;
            }
            columnNames.insert(col.name);

            // Count primary keys
            if (col.isPrimaryKey) {
                pkCount++;
            }
        }

        // Must have exactly one primary key
        if (pkCount != 1) {
            std::cerr << "Table must have exactly one primary key column (found " << pkCount << ")" << std::endl;
            return false;
        }

        return true;
    }

    bool Database::CreateTable(const std::string& tableName, const std::vector<ColumnInfo>& columns) {
        if (!isOpen) {
            std::cerr << "Database not open" << std::endl;
            return false;
        }

        // Check if table already exists
        if (FindTable(tableName) != nullptr) {
            std::cerr << "Error: Table '" << tableName << "' already exists" << std::endl;
            return false;
        }

        // Validate schema
        if (!ValidateTableSchema(tableName, columns)) {
            return false;
        }

        // Create TableEntry
        TableEntry tableEntry;
        strncpy_s(tableEntry.name, sizeof(tableEntry.name), tableName.c_str(), _TRUNCATE);
        tableEntry.schemaOffset = header.schemaOffset;
        tableEntry.rootPage = 0; // Will allocate first page later
        tableEntry.flags = 0;

        // Calculate where to write this table entry
        uint64_t tableEntryOffset = header.tableDirOffset + (header.tableCount * sizeof(TableEntry));

        // Write TableEntry
        file.seekp(tableEntryOffset);
        file.write(reinterpret_cast<const char*>(&tableEntry), sizeof(TableEntry));
        if (!file.good()) {
            std::cerr << "Failed to write table entry" << std::endl;
            return false;
        }

        // Write column definitions at schemaOffset
        file.seekp(header.schemaOffset);
        for (const auto& col : columns) {
            ColumnDef colDef;
            strncpy_s(colDef.name, sizeof(colDef.name), col.name.c_str(), _TRUNCATE);
            colDef.type = static_cast<uint8_t>(col.type);
            colDef.isPrimaryKey = col.isPrimaryKey ? 1 : 0;
            colDef.size = col.size;

            file.write(reinterpret_cast<const char*>(&colDef), sizeof(ColumnDef));
            if (!file.good()) {
                std::cerr << "Failed to write column definition" << std::endl;
                return false;
            }
        }

        // Update header
        header.tableCount++;
        header.schemaOffset += columns.size() * sizeof(ColumnDef);

        // Ensure pageRegionOffset stays beyond schema data
        if (header.schemaOffset >= header.pageRegionOffset) {
            header.pageRegionOffset = ((header.schemaOffset / header.pageSize) + 1) * header.pageSize;
        }

        // Write updated header
        if (!FileManager::WriteHeader(file, header)) {
            std::cerr << "Failed to update header after table creation" << std::endl;
            return false;
        }

        file.flush();

        std::cout << "Table '" << tableName << "' created successfully with " 
                  << columns.size() << " columns" << std::endl;
        return true;
    }

    bool Database::DropTable(const std::string& tableName) {
        if (!isOpen) {
            std::cerr << "Database not open" << std::endl;
            return false;
        }

        // Find table in memory
        int tableIndex = -1;
        for (size_t i = 0; i < tables.size(); i++) {
            if (tables[i]->GetName() == tableName) {
                tableIndex = static_cast<int>(i);
                break;
            }
        }

        if (tableIndex < 0) {
            std::cerr << "Table '" << tableName << "' not found" << std::endl;
            return false;
        }

        // Find the actual table entry index in the file (skipping any already-deleted entries)
        int fileTableIndex = -1;
        int loadedTableCount = 0;

        file.clear();
        file.seekg(header.tableDirOffset);

        for (uint32_t i = 0; i < header.tableCount; i++) {
            TableEntry entry;
            file.read(reinterpret_cast<char*>(&entry), sizeof(TableEntry));
            if (!file.good()) {
                std::cerr << "Failed to read table entry during DROP" << std::endl;
                return false;
            }

            // Check if this entry is deleted (name starts with null)
            if (entry.name[0] == '\0') {
                continue; // Skip deleted entries
            }

            // Check if this is the table we're looking for
            char entryName[49] = {0};
            memcpy(entryName, entry.name, 48);
            entryName[48] = '\0';

            if (std::string(entryName) == tableName) {
                fileTableIndex = static_cast<int>(i);
                break;
            }

            loadedTableCount++;
        }

        if (fileTableIndex < 0) {
            std::cerr << "Table entry not found in file" << std::endl;
            return false;
        }

        // Mark table as deleted by zeroing out its name in the file
        uint64_t tableEntryOffset = header.tableDirOffset + (fileTableIndex * sizeof(TableEntry));
        file.seekp(tableEntryOffset);

        // Write null byte to first character of name
        char nullByte = '\0';
        file.write(&nullByte, 1);

        if (!file.good()) {
            std::cerr << "Failed to mark table as deleted" << std::endl;
            return false;
        }

        file.flush();

        std::cout << "Table '" << tableName << "' dropped successfully" << std::endl;
        return true;
    }

    bool Database::AlterTableAddColumn(const std::string& tableName, const std::string& columnName,
                                        ColumnType columnType, uint16_t columnSize, const std::string& defaultValue) {
        if (!isOpen) {
            std::cerr << "Database not open" << std::endl;
            return false;
        }

        // Find table in memory
        auto table = FindTable(tableName);
        if (!table) {
            std::cerr << "Table '" << tableName << "' not found" << std::endl;
            return false;
        }

        // Check if column already exists
        const auto& columns = table->GetColumns();
        for (const auto& col : columns) {
            if (col.name == columnName) {
                std::cerr << "Column '" << columnName << "' already exists in table '" << tableName << "'" << std::endl;
                return false;
            }
        }

        // Validate column name length
        if (columnName.empty() || columnName.length() >= 48) {
            std::cerr << "Invalid column name length (must be 1-47 chars)" << std::endl;
            return false;
        }

        // Check max columns
        if (columns.size() >= 64) {
            std::cerr << "Cannot add column: table already has maximum of 64 columns" << std::endl;
            return false;
        }

        // Create column definition
        ColumnDef colDef;
        strncpy_s(colDef.name, sizeof(colDef.name), columnName.c_str(), _TRUNCATE);
        colDef.type = static_cast<uint8_t>(columnType);
        colDef.isPrimaryKey = 0; // New columns cannot be primary keys
        colDef.size = columnSize;

        // Write column definition at current schemaOffset
        file.seekp(header.schemaOffset);
        file.write(reinterpret_cast<const char*>(&colDef), sizeof(ColumnDef));
        if (!file.good()) {
            std::cerr << "Failed to write column definition" << std::endl;
            return false;
        }

        // Update header
        header.schemaOffset += sizeof(ColumnDef);

        // Ensure pageRegionOffset stays beyond schema data
        if (header.schemaOffset >= header.pageRegionOffset) {
            header.pageRegionOffset = ((header.schemaOffset / header.pageSize) + 1) * header.pageSize;
        }

        // Write updated header
        if (!FileManager::WriteHeader(file, header)) {
            std::cerr << "Failed to update header after adding column" << std::endl;
            return false;
        }

        // If table has existing rows and a default value is provided, update them
        if (!defaultValue.empty() && table->GetRootPage() != 0) {
            // Read all existing rows and append the default value
            uint64_t rootPage = table->GetRootPage();
            PageHeader pageHdr;
            if (!PageManager::ReadPageHeader(file, rootPage, pageHdr)) {
                std::cerr << "Warning: Failed to read page header for default value update" << std::endl;
            } else {
                // Note: This is a simplified approach. In production, you'd need to:
                // 1. Read each row
                // 2. Deserialize it
                // 3. Append the default value
                // 4. Re-serialize and write back
                // For now, we'll just log a warning
                if (pageHdr.rowCount > 0) {
                    std::cout << "Warning: Table has " << pageHdr.rowCount << " existing row(s). " 
                              << "Existing rows will have NULL for column '" << columnName << "'. "
                              << "Consider using UPDATE to set values." << std::endl;
                }
            }
        }

        file.flush();

        std::cout << "Column '" << columnName << "' added successfully to table '" << tableName << "'" << std::endl;
        return true;
    }

    std::string Database::ExecuteQuery(const std::string& query) {
        if (!isOpen) {
            return "Error: Database not open";
        }

        // Split query into batches using QueryExecutor
        std::vector<std::string> batches = QueryExecutor::SplitIntoBatches(query);

        if (batches.empty()) {
            return "Error: Empty query";
        }

        // Execute each batch
        std::ostringstream allResults;
        for (size_t batchIdx = 0; batchIdx < batches.size(); batchIdx++) {
            const std::string& batch = batches[batchIdx];

            // Split batch into statements using QueryExecutor
            std::vector<std::string> statements = QueryExecutor::SplitIntoStatements(batch);

            // Execute each statement in this batch
            std::ostringstream batchResults;
            for (size_t i = 0; i < statements.size(); i++) {
                // Route statement to appropriate handler using QueryExecutor
                std::string result = QueryExecutor::RouteStatement(*this, statements[i]);

                if (!result.empty()) {
                    batchResults << result;
                    if (i < statements.size() - 1) {
                        batchResults << "\n\n";
                    }
                }
            }

            allResults << batchResults.str();

            // After each batch, flush file and reload table metadata
            if (file.is_open()) {
                file.flush();
                file.clear(); // Clear stream error flags
            }

            // Re-read header to get latest tableCount and offsets
            FileManager::ReadHeader(file, header);

            // Reload table metadata
            FileManager::LoadTables(file, header, tables);

            if (batchIdx < batches.size() - 1) {
                allResults << "\n\n--- Batch Complete ---\n\n";
            }
        }

        return allResults.str();
    }

    std::string Database::ParseCreateTable(const std::string& query) {
        // Use SQLParser to parse the query
        CreateTableCommand cmd = SQLParser::ParseCreateTable(query);

        if (!cmd.valid) {
            return "Error: " + cmd.errorMessage;
        }

        // Create the table
        if (CreateTable(cmd.tableName, cmd.columns)) {
            return "Success: Table '" + cmd.tableName + "' created with " + 
                   std::to_string(cmd.columns.size()) + " columns";
        } else {
            return "Error: Failed to create table (check console for details)";
        }
    }

    std::string Database::ParseDropTable(const std::string& query) {
        // Use SQLParser to parse the query
        DropTableCommand cmd = SQLParser::ParseDropTable(query);

        if (!cmd.valid) {
            return "Error: " + cmd.errorMessage;
        }

        // Drop the table
        bool success = DropTable(cmd.tableName);

        if (success) {
            return "Success: Table '" + cmd.tableName + "' dropped";
        } else {
            // If IF EXISTS was specified and table doesn't exist, treat as success
            if (cmd.ifExists) {
                return "Success: Table '" + cmd.tableName + "' does not exist (IF EXISTS)";
            } else {
                return "Error: Failed to drop table (check console for details)";
            }
        }
    }

    std::shared_ptr<Table> Database::FindTable(const std::string& tableName) {
        for (const auto& table : tables) {
            if (table->GetName() == tableName) {
                return table;
            }
        }
        return nullptr;
    }

    std::string Database::ParseAlterTable(const std::string& query) {
        // Use SQLParser to parse the query
        AlterTableCommand cmd = SQLParser::ParseAlterTableAddColumn(query);

        if (!cmd.valid) {
            return "Error: " + cmd.errorMessage;
        }

        // Add the column
        if (AlterTableAddColumn(cmd.tableName, cmd.columnName, cmd.columnType, 
                                cmd.columnSize, cmd.defaultValue)) {
            return "Success: Column '" + cmd.columnName + "' added to table '" + cmd.tableName + "'";
        } else {
            return "Error: Failed to add column (check console for details)";
        }
    }

    std::string Database::ParseInsertInto(const std::string& query) {
        // Use SQLParser to parse the query
        InsertCommand cmd = SQLParser::ParseInsertInto(query);

        if (!cmd.valid) {
            return "Error: " + cmd.errorMessage;
        }

        // Insert the row
        if (InsertRow(cmd.tableName, cmd.values)) {
            return "Success: 1 row inserted into '" + cmd.tableName + "'";
        } else {
            return "Error: Failed to insert row (check console for details)";
        }
    }

    bool Database::InsertRow(const std::string& tableName, const std::vector<std::string>& values) {
        // Find table
        auto table = FindTable(tableName);
        if (!table) {
            std::cerr << "Table '" << tableName << "' not found" << std::endl;
            return false;
        }

        const auto& columns = table->GetColumns();

        // Validate column count
        if (values.size() != columns.size()) {
            std::cerr << "Column count mismatch: expected " << columns.size() 
                      << ", got " << values.size() << std::endl;
            return false;
        }

        // Check for duplicate primary key
        // Find the primary key column index
        int pkIndex = -1;
        for (size_t i = 0; i < columns.size(); i++) {
            if (columns[i].isPrimaryKey) {
                pkIndex = static_cast<int>(i);
                break;
            }
        }

        if (pkIndex >= 0) {
            std::string newPkValue = values[pkIndex];

            // Use B-Tree for duplicate primary key check (O(log n) instead of O(n))
            uint64_t pkIndexRoot = table->GetPrimaryKeyIndexRoot();

            if (pkIndexRoot != 0) {
                // B-Tree index exists - use it for fast lookup
                BTree pkBTree(file, header, pkIndexRoot);
                uint64_t existingRowOffset;

                if (pkBTree.Search(newPkValue, existingRowOffset)) {
                    std::cerr << "Error: Duplicate primary key value '" << newPkValue 
                             << "' for column '" << columns[pkIndex].name << "'" << std::endl;
                    return false;
                }
            } else {
                // No B-Tree yet - fall back to linear scan (first insert)
                // This will only happen once per table, then B-Tree is created
                uint64_t rootPage = table->GetRootPage();
                if (rootPage != 0) {
                    PageHeader pageHdr;
                    if (PageManager::ReadPageHeader(file, rootPage, pageHdr)) {
                        // Read all rows from page
                        uint64_t currentOffset = rootPage + sizeof(PageHeader);

                        for (uint32_t row = 0; row < pageHdr.rowCount; row++) {
                            // Read row length
                            file.seekg(currentOffset);
                            uint16_t rowSize;
                            file.read(reinterpret_cast<char*>(&rowSize), sizeof(uint16_t));
                            currentOffset += sizeof(uint16_t);

                            // Read row data
                            std::vector<uint8_t> rowData(rowSize);
                            file.seekg(currentOffset);
                            file.read(reinterpret_cast<char*>(rowData.data()), rowSize);
                            currentOffset += rowSize;

                            // Deserialize row
                            std::vector<std::string> existingRow = RowSerializer::DeserializeRow(rowData, columns);

                            // Check if PK matches
                            if (existingRow.size() > static_cast<size_t>(pkIndex)) {
                                if (existingRow[pkIndex] == newPkValue) {
                                    std::cerr << "Error: Duplicate primary key value '" << newPkValue 
                                             << "' for column '" << columns[pkIndex].name << "'" << std::endl;
                                    return false;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Serialize row data using RowSerializer
        std::vector<uint8_t> rowData = RowSerializer::SerializeRow(values, columns);
        if (rowData.empty()) {
            // Error already printed by RowSerializer
            return false;
        }

        // Allocate page if needed
        uint64_t rootPage = table->GetRootPage();
        if (rootPage == 0) {
            // Get the table's on-disk ID
            uint32_t tableId = table->GetTableId();

            rootPage = PageManager::AllocatePage(file, header, tableId);
            if (rootPage == 0) {
                std::cerr << "Failed to allocate page for table" << std::endl;
                return false;
            }

            // Update table's rootPage in memory and on disk
            table->SetRootPage(rootPage);

            // Update TableEntry on disk
            uint64_t tableEntryOffset = header.tableDirOffset + (tableId * sizeof(TableEntry));
            file.seekp(tableEntryOffset + offsetof(TableEntry, rootPage));
            file.write(reinterpret_cast<const char*>(&rootPage), sizeof(uint64_t));
            file.flush();
        }

        // Read current page header
        PageHeader pageHdr;
        if (!PageManager::ReadPageHeader(file, rootPage, pageHdr)) {
            std::cerr << "Failed to read page header" << std::endl;
            return false;
        }

        // Check if row fits in page
        uint16_t rowSize = static_cast<uint16_t>(rowData.size());
        uint16_t requiredSpace = sizeof(uint16_t) + rowSize; // 2 bytes for length + data

        uint16_t availableSpace = header.pageSize - pageHdr.freeOffset;
        if (requiredSpace > availableSpace) {
            std::cerr << "Row too large for page (need " << requiredSpace 
                      << " bytes, have " << availableSpace << ")" << std::endl;
            // TODO: Allocate new page and link it
            return false;
        }

        // Write row to page
        uint64_t rowOffset = rootPage + pageHdr.freeOffset;
        file.seekp(rowOffset);

        // Write row length
        file.write(reinterpret_cast<const char*>(&rowSize), sizeof(uint16_t));

        // Write row data
        file.write(reinterpret_cast<const char*>(rowData.data()), rowData.size());

        // Update page header
        pageHdr.rowCount++;
        pageHdr.freeOffset += requiredSpace;

        if (!PageManager::WritePageHeader(file, rootPage, pageHdr)) {
            std::cerr << "Failed to update page header" << std::endl;
            return false;
        }

        file.flush();

        // Add to B-Tree index (if primary key exists)
        if (pkIndex >= 0) {
            std::string newPkValue = values[pkIndex];
            uint64_t pkIndexRoot = table->GetPrimaryKeyIndexRoot();

            if (pkIndexRoot == 0) {
                // Create new B-Tree for this table's primary key
                BTree pkBTree(file, header, 0);  // 0 = create new tree
                pkIndexRoot = pkBTree.GetRootPage();

                // Store B-Tree root in table metadata
                table->SetPrimaryKeyIndexRoot(pkIndexRoot);

                // Update TableEntry on disk
                uint32_t tableId = table->GetTableId();
                uint64_t tableEntryOffset = header.tableDirOffset + (tableId * sizeof(TableEntry));
                file.seekp(tableEntryOffset + offsetof(TableEntry, primaryKeyIndexRoot));
                file.write(reinterpret_cast<const char*>(&pkIndexRoot), sizeof(uint64_t));
                file.flush();

                std::cout << "Created B-Tree index for table '" << tableName 
                         << "' at page " << pkIndexRoot << std::endl;
            }

            // Insert into B-Tree (key = PK value, value = row offset)
            BTree pkBTree(file, header, pkIndexRoot);
            if (!pkBTree.Insert(newPkValue, rowOffset, true)) {
                std::cerr << "Warning: Failed to insert into B-Tree index" << std::endl;
                // Don't fail the insert - data is already written
            }
        }

        std::cout << "Row inserted successfully into table '" << tableName 
                  << "' (row " << pageHdr.rowCount << ")" << std::endl;
        return true;
    }

    std::string Database::ParseSelect(const std::string& query) {
        // Use SQLParser to parse the query
        SelectCommand cmd = SQLParser::ParseSelect(query);

        if (!cmd.valid) {
            return "Error: " + cmd.errorMessage;
        }

        // Check if this is a JOIN query
        if (cmd.joinType != JoinType::NONE) {
            return SelectJoinRows(cmd.tableName, cmd.joinTableName, cmd.columnNames,
                                cmd.joinType, cmd.leftTableAlias, cmd.rightTableAlias,
                                cmd.joinOnLeftColumn, cmd.joinOnRightColumn,
                                cmd.whereColumn, cmd.whereValue, cmd.whereOperator,
                                cmd.whereConditions, cmd.whereLogicalOps,
                                cmd.orderByColumn, cmd.orderByAscending,
                                cmd.limit, cmd.offset);
        }

        return SelectRows(cmd.tableName, cmd.columnNames, cmd.whereColumn, cmd.whereValue,
                          cmd.whereOperator, cmd.orderByColumn, cmd.orderByAscending,
                          cmd.limit, cmd.offset);
    }

    std::string Database::SelectRows(const std::string& tableName, 
                                      const std::vector<std::string>& columnNames,
                                      const std::string& whereColumn, 
                                      const std::string& whereValue,
                                      const std::string& whereOperator,
                                      const std::string& orderByColumn,
                                      bool orderByAscending,
                                      int limit,
                                      int offset) {
        // Find table
        auto table = FindTable(tableName);
        if (!table) {
            return "Error: Table '" + tableName + "' not found";
        }

        const auto& columns = table->GetColumns();
        uint64_t rootPage = table->GetRootPage();

        if (rootPage == 0) {
            return "0 rows selected from '" + tableName + "'";
        }

        // Determine which columns to select
        std::vector<size_t> selectedColumnIndices;
        if (columnNames.size() == 1 && columnNames[0] == "*") {
            // Select all columns
            for (size_t i = 0; i < columns.size(); i++) {
                selectedColumnIndices.push_back(i);
            }
        } else {
            // Find specified columns
            for (const auto& colName : columnNames) {
                bool found = false;
                for (size_t i = 0; i < columns.size(); i++) {
                    if (columns[i].name == colName) {
                        selectedColumnIndices.push_back(i);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return "Error: Column '" + colName + "' not found in table";
                }
            }
        }

        // Find WHERE column index if specified
        int whereColumnIndex = -1;
        if (!whereColumn.empty()) {
            for (size_t i = 0; i < columns.size(); i++) {
                if (columns[i].name == whereColumn) {
                    whereColumnIndex = static_cast<int>(i);
                    break;
                }
            }
            if (whereColumnIndex == -1) {
                return "Error: WHERE column '" + whereColumn + "' not found";
            }
        }

        // Read page header
        PageHeader pageHdr;
        if (!PageManager::ReadPageHeader(file, rootPage, pageHdr)) {
            return "Error: Failed to read page header";
        }

        // Read all rows from page
        std::vector<std::vector<std::string>> resultRows;
        uint64_t currentOffset = rootPage + sizeof(PageHeader);

        for (uint32_t row = 0; row < pageHdr.rowCount; row++) {
            // Read row length
            file.seekg(currentOffset);
            uint16_t rowSize;
            file.read(reinterpret_cast<char*>(&rowSize), sizeof(uint16_t));
            currentOffset += sizeof(uint16_t);

            // Read row data
            std::vector<uint8_t> rowData(rowSize);
            file.seekg(currentOffset);
            file.read(reinterpret_cast<char*>(rowData.data()), rowSize);
            currentOffset += rowSize;

            // Deserialize row using RowSerializer
            std::vector<std::string> rowValues = RowSerializer::DeserializeRow(rowData, columns);

            // Apply WHERE filter if specified using WhereClauseEvaluator
            if (!WhereClauseEvaluator::Evaluate(rowValues, columns, whereColumn, whereValue, whereOperator)) {
                continue; // Skip this row
            }

            // Extract selected columns
            std::vector<std::string> selectedValues;
            for (size_t idx : selectedColumnIndices) {
                selectedValues.push_back(rowValues[idx]);
            }

            resultRows.push_back(selectedValues);
        }

        // Sort results if ORDER BY specified
        if (!orderByColumn.empty() && !resultRows.empty()) {
            // Find the ORDER BY column index in the selected columns
            int orderByIndex = -1;
            for (size_t i = 0; i < selectedColumnIndices.size(); i++) {
                if (columns[selectedColumnIndices[i]].name == orderByColumn) {
                    orderByIndex = static_cast<int>(i);
                    break;
                }
            }

            if (orderByIndex == -1) {
                return "Error: ORDER BY column '" + orderByColumn + "' not in selected columns";
            }

            // Get the column type for proper sorting
            ColumnType orderColType = columns[selectedColumnIndices[orderByIndex]].type;

            // Sort based on column type
            std::sort(resultRows.begin(), resultRows.end(), 
                [orderByIndex, orderByAscending, orderColType](const std::vector<std::string>& a, 
                                                                const std::vector<std::string>& b) {
                const std::string& aVal = a[orderByIndex];
                const std::string& bVal = b[orderByIndex];

                // Numeric comparison for numeric types
                if (orderColType == ColumnType::TINYINT || orderColType == ColumnType::SMALLINT ||
                    orderColType == ColumnType::INT || orderColType == ColumnType::BIGINT) {
                    long long aNum = std::stoll(aVal);
                    long long bNum = std::stoll(bVal);
                    return orderByAscending ? (aNum < bNum) : (aNum > bNum);
                }
                else if (orderColType == ColumnType::FLOAT || orderColType == ColumnType::REAL) {
                    double aNum = std::stod(aVal);
                    double bNum = std::stod(bVal);
                    return orderByAscending ? (aNum < bNum) : (aNum > bNum);
                }
                // String comparison for all other types (case-insensitive)
                else {
                    // Convert both to lowercase for case-insensitive comparison
                    std::string aLower = aVal;
                    std::string bLower = bVal;
                    std::transform(aLower.begin(), aLower.end(), aLower.begin(), ::tolower);
                    std::transform(bLower.begin(), bLower.end(), bLower.begin(), ::tolower);
                    return orderByAscending ? (aLower < bLower) : (aLower > bLower);
                }
            });
        }

        // Apply OFFSET and LIMIT
        if (offset > 0 || limit >= 0) {
            size_t startIndex = static_cast<size_t>(offset);
            size_t endIndex = resultRows.size();

            // Validate offset
            if (startIndex >= resultRows.size()) {
                // Offset beyond result set
                return "0 rows selected from '" + tableName + "'";
            }

            // Apply limit if specified
            if (limit >= 0) {
                size_t maxRows = static_cast<size_t>(limit);
                endIndex = std::min(startIndex + maxRows, resultRows.size());
            }

            // Extract the range
            std::vector<std::vector<std::string>> limitedRows(
                resultRows.begin() + startIndex,
                resultRows.begin() + endIndex
            );
            resultRows = limitedRows;
        }

        // Format results as table
        if (resultRows.empty()) {
            return "0 rows selected from '" + tableName + "'";
        }

        std::ostringstream result;
        result << resultRows.size() << " row(s) selected from '" << tableName << "':\n\n";

        // Build header
        std::vector<size_t> columnWidths;
        for (size_t idx : selectedColumnIndices) {
            size_t width = columns[idx].name.length();
            // Find max width for this column
            for (const auto& row : resultRows) {
                size_t colIdx = &idx - &selectedColumnIndices[0];
                if (colIdx < row.size()) {
                    width = std::max(width, row[colIdx].length());
                }
            }
            columnWidths.push_back(std::max(width, size_t(10))); // Min width 10
        }

        // Print header
        for (size_t i = 0; i < selectedColumnIndices.size(); i++) {
            size_t idx = selectedColumnIndices[i];
            result << std::left << std::setw(columnWidths[i] + 2) << columns[idx].name;
        }
        result << "\n";

        // Print separator
        for (size_t width : columnWidths) {
            result << std::string(width + 2, '-');
        }
        result << "\n";

        // Print rows
        for (const auto& row : resultRows) {
            for (size_t i = 0; i < row.size(); i++) {
                result << std::left << std::setw(columnWidths[i] + 2) << row[i];
            }
            result << "\n";
        }

        return result.str();
    }

    std::string Database::SelectJoinRows(const std::string& leftTableName, const std::string& rightTableName,
                                         const std::vector<std::string>& columnNames,
                                         JoinType joinType, const std::string& leftTableAlias, const std::string& rightTableAlias,
                                         const std::string& joinOnLeftColumn, const std::string& joinOnRightColumn,
                                         const std::string& whereColumn, const std::string& whereValue,
                                         const std::string& whereOperator,
                                         const std::vector<WhereCondition>& whereConditions,
                                         const std::vector<std::string>& whereLogicalOps,
                                         const std::string& orderByColumn, bool orderByAscending,
                                         int limit, int offset) {
        // Find both tables
        auto leftTable = FindTable(leftTableName);
        if (!leftTable) {
            return "Error: Table '" + leftTableName + "' not found";
        }

        auto rightTable = FindTable(rightTableName);
        if (!rightTable) {
            return "Error: Table '" + rightTableName + "' not found";
        }

        const auto& leftColumns = leftTable->GetColumns();
        const auto& rightColumns = rightTable->GetColumns();

        // Helper to resolve qualified column name (table.column or alias.column)
        auto resolveColumnName = [&](const std::string& qualifiedName, bool& isLeft, std::string& columnName) -> bool {
            size_t dotPos = qualifiedName.find('.');
            if (dotPos != std::string::npos) {
                std::string prefix = qualifiedName.substr(0, dotPos);
                columnName = qualifiedName.substr(dotPos + 1);

                // Check if prefix matches table name or alias
                if (prefix == leftTableName || (!leftTableAlias.empty() && prefix == leftTableAlias)) {
                    isLeft = true;
                    return true;
                } else if (prefix == rightTableName || (!rightTableAlias.empty() && prefix == rightTableAlias)) {
                    isLeft = false;
                    return true;
                }
                return false; // Unknown prefix
            } else {
                // Unqualified - try to find in both tables
                columnName = qualifiedName;
                bool foundInLeft = false;
                bool foundInRight = false;

                for (const auto& col : leftColumns) {
                    if (col.name == columnName) {
                        foundInLeft = true;
                        break;
                    }
                }

                for (const auto& col : rightColumns) {
                    if (col.name == columnName) {
                        foundInRight = true;
                        break;
                    }
                }

                if (foundInLeft && foundInRight) {
                    return false; // Ambiguous
                } else if (foundInLeft) {
                    isLeft = true;
                    return true;
                } else if (foundInRight) {
                    isLeft = false;
                    return true;
                }
                return false; // Not found
            }
        };

        // Resolve ON columns
        bool onLeftIsLeft, onRightIsLeft;
        std::string onLeftCol, onRightCol;

        if (!resolveColumnName(joinOnLeftColumn, onLeftIsLeft, onLeftCol)) {
            return "Error: Cannot resolve column '" + joinOnLeftColumn + "' in JOIN ON clause";
        }
        if (!resolveColumnName(joinOnRightColumn, onRightIsLeft, onRightCol)) {
            return "Error: Cannot resolve column '" + joinOnRightColumn + "' in JOIN ON clause";
        }

        // Find column indices for ON condition
        int leftOnIndex = -1, rightOnIndex = -1;

        if (onLeftIsLeft) {
            for (size_t i = 0; i < leftColumns.size(); i++) {
                if (leftColumns[i].name == onLeftCol) {
                    leftOnIndex = static_cast<int>(i);
                    break;
                }
            }
        } else {
            for (size_t i = 0; i < rightColumns.size(); i++) {
                if (rightColumns[i].name == onLeftCol) {
                    rightOnIndex = static_cast<int>(i);
                    break;
                }
            }
        }

        if (onRightIsLeft) {
            if (leftOnIndex != -1) {
                return "Error: Both ON columns cannot be from the same table";
            }
            for (size_t i = 0; i < leftColumns.size(); i++) {
                if (leftColumns[i].name == onRightCol) {
                    leftOnIndex = static_cast<int>(i);
                    break;
                }
            }
        } else {
            if (rightOnIndex != -1) {
                return "Error: Both ON columns cannot be from the same table";
            }
            for (size_t i = 0; i < rightColumns.size(); i++) {
                if (rightColumns[i].name == onRightCol) {
                    rightOnIndex = static_cast<int>(i);
                    break;
                }
            }
        }

        if (leftOnIndex == -1 || rightOnIndex == -1) {
            return "Error: ON columns not found in tables";
        }

        // Read all rows from both tables
        std::vector<std::vector<std::string>> leftRows;
        std::vector<std::vector<std::string>> rightRows;

        // Read left table
        uint64_t leftRootPage = leftTable->GetRootPage();
        if (leftRootPage == 0) {
            return "0 rows selected from JOIN (left table is empty)";
        }

        PageHeader leftPageHdr;
        if (!PageManager::ReadPageHeader(file, leftRootPage, leftPageHdr)) {
            return "Error: Failed to read left table page header";
        }

        uint64_t leftOffset = leftRootPage + sizeof(PageHeader);
        for (uint32_t i = 0; i < leftPageHdr.rowCount; i++) {
            file.seekg(leftOffset);
            uint16_t rowSize;
            file.read(reinterpret_cast<char*>(&rowSize), sizeof(uint16_t));
            leftOffset += sizeof(uint16_t);

            std::vector<uint8_t> rowData(rowSize);
            file.seekg(leftOffset);
            file.read(reinterpret_cast<char*>(rowData.data()), rowSize);
            leftOffset += rowSize;

            std::vector<std::string> rowValues = RowSerializer::DeserializeRow(rowData, leftColumns);
            leftRows.push_back(rowValues);
        }

        // Read right table
        uint64_t rightRootPage = rightTable->GetRootPage();
        if (rightRootPage == 0) {
            if (joinType == JoinType::LEFT) {
                // For LEFT JOIN, if right table is empty, return all left rows with NULLs
                std::vector<std::vector<std::string>> joinedRows;
                for (const auto& leftRow : leftRows) {
                    std::vector<std::string> merged = leftRow;
                    for (size_t i = 0; i < rightColumns.size(); i++) {
                        merged.push_back("NULL");
                    }
                    joinedRows.push_back(merged);
                }
                leftRows = joinedRows;
                rightRows.clear();
            } else {
                return "0 rows selected from JOIN (right table is empty)";
            }
        } else {
            PageHeader rightPageHdr;
            if (!PageManager::ReadPageHeader(file, rightRootPage, rightPageHdr)) {
                return "Error: Failed to read right table page header";
            }

            uint64_t rightOffset = rightRootPage + sizeof(PageHeader);
            for (uint32_t i = 0; i < rightPageHdr.rowCount; i++) {
                file.seekg(rightOffset);
                uint16_t rowSize;
                file.read(reinterpret_cast<char*>(&rowSize), sizeof(uint16_t));
                rightOffset += sizeof(uint16_t);

                std::vector<uint8_t> rowData(rowSize);
                file.seekg(rightOffset);
                file.read(reinterpret_cast<char*>(rowData.data()), rowSize);
                rightOffset += rowSize;

                std::vector<std::string> rowValues = RowSerializer::DeserializeRow(rowData, rightColumns);
                rightRows.push_back(rowValues);
            }
        }

        // Perform JOIN operation
        std::vector<std::vector<std::string>> joinedRows;
        std::vector<bool> rightRowMatched(rightRows.size(), false);

        // Handle special case where right table is empty and LEFT JOIN already populated
        if (rightRows.empty() && joinType == JoinType::LEFT) {
            joinedRows = leftRows; // Already has NULLs appended
        } else {
            for (const auto& leftRow : leftRows) {
                bool leftRowMatched = false;

                for (size_t rIdx = 0; rIdx < rightRows.size(); rIdx++) {
                    const auto& rightRow = rightRows[rIdx];

                    // Check if ON condition matches
                    if (leftRow[leftOnIndex] == rightRow[rightOnIndex]) {
                        leftRowMatched = true;
                        rightRowMatched[rIdx] = true;

                        // Merge rows: leftRow + rightRow
                        std::vector<std::string> merged = leftRow;
                        merged.insert(merged.end(), rightRow.begin(), rightRow.end());
                        joinedRows.push_back(merged);
                    }
                }

                // For LEFT JOIN, include unmatched left rows with NULL for right columns
                if (joinType == JoinType::LEFT && !leftRowMatched) {
                    std::vector<std::string> merged = leftRow;
                    for (size_t i = 0; i < rightColumns.size(); i++) {
                        merged.push_back("NULL");
                    }
                    joinedRows.push_back(merged);
                }
            }

            // For RIGHT JOIN, include unmatched right rows with NULL for left columns
            if (joinType == JoinType::RIGHT) {
                for (size_t rIdx = 0; rIdx < rightRows.size(); rIdx++) {
                    if (!rightRowMatched[rIdx]) {
                        std::vector<std::string> merged;
                        for (size_t i = 0; i < leftColumns.size(); i++) {
                            merged.push_back("NULL");
                        }
                        merged.insert(merged.end(), rightRows[rIdx].begin(), rightRows[rIdx].end());
                        joinedRows.push_back(merged);
                    }
                }
            }
        }

        // Build merged column list
        std::vector<Column> mergedColumns;
        for (const auto& col : leftColumns) {
            mergedColumns.push_back(col);
        }
        for (const auto& col : rightColumns) {
            mergedColumns.push_back(col);
        }

        // Determine which columns to select
        std::vector<size_t> selectedColumnIndices;

        if (columnNames.size() == 1 && columnNames[0] == "*") {
            // Select all columns from both tables
            for (size_t i = 0; i < mergedColumns.size(); i++) {
                selectedColumnIndices.push_back(i);
            }
        } else {
            // Parse specific columns
            for (const auto& colName : columnNames) {
                bool isLeft;
                std::string resolvedName;
                if (!resolveColumnName(colName, isLeft, resolvedName)) {
                    return "Error: Cannot resolve column '" + colName + "'";
                }

                // Find in merged columns
                bool found = false;
                for (size_t i = 0; i < mergedColumns.size(); i++) {
                    // Check if this column matches
                    if (i < leftColumns.size() && isLeft && mergedColumns[i].name == resolvedName) {
                        selectedColumnIndices.push_back(i);
                        found = true;
                        break;
                    } else if (i >= leftColumns.size() && !isLeft && mergedColumns[i].name == resolvedName) {
                        selectedColumnIndices.push_back(i);
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    return "Error: Column '" + colName + "' not found";
                }
            }
        }

        // Apply WHERE clause to joined rows
        std::vector<std::vector<std::string>> resultRows;

        // Resolve WHERE column to its index in merged columns
        int whereColumnIndex = -1;
        if (!whereColumn.empty()) {
            bool isLeft;
            std::string resolvedName;
            if (resolveColumnName(whereColumn, isLeft, resolvedName)) {
                // Search in the correct half of merged columns
                if (isLeft) {
                    // Search in left table columns (first part of merged)
                    for (size_t i = 0; i < leftColumns.size(); i++) {
                        if (mergedColumns[i].name == resolvedName) {
                            whereColumnIndex = static_cast<int>(i);
                            break;
                        }
                    }
                } else {
                    // Search in right table columns (second part of merged)
                    for (size_t i = leftColumns.size(); i < mergedColumns.size(); i++) {
                        if (mergedColumns[i].name == resolvedName) {
                            whereColumnIndex = static_cast<int>(i);
                            break;
                        }
                    }
                }
            }
        }

        for (const auto& row : joinedRows) {
            // Apply WHERE filter using column index instead of name
            bool matchesWhere = true;
            if (whereColumnIndex >= 0 && whereColumnIndex < static_cast<int>(row.size())) {
                // Manually evaluate WHERE condition using the correct column index
                const std::string& rowValue = row[whereColumnIndex];

                // Use WhereClauseEvaluator's comparison logic
                // But we need to compare directly, not search by name
                try {
                    if (whereOperator == "=") {
                        matchesWhere = (rowValue == whereValue);
                    } else if (whereOperator == "!=" || whereOperator == "<>") {
                        matchesWhere = (rowValue != whereValue);
                    } else if (whereOperator == "<") {
                        // Try numeric comparison first
                        try {
                            double rowNum = std::stod(rowValue);
                            double whereNum = std::stod(whereValue);
                            matchesWhere = (rowNum < whereNum);
                        } catch (...) {
                            matchesWhere = (rowValue < whereValue);
                        }
                    } else if (whereOperator == ">") {
                        try {
                            double rowNum = std::stod(rowValue);
                            double whereNum = std::stod(whereValue);
                            matchesWhere = (rowNum > whereNum);
                        } catch (...) {
                            matchesWhere = (rowValue > whereValue);
                        }
                    } else if (whereOperator == "<=") {
                        try {
                            double rowNum = std::stod(rowValue);
                            double whereNum = std::stod(whereValue);
                            matchesWhere = (rowNum <= whereNum);
                        } catch (...) {
                            matchesWhere = (rowValue <= whereValue);
                        }
                    } else if (whereOperator == ">=") {
                        try {
                            double rowNum = std::stod(rowValue);
                            double whereNum = std::stod(whereValue);
                            matchesWhere = (rowNum >= whereNum);
                        } catch (...) {
                            matchesWhere = (rowValue >= whereValue);
                        }
                    }
                } catch (...) {
                    matchesWhere = false;
                }
            } else if (!whereColumn.empty()) {
                // Fallback to WhereClauseEvaluator if column name wasn't resolved
                matchesWhere = WhereClauseEvaluator::Evaluate(row, mergedColumns, whereColumn, whereValue, whereOperator);
            }

            // Apply compound WHERE clause if present
            if (!whereConditions.empty()) {
                matchesWhere = WhereClauseEvaluator::EvaluateCompound(row, mergedColumns, whereConditions, whereLogicalOps);
            }

            if (matchesWhere) {
                // Keep full row for sorting - we'll extract selected columns later
                resultRows.push_back(row);
            }
        }

        // Apply ORDER BY before extracting selected columns
        if (!orderByColumn.empty()) {
            // Resolve ORDER BY column if it's qualified (table.column or alias.column)
            std::string resolvedOrderByColumn = orderByColumn;
            bool isLeft;
            std::string tempCol;
            if (resolveColumnName(orderByColumn, isLeft, tempCol)) {
                resolvedOrderByColumn = tempCol; // Use unqualified name
            }

            // Find order column in ALL merged columns (not just selected)
            int orderByIndex = -1;
            for (size_t i = 0; i < mergedColumns.size(); i++) {
                if (mergedColumns[i].name == resolvedOrderByColumn) {
                    orderByIndex = static_cast<int>(i);
                    break;
                }
            }

            if (orderByIndex == -1) {
                return "Error: ORDER BY column '" + orderByColumn + "' not found in joined tables";
            }

            ColumnType orderColType = mergedColumns[orderByIndex].type;

            std::sort(resultRows.begin(), resultRows.end(),
                [orderByIndex, orderByAscending, orderColType](const std::vector<std::string>& a,
                                                                const std::vector<std::string>& b) {
                const std::string& aVal = a[orderByIndex];
                const std::string& bVal = b[orderByIndex];

                // Handle NULL values (for LEFT/RIGHT JOINs)
                if (aVal == "NULL" && bVal == "NULL") return false;
                if (aVal == "NULL") return !orderByAscending; // NULLs sort last in ASC, first in DESC
                if (bVal == "NULL") return orderByAscending;

                if (orderColType == ColumnType::TINYINT || orderColType == ColumnType::SMALLINT ||
                    orderColType == ColumnType::INT || orderColType == ColumnType::BIGINT) {
                    long long aNum = std::stoll(aVal);
                    long long bNum = std::stoll(bVal);
                    return orderByAscending ? (aNum < bNum) : (aNum > bNum);
                }
                else if (orderColType == ColumnType::FLOAT || orderColType == ColumnType::REAL) {
                    double aNum = std::stod(aVal);
                    double bNum = std::stod(bVal);
                    return orderByAscending ? (aNum < bNum) : (aNum > bNum);
                }
                else {
                    std::string aLower = aVal;
                    std::string bLower = bVal;
                    std::transform(aLower.begin(), aLower.end(), aLower.begin(), ::tolower);
                    std::transform(bLower.begin(), bLower.end(), bLower.begin(), ::tolower);
                    return orderByAscending ? (aLower < bLower) : (aLower > bLower);
                }
            });
        }

        // NOW extract only selected columns from sorted full rows
        std::vector<std::vector<std::string>> finalRows;
        for (const auto& fullRow : resultRows) {
            std::vector<std::string> selectedRow;
            for (size_t idx : selectedColumnIndices) {
                selectedRow.push_back(fullRow[idx]);
            }
            finalRows.push_back(selectedRow);
        }
        resultRows = finalRows;

        // Apply OFFSET and LIMIT
        if (offset > 0 || limit >= 0) {
            size_t startIndex = static_cast<size_t>(offset);
            size_t endIndex = resultRows.size();

            if (startIndex >= resultRows.size()) {
                return "0 rows selected from JOIN";
            }

            if (limit >= 0) {
                size_t maxRows = static_cast<size_t>(limit);
                endIndex = std::min(startIndex + maxRows, resultRows.size());
            }

            std::vector<std::vector<std::string>> limitedRows(
                resultRows.begin() + startIndex,
                resultRows.begin() + endIndex
            );
            resultRows = limitedRows;
        }

        // Format results
        if (resultRows.empty()) {
            return "0 rows selected from JOIN";
        }

        std::ostringstream result;
        result << resultRows.size() << " row(s) selected from JOIN:\n\n";

        // Build header with table prefixes
        std::vector<size_t> columnWidths;
        for (size_t idx : selectedColumnIndices) {
            std::string header;
            if (idx < leftColumns.size()) {
                header = (leftTableAlias.empty() ? leftTableName : leftTableAlias) + "." + mergedColumns[idx].name;
            } else {
                header = (rightTableAlias.empty() ? rightTableName : rightTableAlias) + "." + mergedColumns[idx].name;
            }

            size_t width = header.length();
            for (const auto& row : resultRows) {
                size_t colIdx = &idx - &selectedColumnIndices[0];
                if (colIdx < row.size()) {
                    width = std::max(width, row[colIdx].length());
                }
            }
            columnWidths.push_back(std::max(width, size_t(10)));
        }

        // Print header
        for (size_t i = 0; i < selectedColumnIndices.size(); i++) {
            size_t idx = selectedColumnIndices[i];
            std::string header;
            if (idx < leftColumns.size()) {
                header = (leftTableAlias.empty() ? leftTableName : leftTableAlias) + "." + mergedColumns[idx].name;
            } else {
                header = (rightTableAlias.empty() ? rightTableName : rightTableAlias) + "." + mergedColumns[idx].name;
            }
            result << std::left << std::setw(columnWidths[i] + 2) << header;
        }
        result << "\n";

        // Print separator
        for (size_t width : columnWidths) {
            result << std::string(width + 2, '-');
        }
        result << "\n";

        // Print rows
        for (const auto& row : resultRows) {
            for (size_t i = 0; i < row.size(); i++) {
                result << std::left << std::setw(columnWidths[i] + 2) << row[i];
            }
            result << "\n";
        }

        return result.str();
    }

    std::string Database::ParseUpdate(const std::string& query) {
        // Use SQLParser to parse the query
        UpdateCommand cmd = SQLParser::ParseUpdate(query);

        if (!cmd.valid) {
            return "Error: " + cmd.errorMessage;
        }

        if (UpdateRow(cmd.tableName, cmd.setColumns, cmd.setValues, cmd.whereColumn, cmd.whereValue)) {
            return "Success: Updated row(s) in table '" + cmd.tableName + "'";
        } else {
            return "Error: Failed to update row(s) (check console for details)";
        }
    }

    bool Database::UpdateRow(const std::string& tableName, const std::vector<std::string>& setColumns,
                             const std::vector<std::string>& setValues, const std::string& whereColumn,
                             const std::string& whereValue) {
        // Find table
        auto table = FindTable(tableName);
        if (!table) {
            std::cerr << "Table '" << tableName << "' not found" << std::endl;
            return false;
        }

        const auto& columns = table->GetColumns();
        uint64_t rootPage = table->GetRootPage();

        if (rootPage == 0) {
            std::cerr << "Table has no data" << std::endl;
            return false;
        }

        // Validate SET columns exist
        std::vector<size_t> setColumnIndices;
        for (const auto& colName : setColumns) {
            bool found = false;
            for (size_t i = 0; i < columns.size(); i++) {
                if (columns[i].name == colName) {
                    setColumnIndices.push_back(i);
                    found = true;
                    break;
                }
            }
            if (!found) {
                std::cerr << "Column '" << colName << "' not found in table" << std::endl;
                return false;
            }
        }

        // Find WHERE column index if specified
        int whereColumnIndex = -1;
        if (!whereColumn.empty()) {
            for (size_t i = 0; i < columns.size(); i++) {
                if (columns[i].name == whereColumn) {
                    whereColumnIndex = static_cast<int>(i);
                    break;
                }
            }
            if (whereColumnIndex == -1) {
                std::cerr << "WHERE column '" << whereColumn << "' not found" << std::endl;
                return false;
            }
        }

        // Check if WHERE is on primary key and B-Tree exists → use fast lookup
        int pkIndex = -1;
        for (size_t i = 0; i < columns.size(); i++) {
            if (columns[i].isPrimaryKey) {
                pkIndex = static_cast<int>(i);
                break;
            }
        }

        bool useBTreeLookup = (pkIndex >= 0 && whereColumnIndex == pkIndex && 
                               table->GetPrimaryKeyIndexRoot() != 0);

        if (useBTreeLookup) {
            // Fast path: Use B-Tree to find row by primary key (O(log n))
            BTree pkBTree(file, header, table->GetPrimaryKeyIndexRoot());
            uint64_t rowOffset;

            if (!pkBTree.Search(whereValue, rowOffset)) {
                std::cout << "No row found with primary key '" << whereValue << "'" << std::endl;
                return false;
            }

            // Read the specific row at rowOffset
            file.seekg(rowOffset);
            uint16_t rowSize;
            file.read(reinterpret_cast<char*>(&rowSize), sizeof(uint16_t));

            std::vector<uint8_t> rowData(rowSize);
            file.read(reinterpret_cast<char*>(rowData.data()), rowSize);

            std::vector<std::string> rowValues = RowSerializer::DeserializeRow(rowData, columns);
            if (rowValues.empty()) {
                std::cerr << "Failed to deserialize row at offset " << rowOffset << std::endl;
                return false;
            }

            // Update the specified columns
            for (size_t i = 0; i < setColumnIndices.size(); i++) {
                rowValues[setColumnIndices[i]] = setValues[i];
            }

            // Serialize updated row
            std::vector<uint8_t> updatedRowData = RowSerializer::SerializeRow(rowValues, columns);
            if (updatedRowData.empty()) {
                std::cerr << "Failed to serialize updated row" << std::endl;
                return false;
            }

            uint16_t newRowSize = static_cast<uint16_t>(updatedRowData.size());

            // Check if updated row fits in same space
            if (newRowSize <= rowSize) {
                // Write updated row in place
                file.seekp(rowOffset);
                file.write(reinterpret_cast<const char*>(&newRowSize), sizeof(uint16_t));
                file.write(reinterpret_cast<const char*>(updatedRowData.data()), updatedRowData.size());

                // If row is smaller, clear remaining space
                if (newRowSize < rowSize) {
                    std::vector<uint8_t> padding(rowSize - newRowSize, 0);
                    file.write(reinterpret_cast<const char*>(padding.data()), padding.size());
                }

                file.flush();
                std::cout << "Updated 1 row(s) in table '" << tableName << "' using B-Tree lookup" << std::endl;
                return true;
            } else {
                std::cerr << "Updated row size (" << newRowSize 
                          << ") exceeds original size (" << rowSize 
                          << "), in-place update not supported" << std::endl;
                return false;
            }
        }

        // Slow path: Linear scan through all rows (fallback for non-PK WHERE or no B-Tree)
        // Read page header
        PageHeader pageHdr;
        if (!PageManager::ReadPageHeader(file, rootPage, pageHdr)) {
            std::cerr << "Failed to read page header" << std::endl;
            return false;
        }

        // Read and update matching rows
        uint64_t currentOffset = rootPage + sizeof(PageHeader);
        int rowsUpdated = 0;

        for (uint32_t row = 0; row < pageHdr.rowCount; row++) {
            // Save current position for potential rewrite
            uint64_t rowStartOffset = currentOffset;

            // Read row length
            file.seekg(currentOffset);
            uint16_t rowSize;
            file.read(reinterpret_cast<char*>(&rowSize), sizeof(uint16_t));
            currentOffset += sizeof(uint16_t);

            // Read row data
            std::vector<uint8_t> rowData(rowSize);
            file.read(reinterpret_cast<char*>(rowData.data()), rowSize);

            // Deserialize row
            std::vector<std::string> rowValues = RowSerializer::DeserializeRow(rowData, columns);
            if (rowValues.empty()) {
                std::cerr << "Failed to deserialize row" << std::endl;
                currentOffset += rowSize;
                continue;
            }

            // Check WHERE condition using WhereClauseEvaluator
            if (!WhereClauseEvaluator::Evaluate(rowValues, columns, whereColumn, whereValue, "=")) {
                currentOffset += rowSize;
                continue;
            }

            // Update the specified columns
            for (size_t i = 0; i < setColumnIndices.size(); i++) {
                rowValues[setColumnIndices[i]] = setValues[i];
            }
                    // Serialize updated row
                    std::vector<uint8_t> updatedRowData = RowSerializer::SerializeRow(rowValues, columns);
                    if (updatedRowData.empty()) {
                        std::cerr << "Failed to serialize updated row" << std::endl;
                        currentOffset += rowSize;
                        continue;
                    }

                    uint16_t newRowSize = static_cast<uint16_t>(updatedRowData.size());

                    // Check if updated row fits in same space
                    if (newRowSize <= rowSize) {
                        // Write updated row in place
                        file.seekp(rowStartOffset);
                        file.write(reinterpret_cast<const char*>(&newRowSize), sizeof(uint16_t));
                        file.write(reinterpret_cast<const char*>(updatedRowData.data()), updatedRowData.size());

                        // If row is smaller, clear remaining space
                        if (newRowSize < rowSize) {
                            std::vector<uint8_t> padding(rowSize - newRowSize, 0);
                            file.write(reinterpret_cast<const char*>(padding.data()), padding.size());
                        }

                        rowsUpdated++;
                    } else {
                        std::cerr << "Updated row size (" << newRowSize 
                                  << ") exceeds original size (" << rowSize 
                                  << "), in-place update not supported" << std::endl;
                        currentOffset += rowSize;
                        continue;
                    }

                    currentOffset += rowSize;
                }

        file.flush();

        std::cout << "Updated " << rowsUpdated << " row(s) in table '" << tableName << "'" << std::endl;
        return rowsUpdated > 0;
    }

    std::string Database::ParseDelete(const std::string& query) {
        // Use SQLParser to parse the query
        DeleteCommand cmd = SQLParser::ParseDelete(query);

        if (!cmd.valid) {
            return "Error: " + cmd.errorMessage;
        }

        if (DeleteRow(cmd.tableName, cmd.whereColumn, cmd.whereValue)) {
            return "Success: Deleted row(s) from table '" + cmd.tableName + "'";
        } else {
            return "Error: Failed to delete row(s) (check console for details)";
        }
    }

    bool Database::DeleteRow(const std::string& tableName, const std::string& whereColumn,
                             const std::string& whereValue) {
        // Find table
        auto table = FindTable(tableName);
        if (!table) {
            std::cerr << "Table '" << tableName << "' not found" << std::endl;
            return false;
        }

        const auto& columns = table->GetColumns();
        uint64_t rootPage = table->GetRootPage();

        if (rootPage == 0) {
            std::cerr << "Table has no data" << std::endl;
            return false;
        }

        // Find WHERE column index
        int whereColumnIndex = -1;
        for (size_t i = 0; i < columns.size(); i++) {
            if (columns[i].name == whereColumn) {
                whereColumnIndex = static_cast<int>(i);
                break;
            }
        }

        if (whereColumnIndex == -1) {
            std::cerr << "WHERE column '" << whereColumn << "' not found" << std::endl;
            return false;
        }

        // Check if this is a primary key deletion with B-Tree
        int pkIndex = -1;
        for (size_t i = 0; i < columns.size(); i++) {
            if (columns[i].isPrimaryKey) {
                pkIndex = static_cast<int>(i);
                break;
            }
        }

        // Read page header
        PageHeader pageHdr;
        if (!PageManager::ReadPageHeader(file, rootPage, pageHdr)) {
            std::cerr << "Failed to read page header" << std::endl;
            return false;
        }

        // Read all rows and mark deletions by setting rowSize to 0
        std::vector<uint64_t> rowOffsets;
        std::vector<uint16_t> rowSizes;
        std::vector<bool> shouldDelete;
        std::vector<std::string> deletedPKValues; // Track PKs to remove from B-Tree
        uint64_t currentOffset = rootPage + sizeof(PageHeader);
        int rowsDeleted = 0;

        // First pass: Read rows and determine which to delete
        for (uint32_t row = 0; row < pageHdr.rowCount; row++) {
            uint64_t rowStartOffset = currentOffset;

            // Read row length
            file.seekg(currentOffset);
            uint16_t rowSize;
            file.read(reinterpret_cast<char*>(&rowSize), sizeof(uint16_t));
            currentOffset += sizeof(uint16_t);

            // Read row data
            std::vector<uint8_t> rowData(rowSize);
            file.read(reinterpret_cast<char*>(rowData.data()), rowSize);

            // Deserialize row
            std::vector<std::string> rowValues = RowSerializer::DeserializeRow(rowData, columns);
            if (rowValues.empty()) {
                std::cerr << "Failed to deserialize row" << std::endl;
                currentOffset += rowSize;
                rowOffsets.push_back(rowStartOffset);
                rowSizes.push_back(rowSize);
                shouldDelete.push_back(false);
                continue;
            }

            // Check WHERE condition using WhereClauseEvaluator
            bool matches = WhereClauseEvaluator::Evaluate(rowValues, columns, whereColumn, whereValue, "=");

            rowOffsets.push_back(rowStartOffset);
            rowSizes.push_back(rowSize);
            shouldDelete.push_back(matches);

            if (matches) {
                rowsDeleted++;
                // If deleting by PK, track the PK value for B-Tree removal
                if (pkIndex >= 0 && static_cast<size_t>(pkIndex) < rowValues.size()) {
                    deletedPKValues.push_back(rowValues[pkIndex]);
                }
            }

            currentOffset += rowSize;
        }

        if (rowsDeleted == 0) {
            std::cout << "No rows matched WHERE condition" << std::endl;
            return false;
        }

        // Remove deleted rows from B-Tree index (if primary key exists)
        if (pkIndex >= 0 && table->GetPrimaryKeyIndexRoot() != 0 && !deletedPKValues.empty()) {
            BTree pkBTree(file, header, table->GetPrimaryKeyIndexRoot());
            for (const auto& pkValue : deletedPKValues) {
                if (!pkBTree.Delete(pkValue)) {
                    std::cerr << "Warning: Failed to delete PK '" << pkValue << "' from B-Tree index" << std::endl;
                }
            }
        }

        // Second pass: Compact the page by removing deleted rows
        uint64_t writeOffset = rootPage + sizeof(PageHeader);
        uint32_t newRowCount = 0;

        for (size_t i = 0; i < shouldDelete.size(); i++) {
            if (!shouldDelete[i]) {
                // Keep this row - may need to move it
                uint64_t readOffset = rowOffsets[i] + sizeof(uint16_t);
                uint16_t rowSize = rowSizes[i];

                // If row has moved, rewrite it
                if (writeOffset != rowOffsets[i]) {
                    // Read row data
                    file.seekg(readOffset);
                    std::vector<uint8_t> rowData(rowSize);
                    file.read(reinterpret_cast<char*>(rowData.data()), rowSize);

                    // Write to new position
                    file.seekp(writeOffset);
                    file.write(reinterpret_cast<const char*>(&rowSize), sizeof(uint16_t));
                    file.write(reinterpret_cast<const char*>(rowData.data()), rowSize);
                }

                writeOffset += sizeof(uint16_t) + rowSize;
                newRowCount++;
            }
        }

        // Update page header with new row count and free offset
        pageHdr.rowCount = newRowCount;
        pageHdr.freeOffset = static_cast<uint16_t>(writeOffset - rootPage);

        if (!PageManager::WritePageHeader(file, rootPage, pageHdr)) {
            std::cerr << "Failed to update page header" << std::endl;
            return false;
        }

        file.flush();

        std::cout << "Deleted " << rowsDeleted << " row(s) from table '" << tableName << "'" << std::endl;
        return true;
    }

    std::string Database::DumpFileHeader() {
        if (!isOpen) {
            return "Error: Database not open";
        }

        return DiagnosticDumper::DumpFileHeader(header);
    }

    std::string Database::DumpTableDir() {
        if (!isOpen) {
            return "Error: Database not open";
        }

        return DiagnosticDumper::DumpTableDir(file, header);
    }

    std::string Database::DumpSchema() {
        if (!isOpen) {
            return "Error: Database not open";
        }

        return DiagnosticDumper::DumpSchema(file, header);
    }

    std::string Database::DumpPageRegion() {
        if (!isOpen) {
            return "Error: Database not open";
        }

        return DiagnosticDumper::DumpPageRegion(file, header);
    }

    std::string Database::DumpTablesLoaded() {
        if (!isOpen) {
            return "Error: Database not open";
        }

        return DiagnosticDumper::DumpTablesLoaded(tables);
    }

    std::string Database::DumpTable(const std::string& tableName) {
        if (!isOpen) {
            return "Error: Database not open";
        }

        return DiagnosticDumper::DumpTable(file, header, tables, tableName);
    }

} // namespace CQL