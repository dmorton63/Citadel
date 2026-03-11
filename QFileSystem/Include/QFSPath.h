#pragma once

// QFileSystem Path - Path manipulation utilities
// Namespace: QFS

#include "QCTypes.h"

namespace QFS
{

    class Path
    {
    public:
        // Path parsing
        static bool isAbsolute(const char *path);
        static bool isRelative(const char *path);

        // Path components
        static void dirname(const char *path, char *result, QC::usize resultSize);
        static void basename(const char *path, char *result, QC::usize resultSize);
        static void extension(const char *path, char *result, QC::usize resultSize);

        // Path manipulation
        static void join(const char *path1, const char *path2, char *result, QC::usize resultSize);
        static void normalize(const char *path, char *result, QC::usize resultSize);
        static void resolve(const char *basePath, const char *relativePath,
                            char *result, QC::usize resultSize);

        // Path comparison
        static bool equals(const char *path1, const char *path2);
        static bool startsWith(const char *path, const char *prefix);

        // Validation
        static bool isValid(const char *path);
        static bool containsInvalidChars(const char *path);

    private:
        Path() = delete;

        static constexpr char SEPARATOR = '/';
    };

} // namespace QFS
