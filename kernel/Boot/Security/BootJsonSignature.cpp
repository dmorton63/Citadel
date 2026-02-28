#include "BootJsonSignature.h"

#include "BootJsonSigKey.h"
#include "Sha256.h"

#include "Boot/Limine/LimineModules.h"
#include "Boot/Tpm/TpmSecureStore.h"

#include "QCString.h"

namespace QK::Boot::Security
{
    namespace
    {
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

        static bool ReadFileByClusterChain(const QC::u8 *image, QC::usize imageSize, const Fat32Layout &fat, QC::u32 firstCluster,
                                           QC::u32 fileSize, QC::u8 *&outBuf, QC::usize &outLen)
        {
            outBuf = nullptr;
            outLen = 0;

            if (fileSize == 0)
                return false;
            if (fileSize > 1024 * 64)
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
}
