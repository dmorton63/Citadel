#pragma once

// QCommon String - String utilities
// Namespace: QC

#include "QCTypes.h"

namespace QC
{

    class String
    {
    public:
        String();
        explicit String(const char *str);
        String(const String &other);
        String(String &&other) noexcept;
        ~String();

        String &operator=(const String &other);
        String &operator=(String &&other) noexcept;

        const char *c_str() const;
        usize length() const;
        bool empty() const;

        char operator[](usize index) const;

        String operator+(const String &other) const;
        String &operator+=(const String &other);

        bool operator==(const String &other) const;
        bool operator!=(const String &other) const;

        // Static utilities
        static usize strlen(const char *str);
        static void strcpy(char *dest, const char *src);
        static void strncpy(char *dest, const char *src, usize n);
        static i32 strcmp(const char *a, const char *b);
        static void memset(void *dest, u8 value, usize size);
        static void memcpy(void *dest, const void *src, usize size);
        static i32 memcmp(const void *a, const void *b, usize size);

    private:
        char *m_data;
        usize m_length;
        usize m_capacity;
    };

} // namespace QC
