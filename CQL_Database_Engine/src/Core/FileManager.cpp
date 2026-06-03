#include "FileManager.h"
#include <iostream>
#include <cstring>

namespace CQL {

    bool FileManager::WriteHeader(std::fstream& file, const FileHeader& header) {
        file.seekp(0);
        file.write(reinterpret_cast<const char*>(&header), sizeof(FileHeader));
        return file.good();
    }

    bool FileManager::ReadHeader(std::fstream& file, FileHeader& header) {
        file.seekg(0);
        file.read(reinterpret_cast<char*>(&header), sizeof(FileHeader));
        return file.good();
    }

    bool FileManager::LoadTables(std::fstream& file, const FileHeader& header, 
                                 std::vector<std::shared_ptr<Table>>& tables) {
        tables.clear();

        if (header.tableCount == 0) {
            return true; // No tables to load
        }

        // Clear any file stream error flags from previous write operations
        file.clear();

        // Read all table entries from table directory
        for (uint32_t i = 0; i < header.tableCount; i++) {
            // Explicitly seek to each table entry to avoid file position corruption
            file.seekg(header.tableDirOffset + (i * sizeof(TableEntry)));

            TableEntry entry;
            file.read(reinterpret_cast<char*>(&entry), sizeof(TableEntry));
            if (!file.good()) {
                std::cerr << "Failed to read table entry " << i << std::endl;
                return false;
            }

            // Skip deleted entries (name starts with null byte)
            if (entry.name[0] == '\0') {
                std::cout << "Skipping deleted table entry " << i << std::endl;
                continue;
            }

            // Ensure table name is null-terminated
            char tableName[49] = {0};
            memcpy(tableName, entry.name, 48);
            tableName[48] = '\0';

            // Create Table object with the actual on-disk table ID
            auto table = std::make_shared<Table>(tableName, entry.schemaOffset, entry.rootPage, i);

            // Set the primary key index root page
            table->SetPrimaryKeyIndexRoot(entry.primaryKeyIndexRoot);

            // Load columns for this table
            // We need to read until we hit the next table's schema or end of schema region
            file.seekg(entry.schemaOffset);

            // Determine how many columns this table has
            // Find the NEXT table's schema offset (even if deleted!) to know where this table's schema ends
            uint64_t nextSchemaOffset = header.schemaOffset; // Default to end of schema region

            if (i + 1 < header.tableCount) {
                // Read the IMMEDIATE next table entry (don't skip deleted ones!)
                std::streampos savedPos = file.tellg();
                uint64_t nextEntryOffset = header.tableDirOffset + ((i + 1) * sizeof(TableEntry));

                TableEntry nextEntry;
                file.seekg(nextEntryOffset);
                file.read(reinterpret_cast<char*>(&nextEntry), sizeof(TableEntry));

                // Use its schemaOffset regardless of whether it's deleted
                // This tells us where the current table's schema ends
                nextSchemaOffset = nextEntry.schemaOffset;

                std::cout << "DEBUG: Next table entry at index " << (i + 1) 
                          << ", schemaOffset=" << nextSchemaOffset 
                          << (nextEntry.name[0] == '\0' ? " [DELETED]" : "") << std::endl;

                file.seekg(savedPos); // Restore position
            }

            std::cout << "DEBUG: Table '" << tableName << "' - entry.schemaOffset=" << entry.schemaOffset 
                      << ", nextSchemaOffset=" << nextSchemaOffset 
                      << ", header.schemaOffset=" << header.schemaOffset << std::endl;

            // Calculate number of columns
            uint64_t schemaBytes = nextSchemaOffset - entry.schemaOffset;
            uint32_t columnCount = static_cast<uint32_t>(schemaBytes / sizeof(ColumnDef));

            std::cout << "DEBUG: schemaBytes=" << schemaBytes << ", ColumnDef size=" << sizeof(ColumnDef) 
                      << ", calculated columnCount=" << columnCount << std::endl;

            // Read column definitions
            for (uint32_t col = 0; col < columnCount; col++) {
                ColumnDef colDef;
                file.read(reinterpret_cast<char*>(&colDef), sizeof(ColumnDef));
                if (!file.good()) {
                    std::cerr << "Failed to read column " << col << " for table " << entry.name << std::endl;
                    return false;
                }

                // Ensure name is null-terminated
                char colName[49] = {0};
                memcpy(colName, colDef.name, 48);
                colName[48] = '\0';

                table->AddColumn(Column(
                    colName,
                    static_cast<ColumnType>(colDef.type),
                    colDef.isPrimaryKey != 0,
                    colDef.size
                ));
            }

            tables.push_back(table);
            std::cout << "Loaded table '" << entry.name << "' with " << columnCount << " columns" << std::endl;
        }

        return true;
    }

} // namespace CQL
