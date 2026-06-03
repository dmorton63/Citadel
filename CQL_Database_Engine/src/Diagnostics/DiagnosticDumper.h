#pragma once

#include "FileHeader.h"
#include "Table.h"
#include <fstream>
#include <vector>
#include <memory>
#include <string>

namespace CQL {

    class DiagnosticDumper {
    public:
        // Dump file header information
        static std::string DumpFileHeader(const FileHeader& header);

        // Dump table directory
        static std::string DumpTableDir(std::fstream& file, const FileHeader& header);

        // Dump schema region
        static std::string DumpSchema(std::fstream& file, const FileHeader& header);

        // Dump page region
        static std::string DumpPageRegion(std::fstream& file, const FileHeader& header);

        // Dump specific table details
        static std::string DumpTable(std::fstream& file, const FileHeader& header,
                                    const std::vector<std::shared_ptr<Table>>& tables,
                                    const std::string& tableName);

        // Dump in-memory table state
        static std::string DumpTablesLoaded(const std::vector<std::shared_ptr<Table>>& tables);
    };

} // namespace CQL
