#pragma once

#include "FileHeader.h"
#include <fstream>
#include <cstdint>

namespace CQL {

    class PageManager {
    public:
        // Allocate a new page for the given table
        // Returns page offset, or 0 on error
        static uint64_t AllocatePage(std::fstream& file, FileHeader& header, uint32_t tableId);

        // Write page header to file at given offset
        static bool WritePageHeader(std::fstream& file, uint64_t pageOffset, const PageHeader& pageHeader);

        // Read page header from file at given offset
        static bool ReadPageHeader(std::fstream& file, uint64_t pageOffset, PageHeader& pageHeader);
    };

} // namespace CQL
