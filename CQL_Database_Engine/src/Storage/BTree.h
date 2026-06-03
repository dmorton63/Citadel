#pragma once

#include "IndexPage.h"
#include <fstream>
#include <vector>
#include <string>
#include <memory>

namespace CQL {

    // Forward declarations
    struct FileHeader;

    // ========================================
    // B-Tree Node (In-Memory Representation)
    // ========================================
    struct BTreeNode {
        bool isLeaf;
        uint64_t pageNum;                           // Page number of this node
        uint64_t parentPage;                        // Parent node page number
        uint64_t nextLeafPage;                      // For leaf nodes: linked list
        std::vector<IndexEntry> entries;            // Keys and values
        std::vector<uint64_t> children;             // Child page numbers (for internal nodes)

        BTreeNode() : isLeaf(true), pageNum(0), parentPage(0), nextLeafPage(0) {}

        // Check if node is full
        bool IsFull() const {
            return entries.size() >= BTreeConfig::ORDER;
        }

        // Check if node has minimum keys (for deletions)
        bool HasMinimumKeys() const {
            return entries.size() >= BTreeConfig::MIN_KEYS;
        }

        // Find the index where a key should be inserted
        size_t FindInsertPosition(const std::string& key) const {
            size_t pos = 0;
            while (pos < entries.size() && entries[pos].key < key) {
                pos++;
            }
            return pos;
        }

        // Check if key exists in this node
        int FindKey(const std::string& key) const {
            for (size_t i = 0; i < entries.size(); i++) {
                if (entries[i].key == key) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }
    };

    // ========================================
    // B-Tree Index Manager
    // Manages a single B-Tree index (primary key or secondary index)
    // ========================================
    class BTree {
    public:
        // Constructor: Create new B-Tree or open existing one
        BTree(std::fstream& dbFile, FileHeader& header, uint64_t rootPageNum = 0);

        // Destructor
        ~BTree();

        // ========================================
        // Core Operations
        // ========================================

        // Insert a key-value pair into the index
        // Returns true on success, false if key already exists (for unique indexes)
        bool Insert(const std::string& key, uint64_t value, bool unique = false);

        // Search for a key and return its value
        // Returns true if found, false otherwise
        bool Search(const std::string& key, uint64_t& value) const;

        // Delete a key from the index
        // Returns true on success, false if key not found
        bool Delete(const std::string& key);

        // Range search: find all keys between minKey and maxKey (inclusive)
        std::vector<IndexEntry> SearchRange(const std::string& minKey, const std::string& maxKey) const;

        // ========================================
        // Metadata
        // ========================================

        // Get root page number
        uint64_t GetRootPage() const { return rootPage; }

        // Get tree statistics (for debugging/monitoring)
        struct TreeStats {
            uint64_t nodeCount;
            uint64_t leafCount;
            uint32_t height;
            uint64_t totalKeys;
        };
        TreeStats GetStatistics() const;

        // Validate tree structure (for testing)
        bool Validate() const;

    private:
        std::fstream& file;
        FileHeader& header;
        uint64_t rootPage;

        // ========================================
        // Node I/O Operations
        // ========================================

        // Allocate a new page for a B-Tree node
        uint64_t AllocateNode();

        // Read a node from disk
        BTreeNode ReadNode(uint64_t pageNum) const;

        // Write a node to disk
        void WriteNode(const BTreeNode& node);

        // Serialize node to byte array
        std::vector<uint8_t> SerializeNode(const BTreeNode& node) const;

        // Deserialize byte array to node
        BTreeNode DeserializeNode(const std::vector<uint8_t>& data, uint64_t pageNum) const;

        // ========================================
        // B-Tree Internal Operations
        // ========================================

        // Insert into a non-full node
        bool InsertNonFull(BTreeNode& node, const std::string& key, uint64_t value, bool unique);

        // Split a full child node
        void SplitChild(BTreeNode& parent, size_t childIndex);

        // Delete from a node
        bool DeleteFromNode(BTreeNode& node, const std::string& key);

        // Merge two nodes (for underflow during deletion)
        void MergeNodes(BTreeNode& parent, size_t leftIndex);

        // Borrow a key from sibling (for underflow during deletion)
        bool BorrowFromSibling(BTreeNode& parent, size_t childIndex);

        // ========================================
        // Helper Functions
        // ========================================

        // Find leaf node that should contain the key
        BTreeNode FindLeaf(const std::string& key) const;

        // Calculate tree height (for statistics)
        uint32_t CalculateHeight(uint64_t nodePageNum) const;

        // Count total keys (for statistics)
        uint64_t CountKeys(uint64_t nodePageNum) const;

        // Validate subtree (recursive)
        bool ValidateSubtree(uint64_t nodePageNum, const std::string& minKey, const std::string& maxKey) const;
    };

} // namespace CQL
