#pragma once
#include <cstdint>
#include <cstring>

namespace CQL {

    // Column type enumeration (maps to ColumnDef.type)
    enum class ColumnType : uint8_t {
        // Integer types
        TINYINT = 0,      // 1 byte (0-255)
        SMALLINT = 1,     // 2 bytes (-32,768 to 32,767)
        INT = 2,          // 4 bytes
        BIGINT = 3,       // 8 bytes

        // String types (size stored in ColumnDef.size)
        CHAR = 10,        // Fixed-length, size required
        VARCHAR = 11,     // Variable-length, size required
        NCHAR = 12,       // Fixed-length Unicode, size required
        NVARCHAR = 13,    // Variable-length Unicode, size required
        TEXT = 14,        // Large text (no size limit)

        // Date/Time types
        DATETIME = 20,    // 8 bytes
        DATETIME2 = 21,   // High precision datetime
        DATE = 22,        // Date only
        TIME = 23,        // Time only

        // Other types
        BOOL = 30,        // Boolean/Bit
        FLOAT = 31,       // 8-byte floating point
        REAL = 32         // 4-byte floating point
    };

    // On-disk structures - must be packed for exact layout
    #pragma pack(push, 1)

    struct FileHeader {
        char magic[8];              // "CQLDB\0\0\0"
        uint32_t version;           // v0 = 1
        uint32_t pageSize;          // 4096 recommended
        uint32_t tableCount;
        uint64_t tableDirOffset;
        uint64_t schemaOffset;
        uint64_t pageRegionOffset;
        uint8_t reserved[212];      // Fixed: 256 - 44 = 212 bytes

        FileHeader() {
            memset(this, 0, sizeof(FileHeader));
            std::strncpy(magic, "CQLDB", sizeof(magic) - 1);
            magic[sizeof(magic) - 1] = '\0';
            version = 1;
            pageSize = 4096;
        }
    };
    static_assert(sizeof(FileHeader) == 256, "FileHeader must be 256 bytes");

    struct TableEntry {
        char name[48];
        uint64_t schemaOffset;
        uint64_t rootPage;
        uint32_t flags;
        uint8_t reserved[60];       // Fixed: 128 - 68 = 60 bytes

        TableEntry() {
            memset(this, 0, sizeof(TableEntry));
        }
    };
    static_assert(sizeof(TableEntry) == 128, "TableEntry must be 128 bytes");

    struct ColumnDef {
        char name[48];
        uint8_t type;               // ColumnType enum value
        uint8_t isPrimaryKey;
        uint16_t size;              // For CHAR, VARCHAR, NCHAR, NVARCHAR (0 = not applicable)
        uint8_t reserved[12];       // Adjusted: 64 - 52 = 12 bytes

        ColumnDef() {
            memset(this, 0, sizeof(ColumnDef));
        }
    };
    static_assert(sizeof(ColumnDef) == 64, "ColumnDef must be 64 bytes");

    struct PageHeader {
        uint32_t pageId;
        uint32_t tableId;
        uint32_t rowCount;
        uint32_t freeOffset;
        uint8_t flags;
        uint8_t reserved[15];       // Correct: 32 - 17 = 15 bytes

        PageHeader() {
            memset(this, 0, sizeof(PageHeader));
        }
    };
    static_assert(sizeof(PageHeader) == 32, "PageHeader must be 32 bytes");

    #pragma pack(pop)

} // namespace CQL