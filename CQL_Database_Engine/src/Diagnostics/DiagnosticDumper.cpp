#include "DiagnosticDumper.h"
#include "RowSerializer.h"
#include "PageManager.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace CQL {

    std::string DiagnosticDumper::DumpFileHeader(const FileHeader& header) {
        std::ostringstream ss;
        ss << "=== FILE HEADER DUMP ===\n\n";
        ss << "Magic:            " << std::string(header.magic, 5) << "\n";
        ss << "Version:          " << header.version << "\n";
        ss << "Page Size:        " << header.pageSize << " bytes\n";
        ss << "Table Count:      " << header.tableCount << "\n";
        ss << "Table Dir Offset: 0x" << std::hex << std::uppercase << header.tableDirOffset 
           << " (" << std::dec << header.tableDirOffset << ")\n";
        ss << "Schema Offset:    0x" << std::hex << std::uppercase << header.schemaOffset 
           << " (" << std::dec << header.schemaOffset << ")\n";
        ss << "Page Region Off:  0x" << std::hex << std::uppercase << header.pageRegionOffset 
           << " (" << std::dec << header.pageRegionOffset << ")\n";

        return ss.str();
    }

    std::string DiagnosticDumper::DumpTableDir(std::fstream& file, const FileHeader& header) {
        std::ostringstream ss;
        ss << "=== TABLE DIRECTORY DUMP ===\n\n";
        ss << "Total entries in header: " << header.tableCount << "\n\n";

        file.clear();
        file.seekg(header.tableDirOffset);

        for (uint32_t i = 0; i < header.tableCount; i++) {
            TableEntry entry;
            file.read(reinterpret_cast<char*>(&entry), sizeof(TableEntry));
            if (!file.good()) {
                ss << "Error: Failed to read table entry " << i << "\n";
                break;
            }

            ss << "Entry #" << i << ":\n";

            // Check if deleted
            if (entry.name[0] == '\0') {
                ss << "  Status:        [DELETED]\n";
                ss << "  Name:          (null)\n";
            } else {
                char name[49] = {0};
                memcpy(name, entry.name, 48);
                name[48] = '\0';
                ss << "  Status:        ACTIVE\n";
                ss << "  Name:          " << name << "\n";
            }

            ss << "  Schema Offset: 0x" << std::hex << std::uppercase << entry.schemaOffset 
               << " (" << std::dec << entry.schemaOffset << ")\n";
            ss << "  Root Page:     0x" << std::hex << std::uppercase << entry.rootPage 
               << " (" << std::dec << entry.rootPage << ")\n";
            ss << "  Flags:         0x" << std::hex << std::uppercase << entry.flags << "\n\n";
        }

        return ss.str();
    }

    std::string DiagnosticDumper::DumpSchema(std::fstream& file, const FileHeader& header) {
        std::ostringstream ss;
        ss << "=== SCHEMA REGION DUMP ===\n\n";
        ss << "Schema starts at: 0x" << std::hex << std::uppercase << 2304 << " (" << std::dec << 2304 << ")\n";
        ss << "Schema next at:   0x" << std::hex << std::uppercase << header.schemaOffset 
           << " (" << std::dec << header.schemaOffset << ")\n";
        ss << "Total bytes used: " << (header.schemaOffset - 2304) << "\n";
        ss << "ColumnDef size:   " << sizeof(ColumnDef) << " bytes\n";
        ss << "Max columns:      " << (header.schemaOffset - 2304) / sizeof(ColumnDef) << "\n\n";

        file.clear();
        file.seekg(2304); // Schema starts at 2304

        uint64_t currentOffset = 2304;
        uint32_t colIndex = 0;

        while (currentOffset < header.schemaOffset) {
            ColumnDef colDef;
            file.read(reinterpret_cast<char*>(&colDef), sizeof(ColumnDef));
            if (!file.good()) {
                ss << "Error: Failed to read column definition at offset 0x" 
                   << std::hex << std::uppercase << currentOffset << "\n";
                break;
            }

            char name[49] = {0};
            memcpy(name, colDef.name, 48);
            name[48] = '\0';

            ss << "Column #" << std::dec << colIndex << " @ 0x" << std::hex << std::uppercase << currentOffset << ":\n";
            ss << "  Name:       " << name << "\n";
            ss << "  Type:       " << static_cast<int>(colDef.type) << " (";

            // Map type to name
            switch (static_cast<ColumnType>(colDef.type)) {
                case ColumnType::TINYINT: ss << "TINYINT"; break;
                case ColumnType::SMALLINT: ss << "SMALLINT"; break;
                case ColumnType::INT: ss << "INT"; break;
                case ColumnType::BIGINT: ss << "BIGINT"; break;
                case ColumnType::CHAR: ss << "CHAR"; break;
                case ColumnType::VARCHAR: ss << "VARCHAR"; break;
                case ColumnType::NCHAR: ss << "NCHAR"; break;
                case ColumnType::NVARCHAR: ss << "NVARCHAR"; break;
                case ColumnType::TEXT: ss << "TEXT"; break;
                case ColumnType::DATETIME: ss << "DATETIME"; break;
                case ColumnType::DATETIME2: ss << "DATETIME2"; break;
                case ColumnType::DATE: ss << "DATE"; break;
                case ColumnType::TIME: ss << "TIME"; break;
                case ColumnType::BOOL: ss << "BOOL"; break;
                case ColumnType::FLOAT: ss << "FLOAT"; break;
                case ColumnType::REAL: ss << "REAL"; break;
                default: ss << "UNKNOWN"; break;
            }
            ss << ")\n";

            ss << "  Primary Key: " << (colDef.isPrimaryKey ? "YES" : "NO") << "\n";
            ss << "  Size:       " << std::dec << colDef.size << "\n\n";

            currentOffset += sizeof(ColumnDef);
            colIndex++;
        }

        return ss.str();
    }

    std::string DiagnosticDumper::DumpPageRegion(std::fstream& file, const FileHeader& header) {
        std::ostringstream ss;
        ss << "=== PAGE REGION DUMP ===\n\n";
        ss << "Page region starts at: 0x" << std::hex << std::uppercase << header.pageRegionOffset 
           << " (" << std::dec << header.pageRegionOffset << ")\n";
        ss << "Page size: " << header.pageSize << " bytes\n\n";

        // Get file size
        file.clear();
        file.seekg(0, std::ios::end);
        uint64_t fileSize = file.tellg();

        ss << "File size: " << fileSize << " bytes\n";
        ss << "Pages in file: " << ((fileSize - header.pageRegionOffset) / header.pageSize) << "\n\n";

        uint64_t currentOffset = header.pageRegionOffset;
        uint32_t pageIndex = 0;

        while (currentOffset < fileSize) {
            PageHeader pageHdr;
            file.seekg(currentOffset);
            file.read(reinterpret_cast<char*>(&pageHdr), sizeof(PageHeader));

            if (!file.good()) {
                ss << "Error: Failed to read page header at offset 0x" 
                   << std::hex << std::uppercase << currentOffset << "\n";
                break;
            }

            // Check if page looks initialized
            if (pageHdr.rowCount > 0 || pageHdr.freeOffset > 0) {
                ss << "Page #" << std::dec << pageIndex << " @ 0x" << std::hex << std::uppercase << currentOffset << ":\n";
                ss << "  Page ID:      " << std::dec << pageHdr.pageId << "\n";
                ss << "  Table ID:     " << pageHdr.tableId << "\n";
                ss << "  Row Count:    " << pageHdr.rowCount << "\n";
                ss << "  Free Offset:  " << pageHdr.freeOffset << " (0x" << std::hex << std::uppercase << pageHdr.freeOffset << ")\n";
                ss << "  Flags:        0x" << std::hex << std::uppercase << pageHdr.flags << "\n";
                ss << "  Data region:  " << std::dec << (pageHdr.freeOffset - sizeof(PageHeader)) << " bytes used\n\n";
            }

            currentOffset += header.pageSize;
            pageIndex++;

            // Safety: don't scan more than 100 pages
            if (pageIndex >= 100) {
                ss << "(Stopped after 100 pages)\n";
                break;
            }
        }

        return ss.str();
    }

    std::string DiagnosticDumper::DumpTablesLoaded(const std::vector<std::shared_ptr<Table>>& tables) {
        std::ostringstream ss;
        ss << "=== IN-MEMORY TABLES LOADED ===\n\n";
        ss << "Tables vector size: " << tables.size() << "\n\n";

        for (size_t i = 0; i < tables.size(); i++) {
            const auto& table = tables[i];
            ss << "Table #" << i << ":\n";
            ss << "  Name:         " << table->GetName() << "\n";
            ss << "  Root Page:    0x" << std::hex << std::uppercase << table->GetRootPage()
               << " (" << std::dec << table->GetRootPage() << ")\n";
            ss << "  Column Count: " << table->GetColumns().size() << "\n";

            if (!table->GetColumns().empty()) {
                ss << "  Columns:\n";
                for (size_t j = 0; j < table->GetColumns().size(); j++) {
                    const auto& col = table->GetColumns()[j];
                    ss << "    " << (j + 1) << ". " << col.name;
                    if (col.isPrimaryKey) {
                        ss << " (PRIMARY KEY)";
                    }
                    ss << "\n";
                }
            }
            ss << "\n";
        }

        return ss.str();
    }

    std::string DiagnosticDumper::DumpTable(std::fstream& file, const FileHeader& header,
                                           const std::vector<std::shared_ptr<Table>>& tables,
                                           const std::string& tableName) {
        // Find table
        std::shared_ptr<Table> table = nullptr;
        for (const auto& t : tables) {
            if (t->GetName() == tableName) {
                table = t;
                break;
            }
        }

        if (!table) {
            return "Error: Table '" + tableName + "' not found";
        }

        std::ostringstream ss;
        ss << "=== TABLE DUMP: " << tableName << " ===\n\n";

        const auto& columns = table->GetColumns();
        uint64_t rootPage = table->GetRootPage();

        ss << "Root Page:     0x" << std::hex << std::uppercase << rootPage 
           << " (" << std::dec << rootPage << ")\n";
        ss << "Column Count:  " << columns.size() << "\n\n";

        ss << "Columns:\n";
        for (size_t i = 0; i < columns.size(); i++) {
            const auto& col = columns[i];
            ss << "  " << (i + 1) << ". " << col.name << " - ";

            switch (col.type) {
                case ColumnType::TINYINT: ss << "TINYINT"; break;
                case ColumnType::SMALLINT: ss << "SMALLINT"; break;
                case ColumnType::INT: ss << "INT"; break;
                case ColumnType::BIGINT: ss << "BIGINT"; break;
                case ColumnType::CHAR: ss << "CHAR(" << col.size << ")"; break;
                case ColumnType::VARCHAR: ss << "VARCHAR(" << col.size << ")"; break;
                case ColumnType::NCHAR: ss << "NCHAR(" << col.size << ")"; break;
                case ColumnType::NVARCHAR: ss << "NVARCHAR(" << col.size << ")"; break;
                case ColumnType::TEXT: ss << "TEXT"; break;
                case ColumnType::DATETIME: ss << "DATETIME"; break;
                case ColumnType::DATETIME2: ss << "DATETIME2"; break;
                case ColumnType::DATE: ss << "DATE"; break;
                case ColumnType::TIME: ss << "TIME"; break;
                case ColumnType::BOOL: ss << "BOOL"; break;
                case ColumnType::FLOAT: ss << "FLOAT"; break;
                case ColumnType::REAL: ss << "REAL"; break;
                default: ss << "UNKNOWN"; break;
            }

            if (col.isPrimaryKey) {
                ss << " PRIMARY KEY";
            }
            ss << "\n";
        }

        if (rootPage == 0) {
            ss << "\n(No data pages allocated)\n";
            return ss.str();
        }

        // Read page data
        PageHeader pageHdr;
        if (!PageManager::ReadPageHeader(file, rootPage, pageHdr)) {
            ss << "\nError: Failed to read page header\n";
            return ss.str();
        }

        ss << "\nPage Header:\n";
        ss << "  Page ID:     " << pageHdr.pageId << "\n";
        ss << "  Table ID:    " << pageHdr.tableId << "\n";
        ss << "  Row Count:   " << pageHdr.rowCount << "\n";
        ss << "  Free Offset: " << pageHdr.freeOffset << "\n";
        ss << "  Flags:       0x" << std::hex << std::uppercase << pageHdr.flags << "\n\n";

        if (pageHdr.rowCount == 0) {
            ss << "(No rows)\n";
            return ss.str();
        }

        ss << "Rows:\n";
        uint64_t currentOffset = rootPage + sizeof(PageHeader);

        for (uint32_t row = 0; row < pageHdr.rowCount; row++) {
            file.seekg(currentOffset);
            uint16_t rowSize;
            file.read(reinterpret_cast<char*>(&rowSize), sizeof(uint16_t));
            currentOffset += sizeof(uint16_t);

            std::vector<uint8_t> rowData(rowSize);
            file.seekg(currentOffset);
            file.read(reinterpret_cast<char*>(rowData.data()), rowSize);
            currentOffset += rowSize;

            std::vector<std::string> rowValues = RowSerializer::DeserializeRow(rowData, columns);

            ss << "  Row " << (row + 1) << ": ";
            for (size_t i = 0; i < rowValues.size(); i++) {
                if (i > 0) ss << ", ";
                ss << columns[i].name << "=" << rowValues[i];
            }
            ss << "\n";
        }

        return ss.str();
    }

} // namespace CQL
