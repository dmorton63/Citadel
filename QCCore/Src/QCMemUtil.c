/*
 * QCommon Memory Utilities - Implementation
 *
 * C-compatible memory and string functions for freestanding environment.
 * These provide the standard library functions that are normally in <string.h>
 */

#include "QCMemUtil.h"

/* ============================================================================
 * Memory Operations
 * ============================================================================ */

void *memset(void *dest, int value, size_t count)
{
    unsigned char *d = (unsigned char *)dest;
    unsigned char v = (unsigned char)value;

    for (size_t i = 0; i < count; ++i)
    {
        d[i] = v;
    }

    return dest;
}

void *memcpy(void *dest, const void *src, size_t count)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    for (size_t i = 0; i < count; ++i)
    {
        d[i] = s[i];
    }

    return dest;
}

void *memmove(void *dest, const void *src, size_t count)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    if (d < s)
    {
        /* Copy forward */
        for (size_t i = 0; i < count; ++i)
        {
            d[i] = s[i];
        }
    }
    else if (d > s)
    {
        /* Copy backward to handle overlap */
        for (size_t i = count; i > 0; --i)
        {
            d[i - 1] = s[i - 1];
        }
    }

    return dest;
}

int memcmp(const void *lhs, const void *rhs, size_t count)
{
    const unsigned char *a = (const unsigned char *)lhs;
    const unsigned char *b = (const unsigned char *)rhs;

    for (size_t i = 0; i < count; ++i)
    {
        if (a[i] != b[i])
        {
            return (int)a[i] - (int)b[i];
        }
    }

    return 0;
}

/* ============================================================================
 * String Operations
 * ============================================================================ */

size_t strlen(const char *str)
{
    if (!str)
        return 0;

    size_t len = 0;
    while (str[len])
    {
        ++len;
    }
    return len;
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++))
        ;
    return dest;
}

char *strncpy(char *dest, const char *src, size_t count)
{
    size_t i = 0;

    /* Copy characters */
    while (i < count && src[i])
    {
        dest[i] = src[i];
        ++i;
    }

    /* Pad with nulls */
    while (i < count)
    {
        dest[i++] = '\0';
    }

    return dest;
}

int strcmp(const char *lhs, const char *rhs)
{
    while (*lhs && (*lhs == *rhs))
    {
        ++lhs;
        ++rhs;
    }
    return (int)(unsigned char)*lhs - (int)(unsigned char)*rhs;
}

int strncmp(const char *lhs, const char *rhs, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        if (lhs[i] != rhs[i])
        {
            return (int)(unsigned char)lhs[i] - (int)(unsigned char)rhs[i];
        }
        if (lhs[i] == '\0')
        {
            return 0;
        }
    }
    return 0;
}

char *strcat(char *dest, const char *src)
{
    char *d = dest;

    /* Find end of dest */
    while (*d)
    {
        ++d;
    }

    /* Copy src */
    while ((*d++ = *src++))
        ;

    return dest;
}

char *strchr(const char *str, int ch)
{
    char c = (char)ch;

    while (*str)
    {
        if (*str == c)
        {
            return (char *)str;
        }
        ++str;
    }

    /* Also check for null terminator if searching for '\0' */
    if (c == '\0')
    {
        return (char *)str;
    }

    return (char *)0;
}

char *strrchr(const char *str, int ch)
{
    char c = (char)ch;
    const char *last = (char *)0;

    while (*str)
    {
        if (*str == c)
        {
            last = str;
        }
        ++str;
    }

    if (c == '\0')
    {
        return (char *)str;
    }

    return (char *)last;
}

char *strstr(const char *haystack, const char *needle)
{
    if (!*needle)
    {
        return (char *)haystack;
    }

    while (*haystack)
    {
        const char *h = haystack;
        const char *n = needle;

        while (*h && *n && (*h == *n))
        {
            ++h;
            ++n;
        }

        if (!*n)
        {
            return (char *)haystack;
        }

        ++haystack;
    }

    return (char *)0;
}
