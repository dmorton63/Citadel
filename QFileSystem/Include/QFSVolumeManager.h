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
        const char *sourceKind = nullptr;
        const char *sourceDetail = nullptr;
        bool persistent = false;
    };

    struct VolumeInfo
    {
        char name[32] = {0};
        char mountPath[128] = {0};
        char sourceKind[24] = {0};
        char sourceDetail[64] = {0};
        char persistenceClass[16] = {0};
        char backingDriver[24] = {0};
        char deviceId[32] = {0};
        FileSystemKind fsKind = FileSystemKind::FAT_AUTO;
        QC::uptr deviceHandle = 0;
        bool mounted = false;
        bool autoMount = false;
        bool persistent = false;
        bool mountFailed = false;
        QC::u32 mountFailCount = 0;
    };

    class VolumeManager
    {
    public:
        static VolumeManager &instance();

        QC::Status registerVolume(const VolumeDefinition &definition);
        QC::Status unregisterVolume(const char *name);
        QC::Status mountVolume(const char *name);
        QC::Status unmountVolume(const char *nameOrPath);
        QC::Status mountAll();
        QC::Status mountPending();
        bool isMounted(const char *name) const;
        QC::Status setAutoMount(const char *name, bool enabled);
        QC::usize copyVolumeInfo(VolumeInfo *out, QC::usize cap) const;

    private:
        struct VolumeRecord
        {
            char name[32];
            char mountPath[128];
            char sourceKind[24];
            char sourceDetail[64];
            FileSystemKind fsKind;
            BlockDevice *device;
            FileSystem *fs;
            bool mounted;
            bool autoMount;
            bool persistent;
            bool mountFailed;
            QC::u32 mountFailCount;
        };

        VolumeManager() = default;
        VolumeManager(const VolumeManager &) = delete;
        VolumeManager &operator=(const VolumeManager &) = delete;

        VolumeRecord *findRecord(const char *name);
        const VolumeRecord *findRecord(const char *name) const;
        VolumeRecord *findRecordByMountPath(const char *mountPath);
        QC::Status mountRecord(VolumeRecord &record);
        FileSystem *createFileSystem(FileSystemKind kind, BlockDevice *device);

        QC::Vector<VolumeRecord> m_volumes;
    };

} // namespace QFS
