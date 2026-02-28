// Minimal JSON parser implementation
// Namespace: QC::JSON

#include "QCJson.h"
#include "QCString.h"

namespace QC
{
    namespace JSON
    {
        namespace
        {
            constexpr char ERROR_UNEXPECTED_EOF[] = "Unexpected end of JSON input";
            constexpr char ERROR_INVALID_NUMBER[] = "Invalid number literal";
            constexpr char ERROR_INVALID_LITERAL[] = "Invalid literal";
            constexpr char ERROR_EXPECTED_COLON[] = "Expected ':' after object key";
            constexpr char ERROR_EXPECTED_STRING[] = "Expected string";
            constexpr char ERROR_EXPECTED_VALUE[] = "Expected value";
            constexpr char ERROR_TRAILING_CONTENT[] = "Unexpected data after root value";
            constexpr char ERROR_UNEXPECTED_CHAR[] = "Unexpected character";
        } // namespace

        Value::Value()
            : m_type(Type::Null)
        {
            m_data.stringValue = nullptr;
        }

        Value::Value(const Value &other)
            : m_type(Type::Null)
        {
            copyFrom(other);
        }

        Value::Value(Value &&other) noexcept
            : m_type(Type::Null)
        {
            moveFrom(static_cast<Value &&>(other));
        }

        Value::~Value()
        {
            destroy();
        }

        Value &Value::operator=(const Value &other)
        {
            if (this != &other)
            {
                destroy();
                copyFrom(other);
            }
            return *this;
        }

        Value &Value::operator=(Value &&other) noexcept
        {
            if (this != &other)
            {
                destroy();
                moveFrom(static_cast<Value &&>(other));
            }
            return *this;
        }

        bool Value::asBool(bool defaultValue) const
        {
            return isBool() ? m_data.boolValue : defaultValue;
        }

        double Value::asNumber(double defaultValue) const
        {
            return isNumber() ? m_data.numberValue : defaultValue;
        }

        const char *Value::asString(const char *defaultValue) const
        {
            return isString() && m_data.stringValue ? m_data.stringValue : defaultValue;
        }

        Object *Value::asObject()
        {
            return isObject() ? m_data.objectValue : nullptr;
        }

        const Object *Value::asObject() const
        {
            return isObject() ? m_data.objectValue : nullptr;
        }

        Array *Value::asArray()
        {
            return isArray() ? m_data.arrayValue : nullptr;
        }

        const Array *Value::asArray() const
        {
            return isArray() ? m_data.arrayValue : nullptr;
        }

        const Value *Value::find(const char *key) const
        {
            if (!isObject() || !key || !m_data.objectValue)
            {
                return nullptr;
            }

            const Object &obj = *m_data.objectValue;
            for (QC::usize i = 0; i < obj.size(); ++i)
            {
                const ObjectEntry &entry = obj[i];
                if (entry.key && QC::String::strcmp(entry.key, key) == 0)
                {
                    return entry.value;
                }
            }
            return nullptr;
        }

        void Value::setNull()
        {
            destroy();
            m_type = Type::Null;
            m_data.stringValue = nullptr;
        }

        void Value::setBool(bool value)
        {
            destroy();
            m_type = Type::Bool;
            m_data.boolValue = value;
        }

        void Value::setNumber(double value)
        {
            destroy();
            m_type = Type::Number;
            m_data.numberValue = value;
        }

        void Value::setString(char *ownedString)
        {
            destroy();
            m_type = Type::String;
            m_data.stringValue = ownedString;
        }

        void Value::setObject(Object *ownedObject)
        {
            destroy();
            m_type = Type::Object;
            m_data.objectValue = ownedObject;
        }

        void Value::setArray(Array *ownedArray)
        {
            destroy();
            m_type = Type::Array;
            m_data.arrayValue = ownedArray;
        }

        void Value::destroy()
        {
            switch (m_type)
            {
            case Type::String:
                if (m_data.stringValue)
                {
                    operator delete[](m_data.stringValue);
                    m_data.stringValue = nullptr;
                }
                break;
            case Type::Object:
                if (m_data.objectValue)
                {
                    Object &obj = *m_data.objectValue;
                    for (QC::usize i = 0; i < obj.size(); ++i)
                    {
                        if (obj[i].key)
                        {
                            operator delete[](obj[i].key);
                            obj[i].key = nullptr;
                        }
                        if (obj[i].value)
                        {
                            delete obj[i].value;
                            obj[i].value = nullptr;
                        }
                    }
                    delete m_data.objectValue;
                    m_data.objectValue = nullptr;
                }
                break;
            case Type::Array:
                if (m_data.arrayValue)
                {
                    Array &arr = *m_data.arrayValue;
                    for (QC::usize i = 0; i < arr.size(); ++i)
                    {
                        if (arr[i])
                        {
                            delete arr[i];
                            arr[i] = nullptr;
                        }
                    }
                    delete m_data.arrayValue;
                    m_data.arrayValue = nullptr;
                }
                break;
            default:
                break;
            }

            m_type = Type::Null;
        }

        void Value::copyFrom(const Value &other)
        {
            m_type = Type::Null;
            m_data.stringValue = nullptr;

            switch (other.m_type)
            {
            case Type::Null:
                setNull();
                break;
            case Type::Bool:
                setBool(other.m_data.boolValue);
                break;
            case Type::Number:
                setNumber(other.m_data.numberValue);
                break;
            case Type::String:
            {
                if (!other.m_data.stringValue)
                {
                    setString(nullptr);
                    break;
                }
                QC::usize len = QC::String::strlen(other.m_data.stringValue);
                char *copy = static_cast<char *>(operator new[](len + 1));
                QC::String::memcpy(copy, other.m_data.stringValue, len + 1);
                setString(copy);
                break;
            }
            case Type::Object:
            {
                Object *obj = new Object();
                if (other.m_data.objectValue)
                {
                    const Object &src = *other.m_data.objectValue;
                    obj->reserve(src.size());
                    for (QC::usize i = 0; i < src.size(); ++i)
                    {
                        ObjectEntry entry;
                        entry.key = nullptr;
                        entry.value = nullptr;

                        if (src[i].key)
                        {
                            QC::usize klen = QC::String::strlen(src[i].key);
                            entry.key = static_cast<char *>(operator new[](klen + 1));
                            QC::String::memcpy(entry.key, src[i].key, klen + 1);
                        }
                        if (src[i].value)
                        {
                            entry.value = new Value(*src[i].value);
                        }

                        obj->push_back(entry);
                    }
                }
                setObject(obj);
                break;
            }
            case Type::Array:
            {
                Array *arr = new Array();
                if (other.m_data.arrayValue)
                {
                    const Array &src = *other.m_data.arrayValue;
                    arr->reserve(src.size());
                    for (QC::usize i = 0; i < src.size(); ++i)
                    {
                        if (src[i])
                        {
                            arr->push_back(new Value(*src[i]));
                        }
                        else
                        {
                            arr->push_back(nullptr);
                        }
                    }
                }
                setArray(arr);
                break;
            }
            }
        }

        void Value::moveFrom(Value &&other) noexcept
        {
            m_type = other.m_type;
            m_data = other.m_data;
            other.m_type = Type::Null;
            other.m_data.stringValue = nullptr;
        }

        Parser::Parser(const char *text, QC::usize length)
            : m_text(text),
              m_length(length),
              m_pos(0),
                            m_error(nullptr),
                            m_options()
        {
        }

                Parser::Parser(const char *text, QC::usize length, const Options &options)
                        : m_text(text),
                            m_length(length),
                            m_pos(0),
                            m_error(nullptr),
                            m_options(options)
                {
                }

        bool Parser::parse(Value &out)
        {
            if (!m_text)
            {
                setError(ERROR_EXPECTED_VALUE);
                return false;
            }

            skipWhitespace();
            if (!parseValue(out))
            {
                return false;
            }

            skipWhitespace();
            if (!eof())
            {
                setError(ERROR_TRAILING_CONTENT);
                return false;
            }

            return true;
        }

        bool Parser::parseValue(Value &out)
        {
            skipWhitespace();
            if (eof())
            {
                setError(ERROR_UNEXPECTED_EOF);
                return false;
            }

            char c = peek();
            switch (c)
            {
            case '{':
                return parseObject(out);
            case '[':
                return parseArray(out);
            case '"':
                return parseString(out);
            case 't':
                return parseLiteral(out, "true", Type::Bool, true);
            case 'f':
                return parseLiteral(out, "false", Type::Bool, false);
            case 'n':
                return parseLiteral(out, "null", Type::Null, false);
            default:
                if (c == '-' || (c >= '0' && c <= '9'))
                {
                    return parseNumber(out);
                }
                setError(ERROR_EXPECTED_VALUE);
                return false;
            }
        }

        bool Parser::parseObject(Value &out)
        {
            if (!expect('{'))
            {
                return false;
            }

            Object *obj = new Object();
            out.setObject(obj);

            skipWhitespace();
            if (match('}'))
            {
                return true;
            }

            while (true)
            {
                char *key = nullptr;
                if (!parseStringRaw(&key))
                {
                    return false;
                }

                skipWhitespace();
                if (!expect(':'))
                {
                    operator delete[](key);
                    setError(ERROR_EXPECTED_COLON);
                    return false;
                }

                Value *value = new Value();
                if (!parseValue(*value))
                {
                    operator delete[](key);
                    delete value;
                    return false;
                }

                ObjectEntry entry;
                entry.key = key;
                entry.value = value;
                obj->push_back(entry);

                skipWhitespace();
                if (match('}'))
                {
                    return true;
                }
                if (!expect(','))
                {
                    return false;
                }
                skipWhitespace();
            }
        }

        bool Parser::parseArray(Value &out)
        {
            if (!expect('['))
            {
                return false;
            }

            Array *arr = new Array();
            out.setArray(arr);

            skipWhitespace();
            if (match(']'))
            {
                return true;
            }

            while (true)
            {
                Value *element = new Value();
                if (!parseValue(*element))
                {
                    delete element;
                    return false;
                }
                arr->push_back(element);

                skipWhitespace();
                if (match(']'))
                {
                    return true;
                }
                if (!expect(','))
                {
                    return false;
                }
                skipWhitespace();
            }
        }

        bool Parser::parseString(Value &out)
        {
            char *value = nullptr;
            if (!parseStringRaw(&value))
            {
                return false;
            }

            out.setString(value);
            return true;
        }

        bool Parser::parseStringRaw(char **outString)
        {
            if (!expect('"'))
            {
                setError(ERROR_EXPECTED_STRING);
                return false;
            }

            QC::Vector<char> buffer;
            while (!eof())
            {
                char c = get();
                if (c == '"')
                {
                    buffer.push_back('\0');
                    QC::usize len = buffer.size();
                    char *result = static_cast<char *>(operator new[](len));
                    for (QC::usize i = 0; i < len; ++i)
                    {
                        result[i] = buffer[i];
                    }
                    *outString = result;
                    return true;
                }

                if (c == '\\')
                {
                    if (eof())
                    {
                        setError(ERROR_UNEXPECTED_EOF);
                        return false;
                    }

                    char esc = get();
                    switch (esc)
                    {
                    case '"':
                        buffer.push_back('"');
                        break;
                    case '\\':
                        buffer.push_back('\\');
                        break;
                    case '/':
                        buffer.push_back('/');
                        break;
                    case 'b':
                        buffer.push_back('\b');
                        break;
                    case 'f':
                        buffer.push_back('\f');
                        break;
                    case 'n':
                        buffer.push_back('\n');
                        break;
                    case 'r':
                        buffer.push_back('\r');
                        break;
                    case 't':
                        buffer.push_back('\t');
                        break;
                    case 'u':
                        if (!appendUnicodeEscape(buffer))
                        {
                            return false;
                        }
                        break;
                    default:
                        buffer.push_back(esc);
                        break;
                    }
                }
                else
                {
                    buffer.push_back(c);
                }
            }

            setError(ERROR_UNEXPECTED_EOF);
            return false;
        }

        bool Parser::parseNumber(Value &out)
        {
            QC::usize start = m_pos;

            if (match('-'))
            {
                // sign consumed
            }

            if (eof())
            {
                setError(ERROR_UNEXPECTED_EOF);
                return false;
            }

            if (!match('0'))
            {
                if (!(peek() >= '1' && peek() <= '9'))
                {
                    setError(ERROR_INVALID_NUMBER);
                    return false;
                }
                while (!eof() && peek() >= '0' && peek() <= '9')
                {
                    get();
                }
            }

            if (match('.'))
            {
                if (eof() || !(peek() >= '0' && peek() <= '9'))
                {
                    setError(ERROR_INVALID_NUMBER);
                    return false;
                }
                while (!eof() && peek() >= '0' && peek() <= '9')
                {
                    get();
                }
            }

            if (!eof() && (peek() == 'e' || peek() == 'E'))
            {
                if (m_options.forbidExponent || m_options.requireCanonicalNumbers)
                {
                    setError(ERROR_INVALID_NUMBER);
                    return false;
                }
                get();
                if (!eof() && (peek() == '+' || peek() == '-'))
                {
                    get();
                }
                if (eof() || !(peek() >= '0' && peek() <= '9'))
                {
                    setError(ERROR_INVALID_NUMBER);
                    return false;
                }
                while (!eof() && peek() >= '0' && peek() <= '9')
                {
                    get();
                }
            }

            QC::usize len = m_pos - start;
            if (len == 0)
            {
                setError(ERROR_INVALID_NUMBER);
                return false;
            }

            // Optional canonical-number enforcement (for deterministic payloads).
            // Enforces:
            // - no exponent (handled above)
            // - no trailing zeros in fractional part
            // - forbid negative zero encodings ("-0", "-0.0", "-0.000")
            if (m_options.requireCanonicalNumbers)
            {
                const char *p = m_text + start;
                const char *end = m_text + start + len;

                bool negative = false;
                if (p < end && *p == '-')
                {
                    negative = true;
                    ++p;
                }

                const char *dot = nullptr;
                for (const char *q = p; q < end; ++q)
                {
                    if (*q == '.')
                    {
                        dot = q;
                        break;
                    }
                }

                if (dot)
                {
                    if (end > dot + 1 && *(end - 1) == '0')
                    {
                        setError(ERROR_INVALID_NUMBER);
                        return false;
                    }
                }

                if (negative)
                {
                    bool anyNonZeroDigit = false;
                    for (const char *q = p; q < end; ++q)
                    {
                        if (*q >= '1' && *q <= '9')
                        {
                            anyNonZeroDigit = true;
                            break;
                        }
                    }
                    if (!anyNonZeroDigit)
                    {
                        setError(ERROR_INVALID_NUMBER);
                        return false;
                    }
                }
            }

            QC::Vector<char> buffer;
            buffer.reserve(len + 1);
            for (QC::usize i = 0; i < len; ++i)
            {
                buffer.push_back(m_text[start + i]);
            }
            buffer.push_back('\0');

            bool negative = false;
            QC::usize idx = 0;
            if (buffer[idx] == '-')
            {
                negative = true;
                ++idx;
            }

            double value = 0.0;
            while (buffer[idx] >= '0' && buffer[idx] <= '9')
            {
                value = value * 10.0 + static_cast<double>(buffer[idx] - '0');
                ++idx;
            }

            if (buffer[idx] == '.')
            {
                ++idx;
                double place = 0.1;
                while (buffer[idx] >= '0' && buffer[idx] <= '9')
                {
                    value += place * static_cast<double>(buffer[idx] - '0');
                    place *= 0.1;
                    ++idx;
                }
            }

            if (buffer[idx] == 'e' || buffer[idx] == 'E')
            {
                ++idx;
                bool expNegative = false;
                if (buffer[idx] == '+' || buffer[idx] == '-')
                {
                    expNegative = (buffer[idx] == '-');
                    ++idx;
                }
                int exponent = 0;
                while (buffer[idx] >= '0' && buffer[idx] <= '9')
                {
                    exponent = exponent * 10 + (buffer[idx] - '0');
                    ++idx;
                }

                double factor = 1.0;
                for (int i = 0; i < exponent; ++i)
                {
                    factor *= 10.0;
                }
                if (expNegative)
                {
                    value /= factor;
                }
                else
                {
                    value *= factor;
                }
            }

            if (buffer[idx] != '\0')
            {
                setError(ERROR_INVALID_NUMBER);
                return false;
            }

            if (negative)
            {
                value = -value;
            }

            out.setNumber(value);
            return true;
        }

        bool Parser::parseLiteral(Value &out, const char *literal, Type type, bool boolValue)
        {
            QC::usize len = QC::String::strlen(literal);
            if (m_pos + len > m_length)
            {
                setError(ERROR_UNEXPECTED_EOF);
                return false;
            }

            for (QC::usize i = 0; i < len; ++i)
            {
                if (m_text[m_pos + i] != literal[i])
                {
                    setError(ERROR_INVALID_LITERAL);
                    return false;
                }
            }

            m_pos += len;
            if (type == Type::Null)
            {
                out.setNull();
                return true;
            }
            if (type == Type::Bool)
            {
                out.setBool(boolValue);
                return true;
            }

            setError(ERROR_INVALID_LITERAL);
            return false;
        }

        void Parser::skipWhitespace()
        {
            while (!eof())
            {
                char c = peek();
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                {
                    ++m_pos;
                }
                else
                {
                    break;
                }
            }
        }

        bool Parser::expect(char c)
        {
            if (eof())
            {
                setError(ERROR_UNEXPECTED_EOF);
                return false;
            }
            if (peek() != c)
            {
                setError(ERROR_UNEXPECTED_CHAR);
                return false;
            }
            ++m_pos;
            return true;
        }

        bool Parser::match(char c)
        {
            if (!eof() && peek() == c)
            {
                ++m_pos;
                return true;
            }
            return false;
        }

        char Parser::peek() const
        {
            return m_text[m_pos];
        }

        char Parser::get()
        {
            return m_text[m_pos++];
        }

        bool Parser::eof() const
        {
            return m_pos >= m_length;
        }

        void Parser::setError(const char *msg)
        {
            if (!m_error)
            {
                m_error = msg;
            }
        }

        QC::i32 Parser::decodeHex(char c) const
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return 10 + (c - 'a');
            if (c >= 'A' && c <= 'F')
                return 10 + (c - 'A');
            return -1;
        }

        bool Parser::appendUnicodeEscape(QC::Vector<char> &buffer)
        {
            if (m_pos + 4 > m_length)
            {
                setError(ERROR_UNEXPECTED_EOF);
                return false;
            }

            QC::u16 code = 0;
            for (int i = 0; i < 4; ++i)
            {
                char c = get();
                QC::i32 value = decodeHex(c);
                if (value < 0)
                {
                    setError("Invalid unicode escape");
                    return false;
                }
                code = static_cast<QC::u16>((code << 4) | static_cast<QC::u16>(value));
            }

            if (code <= 0x7F)
            {
                buffer.push_back(static_cast<char>(code));
            }
            else
            {
                buffer.push_back('?');
            }
            return true;
        }

        bool parseEx(const char *text, QC::usize length, Value &out, const Parser::Options &options)
        {
            Parser parser(text, length, options);
            return parser.parse(out);
        }

        bool parse(const char *text, Value &out)
        {
            Parser parser(text, text ? QC::String::strlen(text) : 0);
            return parser.parse(out);
        }

    } // namespace JSON

} // namespace QC
