#pragma once

#include "Table.h"
#include <vector>
#include <string>
#include <cstdint>

namespace CQL {

    class RowSerializer {
    public:
        // Serialize a row of string values into binary format based on column types
        // Returns empty vector on error (with error message written to cerr)
        static std::vector<uint8_t> SerializeRow(
            const std::vector<std::string>& values,
            const std::vector<Column>& columns);

        // Deserialize binary row data into string values based on column types
        static std::vector<std::string> DeserializeRow(
            const std::vector<uint8_t>& rowData,
            const std::vector<Column>& columns);

    private:
        // Format a single value from binary to string
        static std::string FormatValue(const uint8_t* data, size_t& offset, const Column& col);
    };

} // namespace CQL
