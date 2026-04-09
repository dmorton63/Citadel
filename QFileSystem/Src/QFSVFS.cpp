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

    static QC::u64 fnv1a64(const QC::u8 *data, QC::usize len)
    {
        QC::u64 h = 1469598103934665603ULL;
        for (QC::usize i = 0; i < len; ++i)
        {
            h ^= static_cast<QC::u64>(data[i]);
            h *= 1099511628211ULL;
        }
        return h;
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
        if (!path || !info)
            return QC::Status::InvalidParam;

        char relativePath[256];
        FileSystem *fs = resolvePath(path, relativePath, sizeof(relativePath));

        if (!fs)
            return QC::Status::NotFound;

        const QC::Status st = fs->stat(relativePath, info);
        if (st != QC::Status::Success)
            return st;

        return applyRoleMetadata(path, info);
    }

    QC::Status VFS::setRoleFlag(const char *path, RoleFlag role)
    {
        if (!path || path[0] == '\0')
            return QC::Status::InvalidParam;

        for (QC::usize i = 0; i < m_roleMeta.size(); ++i)
        {
            if (QC::String::strcmp(m_roleMeta[i].path, path) == 0)
            {
                m_roleMeta[i].role = role;
                m_roleMeta[i].hash = computeRoleMetaHash(path, role);
                return QC::Status::Success;
            }
        }

        RoleMetaEntry entry;
        QC::String::memset(entry.path, 0, sizeof(entry.path));
        QC::String::strncpy(entry.path, path, sizeof(entry.path) - 1);
        entry.role = role;
        entry.hash = computeRoleMetaHash(path, role);
        m_roleMeta.push_back(entry);
        return QC::Status::Success;
    }

    bool VFS::exists(const char *path)
    {
        FileInfo info;
        return stat(path, &info) == QC::Status::Success;
    }

    QC::Status VFS::syncAll()
    {
        QC::Status worst = QC::Status::Success;
        for (QC::usize i = 0; i < m_mounts.size(); ++i)
        {
            if (!m_mounts[i].fs)
                continue;
            const QC::Status st = m_mounts[i].fs->sync();
            if (st != QC::Status::Success)
                worst = st;
        }
        return worst;
    }

    QC::u64 VFS::computeRoleMetaHash(const char *path, RoleFlag role) const
    {
        if (!path)
            return 0;

        static constexpr char kRoleMetaSalt[] = "QFS-ROLE-META-v1";
        const QC::usize pathLen = QC::String::strlen(path);

        QC::u64 h = fnv1a64(reinterpret_cast<const QC::u8 *>(kRoleMetaSalt), sizeof(kRoleMetaSalt) - 1);
        h ^= fnv1a64(reinterpret_cast<const QC::u8 *>(path), pathLen);

        QC::u32 rv = static_cast<QC::u32>(role);
        const QC::u8 rb[4] = {
            static_cast<QC::u8>(rv & 0xFF),
            static_cast<QC::u8>((rv >> 8) & 0xFF),
            static_cast<QC::u8>((rv >> 16) & 0xFF),
            static_cast<QC::u8>((rv >> 24) & 0xFF)};
        h ^= fnv1a64(rb, sizeof(rb));
        return h;
    }

    QC::Status VFS::applyRoleMetadata(const char *path, FileInfo *info) const
    {
        if (!path || !info)
            return QC::Status::InvalidParam;

        info->roleFlag = static_cast<QC::u32>(RoleFlag::Everyone);
        info->metadataHash = computeRoleMetaHash(path, RoleFlag::Everyone);

        for (QC::usize i = 0; i < m_roleMeta.size(); ++i)
        {
            const RoleMetaEntry &entry = m_roleMeta[i];
            if (QC::String::strcmp(entry.path, path) != 0)
                continue;

            const QC::u64 expected = computeRoleMetaHash(entry.path, entry.role);
            if (expected != entry.hash)
                return QC::Status::Error;

            info->roleFlag = static_cast<QC::u32>(entry.role);
            info->metadataHash = entry.hash;
            return QC::Status::Success;
        }

        return QC::Status::Success;
    }

} // namespace QFS
