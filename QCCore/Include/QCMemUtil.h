/*
 * QCommon Memory Utilities - C-compatible memory functions
 *
 * This header provides freestanding-safe memory functions that work
 * in both C and C++ code. These replace <string.h> / <cstring> functions.
 *
 * Safe freestanding headers you CAN use:
 *   <cstdint> / <stdint.h>   - Fixed-width integer types
 *   <cstddef> / <stddef.h>   - size_t, ptrdiff_t, nullptr_t
 *   <cstdarg> / <stdarg.h>   - Variadic function support
 *   <cfloat>  / <float.h>    - Floating point limits
 *   <climits> / <limits.h>   - Integer limits
 *   <stdbool.h>              - bool (C only, built-in for C++)
 *   <type_traits>            - Type traits (C++ only)
 *   <initializer_list>       - Initializer lists (C++ only)
 *
 * Headers you CANNOT use (require hosted runtime):
 *   <cstring> / <string.h>   - Use this header instead
 *   <cstdlib> / <stdlib.h>   - malloc/free - use QKMemHeap
 *   <iostream>, <fstream>    - No I/O streams
 *   <string>, <vector>       - Use QCString, QCVector
 *   <exception>              - No exception support
 */

#ifndef QCMEMUTIL_H
#define QCMEMUTIL_H

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
#else
#include <stddef.h>
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    /* Memory operations */
    void *memset(void *dest, int value, size_t count);
    void *memcpy(void *dest, const void *src, size_t count);
    void *memmove(void *dest, const void *src, size_t count);
    int memcmp(const void *lhs, const void *rhs, size_t count);

    /* String operations */
    size_t strlen(const char *str);
    char *strcpy(char *dest, const char *src);
    char *strncpy(char *dest, const char *src, size_t count);
    int strcmp(const char *lhs, const char *rhs);
    int strncmp(const char *lhs, const char *rhs, size_t count);
    char *strcat(char *dest, const char *src);
    char *strchr(const char *str, int ch);
    char *strrchr(const char *str, int ch);
    char *strstr(const char *haystack, const char *needle);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* QCMEMUTIL_H */
