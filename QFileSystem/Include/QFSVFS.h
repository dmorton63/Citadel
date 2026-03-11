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
        bool exists(const char *path);

        // Path resolution
        FileSystem *resolvePath(const char *path, char *relativePath, QC::usize relativeSize);

    private:
        VFS();
        ~VFS();
        VFS(const VFS &) = delete;
        VFS &operator=(const VFS &) = delete;

        QC::Vector<MountPoint> m_mounts;
    };

} // namespace QFS
