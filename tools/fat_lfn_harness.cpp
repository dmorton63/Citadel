#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Minimal standalone harness that reproduces the early-boot VFAT LFN resolution
// logic against a built ramdisk image.
//
// Build:
//   g++ -std=c++20 -O2 -DCITADEL_BOOT_FAT_TRACE=1 tools/fat_lfn_harness.cpp -o build/fat_lfn_harness
// Run:
//   ./build/fat_lfn_harness iso/modules/ramdisk.img /PROD/MODULES/fs.mod.module.json

namespace
{
    using u8 = uint8_t;
    using u16 = uint16_t;
    using u32 = uint32_t;
    using u64 = uint64_t;
    using usize = size_t;

#ifndef CITADEL_BOOT_FAT_TRACE
#define CITADEL_BOOT_FAT_TRACE 0
#endif

    using LogFn = void (*)(const char *);

    static void StdoutLog(const char *msg)
    {
        if (msg)
            std::fputs(msg, stdout);
    }

    static void Trace(LogFn log, const char *msg)
    {
#if CITADEL_BOOT_FAT_TRACE
        if (log && msg)
            log(msg);
#else
        (void)log;
        (void)msg;
#endif
    }

    static void TraceHexByte(LogFn log, u8 v)
    {
#if CITADEL_BOOT_FAT_TRACE
        if (!log)
            return;
        static const char kHex[] = "0123456789ABCDEF";
        char buf[3];
        buf[0] = kHex[(v >> 4) & 0xF];
        buf[1] = kHex[v & 0xF];
        buf[2] = 0;
        log(buf);
#else
        (void)log;
        (void)v;
#endif
    }

    static void TraceU32(LogFn log, u32 v)
    {
#if CITADEL_BOOT_FAT_TRACE
        if (!log)
            return;
        char buf[12];
        usize pos = 0;
        if (v == 0)
            buf[pos++] = '0';
        else
        {
            char tmp[10];
            usize t = 0;
            while (v && t < sizeof(tmp))
            {
                tmp[t++] = static_cast<char>('0' + (v % 10));
                v /= 10;
            }
            while (t)
                buf[pos++] = tmp[--t];
        }
        buf[pos] = 0;
        log(buf);
#else
        (void)log;
        (void)v;
#endif
    }

    static u16 ReadLe16(const u8 *p)
    {
        return static_cast<u16>(p[0] | (static_cast<u16>(p[1]) << 8));
    }

    static u32 ReadLe32(const u8 *p)
    {
        return static_cast<u32>(p[0] | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24));
    }

    struct Fat32Layout
    {
        u32 bytesPerSector = 0;
        u32 sectorsPerCluster = 0;
        u32 reservedSectors = 0;
        u32 numFats = 0;
        u32 fatSizeSectors = 0;
        u32 rootCluster = 0;
        u64 fatOffsetBytes = 0;
        u64 dataOffsetBytes = 0;
        u64 clusterSizeBytes = 0;
    };

    static bool ParseFat32(const u8 *image, usize imageSize, Fat32Layout &out)
    {
        if (!image || imageSize < 512)
            return false;

        const u16 bytesPerSector = ReadLe16(image + 11);
        const u8 sectorsPerCluster = image[13];
        const u16 reservedSectors = ReadLe16(image + 14);
        const u8 numFats = image[16];
        const u16 rootEntryCount = ReadLe16(image + 17);
        const u16 fatSize16 = ReadLe16(image + 22);
        const u32 fatSize32 = ReadLe32(image + 36);
        const u32 rootCluster = ReadLe32(image + 44);

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

        const u64 fatOffsetBytes = static_cast<u64>(reservedSectors) * bytesPerSector;
        const u64 fatsBytes = static_cast<u64>(numFats) * static_cast<u64>(fatSize32) * bytesPerSector;
        const u64 dataOffsetBytes = fatOffsetBytes + fatsBytes;
        const u64 clusterSizeBytes = static_cast<u64>(bytesPerSector) * sectorsPerCluster;

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

    static bool ReadFat32Entry(const u8 *image, usize imageSize, const Fat32Layout &fat, u32 cluster, u32 &outNext)
    {
        outNext = 0;
        if (cluster < 2)
            return false;
        const u64 entryOffset = fat.fatOffsetBytes + static_cast<u64>(cluster) * 4ull;
        if (entryOffset + 4ull > imageSize)
            return false;
        const u32 val = ReadLe32(image + entryOffset) & 0x0FFFFFFFul;
        outNext = val;
        return true;
    }

    static bool ClusterToOffset(const Fat32Layout &fat, u32 cluster, u64 &outOffset)
    {
        outOffset = 0;
        if (cluster < 2)
            return false;
        const u64 clusterIndex = static_cast<u64>(cluster - 2);
        outOffset = fat.dataOffsetBytes + clusterIndex * fat.clusterSizeBytes;
        return true;
    }

    static bool AsciiEqIgnoreCaseN(const char *a, usize aLen, const char *b)
    {
        if (!a || !b)
            return false;
        usize i = 0;
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

    struct FatLongNameEntry
    {
        u8 order;
        u16 name1[5];
        u8 attributes;
        u8 type;
        u8 checksum;
        u16 name2[6];
        u16 firstClusterLow;
        u16 name3[2];
    } __attribute__((packed));

    static u8 Short11Checksum(const char short11[11])
    {
        u8 sum = 0;
        for (int i = 0; i < 11; ++i)
            sum = static_cast<u8>(((sum & 1) ? 0x80 : 0) + (sum >> 1) + static_cast<u8>(short11[i]));
        return sum;
    }

    static void LfnReset(char *buf, usize cap, u8 &checksum, bool &valid)
    {
        if (buf && cap)
            buf[0] = 0;
        checksum = 0;
        valid = false;
    }

    static void LfnPrepend(const char *fragment, char *pending, usize cap)
    {
        if (!fragment || !pending || cap == 0)
            return;
        char combined[260];
        std::memset(combined, 0, sizeof(combined));
        std::strncpy(combined, fragment, sizeof(combined) - 1);
        combined[sizeof(combined) - 1] = 0;
        const usize used = std::strlen(combined);
        if (used + 1 < sizeof(combined))
        {
            std::strncpy(combined + used, pending, sizeof(combined) - 1 - used);
            combined[sizeof(combined) - 1] = 0;
        }
        std::strncpy(pending, combined, cap - 1);
        pending[cap - 1] = 0;
    }

    static void LfnConsume(const FatLongNameEntry &lfn, char *pending, usize cap)
    {
        char frag[64];
        std::memset(frag, 0, sizeof(frag));
        usize out = 0;
        bool ended = false;

        auto emit = [&](u16 ch)
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

    static bool FindNameInDirectory(const u8 *image, usize imageSize, const Fat32Layout &fat,
                                    u32 dirCluster, const char *seg, usize segLen, LogFn log,
                                    u8 &outAttr, u32 &outFirstCluster, u32 &outFileSize)
    {
        if (!seg || segLen == 0)
            return false;

        Trace(log, "FAT: FindNameInDirectory dirCluster=");
        TraceU32(log, dirCluster);
        Trace(log, " seg='");
#if CITADEL_BOOT_FAT_TRACE
        if (log)
        {
            for (usize i = 0; i < segLen; ++i)
            {
                char c = seg[i];
                if (c < 0x20 || c > 0x7E)
                    c = '?';
                char b[2] = {c, 0};
                log(b);
            }
        }
#endif
        Trace(log, "'\n");

        // This harness only cares about the VFAT scan path.
        char pending[256];
        u8 pendingChecksum = 0;
        bool pendingValid = false;
        LfnReset(pending, sizeof(pending), pendingChecksum, pendingValid);

        const u32 bytesPerCluster = static_cast<u32>(fat.clusterSizeBytes);
        u32 cluster = dirCluster;
        const usize maxClusters = static_cast<usize>(imageSize / (fat.clusterSizeBytes ? fat.clusterSizeBytes : 1)) + 2;

        for (usize iter = 0; iter < maxClusters; ++iter)
        {
            u64 clusterOffset = 0;
            if (!ClusterToOffset(fat, cluster, clusterOffset))
                return false;
            if (clusterOffset + fat.clusterSizeBytes > imageSize)
                return false;

            const u8 *p = image + clusterOffset;
            for (u32 off = 0; off + 32 <= bytesPerCluster; off += 32)
            {
                const u8 first = p[off + 0];
                if (first == 0x00)
                    return false;
                if (first == 0xE5)
                {
                    LfnReset(pending, sizeof(pending), pendingChecksum, pendingValid);
                    continue;
                }

                const u8 attr = p[off + 11];
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

                    Trace(log, "FAT:  LFN ent order=");
                    TraceHexByte(log, lfn.order);
                    Trace(log, " attr=");
                    TraceHexByte(log, lfn.attributes);
                    Trace(log, " type=");
                    TraceHexByte(log, lfn.type);
                    Trace(log, " chk=");
                    TraceHexByte(log, lfn.checksum);
                    Trace(log, " pendingValid=");
                    Trace(log, pendingValid ? "1" : "0");
                    Trace(log, " pending='");
                    Trace(log, pending);
                    Trace(log, "'\n");
                    continue;
                }

                const u8 *ent = p + off;
                const u8 entAttr = ent[11];
                if ((entAttr & 0x08) != 0)
                {
                    LfnReset(pending, sizeof(pending), pendingChecksum, pendingValid);
                    continue;
                }

                char sfnDisp[16];
                std::memset(sfnDisp, 0, sizeof(sfnDisp));
                const char *sfn = reinterpret_cast<const char *>(ent + 0);
                usize out = 0;
                for (int i = 0; i < 8 && out + 1 < sizeof(sfnDisp); ++i)
                {
                    const char ch = sfn[i];
                    if (ch == ' ')
                        break;
                    sfnDisp[out++] = ch;
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
                if (haveExt && out + 1 < sizeof(sfnDisp))
                    sfnDisp[out++] = '.';
                if (haveExt)
                {
                    for (int i = 0; i < 3 && out + 1 < sizeof(sfnDisp); ++i)
                    {
                        const char ch = sfn[8 + i];
                        if (ch == ' ')
                            break;
                        sfnDisp[out++] = ch;
                    }
                }
                sfnDisp[out] = 0;

                Trace(log, "FAT:  SFN raw='");
#if CITADEL_BOOT_FAT_TRACE
                if (log)
                {
                    for (int i = 0; i < 11; ++i)
                    {
                        char c = sfn[i];
                        if (c < 0x20 || c > 0x7E)
                            c = '?';
                        char b[2] = {c, 0};
                        log(b);
                    }
                }
#endif
                Trace(log, "' disp='");
                Trace(log, sfnDisp);
                Trace(log, "' pendingValid=");
                Trace(log, pendingValid ? "1" : "0");
                Trace(log, " pending='");
                Trace(log, pending);
                Trace(log, "'\n");

                if (pendingValid)
                {
                    const u8 chk = Short11Checksum(reinterpret_cast<const char *>(ent + 0));
                    if (chk == pendingChecksum)
                    {
                        if (AsciiEqIgnoreCaseN(seg, segLen, pending))
                        {
                            outAttr = entAttr;
                            const u16 hi = ReadLe16(ent + 20);
                            const u16 lo = ReadLe16(ent + 26);
                            outFirstCluster = (static_cast<u32>(hi) << 16) | lo;
                            outFileSize = ReadLe32(ent + 28);
                            return true;
                        }
                    }
                }

                if (AsciiEqIgnoreCaseN(seg, segLen, sfnDisp))
                {
                    outAttr = entAttr;
                    const u16 hi = ReadLe16(ent + 20);
                    const u16 lo = ReadLe16(ent + 26);
                    outFirstCluster = (static_cast<u32>(hi) << 16) | lo;
                    outFileSize = ReadLe32(ent + 28);
                    return true;
                }

                LfnReset(pending, sizeof(pending), pendingChecksum, pendingValid);
            }

            u32 next = 0;
            if (!ReadFat32Entry(image, imageSize, fat, cluster, next))
                return false;
            if (next >= 0x0FFFFFF8u)
                break;
            if (next < 2)
                break;
            cluster = next;
        }

        return false;
    }

    static bool OpenPath(const u8 *image, usize imageSize, const Fat32Layout &fat, const char *path, LogFn log)
    {
        if (!path)
            return false;

        const char *p = path;
        while (*p == '/')
            ++p;
        if (!*p)
            return false;

        u32 dirCluster = fat.rootCluster;
        for (;;)
        {
            const char *seg = p;
            usize segLen = 0;
            while (p[segLen] && p[segLen] != '/')
                ++segLen;

            const bool last = (p[segLen] == 0);
            u8 attr = 0;
            u32 firstCluster = 0;
            u32 fileSize = 0;
            if (!FindNameInDirectory(image, imageSize, fat, dirCluster, seg, segLen, log, attr, firstCluster, fileSize))
                return false;

            if (last)
            {
                Trace(log, "FAT: resolved leaf attr=0x");
                TraceHexByte(log, attr);
                Trace(log, " firstCluster=");
                TraceU32(log, firstCluster);
                Trace(log, " size=");
                TraceU32(log, fileSize);
                Trace(log, "\n");
                return true;
            }

            if ((attr & 0x10) == 0)
                return false;

            p += segLen;
            while (*p == '/')
                ++p;
            if (!*p)
                return false;

            dirCluster = firstCluster;
        }
    }

    static u8 *ReadWholeFile(const char *path, usize &outSize)
    {
        outSize = 0;
        std::FILE *f = std::fopen(path, "rb");
        if (!f)
            return nullptr;
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        if (sz <= 0)
        {
            std::fclose(f);
            return nullptr;
        }
        std::fseek(f, 0, SEEK_SET);
        auto *buf = static_cast<u8 *>(std::malloc(static_cast<usize>(sz)));
        if (!buf)
        {
            std::fclose(f);
            return nullptr;
        }
        if (std::fread(buf, 1, static_cast<usize>(sz), f) != static_cast<usize>(sz))
        {
            std::free(buf);
            std::fclose(f);
            return nullptr;
        }
        std::fclose(f);
        outSize = static_cast<usize>(sz);
        return buf;
    }
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s <fat.img> </path/in/img>\n", argv[0]);
        return 2;
    }

    const char *imgPath = argv[1];
    const char *targetPath = argv[2];

    size_t imgSize = 0;
    auto *img = ReadWholeFile(imgPath, imgSize);
    if (!img)
    {
        std::perror("read image");
        return 3;
    }

    Fat32Layout fat{};
    if (!ParseFat32(img, imgSize, fat))
    {
        std::fprintf(stderr, "ParseFat32 failed\n");
        std::free(img);
        return 4;
    }

    const bool ok = OpenPath(img, imgSize, fat, targetPath, &StdoutLog);
    std::free(img);

    if (!ok)
    {
        std::fprintf(stderr, "OPEN FAILED: %s\n", targetPath);
        return 1;
    }

    std::printf("OPEN OK: %s\n", targetPath);
    return 0;
}
