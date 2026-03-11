#pragma once

// QFileSystem FAT Probe - Detect FAT12/16/32 from a boot sector (BPB)
// Namespace: QFS

#include "QCTypes.h"

namespace QFS
{

    enum class FATKind : QC::u8
    {
        Unknown = 0,
        FAT12 = 12,
        FAT16 = 16,
        FAT32 = 32,
    };

    struct FATProbeResult
    {
        FATKind kind = FATKind::Unknown;
        QC::u16 bytesPerSector = 0;
        QC::u8 sectorsPerCluster = 0;
        QC::u16 reservedSectors = 0;
        QC::u8 fatCount = 0;
        QC::u16 rootEntryCount = 0;
        QC::u32 totalSectors = 0;
        QC::u32 fatSectors = 0;
        QC::u32 rootDirSectors = 0;
        QC::u32 firstDataSector = 0;
        QC::u32 totalClusters = 0;
        bool hasBootSignature = false;
    };

    // Parses the BIOS Parameter Block (BPB) and classifies FAT type using cluster-count rules.
    // Returns false if the sector does not look like a supported FAT boot sector.
    bool probeFATBootSector(const QC::u8 sector[512], FATProbeResult &out);

} // namespace QFS
