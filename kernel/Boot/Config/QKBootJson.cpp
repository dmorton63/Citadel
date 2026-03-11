#include "QKBootJson.h"

#include "Boot/Limine/QKBootLimineModules.h"

#include "QCJson.h"
#include "QCString.h"
#include "QFSFile.h"
#include "QFSVFS.h"

namespace QK::Boot::Config
{
    namespace
    {
        static void LogStr(FLogFn Log, const char *Msg)
        {
            if (Log)
                Log(Msg);
        }

        static void LogSchemaHintIfApplicable(FLogFn Log, const QC::JSON::Value &root)
        {
            if (!Log)
                return;
            if (!root.isObject())
                return;

            const QC::JSON::Value *schema = root.find("$schema");
            const QC::JSON::Value *props = root.find("properties");
            if ((schema && schema->isString()) || (props && props->isObject()))
            {
                Log("boot.json appears to be a JSON-Schema (it defines fields/types). BootGate expects a config instance with concrete values.\r\n");
            }
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

            // FAT32 BPB offsets (within boot sector)
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

            // We expect FAT32 (rootEntryCount should be 0 and fatSize16 should be 0).
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
                        return false; // end of directory
                    if (name0 == 0xE5)
                        continue; // deleted

                    const QC::u8 attr = ent[11];
                    if (attr == 0x0F)
                        continue; // LFN
                    if (attr & 0x08)
                        continue; // volume label

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

            buffer[fileSize] = '\0';
            outBuf = buffer;
            outLen = static_cast<QC::usize>(fileSize);
            return true;
        }

        static bool ReadFileToNullTerminatedBuffer(const char *path, char *&outBuf, QC::usize &outLen)
        {
            outBuf = nullptr;
            outLen = 0;

            if (!path)
                return false;

            QFS::File *file = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
            if (!file)
                return false;

            QC::u64 size64 = file->size();
            if (size64 == 0 || size64 > 1024 * 64)
            {
                QFS::VFS::instance().close(file);
                return false;
            }

            QC::usize size = static_cast<QC::usize>(size64);
            char *buffer = static_cast<char *>(operator new[](size + 1));
            QC::isize readCount = file->read(buffer, size);
            QFS::VFS::instance().close(file);

            if (readCount <= 0)
            {
                operator delete[](buffer);
                return false;
            }

            if (static_cast<QC::usize>(readCount) < size)
                size = static_cast<QC::usize>(readCount);

            buffer[size] = '\0';
            outBuf = buffer;
            outLen = size;
            return true;
        }

        static QC::u64 ClampJsonMiB(const QC::JSON::Value *v)
        {
            if (!v || !v->isNumber())
                return 0;

            const double d = v->asNumber(0.0);
            if (d <= 0.0)
                return 0;

            const double maxU64 = 18446744073709551615.0;
            const double clamped = (d > maxU64) ? maxU64 : d;
            return static_cast<QC::u64>(clamped);
        }

        static bool LoadFromJson(const QC::JSON::Value &root, BootPolicy &out)
        {
            if (!root.isObject())
                return false;

            // Format A (legacy):
            // {
            //   "requirements": { "min_ram_mib": 2048, "recommended_ram_mib": 2048, "require_tpm": false }
            // }
            const QC::JSON::Value *req = root.find("requirements");
            if (req && req->isObject())
            {
                bool any = false;

                const QC::u64 minMiB = ClampJsonMiB(req->find("min_ram_mib"));
                if (minMiB)
                {
                    out.requirements.minRamMiB = minMiB;
                    any = true;
                }

                const QC::u64 recMiB = ClampJsonMiB(req->find("recommended_ram_mib"));
                if (recMiB)
                {
                    out.requirements.recommendedRamMiB = recMiB;
                    any = true;
                }

                const QC::JSON::Value *tpm = req->find("require_tpm");
                if (tpm && tpm->isBool())
                {
                    out.requirements.requireTpm = tpm->asBool(false);
                    any = true;
                }

                return any;
            }

            // Format B (richer config):
            // {
            //   "schema_version": 1,
            //   "min_spec": { "ram_mib_required": 2048, "tpm": { "mode": "optional" } }
            // }
            const QC::JSON::Value *minSpec = root.find("min_spec");
            if (minSpec && minSpec->isObject())
            {
                bool any = false;

                const QC::u64 minMiB = ClampJsonMiB(minSpec->find("ram_mib_required"));
                if (minMiB)
                {
                    out.requirements.minRamMiB = minMiB;
                    any = true;
                }

                const QC::u64 recMiB = ClampJsonMiB(minSpec->find("ram_mib_recommended"));
                if (recMiB)
                {
                    out.requirements.recommendedRamMiB = recMiB;
                    any = true;
                }

                const QC::JSON::Value *tpmObj = minSpec->find("tpm");
                if (tpmObj && tpmObj->isObject())
                {
                    const QC::JSON::Value *mode = tpmObj->find("mode");
                    if (mode && mode->isString())
                    {
                        const char *s = mode->asString(nullptr);
                        if (s)
                        {
                            if (QC::String::strcmp(s, "required") == 0)
                                out.requirements.requireTpm = true;
                            else
                                out.requirements.requireTpm = false;

                            any = true;
                        }
                    }
                }

                const QC::JSON::Value *cpuObj = minSpec->find("cpu");
                if (cpuObj && cpuObj->isObject())
                {
                    const QC::JSON::Value *v = nullptr;

                    v = cpuObj->find("require_64bit");
                    if (v && v->isBool())
                    {
                        out.requirements.cpu.require64bit = v->asBool(false);
                        any = true;
                    }

                    v = cpuObj->find("require_sse2");
                    if (v && v->isBool())
                    {
                        out.requirements.cpu.requireSse2 = v->asBool(false);
                        any = true;
                    }

                    v = cpuObj->find("require_nx");
                    if (v && v->isBool())
                    {
                        out.requirements.cpu.requireNx = v->asBool(false);
                        any = true;
                    }

                    v = cpuObj->find("require_longmode");
                    if (v && v->isBool())
                    {
                        out.requirements.cpu.requireLongmode = v->asBool(false);
                        any = true;
                    }
                }

                return any;
            }

            return false;
        }
    }

    bool LoadBootPolicyFromVfs(FLogFn Log, BootPolicy &OutPolicy)
    {
        OutPolicy = BootPolicy{};

        const char *candidates[] = {"/boot.json", "/BOOT.JSN"};

        for (QC::usize i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
        {
            const char *path = candidates[i];

            char *buffer = nullptr;
            QC::usize len = 0;
            if (!ReadFileToNullTerminatedBuffer(path, buffer, len))
            {
                continue;
            }

            QC::JSON::Value root;
            const bool ok = QC::JSON::parse(buffer, root);
            operator delete[](buffer);

            if (!ok)
            {
                LogStr(Log, "boot.json parse failed; ignoring\r\n");
                return false;
            }

            if (!LoadFromJson(root, OutPolicy))
            {
                LogSchemaHintIfApplicable(Log, root);
                LogStr(Log, "boot.json config shape invalid; ignoring\r\n");
                return false;
            }

            LogStr(Log, "boot.json loaded\r\n");
            return true;
        }

        return false;
    }

    bool LoadBootPolicyFromLimineRamdiskModule(FLogFn Log, QC::u64 ModuleRequest[], BootPolicy &OutPolicy)
    {
        OutPolicy = BootPolicy{};

        const limine_file *Ramdisk = QK::Boot::Limine::FindRamdiskModule(ModuleRequest);
        if (!Ramdisk || !Ramdisk->address || Ramdisk->size < 512)
        {
            return false;
        }

        const QC::u8 *image = static_cast<const QC::u8 *>(Ramdisk->address);
        const QC::usize imageSize = static_cast<QC::usize>(Ramdisk->size);

        Fat32Layout fat{};
        if (!ParseFat32(image, imageSize, fat))
        {
            return false;
        }

        static constexpr char kBootJsn[11] = {'B', 'O', 'O', 'T', ' ', ' ', ' ', ' ', 'J', 'S', 'N'};

        QC::u32 firstCluster = 0;
        QC::u32 fileSize = 0;
        if (!FindShortNameInRoot(image, imageSize, fat, kBootJsn, firstCluster, fileSize))
        {
            return false;
        }

        char *buffer = nullptr;
        QC::usize len = 0;
        if (!ReadFileByClusterChain(image, imageSize, fat, firstCluster, fileSize, buffer, len))
        {
            return false;
        }

        QC::JSON::Value root;
        const bool ok = QC::JSON::parse(buffer, root);
        operator delete[](buffer);

        if (!ok)
        {
            LogStr(Log, "boot.json parse failed; ignoring\r\n");
            return false;
        }

        if (!LoadFromJson(root, OutPolicy))
        {
            LogSchemaHintIfApplicable(Log, root);
            LogStr(Log, "boot.json config shape invalid; ignoring\r\n");
            return false;
        }

        LogStr(Log, "boot.json loaded (BOOT.JSN via ramdisk module)\r\n");
        return true;
    }
}
