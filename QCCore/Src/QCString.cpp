// QCommon String - Implementation
// Namespace: QC

#include "QCString.h"

namespace QC
{

    String::String() : m_data(nullptr), m_length(0), m_capacity(0)
    {
    }

    String::String(const char *str) : m_data(nullptr), m_length(0), m_capacity(0)
    {
        if (str)
        {
            m_length = strlen(str);
            m_capacity = m_length + 1;
            // TODO: Allocate memory using kernel heap
            // m_data = new char[m_capacity];
            // strcpy(m_data, str);
        }
    }

    String::String(const String &other) : m_data(nullptr), m_length(0), m_capacity(0)
    {
        // TODO: Implement copy constructor
    }

    String::String(String &&other) noexcept
        : m_data(other.m_data), m_length(other.m_length), m_capacity(other.m_capacity)
    {
        other.m_data = nullptr;
        other.m_length = 0;
        other.m_capacity = 0;
    }

    String::~String()
    {
        // TODO: Free memory using kernel heap
        // delete[] m_data;
    }

    String &String::operator=(const String &other)
    {
        if (this != &other)
        {
            // TODO: Implement copy assignment
        }
        return *this;
    }

    String &String::operator=(String &&other) noexcept
    {
        if (this != &other)
        {
            // TODO: Free existing data
            m_data = other.m_data;
            m_length = other.m_length;
            m_capacity = other.m_capacity;
            other.m_data = nullptr;
            other.m_length = 0;
            other.m_capacity = 0;
        }
        return *this;
    }

    const char *String::c_str() const
    {
        return m_data ? m_data : "";
    }

    usize String::length() const
    {
        return m_length;
    }

    bool String::empty() const
    {
        return m_length == 0;
    }

    char String::operator[](usize index) const
    {
        return m_data[index];
    }

    String String::operator+(const String &other) const
    {
        // TODO: Implement concatenation
        return String();
    }

    String &String::operator+=(const String &other)
    {
        // TODO: Implement append
        return *this;
    }

    bool String::operator==(const String &other) const
    {
        if (m_length != other.m_length)
            return false;
        return strcmp(m_data, other.m_data) == 0;
    }

    bool String::operator!=(const String &other) const
    {
        return !(*this == other);
    }

    // Static utilities
    usize String::strlen(const char *str)
    {
        if (!str)
            return 0;
        usize len = 0;
        while (str[len])
            ++len;
        return len;
    }

    void String::strcpy(char *dest, const char *src)
    {
        while ((*dest++ = *src++))
            ;
    }

    void String::strncpy(char *dest, const char *src, usize n)
    {
        usize i = 0;
        while (i < n && src[i])
        {
            dest[i] = src[i];
            ++i;
        }
        while (i < n)
        {
            dest[i++] = '\0';
        }
    }

    i32 String::strcmp(const char *a, const char *b)
    {
        while (*a && (*a == *b))
        {
            ++a;
            ++b;
        }
        return static_cast<i32>(*reinterpret_cast<const u8 *>(a)) -
               static_cast<i32>(*reinterpret_cast<const u8 *>(b));
    }

    void String::memset(void *dest, u8 value, usize size)
    {
        u8 *d = static_cast<u8 *>(dest);
        for (usize i = 0; i < size; ++i)
        {
            d[i] = value;
        }
    }

    void String::memcpy(void *dest, const void *src, usize size)
    {
        u8 *d = static_cast<u8 *>(dest);
        const u8 *s = static_cast<const u8 *>(src);
        for (usize i = 0; i < size; ++i)
        {
            d[i] = s[i];
        }
    }

    i32 String::memcmp(const void *a, const void *b, usize size)
    {
        const u8 *pa = static_cast<const u8 *>(a);
        const u8 *pb = static_cast<const u8 *>(b);
        for (usize i = 0; i < size; ++i)
        {
            if (pa[i] != pb[i])
            {
                return static_cast<i32>(pa[i]) - static_cast<i32>(pb[i]);
            }
        }
        return 0;
    }

} // namespace QC
