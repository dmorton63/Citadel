#pragma once

// QFileSystem FAT32 - FAT32 filesystem implementation
// Namespace: QFS

#include "QCTypes.h"
#include "QFSVFS.h"
#include "QFSDirectory.h"

namespace QFS
{

    // FAT32 boot sector
    struct FAT32BootSector
    {
        QC::u8 jump[3];
        char oemName[8];
        QC::u16 bytesPerSector;
        QC::u8 sectorsPerCluster;
        QC::u16 reservedSectors;
        QC::u8 fatCount;
        QC::u16 rootEntryCount;
        QC::u16 totalSectors16;
        QC::u8 mediaType;
        QC::u16 sectorsPerFat16;
        QC::u16 sectorsPerTrack;
        QC::u16 heads;
        QC::u32 hiddenSectors;
        QC::u32 totalSectors32;
        // FAT32 extended
        QC::u32 sectorsPerFat32;
        QC::u16 extFlags;
        QC::u16 version;
        QC::u32 rootCluster;
        QC::u16 fsInfoSector;
        QC::u16 backupBootSector;
        QC::u8 reserved[12];
        QC::u8 driveNumber;
        QC::u8 reserved1;
        QC::u8 bootSignature;
        QC::u32 volumeId;
        char volumeLabel[11];
        char fsType[8];
    } __attribute__((packed));

    // Directory entry
    struct FAT32DirEntry
    {
        char name[11];
        QC::u8 attributes;
        QC::u8 reserved;
        QC::u8 creationTimeTenth;
        QC::u16 creationTime;
        QC::u16 creationDate;
        QC::u16 accessDate;
        QC::u16 clusterHigh;
        QC::u16 modificationTime;
        QC::u16 modificationDate;
        QC::u16 clusterLow;
        QC::u32 size;
    } __attribute__((packed));

    // File attributes
    namespace FAT32Attr
    {
        constexpr QC::u8 ReadOnly = 0x01;
        constexpr QC::u8 Hidden = 0x02;
        constexpr QC::u8 System = 0x04;
        constexpr QC::u8 VolumeId = 0x08;
        constexpr QC::u8 Directory = 0x10;
        constexpr QC::u8 Archive = 0x20;
        constexpr QC::u8 LongName = 0x0F;
    }

    class BlockDevice; // Forward declaration

    class FileSystem
    {
    public:
        virtual ~FileSystem() = default;

        virtual QC::Status mount() = 0;
        virtual QC::Status unmount() = 0;

        virtual File *open(const char *path, OpenMode mode) = 0;
        virtual QC::Status close(File *file) = 0;

        virtual QC::isize read(File *file, void *buffer, QC::usize size) = 0;
        virtual QC::isize write(File *file, const void *buffer, QC::usize size) = 0;

        virtual Directory *openDir(const char *path) = 0;
        virtual QC::Status closeDir(Directory *dir) = 0;
        virtual bool readDir(Directory *dir, DirEntry *entry) = 0;
        virtual void rewindDir(Directory *dir) = 0;

        virtual QC::Status stat(const char *path, FileInfo *info) = 0;
        virtual QC::Status createDir(const char *path) = 0;
        virtual QC::Status remove(const char *path) = 0;
    };

    class FAT32 : public FileSystem
    {
    public:
        FAT32(BlockDevice *device);
        ~FAT32() override;

        QC::Status mount() override;
        QC::Status unmount() override;

        File *open(const char *path, OpenMode mode) override;
        QC::Status close(File *file) override;

        QC::isize read(File *file, void *buffer, QC::usize size) override;
        QC::isize write(File *file, const void *buffer, QC::usize size) override;

        Directory *openDir(const char *path) override;
        QC::Status closeDir(Directory *dir) override;
        bool readDir(Directory *dir, DirEntry *entry) override;
        void rewindDir(Directory *dir) override;

        QC::Status stat(const char *path, FileInfo *info) override;
        QC::Status createDir(const char *path) override;
        QC::Status remove(const char *path) override;

    private:
        struct FATFileHandle
        {
            QC::u32 startCluster;
            QC::u64 size;
            QC::u32 dirCluster;
            QC::u32 dirEntryIndex;
            bool dirty;
        };

        struct FATDirHandle
        {
            QC::u32 startCluster;
            QC::u32 currentCluster;
            QC::u32 entryIndex;

            // Pending VFAT long filename entries (0x0F) immediately preceding an SFN entry.
            char pendingLongName[256];
            QC::u8 pendingLongNameChecksum;
            bool pendingLongNameValid;
        };

        QC::u32 clusterToSector(QC::u32 cluster);
        QC::u32 readFAT(QC::u32 cluster);
        void writeFAT(QC::u32 cluster, QC::u32 value);
        QC::u32 allocateCluster();
        void freeClusterChain(QC::u32 startCluster);

        FAT32DirEntry *findEntry(const char *path, QC::u32 *parentCluster = nullptr, QC::u32 *entryIndex = nullptr);
        bool loadCluster(QC::u32 cluster);
        bool storeCluster(QC::u32 cluster);
        bool iterateDirectory(QC::u32 startCluster, const char *fatName, FAT32DirEntry *outEntry, QC::u32 *entryIndex = nullptr);
        bool updateDirectoryEntry(QC::u32 dirStartCluster, QC::u32 entryIndex, const FAT32DirEntry &entry);
        bool findFreeDirectoryEntry(QC::u32 dirStartCluster, QC::u32 *outEntryIndex);
        QC::u32 traverseToCluster(QC::u32 startCluster, QC::u32 index);
        QC::u32 entryCluster(const FAT32DirEntry &entry) const;
        void parseName(const char *fatName, char *outName);
        void parseName(const FAT32DirEntry &entry, char *outName);
        void formatName(const char *name, char *fatName);

        BlockDevice *m_device;
        FAT32BootSector m_bootSector;
        QC::u32 m_fatStart;
        QC::u32 m_dataStart;
        QC::u32 m_clusterSize;
        QC::u32 m_totalClusters;
        QC::u8 *m_clusterBuffer;
        FAT32DirEntry m_entryCache;
    };

    // Block device interface
    class BlockDevice
    {
    public:
        virtual ~BlockDevice() = default;

        virtual QC::usize sectorSize() const = 0;
        virtual QC::u64 sectorCount() const = 0;

        virtual QC::Status readSector(QC::u64 sector, void *buffer) = 0;
        virtual QC::Status writeSector(QC::u64 sector, const void *buffer) = 0;
        virtual QC::Status readSectors(QC::u64 sector, QC::usize count, void *buffer) = 0;
        virtual QC::Status writeSectors(QC::u64 sector, QC::usize count, const void *buffer) = 0;
    };

} // namespace QFS
