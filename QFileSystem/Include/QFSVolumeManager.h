#pragma once

// QFileSystem Volume Manager - Discover and mount block devices
// Namespace: QFS

#include "QCTypes.h"
#include "QCVector.h"

namespace QFS
{

    class FileSystem;
    class BlockDevice;

    // Forward declaration to avoid circular include issues
    class VFS;

    enum class FileSystemKind : QC::u8
    {
        FAT_AUTO = 0x00,
        FAT32 = 0x01,
        FAT16 = 0x02,
    };

    struct VolumeDefinition
    {
        const char *name;
        const char *mountPath;
        FileSystemKind fsKind;
        BlockDevice *device;
        bool autoMount = true;
    };

    class VolumeManager
    {
    public:
        static VolumeManager &instance();

        QC::Status registerVolume(const VolumeDefinition &definition);
        QC::Status unregisterVolume(const char *name);
        QC::Status mountVolume(const char *name);
        QC::Status mountAll();
        QC::Status mountPending();
        bool isMounted(const char *name) const;

    private:
        struct VolumeRecord
        {
            char name[32];
            char mountPath[128];
            FileSystemKind fsKind;
            BlockDevice *device;
            FileSystem *fs;
            bool mounted;
            bool autoMount;
        };

        VolumeManager() = default;
        VolumeManager(const VolumeManager &) = delete;
        VolumeManager &operator=(const VolumeManager &) = delete;

        VolumeRecord *findRecord(const char *name);
        const VolumeRecord *findRecord(const char *name) const;
        QC::Status mountRecord(VolumeRecord &record);
        FileSystem *createFileSystem(FileSystemKind kind, BlockDevice *device);

        QC::Vector<VolumeRecord> m_volumes;
    };

} // namespace QFS
