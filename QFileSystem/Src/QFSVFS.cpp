// QFileSystem VFS - Implementation
// Namespace: QFS

#include "QFSVFS.h"
#include "QFSFile.h"
#include "QFSDirectory.h"
#include "QFSFAT32.h"
#include "QFSPath.h"
#include "QCLogger.h"
#include "QCString.h"

namespace QFS
{

    static inline char lowerAscii(char c)
    {
        if (c >= 'A' && c <= 'Z')
            return static_cast<char>(c + 32);
        return c;
    }

    static bool startsWithIgnoreCase(const char *s, const char *prefix)
    {
        if (!s || !prefix)
            return false;
        while (*prefix)
        {
            if (lowerAscii(*s) != lowerAscii(*prefix))
                return false;
            ++s;
            ++prefix;
        }
        return true;
    }

    static bool isMountPrefixIgnoreCase(const char *path, const char *mount)
    {
        if (!path || !mount)
            return false;

        const QC::usize mlen = QC::String::strlen(mount);
        if (mlen == 0)
            return false;

        if (!startsWithIgnoreCase(path, mount))
            return false;

        // Boundary-aware: mount "/shared" should not match "/shared2".
        if (mount[mlen - 1] == '/')
            return true;

        const char next = path[mlen];
        return next == '\0' || next == '/';
    }

    VFS &VFS::instance()
    {
        static VFS instance;
        return instance;
    }

    VFS::VFS()
    {
    }

    VFS::~VFS()
    {
    }

    void VFS::initialize()
    {
        QC_LOG_INFO("QFS", "Initializing Virtual File System");
        QC_LOG_INFO("QFS", "VFS initialized");
    }

    QC::Status VFS::mount(const char *path, FileSystem *fs)
    {
        if (!path || !fs)
            return QC::Status::InvalidParam;

        MountPoint mp;
        QC::String::strncpy(mp.path, path, sizeof(mp.path) - 1);
        mp.fs = fs;
        m_mounts.push_back(mp);

        QC_LOG_INFO("QFS", "Mounted filesystem at %s", path);
        return QC::Status::Success;
    }

    QC::Status VFS::unmount(const char *path)
    {
        for (QC::usize i = 0; i < m_mounts.size(); ++i)
        {
            if (QC::String::strcmp(m_mounts[i].path, path) == 0)
            {
                for (QC::usize j = i + 1; j < m_mounts.size(); ++j)
                {
                    m_mounts[j - 1] = m_mounts[j];
                }
                m_mounts.pop_back();
                QC_LOG_INFO("QFS", "Unmounted filesystem at %s", path);
                return QC::Status::Success;
            }
        }
        return QC::Status::NotFound;
    }

    FileSystem *VFS::resolvePath(const char *path, char *relativePath, QC::usize relativeSize)
    {
        FileSystem *bestMatch = nullptr;
        QC::usize bestLen = 0;

        for (QC::usize i = 0; i < m_mounts.size(); ++i)
        {
            QC::usize len = QC::String::strlen(m_mounts[i].path);
            if (isMountPrefixIgnoreCase(path, m_mounts[i].path) && len > bestLen)
            {
                bestMatch = m_mounts[i].fs;
                bestLen = len;
            }
        }

        if (bestMatch && relativePath)
        {
            QC::usize pathLen = QC::String::strlen(path);
            QC::usize copyLen = pathLen - bestLen;
            if (copyLen >= relativeSize)
                copyLen = relativeSize - 1;
            QC::String::strncpy(relativePath, path + bestLen, copyLen);
            relativePath[copyLen] = '\0';

            // Ensure relative path starts with /
            if (relativePath[0] != '/' && copyLen > 0)
            {
                // Shift and prepend /
                for (QC::isize j = static_cast<QC::isize>(copyLen); j >= 0; --j)
                {
                    relativePath[j + 1] = relativePath[j];
                }
                relativePath[0] = '/';
            }
            if (relativePath[0] == '\0')
            {
                relativePath[0] = '/';
                relativePath[1] = '\0';
            }
        }

        return bestMatch;
    }

    File *VFS::open(const char *path, OpenMode mode)
    {
        char relativePath[256];
        FileSystem *fs = resolvePath(path, relativePath, sizeof(relativePath));

        if (!fs)
        {
            QC_LOG_ERROR("QFS", "No filesystem for path: %s", path);
            return nullptr;
        }

        return fs->open(relativePath, mode);
    }

    QC::Status VFS::close(File *file)
    {
        if (!file)
            return QC::Status::InvalidParam;
        FileSystem *fs = file->fileSystem();
        QC::Status status = QC::Status::Success;
        if (fs)
        {
            status = fs->close(file);
        }
        delete file;
        return status;
    }

    Directory *VFS::openDir(const char *path)
    {
        char relativePath[256];
        FileSystem *fs = resolvePath(path, relativePath, sizeof(relativePath));

        if (!fs)
        {
            QC_LOG_ERROR("QFS", "No filesystem for path: %s", path);
            return nullptr;
        }

        return fs->openDir(relativePath);
    }

    QC::Status VFS::closeDir(Directory *dir)
    {
        if (!dir)
            return QC::Status::InvalidParam;
        FileSystem *fs = dir->fileSystem();
        QC::Status status = QC::Status::Success;
        if (fs)
        {
            status = fs->closeDir(dir);
        }
        delete dir;
        return status;
    }

    QC::Status VFS::createDir(const char *path)
    {
        char relativePath[256];
        FileSystem *fs = resolvePath(path, relativePath, sizeof(relativePath));

        if (!fs)
            return QC::Status::NotFound;
        return fs->createDir(relativePath);
    }

    QC::Status VFS::removeDir(const char *path)
    {
        char relativePath[256];
        FileSystem *fs = resolvePath(path, relativePath, sizeof(relativePath));

        if (!fs)
            return QC::Status::NotFound;
        return fs->remove(relativePath);
    }

    QC::Status VFS::remove(const char *path)
    {
        char relativePath[256];
        FileSystem *fs = resolvePath(path, relativePath, sizeof(relativePath));

        if (!fs)
            return QC::Status::NotFound;
        return fs->remove(relativePath);
    }

    QC::Status VFS::rename(const char *oldPath, const char *newPath)
    {
        // TODO: Implement rename
        return QC::Status::NotSupported;
    }

    QC::Status VFS::stat(const char *path, FileInfo *info)
    {
        char relativePath[256];
        FileSystem *fs = resolvePath(path, relativePath, sizeof(relativePath));

        if (!fs)
            return QC::Status::NotFound;
        return fs->stat(relativePath, info);
    }

    bool VFS::exists(const char *path)
    {
        FileInfo info;
        return stat(path, &info) == QC::Status::Success;
    }

} // namespace QFS
