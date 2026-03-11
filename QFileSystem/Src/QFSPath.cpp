// QFileSystem Path - Implementation
// Namespace: QFS

#include "QFSPath.h"
#include "QCString.h"

namespace QFS
{

    bool Path::isAbsolute(const char *path)
    {
        return path && path[0] == SEPARATOR;
    }

    bool Path::isRelative(const char *path)
    {
        return !isAbsolute(path);
    }

    void Path::dirname(const char *path, char *result, QC::usize resultSize)
    {
        if (!path || !result || resultSize == 0)
            return;

        QC::usize len = QC::String::strlen(path);
        if (len == 0)
        {
            result[0] = '.';
            result[1] = '\0';
            return;
        }

        // Find last separator
        QC::isize lastSep = -1;
        for (QC::isize i = static_cast<QC::isize>(len) - 1; i >= 0; --i)
        {
            if (path[i] == SEPARATOR)
            {
                lastSep = i;
                break;
            }
        }

        if (lastSep < 0)
        {
            result[0] = '.';
            result[1] = '\0';
        }
        else if (lastSep == 0)
        {
            result[0] = SEPARATOR;
            result[1] = '\0';
        }
        else
        {
            QC::usize copyLen = static_cast<QC::usize>(lastSep);
            if (copyLen >= resultSize)
                copyLen = resultSize - 1;
            QC::String::strncpy(result, path, copyLen);
            result[copyLen] = '\0';
        }
    }

    void Path::basename(const char *path, char *result, QC::usize resultSize)
    {
        if (!path || !result || resultSize == 0)
            return;

        QC::usize len = QC::String::strlen(path);
        if (len == 0)
        {
            result[0] = '\0';
            return;
        }

        // Skip trailing separators
        while (len > 0 && path[len - 1] == SEPARATOR)
            --len;

        // Find last separator
        QC::usize start = 0;
        for (QC::isize i = static_cast<QC::isize>(len) - 1; i >= 0; --i)
        {
            if (path[i] == SEPARATOR)
            {
                start = static_cast<QC::usize>(i) + 1;
                break;
            }
        }

        QC::usize copyLen = len - start;
        if (copyLen >= resultSize)
            copyLen = resultSize - 1;
        QC::String::strncpy(result, path + start, copyLen);
        result[copyLen] = '\0';
    }

    void Path::extension(const char *path, char *result, QC::usize resultSize)
    {
        if (!path || !result || resultSize == 0)
            return;

        result[0] = '\0';

        char base[256];
        basename(path, base, sizeof(base));

        QC::usize len = QC::String::strlen(base);

        // Find last dot
        for (QC::isize i = static_cast<QC::isize>(len) - 1; i >= 0; --i)
        {
            if (base[i] == '.')
            {
                QC::usize extLen = len - static_cast<QC::usize>(i);
                if (extLen >= resultSize)
                    extLen = resultSize - 1;
                QC::String::strncpy(result, base + i, extLen);
                result[extLen] = '\0';
                return;
            }
        }
    }

    void Path::join(const char *path1, const char *path2, char *result, QC::usize resultSize)
    {
        if (!result || resultSize == 0)
            return;

        result[0] = '\0';

        if (!path1 || path1[0] == '\0')
        {
            if (path2)
            {
                QC::String::strncpy(result, path2, resultSize - 1);
                result[resultSize - 1] = '\0';
            }
            return;
        }

        QC::usize len1 = QC::String::strlen(path1);

        // Copy first path
        QC::String::strncpy(result, path1, resultSize - 1);

        // Add separator if needed
        if (len1 > 0 && result[len1 - 1] != SEPARATOR && path2 && path2[0] != SEPARATOR)
        {
            if (len1 < resultSize - 1)
            {
                result[len1] = SEPARATOR;
                result[len1 + 1] = '\0';
                ++len1;
            }
        }

        // Append second path
        if (path2)
        {
            QC::usize remaining = resultSize - len1 - 1;
            QC::String::strncpy(result + len1, path2, remaining);
            result[resultSize - 1] = '\0';
        }
    }

    void Path::normalize(const char *path, char *result, QC::usize resultSize)
    {
        if (!path || !result || resultSize == 0)
            return;

        // TODO: Implement proper path normalization (remove . and ..)
        QC::String::strncpy(result, path, resultSize - 1);
        result[resultSize - 1] = '\0';
    }

    void Path::resolve(const char *basePath, const char *relativePath,
                       char *result, QC::usize resultSize)
    {
        if (isAbsolute(relativePath))
        {
            QC::String::strncpy(result, relativePath, resultSize - 1);
            result[resultSize - 1] = '\0';
        }
        else
        {
            char baseDir[256];
            dirname(basePath, baseDir, sizeof(baseDir));
            join(baseDir, relativePath, result, resultSize);
        }

        normalize(result, result, resultSize);
    }

    bool Path::equals(const char *path1, const char *path2)
    {
        if (!path1 || !path2)
            return path1 == path2;
        return QC::String::strcmp(path1, path2) == 0;
    }

    bool Path::startsWith(const char *path, const char *prefix)
    {
        if (!path || !prefix)
            return false;

        QC::usize prefixLen = QC::String::strlen(prefix);
        QC::usize pathLen = QC::String::strlen(path);

        if (prefixLen > pathLen)
            return false;

        return QC::String::memcmp(path, prefix, prefixLen) == 0;
    }

    bool Path::isValid(const char *path)
    {
        if (!path || path[0] == '\0')
            return false;
        return !containsInvalidChars(path);
    }

    bool Path::containsInvalidChars(const char *path)
    {
        if (!path)
            return true;

        // Check for invalid characters
        while (*path)
        {
            char c = *path;
            if (c == '\0' || c == '\n' || c == '\r')
            {
                return true;
            }
            ++path;
        }

        return false;
    }

} // namespace QFS
