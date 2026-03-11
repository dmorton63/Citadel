// QFileSystem FAT Probe - Implementation
// Namespace: QFS

#include "QFSFATProbe.h"

namespace QFS
{
    namespace
    {
        constexpr QC::u32 FAT12_MAX_CLUSTERS = 4084;
        constexpr QC::u32 FAT16_MAX_CLUSTERS = 65524;

        static inline QC::u16 rd16(const QC::u8 *p)
        {
            return static_cast<QC::u16>(p[0] | (static_cast<QC::u16>(p[1]) << 8));
        }

        static inline QC::u32 rd32(const QC::u8 *p)
        {
            return static_cast<QC::u32>(p[0] | (static_cast<QC::u32>(p[1]) << 8) | (static_cast<QC::u32>(p[2]) << 16) | (static_cast<QC::u32>(p[3]) << 24));
        }

        static bool isPowerOfTwo(QC::u32 v)
        {
            return v != 0 && (v & (v - 1)) == 0;
        }

        static FATKind classifyByClusters(QC::u32 totalClusters)
        {
            if (totalClusters == 0)
                return FATKind::Unknown;
            if (totalClusters <= FAT12_MAX_CLUSTERS)
                return FATKind::FAT12;
            if (totalClusters <= FAT16_MAX_CLUSTERS)
                return FATKind::FAT16;
            return FATKind::FAT32;
        }
    }

    bool probeFATBootSector(const QC::u8 sector[512], FATProbeResult &out)
    {
        // Offsets from FAT BPB spec (boot sector / BPB)
        const QC::u16 bytesPerSector = rd16(&sector[11]);
        const QC::u8 sectorsPerCluster = sector[13];
        const QC::u16 reservedSectors = rd16(&sector[14]);
        const QC::u8 fatCount = sector[16];
        const QC::u16 rootEntryCount = rd16(&sector[17]);
        const QC::u16 totalSectors16 = rd16(&sector[19]);
        const QC::u16 fatSz16 = rd16(&sector[22]);
        const QC::u32 totalSectors32 = rd32(&sector[32]);
        const QC::u32 fatSz32 = rd32(&sector[36]);

        out = FATProbeResult{};
        out.bytesPerSector = bytesPerSector;
        out.sectorsPerCluster = sectorsPerCluster;
        out.reservedSectors = reservedSectors;
        out.fatCount = fatCount;
        out.rootEntryCount = rootEntryCount;
        out.hasBootSignature = (sector[510] == 0x55 && sector[511] == 0xAA);

        // We only support 512-byte sectors today.
        if (bytesPerSector != 512)
            return false;

        // Basic sanity checks.
        if (sectorsPerCluster == 0 || !isPowerOfTwo(sectorsPerCluster) || sectorsPerCluster > 128)
            return false;
        if (reservedSectors == 0)
            return false;
        if (fatCount == 0)
            return false;

        const QC::u32 totalSectors = (totalSectors16 != 0) ? static_cast<QC::u32>(totalSectors16) : totalSectors32;
        out.totalSectors = totalSectors;
        if (totalSectors == 0)
            return false;

        const QC::u32 fatSectors = (fatSz16 != 0) ? static_cast<QC::u32>(fatSz16) : fatSz32;
        out.fatSectors = fatSectors;
        if (fatSectors == 0)
            return false;

        const QC::u32 rootDirSectors = (static_cast<QC::u32>(rootEntryCount) * 32U + (bytesPerSector - 1U)) / bytesPerSector;
        out.rootDirSectors = rootDirSectors;

        const QC::u32 firstDataSector = static_cast<QC::u32>(reservedSectors) + (static_cast<QC::u32>(fatCount) * fatSectors) + rootDirSectors;
        out.firstDataSector = firstDataSector;

        if (totalSectors <= firstDataSector)
            return false;

        const QC::u32 dataSectors = totalSectors - firstDataSector;
        const QC::u32 totalClusters = dataSectors / sectorsPerCluster;
        out.totalClusters = totalClusters;

        FATKind kind = classifyByClusters(totalClusters);

        // Cross-check common BPB expectations; if they disagree, keep the cluster-count classification.
        // FAT32 typically has rootEntryCount==0 and fatSz16==0.
        if (kind == FATKind::FAT32)
        {
            // If these are obviously inconsistent, still allow cluster-based classification.
            // (vvfat and some tools can produce quirky values.)
        }

        out.kind = kind;
        return (kind == FATKind::FAT16 || kind == FATKind::FAT32 || kind == FATKind::FAT12);
    }

} // namespace QFS
