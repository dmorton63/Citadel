#include "Table.h"
#include <sstream>

namespace CQL {

    Table::Table(const std::string& name, uint64_t schemaOffset, uint64_t rootPage, uint32_t tableId)
        : name(name), schemaOffset(schemaOffset), rootPage(rootPage), primaryKeyIndexRoot(0), tableId(tableId) {}

    void Table::AddColumn(const Column& col) {
        columns.push_back(col);
    }

    const Column* Table::GetPrimaryKeyColumn() const {
        for (const auto& col : columns) {
            if (col.isPrimaryKey) {
                return &col;
            }
        }
        return nullptr;
    }

    std::string Table::GetTypeName(ColumnType type, uint16_t size) {
        std::ostringstream oss;
        
        switch (type) {
            case ColumnType::TINYINT:   return "TINYINT";
            case ColumnType::SMALLINT:  return "SMALLINT";
            case ColumnType::INT:       return "INT";
            case ColumnType::BIGINT:    return "BIGINT";
            
            case ColumnType::CHAR:
                oss << "CHAR(" << size << ")";
                return oss.str();
            case ColumnType::VARCHAR:
                oss << "VARCHAR(" << size << ")";
                return oss.str();
            case ColumnType::NCHAR:
                oss << "NCHAR(" << size << ")";
                return oss.str();
            case ColumnType::NVARCHAR:
                oss << "NVARCHAR(" << size << ")";
                return oss.str();
            case ColumnType::TEXT:      return "TEXT";
            
            case ColumnType::DATETIME:  return "DATETIME";
            case ColumnType::DATETIME2: return "DATETIME2";
            case ColumnType::DATE:      return "DATE";
            case ColumnType::TIME:      return "TIME";
            
            case ColumnType::BOOL:      return "BIT";
            case ColumnType::FLOAT:     return "FLOAT";
            case ColumnType::REAL:      return "REAL";
            
            default:                    return "UNKNOWN";
        }
    }

} // namespace CQL
