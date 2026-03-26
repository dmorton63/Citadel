#include "QKBootJsonSignature.h"

#include "QKBootJsonSigKey.h"
#include "QKBootSha256.h"

#include "Boot/Limine/QKBootLimineModules.h"
#include "Boot/TPM/QKBootTPMSecureStore.h"

#include "QCString.h"

namespace QK::Boot::Security
{
    namespace
    {
        // Enable verbose VFAT LFN tracing during development.
        // This is compiled out by default.
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

        static constexpr bool kProductionMode =
#if defined(CITADEL_PRODUCTION) && (CITADEL_PRODUCTION != 0)
            true;
#else
            false;
#endif

        static void LogStr(FLogFn Log, const char *Msg)
        {
            if (Log)
                Log(Msg);
        }

        static QC::usize StrLenOr0(const char *s)
        {
            return s ? static_cast<QC::usize>(QC::String::strlen(s)) : 0;
        }

        static void MeasureDigest(FLogFn Log, QC::u32 Pcr, const char *Label, const QC::u8 Digest[32])
        {
            if (!QK::Boot::Tpm::IsReady())
                return;
            if (!Digest)
                return;

            if (!QK::Boot::Tpm::ExtendPcrSha256Digest(Pcr, Digest, Log))
                return;

            if (Log && Label)
            {
                Log("TPM2: PCR ");
                char buf[12];
                QC::usize pos = 0;
                QC::u32 v = Pcr;
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
                Log(" extended: ");
                Log(Label);
                Log("\r\n");
            }
        }

        static void MeasureBytes(FLogFn Log, QC::u32 Pcr, const char *Label, const void *Data, QC::usize Len)
        {
            if (!QK::Boot::Tpm::IsReady())
                return;
            if (!Data || Len == 0)
                return;

            QC::u8 digest[32];
            Sha256(static_cast<const QC::u8 *>(Data), Len, digest);
            MeasureDigest(Log, Pcr, Label, digest);
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

        static bool FindShortNameInDirectory(const QC::u8 *image, QC::usize imageSize, const Fat32Layout &fat, QC::u32 dirCluster,
                                             const char short11[11], QC::u8 &outAttr, QC::u32 &outFirstCluster, QC::u32 &outFileSize)
        {
            outAttr = 0;
            outFirstCluster = 0;
            outFileSize = 0;

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

                    outAttr = attr;
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

        static bool FindShortNameInRoot(const QC::u8 *image, QC::usize imageSize, const Fat32Layout &fat, const char short11[11],
                                        QC::u32 &outFirstCluster, QC::u32 &outFileSize)
        {
            QC::u8 attr = 0;
            return FindShortNameInDirectory(image, imageSize, fat, fat.rootCluster, short11, attr, outFirstCluster, outFileSize);
        }

        static bool ReadFileByClusterChainWithMax(const QC::u8 *image, QC::usize imageSize, const Fat32Layout &fat, QC::u32 firstCluster,
                                                  QC::u32 fileSize, QC::usize maxBytes, QC::u8 *&outBuf, QC::usize &outLen)
        {
            outBuf = nullptr;
            outLen = 0;

            if (fileSize == 0)
                return false;
            if (maxBytes && fileSize > maxBytes)
                return false;

            QC::u8 *buffer = static_cast<QC::u8 *>(operator new[](static_cast<QC::usize>(fileSize)));
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

            outBuf = buffer;
            outLen = static_cast<QC::usize>(fileSize);
            return true;
        }

        static bool ReadFileByClusterChain(const QC::u8 *image, QC::usize imageSize, const Fat32Layout &fat, QC::u32 firstCluster,
                                           QC::u32 fileSize, QC::u8 *&outBuf, QC::usize &outLen)
        {
            return ReadFileByClusterChainWithMax(image, imageSize, fat, firstCluster, fileSize, 1024u * 64u, outBuf, outLen);
        }

        static bool SegmentToShort11(const char *seg, QC::usize segLen, char outShort11[11])
        {
            if (!seg || segLen == 0)
                return false;

            for (int i = 0; i < 11; ++i)
                outShort11[i] = ' ';

            QC::usize dot = segLen;
            for (QC::usize i = 0; i < segLen; ++i)
            {
                if (seg[i] == '.')
                {
                    dot = i;
                    break;
                }
            }

            const QC::usize nameLen = dot;
            const QC::usize extLen = (dot < segLen) ? (segLen - dot - 1) : 0;

            if (nameLen == 0 || nameLen > 8)
                return false;
            if (extLen > 3)
                return false;

            auto normalize = [](char c) -> char
            {
                if (c >= 'a' && c <= 'z')
                    c = static_cast<char>(c - 32);
                return c;
            };

            auto validChar = [](char c) -> bool
            {
                if (c >= 'A' && c <= 'Z')
                    return true;
                if (c >= 'a' && c <= 'z')
                    return true;
                if (c >= '0' && c <= '9')
                    return true;
                if (c == '_' || c == '-')
                    return true;
                return false;
            };

            for (QC::usize i = 0; i < nameLen; ++i)
            {
                char c = seg[i];
                if (!validChar(c))
                    return false;
                outShort11[i] = normalize(c);
            }

            for (QC::usize i = 0; i < extLen; ++i)
            {
                char c = seg[dot + 1 + i];
                if (!validChar(c))
                    return false;
                outShort11[8 + i] = normalize(c);
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
                                        QC::u32 dirCluster,
                                        const char *seg, QC::usize segLen,
                                        FLogFn Log,
                                        QC::u8 &outAttr, QC::u32 &outFirstCluster, QC::u32 &outFileSize)
        {
            if (!seg || segLen == 0)
                return false;

            FatTrace(Log, "FAT: FindNameInDirectory dirCluster=");
            FatTraceU32(Log, dirCluster);
            FatTrace(Log, " seg='");
#if CITADEL_BOOT_FAT_TRACE
            if (Log)
            {
                for (QC::usize i = 0; i < segLen; ++i)
                {
                    char c = seg[i];
                    if (c < 0x20 || c > 0x7E)
                        c = '?';
                    char b[2] = {c, 0};
                    Log(b);
                }
            }
#endif
            FatTrace(Log, "'\r\n");

            // Fast path: strict 8.3 segment.
            char short11[11];
            if (SegmentToShort11(seg, segLen, short11))
            {
                FatTrace(Log, "FAT: 8.3 fast path\r\n");
                return FindShortNameInDirectory(image, imageSize, fat, dirCluster, short11, outAttr, outFirstCluster, outFileSize);
            }

            // Fallback: scan directory and match VFAT long names.
            // This covers segments that are not representable in strict 8.3 (extra dots, longer names, etc.).
            char pending[256];
            QC::u8 pendingChecksum = 0;
            bool pendingValid = false;
            LfnReset(pending, sizeof(pending), pendingChecksum, pendingValid);

            const QC::u32 bytesPerCluster = static_cast<QC::u32>(fat.clusterSizeBytes);
            QC::u32 cluster = dirCluster;
            const QC::usize maxClusters = static_cast<QC::usize>(imageSize / (fat.clusterSizeBytes ? fat.clusterSizeBytes : 1)) + 2;

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

                        FatTrace(Log, "FAT:  LFN ent order=");
                        FatTraceHexByte(Log, lfn.order);
                        FatTrace(Log, " attr=");
                        FatTraceHexByte(Log, lfn.attributes);
                        FatTrace(Log, " type=");
                        FatTraceHexByte(Log, lfn.type);
                        FatTrace(Log, " chk=");
                        FatTraceHexByte(Log, lfn.checksum);
                        FatTrace(Log, " pendingValid=");
                        FatTrace(Log, pendingValid ? "1" : "0");
                        FatTrace(Log, " pending='");
                        FatTrace(Log, pending);
                        FatTrace(Log, "'\r\n");
                        continue;
                    }

                    // Regular SFN entry.
                    const QC::u8 *ent = p + off;
                    const QC::u8 entAttr = ent[11];
                    if ((entAttr & 0x08) != 0)
                    {
                        LfnReset(pending, sizeof(pending), pendingChecksum, pendingValid);
                        continue;
                    }

                    // If we don't have a valid LFN chain, still allow matching the displayed SFN name
                    // for segments that failed strict 8.3 parsing (e.g., case/punctuation differences).
                    if (!pendingValid)
                    {
                        char sfnName[16];
                        QC::String::memset(sfnName, 0, sizeof(sfnName));
                        // Minimal SFN (11-byte) to display name formatter: trims spaces, inserts '.' if ext present.
                        const char *sfn = reinterpret_cast<const char *>(ent + 0);
                        QC::usize out = 0;
                        // base 8
                        for (int i = 0; i < 8 && out + 1 < sizeof(sfnName); ++i)
                        {
                            const char ch = sfn[i];
                            if (ch == ' ')
                                break;
                            sfnName[out++] = ch;
                        }
                        // ext 3
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

                        FatTrace(Log, "FAT:  SFN entry raw='");
#if CITADEL_BOOT_FAT_TRACE
                        if (Log)
                        {
                            for (int i = 0; i < 11; ++i)
                            {
                                char c = reinterpret_cast<const char *>(ent + 0)[i];
                                if (c < 0x20 || c > 0x7E)
                                    c = '?';
                                char b[2] = {c, 0};
                                Log(b);
                            }
                        }
#endif
                        FatTrace(Log, "' disp='");
                        FatTrace(Log, sfnName);
                        FatTrace(Log, "'\r\n");

                        if (AsciiEqIgnoreCaseN(seg, segLen, sfnName))
                        {
                            outAttr = entAttr;
                            const QC::u16 hi = ReadLe16(ent + 20);
                            const QC::u16 lo = ReadLe16(ent + 26);
                            outFirstCluster = (static_cast<QC::u32>(hi) << 16) | lo;
                            outFileSize = ReadLe32(ent + 28);
                            return true;
                        }
                    }

                    if (pendingValid)
                    {
                        const QC::u8 chk = Short11Checksum(reinterpret_cast<const char *>(ent + 0));
                        if (chk == pendingChecksum)
                        {
                            // Compare against requested segment.
                            FatTrace(Log, "FAT:  LFN terminal SFN checksum matches; compare seg vs pending: seg='\r\n");
                            if (AsciiEqIgnoreCaseN(seg, segLen, pending))
                            {
                                outAttr = entAttr;
                                const QC::u16 hi = ReadLe16(ent + 20);
                                const QC::u16 lo = ReadLe16(ent + 26);
                                outFirstCluster = (static_cast<QC::u32>(hi) << 16) | lo;
                                outFileSize = ReadLe32(ent + 28);
                                return true;
                            }
                        }
                        else
                        {
                            FatTrace(Log, "FAT:  LFN terminal SFN checksum mismatch (discard)\r\n");
                        }
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

        static bool ReadFileFromLimineRamdiskPath(QC::u64 ModuleRequest[], const char *path, FLogFn Log, QC::u8 *&outBuf, QC::usize &outLen)
        {
            outBuf = nullptr;
            outLen = 0;

            const limine_file *Ramdisk = QK::Boot::Limine::FindRamdiskModule(ModuleRequest);
            if (!Ramdisk || !Ramdisk->address || Ramdisk->size < 512)
                return false;

            const QC::u8 *image = static_cast<const QC::u8 *>(Ramdisk->address);
            const QC::usize imageSize = static_cast<QC::usize>(Ramdisk->size);

            Fat32Layout fat{};
            if (!ParseFat32(image, imageSize, fat))
                return false;

            if (!path)
                return false;

            FatTrace(Log, "FAT: ReadFileFromLimineRamdiskPath path='");
            FatTrace(Log, path ? path : "(null)");
            FatTrace(Log, "'\r\n");

            // Skip leading slashes.
            const char *p = path;
            while (*p == '/')
                ++p;
            if (!*p)
                return false;

            QC::u32 dirCluster = fat.rootCluster;
            for (;;)
            {
                const char *seg = p;
                QC::usize segLen = 0;
                while (p[segLen] && p[segLen] != '/')
                    ++segLen;

                if (segLen == 0)
                    return false;

                const bool last = (p[segLen] == 0);
                QC::u8 attr = 0;
                QC::u32 firstCluster = 0;
                QC::u32 fileSize = 0;
                if (!FindNameInDirectory(image, imageSize, fat, dirCluster, seg, segLen, Log, attr, firstCluster, fileSize))
                    return false;

                if (last)
                {
                    if (attr & 0x10)
                        return false;
                    return ReadFileByClusterChainWithMax(image, imageSize, fat, firstCluster, fileSize, 1024u * 1024u, outBuf, outLen);
                }

                if ((attr & 0x10) == 0)
                    return false;

                // Advance to next segment.
                p += segLen;
                while (*p == '/')
                    ++p;
                if (!*p)
                    return false;

                dirCluster = firstCluster;
            }
        }

        static bool AsciiEqIgnoreCase(const char *a, const char *b)
        {
            if (!a || !b)
                return false;
            while (*a && *b)
            {
                char ca = *a;
                char cb = *b;
                if (ca >= 'A' && ca <= 'Z')
                    ca = static_cast<char>(ca + 32);
                if (cb >= 'A' && cb <= 'Z')
                    cb = static_cast<char>(cb + 32);
                if (ca != cb)
                    return false;
                ++a;
                ++b;
            }
            return (*a == 0) && (*b == 0);
        }

        static bool AsciiStartsWithIgnoreCase(const char *s, const char *prefix)
        {
            if (!s || !prefix)
                return false;
            while (*prefix)
            {
                char a = *s;
                char b = *prefix;
                if (!a)
                    return false;
                if (a >= 'A' && a <= 'Z')
                    a = static_cast<char>(a + 32);
                if (b >= 'A' && b <= 'Z')
                    b = static_cast<char>(b + 32);
                if (a != b)
                    return false;
                ++s;
                ++prefix;
            }
            return true;
        }

        static const char *FindSubstrIgnoreCase(const char *haystack, const char *needle)
        {
            if (!haystack || !needle || !*needle)
                return nullptr;
            for (const char *p = haystack; *p; ++p)
            {
                const char *a = p;
                const char *b = needle;
                while (*a && *b)
                {
                    char ca = *a;
                    char cb = *b;
                    if (ca >= 'A' && ca <= 'Z')
                        ca = static_cast<char>(ca + 32);
                    if (cb >= 'A' && cb <= 'Z')
                        cb = static_cast<char>(cb + 32);
                    if (ca != cb)
                        break;
                    ++a;
                    ++b;
                }
                if (*b == 0)
                    return p;
            }
            return nullptr;
        }

        static const char *SkipWs(const char *p)
        {
            while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
                ++p;
            return p;
        }

        static const char *FindChar(const char *s, char c)
        {
            if (!s)
                return nullptr;
            for (const char *p = s; *p; ++p)
            {
                if (*p == c)
                    return p;
            }
            return nullptr;
        }

        static const char *FindLastChar(const char *s, char c)
        {
            if (!s)
                return nullptr;
            const char *last = nullptr;
            for (const char *p = s; *p; ++p)
            {
                if (*p == c)
                    last = p;
            }
            return last;
        }

        static bool ParseAttrValueInTag(const char *tagBegin, const char *tagEnd, const char *attrName,
                                        const char *&outValueBegin, QC::usize &outValueLen)
        {
            outValueBegin = nullptr;
            outValueLen = 0;
            if (!tagBegin || !tagEnd || tagEnd <= tagBegin || !attrName || !*attrName)
                return false;

            const QC::usize nameLen = static_cast<QC::usize>(QC::String::strlen(attrName));

            for (const char *p = tagBegin; p < tagEnd; ++p)
            {
                // Find start of a name token.
                if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '<' || *p == '/')
                    continue;

                // Compare attr name ignoring case.
                const char *n = p;
                QC::usize matched = 0;
                while ((n + matched) < tagEnd && matched < nameLen)
                {
                    char ca = n[matched];
                    char cb = attrName[matched];
                    if (ca >= 'A' && ca <= 'Z')
                        ca = static_cast<char>(ca + 32);
                    if (cb >= 'A' && cb <= 'Z')
                        cb = static_cast<char>(cb + 32);
                    if (ca != cb)
                        break;
                    ++matched;
                }
                if (matched != nameLen)
                    continue;

                const char *q = n + matched;
                if (q >= tagEnd)
                    continue;

                // Ensure token boundary.
                if (!(*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n' || *q == '='))
                    continue;

                q = SkipWs(q);
                if (q >= tagEnd || *q != '=')
                    continue;
                ++q;
                q = SkipWs(q);
                if (q >= tagEnd)
                    return false;

                if (*q == '"' || *q == '\'')
                {
                    const char quote = *q++;
                    const char *v = q;
                    while (q < tagEnd && *q && *q != quote)
                        ++q;
                    outValueBegin = v;
                    outValueLen = static_cast<QC::usize>(q - v);
                    return true;
                }
                else
                {
                    const char *v = q;
                    while (q < tagEnd && *q && *q != ' ' && *q != '\t' && *q != '\r' && *q != '\n' && *q != '>' && *q != '/')
                        ++q;
                    outValueBegin = v;
                    outValueLen = static_cast<QC::usize>(q - v);
                    return true;
                }
            }

            return false;
        }

        static bool IsHttpLikeUrl(const char *s, QC::usize len)
        {
            if (!s || len == 0)
                return false;
            // Compare prefixes case-insensitively.
            if (len >= 7 && AsciiStartsWithIgnoreCase(s, "http://"))
                return true;
            if (len >= 8 && AsciiStartsWithIgnoreCase(s, "https://"))
                return true;
            return false;
        }

        static bool AddUniquePath(char paths[][160], QC::usize &count, QC::usize capacity, const char *p, QC::usize len)
        {
            if (!p || len == 0)
                return false;
            if (len >= 160)
                len = 159;

            // Only enforce absolute paths.
            if (p[0] != '/')
                return false;

            for (QC::usize i = 0; i < count; ++i)
            {
                if (QC::String::strlen(paths[i]) == len && QC::String::memcmp(paths[i], p, len) == 0)
                    return true;
            }

            if (count >= capacity)
                return false;

            QC::String::memcpy(paths[count], p, len);
            paths[count][len] = 0;
            ++count;
            return true;
        }

        static QC::usize CollectCuimlssImportsFromCmlText(const char *text, char outPaths[][160], QC::usize capacity)
        {
            if (!text || !capacity)
                return 0;

            QC::usize count = 0;
            for (QC::usize i = 0; i < capacity; ++i)
                outPaths[i][0] = 0;

            // 1) <ImportStyle src="..."/>
            const char *p = text;
            while ((p = FindSubstrIgnoreCase(p, "<importstyle")) != nullptr)
            {
                const char *tagEnd = FindChar(p, '>');
                if (!tagEnd)
                    break;

                const char *v = nullptr;
                QC::usize vlen = 0;
                if (ParseAttrValueInTag(p, tagEnd, "src", v, vlen))
                {
                    if (!IsHttpLikeUrl(v, vlen))
                        (void)AddUniquePath(outPaths, count, capacity, v, vlen);
                }

                p = tagEnd + 1;
            }

            // 2) <link rel="stylesheet" type="text/cuimlss" href="...">
            p = text;
            while ((p = FindSubstrIgnoreCase(p, "<link")) != nullptr)
            {
                const char *tagEnd = FindChar(p, '>');
                if (!tagEnd)
                    break;

                const char *rel = nullptr;
                QC::usize relLen = 0;
                const char *type = nullptr;
                QC::usize typeLen = 0;
                const char *href = nullptr;
                QC::usize hrefLen = 0;

                (void)ParseAttrValueInTag(p, tagEnd, "rel", rel, relLen);
                (void)ParseAttrValueInTag(p, tagEnd, "type", type, typeLen);
                (void)ParseAttrValueInTag(p, tagEnd, "href", href, hrefLen);

                bool relOk = false;
                bool typeOk = false;

                if (rel && relLen)
                {
                    char tmp[24];
                    const QC::usize n = (relLen < (sizeof(tmp) - 1)) ? relLen : (sizeof(tmp) - 1);
                    QC::String::memcpy(tmp, rel, n);
                    tmp[n] = 0;
                    relOk = AsciiEqIgnoreCase(tmp, "stylesheet");
                }
                if (type && typeLen)
                {
                    char tmp[32];
                    const QC::usize n = (typeLen < (sizeof(tmp) - 1)) ? typeLen : (sizeof(tmp) - 1);
                    QC::String::memcpy(tmp, type, n);
                    tmp[n] = 0;
                    typeOk = AsciiEqIgnoreCase(tmp, "text/cuimlss");
                }

                if ((!rel || relOk) && (!type || typeOk) && href && hrefLen)
                {
                    if (!IsHttpLikeUrl(href, hrefLen))
                        (void)AddUniquePath(outPaths, count, capacity, href, hrefLen);
                }

                p = tagEnd + 1;
            }

            return count;
        }

        static void DeriveSigPathFromTarget(const char *targetPath, char outSigPath[256])
        {
            outSigPath[0] = 0;
            if (!targetPath || !*targetPath)
                return;

            const QC::usize len = static_cast<QC::usize>(QC::String::strlen(targetPath));
            const QC::usize copyLen = (len < 255) ? len : 255;
            QC::String::memcpy(outSigPath, targetPath, copyLen);
            outSigPath[copyLen] = 0;

            // Replace extension with .SIG if possible.
            QC::isize lastSlash = -1;
            QC::isize lastDot = -1;
            for (QC::usize i = 0; i < copyLen; ++i)
            {
                if (outSigPath[i] == '/')
                    lastSlash = static_cast<QC::isize>(i);
                if (outSigPath[i] == '.')
                    lastDot = static_cast<QC::isize>(i);
            }

            if (lastDot > lastSlash)
            {
                QC::usize pos = static_cast<QC::usize>(lastDot + 1);
                outSigPath[pos + 0] = 'S';
                outSigPath[pos + 1] = 'I';
                outSigPath[pos + 2] = 'G';
                outSigPath[pos + 3] = 0;
            }
            else
            {
                // Append if no dot.
                if (copyLen + 4 < 256)
                {
                    outSigPath[copyLen + 0] = '.';
                    outSigPath[copyLen + 1] = 'S';
                    outSigPath[copyLen + 2] = 'I';
                    outSigPath[copyLen + 3] = 'G';
                    outSigPath[copyLen + 4] = 0;
                }
            }
        }

        static bool VerifySignedFileFromLimineRamdiskPath(FLogFn Log, QC::u64 ModuleRequest[], const char *targetPath, const char *sigPath,
                                                          bool enforce)
        {
            if (!targetPath || !sigPath)
                return !enforce;

            QC::u8 *buf = nullptr;
            QC::usize len = 0;
            if (!ReadFileFromLimineRamdiskPath(ModuleRequest, targetPath, Log, buf, len))
            {
                if (enforce)
                {
                    LogStr(Log, "DesktopSig: referenced file missing; refusing (production mode)\r\n");
                    return false;
                }
                LogStr(Log, "DesktopSig: referenced file missing; continuing (dev mode)\r\n");
                return true;
            }

            QC::u8 *sigBuf = nullptr;
            QC::usize sigLen = 0;
            const bool haveSig = ReadFileFromLimineRamdiskPath(ModuleRequest, sigPath, Log, sigBuf, sigLen);
            if (!haveSig)
            {
                operator delete[](buf);
                if (enforce)
                {
                    LogStr(Log, "DesktopSig: signature missing; refusing (production mode)\r\n");
                    return false;
                }
                LogStr(Log, "DesktopSig: signature missing; continuing (dev mode)\r\n");
                return true;
            }

            if (sigLen != 256)
            {
                operator delete[](buf);
                operator delete[](sigBuf);
                if (enforce)
                {
                    LogStr(Log, "DesktopSig: signature wrong size; refusing (production mode)\r\n");
                    return false;
                }
                LogStr(Log, "DesktopSig: signature wrong size; continuing (dev mode)\r\n");
                return true;
            }

            QC::u8 digest[32];
            Sha256(buf, len, digest);
            const bool ok = QK::Boot::Tpm::VerifyRsa2048RsassaSha256Digest(kBootJsonRsa2048Modulus, digest, sigBuf, Log);

            operator delete[](buf);
            operator delete[](sigBuf);

            if (ok)
                return true;

            if (enforce)
            {
                LogStr(Log, "DesktopSig: signature invalid; refusing (production mode)\r\n");
                return false;
            }
            LogStr(Log, "DesktopSig: signature invalid; continuing (dev mode)\r\n");
            return true;
        }

        static bool VerifySignedFileFromLimineRamdiskPathWithPolicy(FLogFn Log, QC::u64 ModuleRequest[], const char *targetPath, SigPolicy Policy)
        {
            char sigPath[256]{};
            DeriveSigPathFromTarget(targetPath, sigPath);
            const bool enforce = (Policy == SigPolicy::RequireValid);

            // Primary: derived .SIG path.
            if (VerifySignedFileFromLimineRamdiskPath(Log, ModuleRequest, targetPath, sigPath, enforce))
                return true;

            // Fallback: allow lowercase .sig convention if the caller provided it on disk.
            // This is useful for manual authoring on case-sensitive filesystems.
            char sigLower[256]{};
            QC::String::strncpy(sigLower, sigPath, sizeof(sigLower) - 1);
            sigLower[sizeof(sigLower) - 1] = 0;

            const QC::usize n = static_cast<QC::usize>(QC::String::strlen(sigLower));
            if (n >= 4)
            {
                sigLower[n - 3] = 's';
                sigLower[n - 2] = 'i';
                sigLower[n - 1] = 'g';
            }

            return VerifySignedFileFromLimineRamdiskPath(Log, ModuleRequest, targetPath, sigLower, enforce);
        }

        static bool ReadFileFromLimineRamdisk(QC::u64 ModuleRequest[], const char short11[11], QC::u8 *&outBuf, QC::usize &outLen)
        {
            outBuf = nullptr;
            outLen = 0;

            const limine_file *Ramdisk = QK::Boot::Limine::FindRamdiskModule(ModuleRequest);
            if (!Ramdisk || !Ramdisk->address || Ramdisk->size < 512)
                return false;

            const QC::u8 *image = static_cast<const QC::u8 *>(Ramdisk->address);
            const QC::usize imageSize = static_cast<QC::usize>(Ramdisk->size);

            Fat32Layout fat{};
            if (!ParseFat32(image, imageSize, fat))
                return false;

            QC::u32 firstCluster = 0;
            QC::u32 fileSize = 0;
            if (!FindShortNameInRoot(image, imageSize, fat, short11, firstCluster, fileSize))
                return false;

            return ReadFileByClusterChain(image, imageSize, fat, firstCluster, fileSize, outBuf, outLen);
        }
    }

    bool VerifyBootJsnSignatureFromLimineRamdiskModule(FLogFn Log, QC::u64 ModuleRequest[], QC::u64 ExecutableFileRequest[])
    {
        static constexpr char kBootJsn[11] = {'B', 'O', 'O', 'T', ' ', ' ', ' ', ' ', 'J', 'S', 'N'};
        static constexpr char kBootSig[11] = {'B', 'O', 'O', 'T', ' ', ' ', ' ', ' ', 'S', 'I', 'G'};
        static constexpr char kSysCfgJsn[11] = {'S', 'Y', 'S', 'C', 'F', 'G', ' ', ' ', 'J', 'S', 'N'};
        static constexpr char kSysCfgSig[11] = {'S', 'Y', 'S', 'C', 'F', 'G', ' ', ' ', 'S', 'I', 'G'};

        if (kProductionMode)
            LogStr(Log, "BootSig: mode=production\r\n");
        else
            LogStr(Log, "BootSig: mode=development\r\n");

        // Measurement-only (no refusal paths):
        // - PCR7: kernel cmdline
        // - PCR16: kernel executable bytes
        // - PCR16: Limine module list metadata
        if (QK::Boot::Tpm::IsReady())
        {
            if (ExecutableFileRequest)
            {
                const auto *resp = reinterpret_cast<const limine_executable_file_response *>(ExecutableFileRequest[5]);
                const limine_file *kf = resp ? resp->executable_file : nullptr;
                if (kf)
                {
                    if (kf->cmdline)
                        MeasureBytes(Log, 7, "kernel cmdline", kf->cmdline, StrLenOr0(kf->cmdline));
                    if (kf->address && kf->size)
                        MeasureBytes(Log, 16, "kernel image", kf->address, static_cast<QC::usize>(kf->size));
                }
            }

            const limine_module_response *mods = QK::Boot::Limine::GetModuleResponse(ModuleRequest);
            if (mods && mods->module_count && mods->modules)
            {
                const QC::u64 count = mods->module_count;
                const QC::usize digestsLen = static_cast<QC::usize>(count) * 32u;
                QC::u8 *digests = static_cast<QC::u8 *>(operator new[](digestsLen));
                QC::usize wrote = 0;

                for (QC::u64 i = 0; i < count; ++i)
                {
                    const limine_file *m = mods->modules[i];
                    if (!m)
                        continue;

                    const QC::usize pathLen = StrLenOr0(m->path);
                    const QC::usize cmdLen = StrLenOr0(m->cmdline);
                    const QC::usize bufLen = 4 + pathLen + 1 + cmdLen + 1 + 8;
                    QC::u8 *buf = static_cast<QC::u8 *>(operator new[](bufLen));
                    QC::usize off = 0;

                    buf[off++] = 'M';
                    buf[off++] = 'O';
                    buf[off++] = 'D';
                    buf[off++] = 0;

                    if (pathLen)
                    {
                        QC::String::memcpy(buf + off, m->path, pathLen);
                        off += pathLen;
                    }
                    buf[off++] = 0;

                    if (cmdLen)
                    {
                        QC::String::memcpy(buf + off, m->cmdline, cmdLen);
                        off += cmdLen;
                    }
                    buf[off++] = 0;

                    QC::u64 sz = m->size;
                    for (int b = 0; b < 8; ++b)
                    {
                        buf[off++] = static_cast<QC::u8>(sz & 0xFFu);
                        sz >>= 8;
                    }

                    QC::u8 d[32];
                    Sha256(buf, bufLen, d);
                    operator delete[](buf);

                    if (wrote + 32u <= digestsLen)
                    {
                        QC::String::memcpy(digests + wrote, d, 32);
                        wrote += 32;
                    }
                }

                if (wrote)
                {
                    QC::u8 listDigest[32];
                    Sha256(digests, wrote, listDigest);
                    MeasureDigest(Log, 16, "limine modules", listDigest);
                }

                operator delete[](digests);
            }
        }

        QC::u8 *bootBuf = nullptr;
        QC::usize bootLen = 0;
        if (!ReadFileFromLimineRamdisk(ModuleRequest, kBootJsn, bootBuf, bootLen))
        {
            // No BOOT.JSN => nothing to verify in dev.
            if (!kProductionMode)
                return true;

            LogStr(Log, "BootSig: BOOT.JSN missing; refusing (production mode)\r\n");
            return false;
        }

        QC::u8 *sigBuf = nullptr;
        QC::usize sigLen = 0;
        const bool haveSig = ReadFileFromLimineRamdisk(ModuleRequest, kBootSig, sigBuf, sigLen);

        if (!haveSig)
        {
            operator delete[](bootBuf);
            if (!kProductionMode)
            {
                LogStr(Log, "BootSig: BOOT.SIG missing; continuing (dev mode)\r\n");
                return true;
            }

            LogStr(Log, "BootSig: BOOT.SIG missing; refusing (production mode)\r\n");
            return false;
        }

        if (sigLen != 256)
        {
            operator delete[](bootBuf);
            operator delete[](sigBuf);
            LogStr(Log, "BootSig: BOOT.SIG wrong size; expected 256 bytes\r\n");
            return false;
        }

        if (!QK::Boot::Tpm::IsReady())
        {
            operator delete[](bootBuf);
            operator delete[](sigBuf);
            if (!kProductionMode)
            {
                LogStr(Log, "BootSig: TPM not ready; skipping BOOT.SIG verification (dev mode)\r\n");
                return true;
            }

            LogStr(Log, "BootSig: TPM not ready; refusing (production mode)\r\n");
            return false;
        }

        QC::u8 digest[32];
        Sha256(bootBuf, bootLen, digest);

        // Measurement: record BOOT.JSN into PCR7 for attestation.
        MeasureDigest(Log, 7, "BOOT.JSN", digest);

        // Measurement: record BOOT.SIG into PCR7.
        {
            QC::u8 sigDigest[32];
            Sha256(sigBuf, sigLen, sigDigest);
            MeasureDigest(Log, 7, "BOOT.SIG", sigDigest);
        }

        const bool ok = QK::Boot::Tpm::VerifyRsa2048RsassaSha256Digest(kBootJsonRsa2048Modulus, digest, sigBuf, Log);

        operator delete[](bootBuf);
        operator delete[](sigBuf);

        if (!ok)
        {
            LogStr(Log, "BootSig: signature invalid\r\n");
            return false;
        }

        LogStr(Log, "BootSig: signature valid\r\n");

        // Next-layer root config index (optional in dev, required in production).
        QC::u8 *sysBuf = nullptr;
        QC::usize sysLen = 0;
        if (!ReadFileFromLimineRamdisk(ModuleRequest, kSysCfgJsn, sysBuf, sysLen))
        {
            if (!kProductionMode)
            {
                LogStr(Log, "BootSig: SYSCFG.JSN missing; continuing (dev mode)\r\n");
                return true;
            }

            LogStr(Log, "BootSig: SYSCFG.JSN missing; refusing (production mode)\r\n");
            return false;
        }

        QC::u8 *sysSigBuf = nullptr;
        QC::usize sysSigLen = 0;
        const bool haveSysSig = ReadFileFromLimineRamdisk(ModuleRequest, kSysCfgSig, sysSigBuf, sysSigLen);
        if (!haveSysSig)
        {
            operator delete[](sysBuf);
            if (!kProductionMode)
            {
                LogStr(Log, "BootSig: SYSCFG.SIG missing; continuing (dev mode)\r\n");
                return true;
            }

            LogStr(Log, "BootSig: SYSCFG.SIG missing; refusing (production mode)\r\n");
            return false;
        }

        if (sysSigLen != 256)
        {
            operator delete[](sysBuf);
            operator delete[](sysSigBuf);
            LogStr(Log, "BootSig: SYSCFG.SIG wrong size; expected 256 bytes\r\n");
            return false;
        }

        if (!QK::Boot::Tpm::IsReady())
        {
            operator delete[](sysBuf);
            operator delete[](sysSigBuf);
            if (!kProductionMode)
            {
                LogStr(Log, "BootSig: TPM not ready; skipping SYSCFG.SIG verification (dev mode)\r\n");
                return true;
            }

            LogStr(Log, "BootSig: TPM not ready; refusing (production mode)\r\n");
            return false;
        }

        QC::u8 sysDigest[32];
        Sha256(sysBuf, sysLen, sysDigest);
        MeasureDigest(Log, 7, "SYSCFG.JSN", sysDigest);

        {
            QC::u8 sigDigest[32];
            Sha256(sysSigBuf, sysSigLen, sigDigest);
            MeasureDigest(Log, 7, "SYSCFG.SIG", sigDigest);
        }

        const bool sysOk = QK::Boot::Tpm::VerifyRsa2048RsassaSha256Digest(kBootJsonRsa2048Modulus, sysDigest, sysSigBuf, Log);

        operator delete[](sysBuf);
        operator delete[](sysSigBuf);

        if (!sysOk)
        {
            LogStr(Log, "BootSig: SYSCFG signature invalid\r\n");
            return false;
        }

        LogStr(Log, "BootSig: SYSCFG signature valid\r\n");
        return true;
    }

    bool VerifyDesktopCmlSignatureFromLimineRamdiskModule(FLogFn Log, QC::u64 ModuleRequest[])
    {
        static constexpr const char *kDesktopCandidates[] = {
            "/SYSTEM/UI/DESKTOP.CML",
            "/PROD/DESKTOP.CML",
            "/GOLDEN/DESKTOP.CML",
            "/DESKTOP.CML",
        };

        static constexpr const char *kSigCandidates[] = {
            "/SYSTEM/UI/DESKTOP.SIG",
            "/PROD/DESKTOP.SIG",
            "/GOLDEN/DESKTOP.SIG",
            "/DESKTOP.SIG",
        };

        const bool enforce = kProductionMode;
        if (enforce)
            LogStr(Log, "DesktopSig: enforcement=on\r\n");
        else
            LogStr(Log, "DesktopSig: enforcement=off\r\n");

        if (!QK::Boot::Tpm::IsReady())
        {
            if (!enforce)
            {
                LogStr(Log, "DesktopSig: TPM not ready; skipping verification (dev mode)\r\n");
                return true;
            }

            LogStr(Log, "DesktopSig: TPM not ready; refusing (production mode)\r\n");
            return false;
        }

        bool sawDesktop = false;
        for (QC::usize i = 0; i < (sizeof(kDesktopCandidates) / sizeof(kDesktopCandidates[0])); ++i)
        {
            QC::u8 *desktopBuf = nullptr;
            QC::usize desktopLen = 0;
            if (!ReadFileFromLimineRamdiskPath(ModuleRequest, kDesktopCandidates[i], Log, desktopBuf, desktopLen))
                continue;

            sawDesktop = true;

            QC::u8 *sigBuf = nullptr;
            QC::usize sigLen = 0;
            const bool haveSig = ReadFileFromLimineRamdiskPath(ModuleRequest, kSigCandidates[i], Log, sigBuf, sigLen);

            if (!haveSig)
            {
                operator delete[](desktopBuf);
                if (!enforce)
                {
                    LogStr(Log, "DesktopSig: DESKTOP.SIG missing; continuing (dev mode)\r\n");
                    return true;
                }

                LogStr(Log, "DesktopSig: DESKTOP.SIG missing; refusing (production mode)\r\n");
                return false;
            }

            if (sigLen != 256)
            {
                operator delete[](desktopBuf);
                operator delete[](sigBuf);
                LogStr(Log, "DesktopSig: DESKTOP.SIG wrong size; expected 256 bytes\r\n");
                if (!enforce)
                    return true;
                return false;
            }

            QC::u8 digest[32];
            Sha256(desktopBuf, desktopLen, digest);

            // Measurement: record the chosen desktop definition into PCR7 for attestation.
            MeasureDigest(Log, 7, "DESKTOP.CML", digest);

            const bool ok = QK::Boot::Tpm::VerifyRsa2048RsassaSha256Digest(kBootJsonRsa2048Modulus, digest, sigBuf, Log);

            // Keep desktopBuf around until we finish verifying referenced imports.
            operator delete[](sigBuf);

            if (ok)
            {
                LogStr(Log, "DesktopSig: signature valid\r\n");

                // Parse the desktop text for any CUIMLSS imports, and enforce signatures for each referenced .CXS.
                char *desktopText = static_cast<char *>(operator new[](desktopLen + 1));
                QC::String::memcpy(desktopText, desktopBuf, desktopLen);
                desktopText[desktopLen] = 0;

                static constexpr QC::usize kMaxImports = 32;
                char imports[kMaxImports][160];
                const QC::usize importCount = CollectCuimlssImportsFromCmlText(desktopText, imports, kMaxImports);
                operator delete[](desktopText);

                bool importsOk = true;
                for (QC::usize imp = 0; imp < importCount; ++imp)
                {
                    const char *stylePath = imports[imp];
                    if (!stylePath || !*stylePath)
                        continue;

                    // Only enforce for paths that look like stylesheets.
                    const char *dot = FindLastChar(stylePath, '.');
                    if (!dot)
                        continue;
                    if (!AsciiEqIgnoreCase(dot + 1, "cxs"))
                        continue;

                    char sigPath[256];
                    DeriveSigPathFromTarget(stylePath, sigPath);
                    if (!sigPath[0])
                        continue;

                    if (!VerifySignedFileFromLimineRamdiskPath(Log, ModuleRequest, stylePath, sigPath, enforce))
                    {
                        importsOk = false;
                        if (enforce)
                            break;
                    }
                }

                operator delete[](desktopBuf);
                if (!importsOk)
                    return false;
                return true;
            }

            operator delete[](desktopBuf);

            LogStr(Log, "DesktopSig: signature invalid\r\n");
            if (!enforce)
                return true;

            // Continue to next candidate if present (fallback behavior).
        }

        if (!sawDesktop)
        {
            LogStr(Log, "DesktopSig: no DESKTOP.CML found; continuing\r\n");
            return true;
        }

        if (!enforce)
            return true;

        LogStr(Log, "DesktopSig: no valid signed desktop found; refusing (production mode)\r\n");
        return false;
    }

    bool VerifySignedFileFromLimineRamdiskPath(FLogFn Log, QC::u64 ModuleRequest[], const char *RamdiskPath, SigPolicy Policy)
    {
        if (!RamdiskPath || !*RamdiskPath)
        {
            LogStr(Log, "VerifySignedFileFromLimineRamdiskPath: invalid path\r\n");
            return Policy == SigPolicy::WarnOnly;
        }

        return VerifySignedFileFromLimineRamdiskPathWithPolicy(Log, ModuleRequest, RamdiskPath, Policy);
    }
}
