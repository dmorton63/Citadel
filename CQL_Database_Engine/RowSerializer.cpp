#include "RowSerializer.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace CQL {

    std::vector<uint8_t> RowSerializer::SerializeRow(
        const std::vector<std::string>& values,
        const std::vector<Column>& columns) {

        std::vector<uint8_t> rowData;

        for (size_t i = 0; i < columns.size(); i++) {
            const auto& col = columns[i];
            const std::string& value = values[i];

            try {
                switch (col.type) {
                    case ColumnType::TINYINT: {
                        uint8_t val = static_cast<uint8_t>(std::stoul(value));
                        rowData.push_back(val);
                        break;
                    }
                    case ColumnType::SMALLINT: {
                        int16_t val = static_cast<int16_t>(std::stoi(value));
                        rowData.insert(rowData.end(), reinterpret_cast<uint8_t*>(&val), 
                                      reinterpret_cast<uint8_t*>(&val) + sizeof(int16_t));
                        break;
                    }
                    case ColumnType::INT: {
                        int32_t val = std::stoi(value);
                        rowData.insert(rowData.end(), reinterpret_cast<uint8_t*>(&val), 
                                      reinterpret_cast<uint8_t*>(&val) + sizeof(int32_t));
                        break;
                    }
                    case ColumnType::BIGINT: {
                        int64_t val = std::stoll(value);
                        rowData.insert(rowData.end(), reinterpret_cast<uint8_t*>(&val), 
                                      reinterpret_cast<uint8_t*>(&val) + sizeof(int64_t));
                        break;
                    }
                    case ColumnType::BOOL: {
                        uint8_t val = (value == "1" || value == "true" || value == "TRUE") ? 1 : 0;
                        rowData.push_back(val);
                        break;
                    }
                    case ColumnType::FLOAT: {
                        double val = std::stod(value);
                        rowData.insert(rowData.end(), reinterpret_cast<uint8_t*>(&val), 
                                      reinterpret_cast<uint8_t*>(&val) + sizeof(double));
                        break;
                    }
                    case ColumnType::REAL: {
                        float val = std::stof(value);
                        rowData.insert(rowData.end(), reinterpret_cast<uint8_t*>(&val), 
                                      reinterpret_cast<uint8_t*>(&val) + sizeof(float));
                        break;
                    }
                    case ColumnType::CHAR:
                    case ColumnType::NCHAR: {
                        // Fixed-length string, pad with zeros
                        uint16_t maxLen = col.size;
                        std::string padded = value;
                        if (padded.length() > maxLen) padded.resize(maxLen);
                        while (padded.length() < maxLen) padded += '\0';
                        rowData.insert(rowData.end(), padded.begin(), padded.end());
                        break;
                    }
                    case ColumnType::VARCHAR:
                    case ColumnType::NVARCHAR:
                    case ColumnType::TEXT: {
                        // Variable-length string: 2-byte length + data
                        uint16_t len = static_cast<uint16_t>(value.length());
                        if (col.type != ColumnType::TEXT && len > col.size) {
                            std::cerr << "String too long for column " << col.name << std::endl;
                            return {}; // Return empty vector on error
                        }
                        rowData.insert(rowData.end(), reinterpret_cast<uint8_t*>(&len), 
                                      reinterpret_cast<uint8_t*>(&len) + sizeof(uint16_t));
                        rowData.insert(rowData.end(), value.begin(), value.end());
                        break;
                    }
                    case ColumnType::DATETIME:
                    case ColumnType::DATETIME2:
                    case ColumnType::DATE:
                    case ColumnType::TIME: {
                        // For now, store as 64-bit integer (timestamp or encoded value)
                        // TODO: Implement proper date/time parsing
                        int64_t val = std::stoll(value);
                        rowData.insert(rowData.end(), reinterpret_cast<uint8_t*>(&val), 
                                      reinterpret_cast<uint8_t*>(&val) + sizeof(int64_t));
                        break;
                    }
                    default:
                        std::cerr << "Unsupported column type for serialization" << std::endl;
                        return {}; // Return empty vector on error
                }
            } catch (const std::exception& e) {
                std::cerr << "Error converting value '" << value << "' for column " 
                          << col.name << ": " << e.what() << std::endl;
                return {}; // Return empty vector on error
            }
        }

        return rowData;
    }

    std::vector<std::string> RowSerializer::DeserializeRow(
        const std::vector<uint8_t>& rowData,
        const std::vector<Column>& columns) {

        std::vector<std::string> values;
        size_t offset = 0;

        for (const auto& col : columns) {
            if (offset >= rowData.size()) {
                values.push_back("<ERROR: truncated>");
                continue;
            }
            values.push_back(FormatValue(rowData.data(), offset, col));
        }

        return values;
    }

    std::string RowSerializer::FormatValue(const uint8_t* data, size_t& offset, const Column& col) {
        std::ostringstream ss;

        switch (col.type) {
            case ColumnType::TINYINT: {
                uint8_t val = data[offset];
                offset += 1;
                ss << static_cast<int>(val);
                break;
            }
            case ColumnType::SMALLINT: {
                int16_t val;
                memcpy(&val, data + offset, sizeof(int16_t));
                offset += sizeof(int16_t);
                ss << val;
                break;
            }
            case ColumnType::INT: {
                int32_t val;
                memcpy(&val, data + offset, sizeof(int32_t));
                offset += sizeof(int32_t);
                ss << val;
                break;
            }
            case ColumnType::BIGINT: {
                int64_t val;
                memcpy(&val, data + offset, sizeof(int64_t));
                offset += sizeof(int64_t);
                ss << val;
                break;
            }
            case ColumnType::BOOL: {
                uint8_t val = data[offset];
                offset += 1;
                ss << (val ? "true" : "false");
                break;
            }
            case ColumnType::FLOAT: {
                double val;
                memcpy(&val, data + offset, sizeof(double));
                offset += sizeof(double);
                ss << std::fixed << std::setprecision(2) << val;
                break;
            }
            case ColumnType::REAL: {
                float val;
                memcpy(&val, data + offset, sizeof(float));
                offset += sizeof(float);
                ss << std::fixed << std::setprecision(2) << val;
                break;
            }
            case ColumnType::CHAR:
            case ColumnType::NCHAR: {
                // Fixed-length string
                std::string str(reinterpret_cast<const char*>(data + offset), col.size);
                // Trim trailing nulls
                size_t end = str.find('\0');
                if (end != std::string::npos) {
                    str = str.substr(0, end);
                }
                offset += col.size;
                ss << str;
                break;
            }
            case ColumnType::VARCHAR:
            case ColumnType::NVARCHAR:
            case ColumnType::TEXT: {
                // Variable-length string: 2-byte length + data
                uint16_t len;
                memcpy(&len, data + offset, sizeof(uint16_t));
                offset += sizeof(uint16_t);
                std::string str(reinterpret_cast<const char*>(data + offset), len);
                offset += len;
                ss << str;
                break;
            }
            case ColumnType::DATETIME:
            case ColumnType::DATETIME2:
            case ColumnType::DATE:
            case ColumnType::TIME: {
                int64_t val;
                memcpy(&val, data + offset, sizeof(int64_t));
                offset += sizeof(int64_t);
                // For now, just display as number
                // TODO: Format as date/time string
                ss << val;
                break;
            }
            default:
                ss << "<unsupported>";
                break;
        }

        return ss.str();
    }

} // namespace CQL
