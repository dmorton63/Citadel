#include "QKBootRamdiskFile.h"

#include "Boot/Limine/QKBootLimineModules.h"

#include "QCString.h"

namespace QK::Boot::Config
{
    namespace
    {
        static void LogStr(FLogFn Log, const char *Msg)
        {
            if (Log)
                Log(Msg);
        }

        static QC::u16 ReadLe16(const QC::u8 *p)
        {
            return static_cast<QC::u16>(p[0] | (static_cast<QC::u16>(p[1]) << 8));
        }

        static QC::u32 ReadLe32(const QC::u8 *p)
        {
            return static_cast<QC::u32>(p[0] | (static_cast<QC::u32>(p[1]) << 8) | (static_cast<QC::u32>(p[2]) << 16) |
                                       (static_cast<QC::u32>(p[3]) << 24));
        }

        struct Fat32Layout
        {
            QC::u32 bytesPerSector = 0;
            QC::u32 sectorsPerCluster = 0;
            QC::u32 reservedSectors = 0;
            QC::u32 numFats = 0;
            QC::u32 fatSizeSectors = 0;
            QC::u32 rootCluster = 0;
            QC::u64 fatOffsetBytes = 0;
            QC::u64 dataOffsetBytes = 0;
            QC::u64 clusterSizeBytes = 0;
        };

        static bool ParseFat32(const QC::u8 *image, QC::usize imageSize, Fat32Layout &out)
        {
            if (!image || imageSize < 512)
                return false;

            const QC::u16 bytesPerSector = ReadLe16(image + 11);
            const QC::u8 sectorsPerCluster = image[13];
            const QC::u16 reservedSectors = ReadLe16(image + 14);
            const QC::u8 numFats = image[16];
            const QC::u16 rootEntryCount = ReadLe16(image + 17);
            const QC::u16 fatSize16 = ReadLe16(image + 22);
            const QC::u32 fatSize32 = ReadLe32(image + 36);
            const QC::u32 rootCluster = ReadLe32(image + 44);

            if (bytesPerSector == 0 || (bytesPerSector & (bytesPerSector - 1)) != 0)
                return false;
            if (sectorsPerCluster == 0 || (sectorsPerCluster & (sectorsPerCluster - 1)) != 0)
                return false;
            if (numFats == 0)
                return false;
            if (rootEntryCount != 0)
                return false;
            if (fatSize16 != 0)
                return false;
            if (fatSize32 == 0)
                return false;
            if (rootCluster < 2)
                return false;

            const QC::u64 fatOffsetBytes = static_cast<QC::u64>(reservedSectors) * bytesPerSector;
            const QC::u64 fatsBytes = static_cast<QC::u64>(numFats) * static_cast<QC::u64>(fatSize32) * bytesPerSector;
            const QC::u64 dataOffsetBytes = fatOffsetBytes + fatsBytes;
            const QC::u64 clusterSizeBytes = static_cast<QC::u64>(bytesPerSector) * sectorsPerCluster;

            if (fatOffsetBytes >= imageSize)
                return false;
            if (dataOffsetBytes >= imageSize)
                return false;
            if (clusterSizeBytes == 0)
                return false;

            out.bytesPerSector = bytesPerSector;
            out.sectorsPerCluster = sectorsPerCluster;
            out.reservedSectors = reservedSectors;
            out.numFats = numFats;
            out.fatSizeSectors = fatSize32;
            out.rootCluster = rootCluster;
            out.fatOffsetBytes = fatOffsetBytes;
            out.dataOffsetBytes = dataOffsetBytes;
            out.clusterSizeBytes = clusterSizeBytes;
            return true;
        }

        static bool ReadFat32Entry(const QC::u8 *image, QC::usize imageSize, const Fat32Layout &fat, QC::u32 cluster, QC::u32 &outNext)
        {
            outNext = 0;
            if (cluster < 2)
                return false;

            const QC::u64 entryOffset = fat.fatOffsetBytes + static_cast<QC::u64>(cluster) * 4ull;
            if (entryOffset + 4ull > imageSize)
                return false;

            const QC::u32 val = ReadLe32(image + entryOffset) & 0x0FFFFFFFul;
            outNext = val;
            return true;
        }

        static bool ClusterToOffset(const Fat32Layout &fat, QC::u32 cluster, QC::u64 &outOffset)
        {
            outOffset = 0;
            if (cluster < 2)
                return false;

            const QC::u64 clusterIndex = static_cast<QC::u64>(cluster - 2);
            outOffset = fat.dataOffsetBytes + clusterIndex * fat.clusterSizeBytes;
            return true;
        }

        static bool FindShortNameInRoot(const QC::u8 *image, QC::usize imageSize, const Fat32Layout &fat, const char short11[11],
                                        QC::u32 &outFirstCluster, QC::u32 &outFileSize)
        {
            outFirstCluster = 0;
            outFileSize = 0;

            QC::u32 cluster = fat.rootCluster;
            const QC::usize maxClusters = static_cast<QC::usize>(imageSize / (fat.clusterSizeBytes ? fat.clusterSizeBytes : 1)) + 2;

            for (QC::usize iter = 0; iter < maxClusters; ++iter)
            {
                QC::u64 clusterOffset = 0;
                if (!ClusterToOffset(fat, cluster, clusterOffset))
                    return false;
                if (clusterOffset + fat.clusterSizeBytes > imageSize)
                    return false;

                const QC::u8 *dir = image + clusterOffset;
                const QC::usize entries = static_cast<QC::usize>(fat.clusterSizeBytes / 32ull);

                for (QC::usize e = 0; e < entries; ++e)
                {
                    const QC::u8 *ent = dir + e * 32;
                    const QC::u8 name0 = ent[0];
                    if (name0 == 0x00)
                        return false;
                    if (name0 == 0xE5)
                        continue;

                    const QC::u8 attr = ent[11];
                    if (attr == 0x0F)
                        continue;
                    if (attr & 0x08)
                        continue;

                    if (QC::String::memcmp(ent, short11, 11) != 0)
                        continue;

                    const QC::u16 hi = ReadLe16(ent + 20);
                    const QC::u16 lo = ReadLe16(ent + 26);
                    const QC::u32 firstCluster = (static_cast<QC::u32>(hi) << 16) | lo;
                    const QC::u32 fileSize = ReadLe32(ent + 28);

                    if (firstCluster < 2)
                        return false;

                    outFirstCluster = firstCluster;
                    outFileSize = fileSize;
                    return true;
                }

                QC::u32 next = 0;
                if (!ReadFat32Entry(image, imageSize, fat, cluster, next))
                    return false;
                if (next >= 0x0FFFFFF8ul)
                    return false;
                if (next == 0)
                    return false;
                cluster = next;
            }

            return false;
        }

        static bool FindShortNameInDirectory(const QC::u8 *image, QC::usize imageSize, const Fat32Layout &fat, QC::u32 dirCluster,
                                             const char short11[11], QC::u32 &outFirstCluster, QC::u32 &outFileSize, QC::u8 &outAttr)
        {
            outFirstCluster = 0;
            outFileSize = 0;
            outAttr = 0;

            if (dirCluster < 2)
                return false;

            QC::u32 cluster = dirCluster;
            const QC::usize maxClusters = static_cast<QC::usize>(imageSize / (fat.clusterSizeBytes ? fat.clusterSizeBytes : 1)) + 2;

            for (QC::usize iter = 0; iter < maxClusters; ++iter)
            {
                QC::u64 clusterOffset = 0;
                if (!ClusterToOffset(fat, cluster, clusterOffset))
                    return false;
                if (clusterOffset + fat.clusterSizeBytes > imageSize)
                    return false;

                const QC::u8 *dir = image + clusterOffset;
                const QC::usize entries = static_cast<QC::usize>(fat.clusterSizeBytes / 32ull);

                for (QC::usize e = 0; e < entries; ++e)
                {
                    const QC::u8 *ent = dir + e * 32;
                    const QC::u8 name0 = ent[0];
                    if (name0 == 0x00)
                        return false;
                    if (name0 == 0xE5)
                        continue;

                    const QC::u8 attr = ent[11];
                    if (attr == 0x0F)
                        continue;
                    if (attr & 0x08)
                        continue;

                    if (QC::String::memcmp(ent, short11, 11) != 0)
                        continue;

                    const QC::u16 hi = ReadLe16(ent + 20);
                    const QC::u16 lo = ReadLe16(ent + 26);
                    const QC::u32 firstCluster = (static_cast<QC::u32>(hi) << 16) | lo;
                    const QC::u32 fileSize = ReadLe32(ent + 28);

                    if (firstCluster < 2)
                        return false;

                    outFirstCluster = firstCluster;
                    outFileSize = fileSize;
                    outAttr = attr;
                    return true;
                }

                QC::u32 next = 0;
                if (!ReadFat32Entry(image, imageSize, fat, cluster, next))
                    return false;
                if (next >= 0x0FFFFFF8ul)
                    return false;
                if (next == 0)
                    return false;
                cluster = next;
            }

            return false;
        }

        static bool ReadFileByClusterChain(const QC::u8 *image, QC::usize imageSize, const Fat32Layout &fat, QC::u32 firstCluster,
                                           QC::u32 fileSize, char *&outBuf, QC::usize &outLen)
        {
            outBuf = nullptr;
            outLen = 0;

            if (fileSize == 0)
                return false;
            if (fileSize > 1024 * 64)
                return false;

            char *buffer = static_cast<char *>(operator new[](static_cast<QC::usize>(fileSize) + 1));
            QC::usize written = 0;

            QC::u32 cluster = firstCluster;
            const QC::usize maxClusters = static_cast<QC::usize>(imageSize / (fat.clusterSizeBytes ? fat.clusterSizeBytes : 1)) + 2;

            for (QC::usize iter = 0; iter < maxClusters && written < fileSize; ++iter)
            {
                QC::u64 clusterOffset = 0;
                if (!ClusterToOffset(fat, cluster, clusterOffset))
                    break;
                if (clusterOffset + fat.clusterSizeBytes > imageSize)
                    break;

                const QC::usize remaining = static_cast<QC::usize>(fileSize) - written;
                const QC::usize chunk = (remaining < fat.clusterSizeBytes) ? remaining : static_cast<QC::usize>(fat.clusterSizeBytes);
                QC::String::memcpy(buffer + written, image + clusterOffset, chunk);
                written += chunk;

                if (written >= fileSize)
                    break;

                QC::u32 next = 0;
                if (!ReadFat32Entry(image, imageSize, fat, cluster, next))
                    break;
                if (next >= 0x0FFFFFF8ul)
                    break;
                if (next < 2)
                    break;

                cluster = next;
            }

            if (written != fileSize)
            {
                operator delete[](buffer);
                return false;
            }

            buffer[fileSize] = 0;
            outBuf = buffer;
            outLen = static_cast<QC::usize>(fileSize);
            return true;
        }

        static bool SegmentToShort11(const char *Segment, char out[11])
        {
            if (!Segment || !out)
                return false;

            const char *base = Segment;
            if (!*base)
                return false;

            // Split name/ext.
            const char *dot = nullptr;
            for (const char *p = base; *p; ++p)
            {
                if (*p == '.')
                {
                    dot = p;
                    break;
                }
            }

            QC::usize nameLen = 0;
            QC::usize extLen = 0;

            if (dot)
            {
                nameLen = static_cast<QC::usize>(dot - base);
                const char *ext = dot + 1;
                while (ext[extLen])
                    ++extLen;
            }
            else
            {
                while (base[nameLen])
                    ++nameLen;
                extLen = 0;
            }

            if (nameLen == 0 || nameLen > 8)
                return false;
            if (extLen > 3)
                return false;

            // Fill with spaces.
            for (QC::usize i = 0; i < 11; ++i)
                out[i] = ' ';

            // Copy & uppercase.
            for (QC::usize i = 0; i < nameLen; ++i)
            {
                char c = base[i];
                if (c >= 'a' && c <= 'z')
                    c = static_cast<char>(c - 'a' + 'A');
                out[i] = c;
            }

            if (dot)
            {
                const char *ext = dot + 1;
                for (QC::usize i = 0; i < extLen; ++i)
                {
                    char c = ext[i];
                    if (c >= 'a' && c <= 'z')
                        c = static_cast<char>(c - 'a' + 'A');
                    out[8 + i] = c;
                }
            }

            return true;
        }

        static bool ReadFileBy83Path(const QC::u8 *image, QC::usize imageSize, const Fat32Layout &fat, const char *Path, char *&outBuf,
                                     QC::usize &outLen)
        {
            outBuf = nullptr;
            outLen = 0;

            if (!Path)
                return false;

            const char *p = Path;
            while (*p == '/')
                ++p;

            QC::u32 curDir = fat.rootCluster;

            // Empty path means "root"; not a file.
            if (!*p)
                return false;

            while (*p)
            {
                // Extract next segment.
                char segment[32];
                QC::usize segLen = 0;
                while (*p && *p != '/' && segLen + 1 < sizeof(segment))
                {
                    segment[segLen++] = *p;
                    ++p;
                }
                segment[segLen] = 0;

                while (*p == '/')
                    ++p;

                if (segLen == 0)
                    continue;

                char short11[11];
                if (!SegmentToShort11(segment, short11))
                    return false;

                const bool last = (*p == 0);
                QC::u32 firstCluster = 0;
                QC::u32 fileSize = 0;
                QC::u8 attr = 0;
                if (!FindShortNameInDirectory(image, imageSize, fat, curDir, short11, firstCluster, fileSize, attr))
                    return false;

                if (!last)
                {
                    if ((attr & 0x10) == 0)
                        return false;
                    curDir = firstCluster;
                    continue;
                }

                // Last segment: must be a file.
                if (attr & 0x10)
                    return false;

                return ReadFileByClusterChain(image, imageSize, fat, firstCluster, fileSize, outBuf, outLen);
            }

            return false;
        }
    }

    bool ReadFileFromLimineRamdiskModule(FLogFn Log, QC::u64 ModuleRequest[], const char *Path, char *&OutBuf, QC::usize &OutLen)
    {
        OutBuf = nullptr;
        OutLen = 0;

        const limine_file *Ramdisk = QK::Boot::Limine::FindRamdiskModule(ModuleRequest);
        if (!Ramdisk || !Ramdisk->address || Ramdisk->size < 512)
            return false;

        const QC::u8 *image = static_cast<const QC::u8 *>(Ramdisk->address);
        const QC::usize imageSize = static_cast<QC::usize>(Ramdisk->size);

        Fat32Layout fat{};
        if (!ParseFat32(image, imageSize, fat))
            return false;

        if (!ReadFileBy83Path(image, imageSize, fat, Path, OutBuf, OutLen))
        {
            LogStr(Log, "RamdiskFile: cannot open 8.3 path: ");
            LogStr(Log, Path ? Path : "(null)");
            LogStr(Log, "\r\n");
            return false;
        }

        return true;
    }
}
