#include "PageManager.h"
#include <iostream>
#include <cstring>

namespace CQL {

    uint64_t PageManager::AllocatePage(std::fstream& file, FileHeader& header, uint32_t tableId) {
        // Calculate next page offset
        // For now, just append to the end of the file
        file.seekp(0, std::ios::end);
        uint64_t fileSize = file.tellp();

        // Round up to next page boundary
        uint64_t pageOffset = ((fileSize + header.pageSize - 1) / header.pageSize) * header.pageSize;

        // Ensure it's at least at pageRegionOffset
        if (pageOffset < header.pageRegionOffset) {
            pageOffset = header.pageRegionOffset;
        }

        // Initialize page header
        PageHeader pageHdr;
        memset(&pageHdr, 0, sizeof(PageHeader));
        pageHdr.pageId = static_cast<uint32_t>(pageOffset / header.pageSize);
        pageHdr.tableId = tableId;
        pageHdr.rowCount = 0;
        pageHdr.freeOffset = sizeof(PageHeader);
        pageHdr.flags = 0;

        // Write page header
        if (!WritePageHeader(file, pageOffset, pageHdr)) {
            std::cerr << "Failed to write new page header" << std::endl;
            return 0;
        }

        std::cout << "Allocated new page at offset " << pageOffset 
                  << " (pageId " << pageHdr.pageId << ")" << std::endl;

        return pageOffset;
    }

    bool PageManager::WritePageHeader(std::fstream& file, uint64_t pageOffset, const PageHeader& pageHeader) {
        file.seekp(pageOffset);
        file.write(reinterpret_cast<const char*>(&pageHeader), sizeof(PageHeader));
        return file.good();
    }

    bool PageManager::ReadPageHeader(std::fstream& file, uint64_t pageOffset, PageHeader& pageHeader) {
        file.seekg(pageOffset);
        file.read(reinterpret_cast<char*>(&pageHeader), sizeof(PageHeader));
        return file.good();
    }

} // namespace CQL
