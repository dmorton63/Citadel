#pragma once

// QFileSystem FAT16 - FAT16 filesystem implementation (read-focused)
// Namespace: QFS

#include "QFSFAT32.h" // Reuse FileSystem/BlockDevice interfaces and common VFS types

namespace QFS
{

    struct FAT16BootSector
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
        // FAT16 extended
        QC::u8 driveNumber;
        QC::u8 reserved1;
        QC::u8 bootSignature;
        QC::u32 volumeId;
        char volumeLabel[11];
        char fsType[8];
    } __attribute__((packed));

    class FAT16 : public FileSystem
    {
    public:
        explicit FAT16(BlockDevice *device);
        ~FAT16() override;

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
            QC::u32 dirEntryIndex;
            bool dirty;
        };

        struct FATDirHandle
        {
            bool isRoot;
            QC::u32 startCluster;
            QC::u32 currentCluster;
            QC::u32 entryIndex;
            QC::u32 rootEntryIndex;

            // Pending VFAT long filename entries (0x0F) immediately preceding an SFN entry.
            char pendingLongName[256];
            QC::u8 pendingLongNameChecksum;
            bool pendingLongNameValid;
        };

        QC::u32 clusterToSector(QC::u32 cluster) const;
        bool loadCluster(QC::u32 cluster);
        bool storeCluster(QC::u32 cluster);

        QC::u16 readFAT(QC::u32 cluster);
        void writeFAT(QC::u32 cluster, QC::u16 value);
        QC::u32 allocateCluster();
        void freeClusterChain(QC::u32 startCluster);
        QC::u32 traverseToCluster(QC::u32 startCluster, QC::u32 index);
        QC::u32 entryCluster(const FAT32DirEntry &entry) const;

        bool iterateClusterDirectory(QC::u32 startCluster, const char *fatName, FAT32DirEntry *outEntry, QC::u32 *entryIndex = nullptr);
        bool iterateRootDirectory(const char *fatName, FAT32DirEntry *outEntry, QC::u32 *entryIndex = nullptr);
        FAT32DirEntry *findEntry(const char *path);
        bool updateRootDirectoryEntry(QC::u32 entryIndex, const FAT32DirEntry &entry);
        bool findFreeRootDirectoryEntry(QC::u32 *outEntryIndex);

        void parseName(const char *fatName, char *outName);
        void parseName(const FAT32DirEntry &entry, char *outName);
        void formatName(const char *name, char *fatName);

        BlockDevice *m_device;
        FAT16BootSector m_bootSector;
        QC::u32 m_fatStart;
        QC::u32 m_rootDirStart;
        QC::u32 m_rootDirSectors;
        QC::u32 m_dataStart;
        QC::u32 m_clusterSize;
        QC::u32 m_totalClusters;
        QC::u8 *m_clusterBuffer;
        FAT32DirEntry m_entryCache;
    };

} // namespace QFS
