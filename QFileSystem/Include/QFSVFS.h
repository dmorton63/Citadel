#pragma once

// QFileSystem VFS - Virtual File System
// Namespace: QFS

#include "QCTypes.h"
#include "QCString.h"
#include "QCVector.h"

namespace QFS
{

    class File;
    class Directory;
    class FileSystem;

    // File open modes
    enum class OpenMode : QC::u8
    {
        Read = 0x01,
        Write = 0x02,
        Append = 0x04,
        Create = 0x08,
        Truncate = 0x10,
        Binary = 0x20
    };

    inline OpenMode operator|(OpenMode a, OpenMode b)
    {
        return static_cast<OpenMode>(static_cast<QC::u8>(a) | static_cast<QC::u8>(b));
    }

    inline bool operator&(OpenMode a, OpenMode b)
    {
        return (static_cast<QC::u8>(a) & static_cast<QC::u8>(b)) != 0;
    }

    // File types
    enum class FileType : QC::u8
    {
        Regular,
        Directory,
        SymLink,
        Device,
        Pipe,
        Socket
    };

    // Metadata role policy tag (MVP).
    enum class RoleFlag : QC::u32
    {
        Everyone = 0,
        User = 1,
        Admin = 2,
        System = 3,
        Sc = 4,
        Protected = 5
    };

    // File info
    struct FileInfo
    {
        char name[256];
        FileType type;
        QC::u64 size;
        QC::u64 createdTime;
        QC::u64 modifiedTime;
        QC::u64 accessedTime;
        QC::u32 permissions;
        QC::u32 uid;
        QC::u32 gid;
        QC::u32 roleFlag;
        QC::u64 metadataHash;
    };

    // Mount point
    struct MountPoint
    {
        char path[256];
        FileSystem *fs;
    };

    class VFS
    {
    public:
        static VFS &instance();

        void initialize();

        // Mounting
        QC::Status mount(const char *path, FileSystem *fs);
        QC::Status unmount(const char *path);

        // File operations
        File *open(const char *path, OpenMode mode);
        QC::Status close(File *file);

        // Directory operations
        Directory *openDir(const char *path);
        QC::Status closeDir(Directory *dir);
        QC::Status createDir(const char *path);
        QC::Status removeDir(const char *path);

        // File management
        QC::Status remove(const char *path);
        QC::Status rename(const char *oldPath, const char *newPath);
        QC::Status stat(const char *path, FileInfo *info);
        QC::Status setRoleFlag(const char *path, RoleFlag role);
        bool exists(const char *path);

        // Best-effort flush for all mounted filesystems.
        QC::Status syncAll();

        // Path resolution
        FileSystem *resolvePath(const char *path, char *relativePath, QC::usize relativeSize);

    private:
        struct RoleMetaEntry
        {
            char path[256];
            RoleFlag role = RoleFlag::Everyone;
            QC::u64 hash = 0;
        };

        QC::u64 computeRoleMetaHash(const char *path, RoleFlag role) const;
        QC::Status applyRoleMetadata(const char *path, FileInfo *info) const;

        VFS();
        ~VFS();
        VFS(const VFS &) = delete;
        VFS &operator=(const VFS &) = delete;

        QC::Vector<MountPoint> m_mounts;
        QC::Vector<RoleMetaEntry> m_roleMeta;
    };

} // namespace QFS
