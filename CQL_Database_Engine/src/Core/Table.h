#pragma once
#include <string>
#include <vector>
#include "FileHeader.h"

namespace CQL {

    // In-memory representation of a table column
    struct Column {
        std::string name;
        ColumnType type;
        bool isPrimaryKey;
        uint16_t size;

        Column(const std::string& n, ColumnType t, bool pk, uint16_t sz)
            : name(n), type(t), isPrimaryKey(pk), size(sz) {}
    };

    // In-memory representation of a table
    class Table {
    public:
        Table(const std::string& name, uint64_t schemaOffset, uint64_t rootPage, uint32_t tableId = 0);

        const std::string& GetName() const { return name; }
        uint64_t GetSchemaOffset() const { return schemaOffset; }
        uint64_t GetRootPage() const { return rootPage; }
        uint64_t GetPrimaryKeyIndexRoot() const { return primaryKeyIndexRoot; }
        uint32_t GetTableId() const { return tableId; }
        const std::vector<Column>& GetColumns() const { return columns; }

        void SetRootPage(uint64_t page) { rootPage = page; }
        void SetPrimaryKeyIndexRoot(uint64_t page) { primaryKeyIndexRoot = page; }
        void AddColumn(const Column& col);
        const Column* GetPrimaryKeyColumn() const;

        // Get type name as string for display
        static std::string GetTypeName(ColumnType type, uint16_t size);

    private:
        std::string name;
        uint64_t schemaOffset;
        uint64_t rootPage;
        uint64_t primaryKeyIndexRoot;  // B-Tree root page for primary key index
        uint32_t tableId;  // On-disk TableEntry index
        std::vector<Column> columns;
    };

} // namespace CQL
