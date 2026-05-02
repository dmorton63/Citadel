#pragma once

#include "FileHeader.h"
#include "Table.h"
#include <fstream>
#include <vector>
#include <memory>

namespace CQL {

    class FileManager {
    public:
        // Write file header to beginning of file
        static bool WriteHeader(std::fstream& file, const FileHeader& header);

        // Read file header from beginning of file
        static bool ReadHeader(std::fstream& file, FileHeader& header);

        // Load all table metadata from file
        // Returns true on success, false on error
        static bool LoadTables(std::fstream& file, const FileHeader& header, 
                              std::vector<std::shared_ptr<Table>>& tables);
    };

} // namespace CQL
