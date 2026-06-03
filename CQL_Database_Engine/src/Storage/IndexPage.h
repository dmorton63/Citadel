#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace CQL {

    // ========================================
    // Index Page Header
    // Stored at the beginning of each B-Tree node page
    // ========================================
    #pragma pack(push, 1)
    struct IndexPageHeader {
        uint8_t isLeaf;              // 1 if leaf node, 0 if internal node
        uint8_t reserved;            // Padding for alignment
        uint16_t keyCount;           // Number of keys currently in this node
        uint64_t parentPage;         // Page number of parent (0 if root)
        uint64_t nextLeafPage;       // For leaf nodes: link to next leaf (range scans)

        IndexPageHeader() 
            : isLeaf(1), reserved(0), keyCount(0), parentPage(0), nextLeafPage(0) {}
    };
    #pragma pack(pop)

    // ========================================
    // Index Entry
    // Each entry in an index page contains:
    // - Key: the indexed value (e.g., ThemeId = "5")
    // - Value: pointer to data (row offset or page number)
    // ========================================
    struct IndexEntry {
        std::string key;             // Indexed value (supports all column types as strings)
        uint64_t value;              // For leaf: row offset; for internal: child page number

        IndexEntry() : value(0) {}
        IndexEntry(const std::string& k, uint64_t v) : key(k), value(v) {}

        // Comparison operators for sorting
        bool operator<(const IndexEntry& other) const {
            return key < other.key;
        }
        bool operator==(const IndexEntry& other) const {
            return key == other.key;
        }
    };

    // ========================================
    // B-Tree Configuration
    // ========================================
    namespace BTreeConfig {
        // Maximum number of entries per node
        // With 4KB pages and ~100 byte entries, we can fit ~40 entries
        // Using order 32 gives good balance between tree height and node utilization
        constexpr int ORDER = 32;

        // Minimum number of entries (ORDER/2, rounded up)
        constexpr int MIN_KEYS = (ORDER + 1) / 2;

        // Maximum children for internal nodes (ORDER + 1)
        constexpr int MAX_CHILDREN = ORDER + 1;
    };

} // namespace CQL
