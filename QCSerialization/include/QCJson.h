#pragma once

// Minimal JSON parser and value tree for kernel subsystems
// Namespace: QC::JSON
//
// Notes:
// - Designed for freestanding kernel use (no libc / no exceptions)
// - Owns all memory it allocates (strings/arrays/objects)

#include "QCTypes.h"
#include "QCVector.h"

namespace QC
{
    namespace JSON
    {
        enum class Type : QC::u8
        {
            Null,
            Bool,
            Number,
            String,
            Object,
            Array
        };

        class Value;

        struct ObjectEntry
        {
            char *key;
            Value *value;
        };

        using Object = QC::Vector<ObjectEntry>;
        using Array = QC::Vector<Value *>;

        class Value
        {
        public:
            Value();
            Value(const Value &other);
            Value(Value &&other) noexcept;
            ~Value();

            Value &operator=(const Value &other);
            Value &operator=(Value &&other) noexcept;

            Type type() const { return m_type; }
            bool isNull() const { return m_type == Type::Null; }
            bool isBool() const { return m_type == Type::Bool; }
            bool isNumber() const { return m_type == Type::Number; }
            bool isString() const { return m_type == Type::String; }
            bool isObject() const { return m_type == Type::Object; }
            bool isArray() const { return m_type == Type::Array; }

            bool asBool(bool defaultValue = false) const;
            double asNumber(double defaultValue = 0.0) const;
            const char *asString(const char *defaultValue = "") const;

            Object *asObject();
            const Object *asObject() const;

            Array *asArray();
            const Array *asArray() const;

            const Value *find(const char *key) const;

        private:
            friend class Parser;

            void destroy();
            void copyFrom(const Value &other);
            void moveFrom(Value &&other) noexcept;

            void setNull();
            void setBool(bool value);
            void setNumber(double value);
            void setString(char *ownedString);
            void setObject(Object *ownedObject);
            void setArray(Array *ownedArray);

            Type m_type;
            union
            {
                bool boolValue;
                double numberValue;
                char *stringValue;
                Object *objectValue;
                Array *arrayValue;
            } m_data;
        };

        class Parser
        {
        public:
            struct Options
            {
                // When true, numeric literals using exponent notation (e/E) are rejected.
                bool forbidExponent = false;

                // When true, numeric literals must already be in canonical form.
                // This is used for deterministic hashing/signing payloads.
                bool requireCanonicalNumbers = false;
            };

            Parser(const char *text, QC::usize length);
            Parser(const char *text, QC::usize length, const Options &options);

            bool parse(Value &out);
            const char *error() const { return m_error; }
            QC::usize errorOffset() const { return m_pos; }

        private:
            bool parseValue(Value &out);
            bool parseObject(Value &out);
            bool parseArray(Value &out);
            bool parseString(Value &out);
            bool parseStringRaw(char **out);
            bool parseNumber(Value &out);
            bool parseLiteral(Value &out, const char *literal, Type type, bool boolValue);

            void skipWhitespace();
            bool expect(char c);
            bool match(char c);
            char peek() const;
            char get();
            bool eof() const;
            void setError(const char *msg);

            QC::i32 decodeHex(char c) const;
            bool appendUnicodeEscape(QC::Vector<char> &buffer);

            const char *m_text;
            QC::usize m_length;
            QC::usize m_pos;
            const char *m_error;

            Options m_options;
        };

        bool parse(const char *text, Value &out);

        // Extended parse variant for consumers that require additional constraints.
        bool parseEx(const char *text, QC::usize length, Value &out, const Parser::Options &options);

    } // namespace JSON

} // namespace QC
