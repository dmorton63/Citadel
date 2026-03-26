#include "QKBootRamdiskFile.h"

#include "Boot/Limine/QKBootLimineModules.h"

#include "QCString.h"

namespace QK::Boot::Config
{
    namespace
    {
        // Verbose VFAT/LFN tracing (compiled out by default).
#ifndef CITADEL_BOOT_FAT_TRACE
#define CITADEL_BOOT_FAT_TRACE 0
#endif

        static void FatTrace(FLogFn Log, const char *Msg)
        {
#if CITADEL_BOOT_FAT_TRACE
            if (Log && Msg)
                Log(Msg);
#else
            (void)Log;
            (void)Msg;
#endif
        }

        static void FatTraceHexByte(FLogFn Log, QC::u8 v)
        {
#if CITADEL_BOOT_FAT_TRACE
            if (!Log)
                return;
            static const char kHex[] = "0123456789ABCDEF";
            char buf[3];
            buf[0] = kHex[(v >> 4) & 0xF];
            buf[1] = kHex[v & 0xF];
            buf[2] = 0;
            Log(buf);
#else
            (void)Log;
            (void)v;
#endif
        }

        static void FatTraceU32(FLogFn Log, QC::u32 v)
        {
#if CITADEL_BOOT_FAT_TRACE
            if (!Log)
                return;
            char buf[12];
            QC::usize pos = 0;
            if (v == 0)
                buf[pos++] = '0';
            else
            {
                char tmp[10];
                QC::usize t = 0;
                while (v && t < sizeof(tmp))
                {
                    tmp[t++] = static_cast<char>('0' + (v % 10));
                    v /= 10;
                }
                while (t)
                    buf[pos++] = tmp[--t];
            }
            buf[pos] = 0;
            Log(buf);
#else
            (void)Log;
            (void)v;
#endif
        }

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

        struct FatLongNameEntry
        {
            QC::u8 order;
            QC::u16 name1[5];
            QC::u8 attributes;
            QC::u8 type;
            QC::u8 checksum;
            QC::u16 name2[6];
            QC::u16 firstClusterLow;
            QC::u16 name3[2];
        } __attribute__((packed));

        static QC::u8 Short11Checksum(const char short11[11])
        {
            QC::u8 sum = 0;
            for (int i = 0; i < 11; ++i)
                sum = static_cast<QC::u8>(((sum & 1) ? 0x80 : 0) + (sum >> 1) + static_cast<QC::u8>(short11[i]));
            return sum;
        }

        static bool AsciiEqIgnoreCaseN(const char *a, QC::usize aLen, const char *b)
        {
            if (!a || !b)
                return false;
            QC::usize i = 0;
            while (i < aLen && b[i])
            {
                char ca = a[i];
                char cb = b[i];
                if (ca >= 'A' && ca <= 'Z')
                    ca = static_cast<char>(ca + 32);
                if (cb >= 'A' && cb <= 'Z')
                    cb = static_cast<char>(cb + 32);
                if (ca != cb)
                    return false;
                ++i;
            }
            return (i == aLen) && (b[i] == 0);
        }

        static void LfnReset(char *buf, QC::usize cap, QC::u8 &checksum, bool &valid)
        {
            if (buf && cap)
                buf[0] = 0;
            checksum = 0;
            valid = false;
        }

        static void LfnPrepend(const char *fragment, char *pending, QC::usize cap)
        {
            if (!fragment || !pending || cap == 0)
                return;
            char combined[260];
            QC::String::memset(combined, 0, sizeof(combined));
            QC::String::strncpy(combined, fragment, sizeof(combined) - 1);
            combined[sizeof(combined) - 1] = 0;
            const QC::usize used = QC::String::strlen(combined);
            if (used + 1 < sizeof(combined))
            {
                QC::String::strncpy(combined + used, pending, sizeof(combined) - 1 - used);
                combined[sizeof(combined) - 1] = 0;
            }
            QC::String::strncpy(pending, combined, cap - 1);
            pending[cap - 1] = 0;
        }

        static void LfnConsume(const FatLongNameEntry &lfn, char *pending, QC::usize cap)
        {
            char frag[64];
            QC::String::memset(frag, 0, sizeof(frag));
            QC::usize out = 0;
            bool ended = false;

            auto emit = [&](QC::u16 ch)
            {
                if (ended)
                    return;
                if (ch == 0x0000)
                {
                    ended = true;
                    return;
                }
                if (ch == 0xFFFF)
                    return;
                char c = (ch <= 0x007F) ? static_cast<char>(ch & 0xFF) : '?';
                if (out + 1 < sizeof(frag))
                    frag[out++] = c;
            };

            for (int i = 0; i < 5; ++i)
                emit(lfn.name1[i]);
            for (int i = 0; i < 6; ++i)
                emit(lfn.name2[i]);
            for (int i = 0; i < 2; ++i)
                emit(lfn.name3[i]);

            frag[out] = 0;
            if (out)
                LfnPrepend(frag, pending, cap);
        }

        static bool FindNameInDirectory(const QC::u8 *image, QC::usize imageSize, const Fat32Layout &fat,
                                        QC::u32 dirCluster, const char *seg, QC::usize segLen, FLogFn Log,
                                        QC::u32 &outFirstCluster, QC::u32 &outFileSize, QC::u8 &outAttr)
        {
            outFirstCluster = 0;
            outFileSize = 0;
            outAttr = 0;

            if (!seg || segLen == 0)
                return false;

            // Fast path: strict 8.3 segment.
            char short11[11];
            if (SegmentToShort11(seg, short11))
                return FindShortNameInDirectory(image, imageSize, fat, dirCluster, short11, outFirstCluster, outFileSize, outAttr);

            // VFAT scan fallback (LFN + displayed SFN match).
            char pending[256];
            QC::u8 pendingChecksum = 0;
            bool pendingValid = false;
            LfnReset(pending, sizeof(pending), pendingChecksum, pendingValid);

            const QC::u32 bytesPerCluster = static_cast<QC::u32>(fat.clusterSizeBytes);
            QC::u32 cluster = dirCluster;
            const QC::usize maxClusters = static_cast<QC::usize>(imageSize / (fat.clusterSizeBytes ? fat.clusterSizeBytes : 1)) + 2;

            FatTrace(Log, "RamdiskFile: VFAT scan dirCluster=");
            FatTraceU32(Log, dirCluster);
            FatTrace(Log, " segLen=");
            FatTraceU32(Log, static_cast<QC::u32>(segLen));
            FatTrace(Log, "\r\n");

            for (QC::usize iter = 0; iter < maxClusters; ++iter)
            {
                QC::u64 clusterOffset = 0;
                if (!ClusterToOffset(fat, cluster, clusterOffset))
                    return false;
                if (clusterOffset + fat.clusterSizeBytes > imageSize)
                    return false;

                const QC::u8 *p = image + clusterOffset;
                for (QC::u32 off = 0; off + 32 <= bytesPerCluster; off += 32)
                {
                    const QC::u8 first = p[off + 0];
                    if (first == 0x00)
                        return false;
                    if (first == 0xE5)
                    {
                        LfnReset(pending, sizeof(pending), pendingChecksum, pendingValid);
                        continue;
                    }

                    const QC::u8 attr = p[off + 11];
                    if (attr == 0x0F)
                    {
                        const auto &lfn = *reinterpret_cast<const FatLongNameEntry *>(p + off);
                        if (lfn.type != 0)
                        {
                            LfnReset(pending, sizeof(pending), pendingChecksum, pendingValid);
                            continue;
                        }
                        if (lfn.order & 0x40)
                        {
                            LfnReset(pending, sizeof(pending), pendingChecksum, pendingValid);
                            pendingChecksum = lfn.checksum;
                            pendingValid = true;
                        }
                        if (pendingValid)
                            LfnConsume(lfn, pending, sizeof(pending));

                        FatTrace(Log, "RamdiskFile:  LFN order=");
                        FatTraceHexByte(Log, lfn.order);
                        FatTrace(Log, " chk=");
                        FatTraceHexByte(Log, lfn.checksum);
                        FatTrace(Log, " pending='");
                        FatTrace(Log, pending);
                        FatTrace(Log, "'\r\n");
                        continue;
                    }

                    const QC::u8 *ent = p + off;
                    const QC::u8 entAttr = ent[11];
                    if ((entAttr & 0x08) != 0)
                    {
                        LfnReset(pending, sizeof(pending), pendingChecksum, pendingValid);
                        continue;
                    }

                    // SFN displayed name formatter (NAME.EXT)
                    char sfnName[16];
                    QC::String::memset(sfnName, 0, sizeof(sfnName));
                    const char *sfn = reinterpret_cast<const char *>(ent + 0);
                    QC::usize out = 0;
                    for (int i = 0; i < 8 && out + 1 < sizeof(sfnName); ++i)
                    {
                        const char ch = sfn[i];
                        if (ch == ' ')
                            break;
                        sfnName[out++] = ch;
                    }
                    bool haveExt = false;
                    for (int i = 0; i < 3; ++i)
                    {
                        if (sfn[8 + i] != ' ')
                        {
                            haveExt = true;
                            break;
                        }
                    }
                    if (haveExt && out + 1 < sizeof(sfnName))
                        sfnName[out++] = '.';
                    if (haveExt)
                    {
                        for (int i = 0; i < 3 && out + 1 < sizeof(sfnName); ++i)
                        {
                            const char ch = sfn[8 + i];
                            if (ch == ' ')
                                break;
                            sfnName[out++] = ch;
                        }
                    }
                    sfnName[out] = 0;

                    if (pendingValid)
                    {
                        const QC::u8 chk = Short11Checksum(reinterpret_cast<const char *>(ent + 0));
                        if (chk == pendingChecksum)
                        {
                            if (AsciiEqIgnoreCaseN(seg, segLen, pending))
                            {
                                const QC::u16 hi = ReadLe16(ent + 20);
                                const QC::u16 lo = ReadLe16(ent + 26);
                                const QC::u32 firstCluster = (static_cast<QC::u32>(hi) << 16) | lo;
                                if (firstCluster < 2)
                                    return false;
                                outFirstCluster = firstCluster;
                                outFileSize = ReadLe32(ent + 28);
                                outAttr = entAttr;
                                return true;
                            }
                        }
                    }

                    if (AsciiEqIgnoreCaseN(seg, segLen, sfnName))
                    {
                        const QC::u16 hi = ReadLe16(ent + 20);
                        const QC::u16 lo = ReadLe16(ent + 26);
                        const QC::u32 firstCluster = (static_cast<QC::u32>(hi) << 16) | lo;
                        if (firstCluster < 2)
                            return false;
                        outFirstCluster = firstCluster;
                        outFileSize = ReadLe32(ent + 28);
                        outAttr = entAttr;
                        return true;
                    }

                    LfnReset(pending, sizeof(pending), pendingChecksum, pendingValid);
                }

                QC::u32 next = 0;
                if (!ReadFat32Entry(image, imageSize, fat, cluster, next))
                    return false;
                if (next >= 0x0FFFFFF8ul)
                    break;
                if (next < 2)
                    break;
                cluster = next;
            }

            return false;
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

        static bool ReadFileByPath(const QC::u8 *image, QC::usize imageSize, const Fat32Layout &fat, const char *Path, FLogFn Log,
                                   char *&outBuf, QC::usize &outLen)
        {
            outBuf = nullptr;
            outLen = 0;

            if (!Path)
                return false;

            const char *p = Path;
            while (*p == '/')
                ++p;
            if (!*p)
                return false;

            QC::u32 curDir = fat.rootCluster;

            while (*p)
            {
                const char *seg = p;
                QC::usize segLen = 0;
                while (p[segLen] && p[segLen] != '/')
                    ++segLen;
                if (segLen == 0)
                    return false;

                const bool last = (p[segLen] == 0);

                QC::u32 firstCluster = 0;
                QC::u32 fileSize = 0;
                QC::u8 attr = 0;
                if (!FindNameInDirectory(image, imageSize, fat, curDir, seg, segLen, Log, firstCluster, fileSize, attr))
                    return false;

                if (!last)
                {
                    if ((attr & 0x10) == 0)
                        return false;
                    curDir = firstCluster;
                    p += segLen;
                    while (*p == '/')
                        ++p;
                    continue;
                }

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

        if (!ReadFileByPath(image, imageSize, fat, Path, Log, OutBuf, OutLen))
        {
            LogStr(Log, "RamdiskFile: cannot open path: ");
            LogStr(Log, Path ? Path : "(null)");
            LogStr(Log, "\r\n");
            return false;
        }

        return true;
    }
}
