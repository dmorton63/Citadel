#include "BTree.h"
#include "FileHeader.h"
#include "PageManager.h"
#include <algorithm>
#include <iostream>
#include <cstring>

namespace CQL {

    // ========================================
    // Constructor / Destructor
    // ========================================

    BTree::BTree(std::fstream& dbFile, FileHeader& hdr, uint64_t rootPageNum)
        : file(dbFile), header(hdr), rootPage(rootPageNum) {

        // If no root page provided, create a new tree
        if (rootPage == 0) {
            rootPage = AllocateNode();

            // Initialize empty root (leaf node)
            BTreeNode root;
            root.isLeaf = true;
            root.pageNum = rootPage;
            root.parentPage = 0;
            root.nextLeafPage = 0;

            WriteNode(root);
        }
    }

    BTree::~BTree() {
        // Nothing to cleanup (file is managed externally)
    }

    // ========================================
    // Core Operations - INSERT
    // ========================================

    bool BTree::Insert(const std::string& key, uint64_t value, bool unique) {
        // Check if key already exists (for unique indexes)
        if (unique) {
            uint64_t existingValue;
            if (Search(key, existingValue)) {
                std::cerr << "BTree::Insert: Duplicate key '" << key << "'" << std::endl;
                return false;
            }
        }

        // Read root node
        BTreeNode root = ReadNode(rootPage);

        // If root is full, split it
        if (root.IsFull()) {
            // Create new root
            uint64_t newRootPage = AllocateNode();
            BTreeNode newRoot;
            newRoot.isLeaf = false;
            newRoot.pageNum = newRootPage;
            newRoot.parentPage = 0;
            newRoot.children.push_back(rootPage);

            // Update old root's parent
            root.parentPage = newRootPage;
            WriteNode(root);

            // Split old root
            SplitChild(newRoot, 0);

            // Write new root
            WriteNode(newRoot);

            // Update root pointer
            rootPage = newRootPage;
        }

        // Re-read root (may have changed after split)
        root = ReadNode(rootPage);

        // Insert into non-full root
        return InsertNonFull(root, key, value, unique);
    }

    bool BTree::InsertNonFull(BTreeNode& node, const std::string& key, uint64_t value, bool unique) {
        if (node.isLeaf) {
            // Find insertion position
            size_t pos = node.FindInsertPosition(key);

            // Insert the entry
            node.entries.insert(node.entries.begin() + pos, IndexEntry(key, value));

            // Write node back to disk
            WriteNode(node);

            return true;
        } else {
            // Internal node - find which child to descend to
            size_t pos = node.FindInsertPosition(key);

            // Adjust position if we're at the end
            if (pos >= node.children.size()) {
                pos = node.children.size() - 1;
            }

            // Read child node
            BTreeNode child = ReadNode(node.children[pos]);

            // If child is full, split it
            if (child.IsFull()) {
                SplitChild(node, pos);

                // After split, determine which of the two children to descend to
                if (key > node.entries[pos].key) {
                    pos++;
                }

                // Re-read child (may have changed)
                child = ReadNode(node.children[pos]);
            }

            // Recursively insert into child
            return InsertNonFull(child, key, value, unique);
        }
    }

    void BTree::SplitChild(BTreeNode& parent, size_t childIndex) {
        // Read the full child
        BTreeNode fullChild = ReadNode(parent.children[childIndex]);

        // Create new sibling node
        uint64_t newSiblingPage = AllocateNode();
        BTreeNode newSibling;
        newSibling.isLeaf = fullChild.isLeaf;
        newSibling.pageNum = newSiblingPage;
        newSibling.parentPage = parent.pageNum;

        // Find the middle point
        size_t mid = fullChild.entries.size() / 2;

        // Move the upper half of entries to new sibling
        newSibling.entries.assign(
            fullChild.entries.begin() + mid + 1,
            fullChild.entries.end()
        );

        // If internal node, also move children
        if (!fullChild.isLeaf) {
            newSibling.children.assign(
                fullChild.children.begin() + mid + 1,
                fullChild.children.end()
            );
        } else {
            // For leaf nodes, link them together
            newSibling.nextLeafPage = fullChild.nextLeafPage;
            fullChild.nextLeafPage = newSiblingPage;
        }

        // The middle entry moves up to parent
        IndexEntry middleEntry = fullChild.entries[mid];

        // Truncate the full child
        fullChild.entries.resize(mid);
        if (!fullChild.isLeaf) {
            fullChild.children.resize(mid + 1);
        }

        // Insert middle entry into parent
        parent.entries.insert(
            parent.entries.begin() + childIndex,
            middleEntry
        );

        // Insert new sibling pointer into parent
        parent.children.insert(
            parent.children.begin() + childIndex + 1,
            newSiblingPage
        );

        // Write all modified nodes to disk
        WriteNode(fullChild);
        WriteNode(newSibling);
        WriteNode(parent);
    }

    // ========================================
    // Core Operations - SEARCH
    // ========================================

    bool BTree::Search(const std::string& key, uint64_t& value) const {
        BTreeNode node = ReadNode(rootPage);

        while (true) {
            // Search for key in current node
            int keyIndex = node.FindKey(key);

            if (keyIndex >= 0) {
                // Key found!
                value = node.entries[keyIndex].value;
                return true;
            }

            // Key not in this node
            if (node.isLeaf) {
                // Reached a leaf without finding the key
                return false;
            }

            // Internal node - find which child to descend to
            size_t pos = node.FindInsertPosition(key);

            // Adjust position if we're at the end
            if (pos >= node.children.size()) {
                pos = node.children.size() - 1;
            }

            // Read child and continue search
            node = ReadNode(node.children[pos]);
        }
    }

    std::vector<IndexEntry> BTree::SearchRange(const std::string& minKey, const std::string& maxKey) const {
        std::vector<IndexEntry> results;

        // Find the leaf node containing minKey
        BTreeNode leaf = FindLeaf(minKey);

        // Traverse leaf nodes via linked list
        while (leaf.pageNum != 0) {
            for (const auto& entry : leaf.entries) {
                if (entry.key >= minKey && entry.key <= maxKey) {
                    results.push_back(entry);
                } else if (entry.key > maxKey) {
                    // Past the range, we're done
                    return results;
                }
            }

            // Move to next leaf
            if (leaf.nextLeafPage == 0) {
                break;
            }
            leaf = ReadNode(leaf.nextLeafPage);
        }

        return results;
    }

    BTreeNode BTree::FindLeaf(const std::string& key) const {
        BTreeNode node = ReadNode(rootPage);

        while (!node.isLeaf) {
            size_t pos = node.FindInsertPosition(key);

            if (pos >= node.children.size()) {
                pos = node.children.size() - 1;
            }

            node = ReadNode(node.children[pos]);
        }

        return node;
    }

    // ========================================
    // Core Operations - DELETE
    // ========================================

    bool BTree::Delete(const std::string& key) {
        BTreeNode root = ReadNode(rootPage);

        bool deleted = DeleteFromNode(root, key);

        // If root is now empty and has children, make first child the new root
        if (!root.isLeaf && root.entries.empty() && !root.children.empty()) {
            rootPage = root.children[0];

            // Update new root's parent pointer
            BTreeNode newRoot = ReadNode(rootPage);
            newRoot.parentPage = 0;
            WriteNode(newRoot);
        }

        return deleted;
    }

    bool BTree::DeleteFromNode(BTreeNode& node, const std::string& key) {
        int keyIndex = node.FindKey(key);

        if (keyIndex >= 0) {
            // Key found in this node
            if (node.isLeaf) {
                // Simple case: just remove from leaf
                node.entries.erase(node.entries.begin() + keyIndex);
                WriteNode(node);
                return true;
            } else {
                // Internal node deletion (complex - simplified for now)
                // TODO: Implement proper internal node deletion with successor/predecessor
                std::cerr << "BTree::Delete: Internal node deletion not fully implemented" << std::endl;
                return false;
            }
        } else {
            // Key not in this node
            if (node.isLeaf) {
                // Key doesn't exist
                return false;
            }

            // Descend to appropriate child
            size_t pos = node.FindInsertPosition(key);

            if (pos >= node.children.size()) {
                pos = node.children.size() - 1;
            }

            BTreeNode child = ReadNode(node.children[pos]);
            return DeleteFromNode(child, key);
        }
    }

    // Placeholder implementations for merge/borrow (complex operations)
    void BTree::MergeNodes(BTreeNode& parent, size_t leftIndex) {
        // TODO: Implement node merging for underflow handling
        std::cerr << "BTree::MergeNodes: Not yet implemented" << std::endl;
    }

    bool BTree::BorrowFromSibling(BTreeNode& parent, size_t childIndex) {
        // TODO: Implement key borrowing for underflow handling
        std::cerr << "BTree::BorrowFromSibling: Not yet implemented" << std::endl;
        return false;
    }

    // ========================================
    // Continued in BTree.cpp (Part 2)...
    // ========================================

    // ========================================
    // Node I/O Operations
    // ========================================

    uint64_t BTree::AllocateNode() {
        // Use PageManager to allocate a new page
        // Note: We use tableId = 0xFFFFFFFF to indicate index pages
        return PageManager::AllocatePage(file, header, 0xFFFFFFFF);
    }

    BTreeNode BTree::ReadNode(uint64_t pageNum) const {
        if (pageNum == 0) {
            std::cerr << "BTree::ReadNode: Invalid page number 0" << std::endl;
            return BTreeNode();
        }

        // Read the page data
        file.seekg(pageNum);

        std::vector<uint8_t> pageData(header.pageSize);
        file.read(reinterpret_cast<char*>(pageData.data()), header.pageSize);

        if (!file.good()) {
            std::cerr << "BTree::ReadNode: Failed to read page " << pageNum << std::endl;
            return BTreeNode();
        }

        return DeserializeNode(pageData, pageNum);
    }

    void BTree::WriteNode(const BTreeNode& node) {
        if (node.pageNum == 0) {
            std::cerr << "BTree::WriteNode: Invalid page number 0" << std::endl;
            return;
        }

        // Serialize the node
        std::vector<uint8_t> pageData = SerializeNode(node);

        // Pad to page size
        if (pageData.size() < header.pageSize) {
            pageData.resize(header.pageSize, 0);
        }

        // Write to disk
        file.seekp(node.pageNum);
        file.write(reinterpret_cast<const char*>(pageData.data()), header.pageSize);
        file.flush();

        if (!file.good()) {
            std::cerr << "BTree::WriteNode: Failed to write page " << node.pageNum << std::endl;
        }
    }

    std::vector<uint8_t> BTree::SerializeNode(const BTreeNode& node) const {
        std::vector<uint8_t> data;

        // Write IndexPageHeader
        IndexPageHeader hdr;
        hdr.isLeaf = node.isLeaf ? 1 : 0;
        hdr.keyCount = static_cast<uint16_t>(node.entries.size());
        hdr.parentPage = node.parentPage;
        hdr.nextLeafPage = node.nextLeafPage;

        const uint8_t* hdrBytes = reinterpret_cast<const uint8_t*>(&hdr);
        data.insert(data.end(), hdrBytes, hdrBytes + sizeof(IndexPageHeader));

        // Write entries
        for (const auto& entry : node.entries) {
            // Write key length
            uint16_t keyLen = static_cast<uint16_t>(entry.key.length());
            const uint8_t* lenBytes = reinterpret_cast<const uint8_t*>(&keyLen);
            data.insert(data.end(), lenBytes, lenBytes + sizeof(uint16_t));

            // Write key data
            data.insert(data.end(), entry.key.begin(), entry.key.end());

            // Write value
            const uint8_t* valBytes = reinterpret_cast<const uint8_t*>(&entry.value);
            data.insert(data.end(), valBytes, valBytes + sizeof(uint64_t));
        }

        // Write children (for internal nodes)
        if (!node.isLeaf) {
            for (uint64_t childPage : node.children) {
                const uint8_t* childBytes = reinterpret_cast<const uint8_t*>(&childPage);
                data.insert(data.end(), childBytes, childBytes + sizeof(uint64_t));
            }
        }

        return data;
    }

    BTreeNode BTree::DeserializeNode(const std::vector<uint8_t>& data, uint64_t pageNum) const {
        BTreeNode node;
        node.pageNum = pageNum;

        size_t offset = 0;

        // Read IndexPageHeader
        if (data.size() < sizeof(IndexPageHeader)) {
            std::cerr << "BTree::DeserializeNode: Data too small for header" << std::endl;
            return node;
        }

        IndexPageHeader hdr;
        std::memcpy(&hdr, data.data() + offset, sizeof(IndexPageHeader));
        offset += sizeof(IndexPageHeader);

        node.isLeaf = (hdr.isLeaf != 0);
        node.parentPage = hdr.parentPage;
        node.nextLeafPage = hdr.nextLeafPage;

        // Read entries
        for (uint16_t i = 0; i < hdr.keyCount; i++) {
            if (offset + sizeof(uint16_t) > data.size()) {
                std::cerr << "BTree::DeserializeNode: Incomplete entry data" << std::endl;
                break;
            }

            // Read key length
            uint16_t keyLen;
            std::memcpy(&keyLen, data.data() + offset, sizeof(uint16_t));
            offset += sizeof(uint16_t);

            if (offset + keyLen + sizeof(uint64_t) > data.size()) {
                std::cerr << "BTree::DeserializeNode: Incomplete key/value data" << std::endl;
                break;
            }

            // Read key
            std::string key(reinterpret_cast<const char*>(data.data() + offset), keyLen);
            offset += keyLen;

            // Read value
            uint64_t value;
            std::memcpy(&value, data.data() + offset, sizeof(uint64_t));
            offset += sizeof(uint64_t);

            node.entries.emplace_back(key, value);
        }

        // Read children (for internal nodes)
        if (!node.isLeaf) {
            size_t childCount = hdr.keyCount + 1;
            for (size_t i = 0; i < childCount; i++) {
                if (offset + sizeof(uint64_t) > data.size()) {
                    std::cerr << "BTree::DeserializeNode: Incomplete children data" << std::endl;
                    break;
                }

                uint64_t childPage;
                std::memcpy(&childPage, data.data() + offset, sizeof(uint64_t));
                offset += sizeof(uint64_t);

                node.children.push_back(childPage);
            }
        }

        return node;
    }

    // ========================================
    // Statistics & Validation
    // ========================================

    BTree::TreeStats BTree::GetStatistics() const {
        TreeStats stats;
        stats.nodeCount = 0;
        stats.leafCount = 0;
        stats.height = CalculateHeight(rootPage);
        stats.totalKeys = CountKeys(rootPage);

        return stats;
    }

    uint32_t BTree::CalculateHeight(uint64_t nodePageNum) const {
        if (nodePageNum == 0) {
            return 0;
        }

        BTreeNode node = ReadNode(nodePageNum);

        if (node.isLeaf) {
            return 1;
        }

        if (node.children.empty()) {
            return 1;
        }

        return 1 + CalculateHeight(node.children[0]);
    }

    uint64_t BTree::CountKeys(uint64_t nodePageNum) const {
        if (nodePageNum == 0) {
            return 0;
        }

        BTreeNode node = ReadNode(nodePageNum);
        uint64_t count = node.entries.size();

        if (!node.isLeaf) {
            for (uint64_t childPage : node.children) {
                count += CountKeys(childPage);
            }
        }

        return count;
    }

    bool BTree::Validate() const {
        return ValidateSubtree(rootPage, "", "");
    }

    bool BTree::ValidateSubtree(uint64_t nodePageNum, const std::string& minKey, const std::string& maxKey) const {
        if (nodePageNum == 0) {
            return true;
        }

        BTreeNode node = ReadNode(nodePageNum);

        // Check key ordering within node
        for (size_t i = 1; i < node.entries.size(); i++) {
            if (node.entries[i - 1].key >= node.entries[i].key) {
                std::cerr << "BTree::Validate: Keys out of order in node " << nodePageNum << std::endl;
                return false;
            }
        }

        // Check key bounds
        if (!minKey.empty() && !node.entries.empty() && node.entries.front().key < minKey) {
            std::cerr << "BTree::Validate: Key below minimum in node " << nodePageNum << std::endl;
            return false;
        }
        if (!maxKey.empty() && !node.entries.empty() && node.entries.back().key > maxKey) {
            std::cerr << "BTree::Validate: Key above maximum in node " << nodePageNum << std::endl;
            return false;
        }

        // Recursively validate children
        if (!node.isLeaf) {
            for (size_t i = 0; i < node.children.size(); i++) {
                std::string childMin = (i == 0) ? minKey : node.entries[i - 1].key;
                std::string childMax = (i < node.entries.size()) ? node.entries[i].key : maxKey;

                if (!ValidateSubtree(node.children[i], childMin, childMax)) {
                    return false;
                }
            }
        }

        return true;
    }

} // namespace CQL

