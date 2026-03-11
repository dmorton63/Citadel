// QFileSystem FAT16 - Implementation
// Namespace: QFS

#include "QFSFAT16.h"
#include "QFSFile.h"
#include "QFSDirectory.h"
#include "QFSPath.h"
#include "QKMemHeap.h"
#include "QCLogger.h"
#include "QCString.h"

namespace QFS
{

    static inline bool isLowerAlpha(char c)
    {
        return c >= 'a' && c <= 'z';
    }

    static inline bool isUpperAlpha(char c)
    {
        return c >= 'A' && c <= 'Z';
    }

    static QC::u8 computeNtCaseFlagsForSfnDisplay(const char *name)
    {
        if (!name)
            return 0;

        bool baseHasLower = false;
        bool baseHasUpper = false;
        bool extHasLower = false;
        bool extHasUpper = false;

        int i = 0;
        int copied = 0;
        while (name[i] && name[i] != '.' && copied < 8)
        {
            if (isLowerAlpha(name[i]))
                baseHasLower = true;
            else if (isUpperAlpha(name[i]))
                baseHasUpper = true;
            ++i;
            ++copied;
        }

        while (name[i] && name[i] != '.')
            ++i;
        if (name[i] == '.')
            ++i;

        copied = 0;
        while (name[i] && copied < 3)
        {
            if (isLowerAlpha(name[i]))
                extHasLower = true;
            else if (isUpperAlpha(name[i]))
                extHasUpper = true;
            ++i;
            ++copied;
        }

        QC::u8 flags = 0;
        if (baseHasLower && !baseHasUpper)
            flags |= 0x08;
        if (extHasLower && !extHasUpper)
            flags |= 0x10;
        return flags;
    }

    namespace
    {
        struct FATLongNameEntry
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

        static QC::u8 sfnChecksum(const char sfn[11])
        {
            QC::u8 sum = 0;
            for (int i = 0; i < 11; ++i)
            {
                sum = static_cast<QC::u8>(((sum & 1) ? 0x80 : 0) + (sum >> 1) + static_cast<QC::u8>(sfn[i]));
            }
            return sum;
        }

        static inline char lowerAscii(char c)
        {
            if (c >= 'A' && c <= 'Z')
                return static_cast<char>(c + 32);
            return c;
        }

        static bool equalsIgnoreCase(const char *a, const char *b)
        {
            if (!a || !b)
                return false;
            while (*a && *b)
            {
                if (lowerAscii(*a) != lowerAscii(*b))
                    return false;
                ++a;
                ++b;
            }
            return *a == '\0' && *b == '\0';
        }

        static void lfnClear(char *buf, QC::usize bufSize, QC::u8 &checksum, bool &valid)
        {
            if (buf && bufSize)
                buf[0] = '\0';
            checksum = 0;
            valid = false;
        }

        static void lfnPrependFragment(const char *fragment, char *pending, QC::usize pendingSize)
        {
            if (!fragment || !pending || pendingSize == 0)
                return;

            char combined[256];
            QC::String::memset(combined, 0, sizeof(combined));
            QC::String::strncpy(combined, fragment, sizeof(combined) - 1);
            combined[sizeof(combined) - 1] = '\0';

            const QC::usize used = QC::String::strlen(combined);
            if (used + 1 < sizeof(combined))
            {
                QC::String::strncpy(combined + used, pending, sizeof(combined) - 1 - used);
                combined[sizeof(combined) - 1] = '\0';
            }

            QC::String::strncpy(pending, combined, pendingSize - 1);
            pending[pendingSize - 1] = '\0';
        }

        static void lfnConsume(const FATLongNameEntry &lfn, char *pending, QC::usize pendingSize)
        {
            char frag[64];
            QC::String::memset(frag, 0, sizeof(frag));
            QC::usize outIdx = 0;
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
                if (outIdx + 1 < sizeof(frag))
                    frag[outIdx++] = c;
            };

            for (int i = 0; i < 5; ++i)
                emit(lfn.name1[i]);
            for (int i = 0; i < 6; ++i)
                emit(lfn.name2[i]);
            for (int i = 0; i < 2; ++i)
                emit(lfn.name3[i]);

            frag[outIdx] = '\0';
            if (outIdx > 0)
                lfnPrependFragment(frag, pending, pendingSize);
        }
    }
    namespace
    {
        constexpr QC::u16 FAT16_EOC = 0xFFF8;
        constexpr QC::u16 FAT16_BAD = 0xFFF7;

        static inline QC::u16 rd16_unaligned(const void *p)
        {
            QC::u16 v;
            QC::String::memcpy(&v, p, sizeof(v));
            return v;
        }
    }

    FAT16::FAT16(BlockDevice *device)
        : m_device(device), m_fatStart(0), m_rootDirStart(0), m_rootDirSectors(0), m_dataStart(0), m_clusterSize(0), m_totalClusters(0), m_clusterBuffer(nullptr)
    {
        QC::String::memset(&m_bootSector, 0, sizeof(m_bootSector));
        QC::String::memset(&m_entryCache, 0, sizeof(m_entryCache));
    }

    FAT16::~FAT16()
    {
        unmount();
    }

    QC::Status FAT16::mount()
    {
        QC_LOG_INFO("QFSFAT16", "Mounting FAT16 filesystem");

        QC::u8 sectorBuffer[512];
        QC::Status status = m_device->readSector(0, sectorBuffer);
        if (status != QC::Status::Success)
        {
            QC_LOG_ERROR("QFSFAT16", "Failed to read boot sector");
            return status;
        }

        QC::String::memcpy(&m_bootSector, sectorBuffer, sizeof(FAT16BootSector));

        if (m_bootSector.bytesPerSector != 512)
        {
            QC_LOG_ERROR("QFSFAT16", "Unsupported sector size: %u", m_bootSector.bytesPerSector);
            return QC::Status::NotSupported;
        }
        if (m_bootSector.sectorsPerCluster == 0)
            return QC::Status::NotSupported;
        if (m_bootSector.reservedSectors == 0)
            return QC::Status::NotSupported;
        if (m_bootSector.fatCount == 0)
            return QC::Status::NotSupported;
        if (m_bootSector.sectorsPerFat16 == 0)
            return QC::Status::NotSupported;
        if (m_bootSector.rootEntryCount == 0)
            return QC::Status::NotSupported;

        const QC::u32 bytesPerSector = m_bootSector.bytesPerSector;
        const QC::u32 totalSectors = (m_bootSector.totalSectors32 != 0) ? m_bootSector.totalSectors32 : m_bootSector.totalSectors16;

        m_fatStart = m_bootSector.reservedSectors;
        m_rootDirStart = m_fatStart + (m_bootSector.fatCount * m_bootSector.sectorsPerFat16);
        m_rootDirSectors = (static_cast<QC::u32>(m_bootSector.rootEntryCount) * 32U + (bytesPerSector - 1U)) / bytesPerSector;
        m_dataStart = m_rootDirStart + m_rootDirSectors;
        m_clusterSize = m_bootSector.bytesPerSector * m_bootSector.sectorsPerCluster;

        if (totalSectors > m_dataStart)
        {
            const QC::u32 dataSectors = totalSectors - m_dataStart;
            m_totalClusters = dataSectors / m_bootSector.sectorsPerCluster;
        }

        m_clusterBuffer = static_cast<QC::u8 *>(QK::Memory::Heap::instance().allocate(m_clusterSize));
        if (!m_clusterBuffer)
        {
            QC_LOG_ERROR("QFSFAT16", "Failed to allocate cluster buffer (%u bytes)", m_clusterSize);
            return QC::Status::OutOfMemory;
        }

        QC_LOG_INFO("QFSFAT16", "FAT16 mounted: %u bytes/cluster, root entries=%u",
                    m_clusterSize, static_cast<unsigned>(m_bootSector.rootEntryCount));

        return QC::Status::Success;
    }

    QC::Status FAT16::unmount()
    {
        if (m_clusterBuffer)
        {
            QK::Memory::Heap::instance().free(m_clusterBuffer);
            m_clusterBuffer = nullptr;
        }
        return QC::Status::Success;
    }

    QC::u32 FAT16::clusterToSector(QC::u32 cluster) const
    {
        return m_dataStart + ((cluster - 2) * m_bootSector.sectorsPerCluster);
    }

    bool FAT16::loadCluster(QC::u32 cluster)
    {
        if (cluster < 2)
            return false;

        QC::u32 sector = clusterToSector(cluster);
        QC::usize count = m_bootSector.sectorsPerCluster;
        QC::Status status = m_device->readSectors(sector, count, m_clusterBuffer);
        return status == QC::Status::Success;
    }

    bool FAT16::storeCluster(QC::u32 cluster)
    {
        if (cluster < 2)
            return false;
        QC::u32 sector = clusterToSector(cluster);
        QC::usize count = m_bootSector.sectorsPerCluster;
        QC::Status status = m_device->writeSectors(sector, count, m_clusterBuffer);
        return status == QC::Status::Success;
    }

    QC::u16 FAT16::readFAT(QC::u32 cluster)
    {
        const QC::u32 fatOffset = cluster * 2U;
        const QC::u32 fatSector = m_fatStart + (fatOffset / m_bootSector.bytesPerSector);
        const QC::u32 entryOffset = fatOffset % m_bootSector.bytesPerSector;

        QC::u8 buffer[512];
        if (m_device->readSector(fatSector, buffer) != QC::Status::Success)
            return 0;

        return rd16_unaligned(&buffer[entryOffset]);
    }

    void FAT16::writeFAT(QC::u32 cluster, QC::u16 value)
    {
        const QC::u32 fatOffset = cluster * 2U;
        const QC::u32 sectorOffset = fatOffset / m_bootSector.bytesPerSector;
        const QC::u32 entryOffset = fatOffset % m_bootSector.bytesPerSector;

        QC::u8 buffer[512];
        for (QC::u32 fatIndex = 0; fatIndex < m_bootSector.fatCount; ++fatIndex)
        {
            const QC::u32 fatBase = m_fatStart + (fatIndex * m_bootSector.sectorsPerFat16);
            const QC::u32 fatSector = fatBase + sectorOffset;
            if (m_device->readSector(fatSector, buffer) != QC::Status::Success)
                continue;
            buffer[entryOffset + 0] = static_cast<QC::u8>(value & 0xFF);
            buffer[entryOffset + 1] = static_cast<QC::u8>((value >> 8) & 0xFF);
            (void)m_device->writeSector(fatSector, buffer);
        }
    }

    QC::u32 FAT16::allocateCluster()
    {
        if (m_totalClusters == 0)
            return 0;

        for (QC::u32 cluster = 2; cluster < m_totalClusters + 2; ++cluster)
        {
            if (readFAT(cluster) == 0x0000)
            {
                writeFAT(cluster, 0xFFFF);
                QC::String::memset(m_clusterBuffer, 0, m_clusterSize);
                storeCluster(cluster);
                return cluster;
            }
        }
        return 0;
    }

    void FAT16::freeClusterChain(QC::u32 startCluster)
    {
        QC::u32 cluster = startCluster;
        while (cluster >= 2 && cluster < FAT16_BAD)
        {
            QC::u16 next = readFAT(cluster);
            writeFAT(cluster, 0x0000);
            if (next >= FAT16_EOC || next < 2 || next == FAT16_BAD)
                break;
            cluster = next;
        }
    }

    QC::u32 FAT16::traverseToCluster(QC::u32 startCluster, QC::u32 index)
    {
        QC::u32 cluster = startCluster;
        for (QC::u32 i = 0; i < index; ++i)
        {
            QC::u16 next = readFAT(cluster);
            if (next >= FAT16_EOC)
                break;
            if (next < 2 || next == FAT16_BAD)
                break;
            cluster = next;
        }
        return cluster;
    }

    QC::u32 FAT16::entryCluster(const FAT32DirEntry &entry) const
    {
        return static_cast<QC::u32>(entry.clusterLow);
    }

    bool FAT16::iterateClusterDirectory(QC::u32 startCluster, const char *fatName, FAT32DirEntry *outEntry, QC::u32 *entryIndex)
    {
        if (!outEntry)
            return false;

        QC::u32 cluster = startCluster;
        QC::u32 index = 0;

        while (cluster >= 2 && cluster < FAT16_BAD)
        {
            if (!loadCluster(cluster))
                return false;

            const QC::u32 entriesPerCluster = m_clusterSize / sizeof(FAT32DirEntry);
            auto *entries = reinterpret_cast<FAT32DirEntry *>(m_clusterBuffer);

            for (QC::u32 i = 0; i < entriesPerCluster; ++i, ++index)
            {
                FAT32DirEntry &entry = entries[i];
                const unsigned char name0 = static_cast<unsigned char>(entry.name[0]);
                if (name0 == 0x00)
                    return false;
                if (name0 == 0xE5)
                    continue;
                if ((entry.attributes & FAT32Attr::LongName) == FAT32Attr::LongName)
                    continue;

                if (!fatName || QC::String::memcmp(entry.name, fatName, 11) == 0)
                {
                    QC::String::memcpy(outEntry, &entry, sizeof(FAT32DirEntry));
                    if (entryIndex)
                        *entryIndex = index;
                    return true;
                }
            }

            QC::u16 next = readFAT(cluster);
            if (next >= FAT16_EOC)
                break;
            if (next < 2 || next == FAT16_BAD)
                break;
            cluster = next;
        }

        return false;
    }

    bool FAT16::iterateRootDirectory(const char *fatName, FAT32DirEntry *outEntry, QC::u32 *entryIndex)
    {
        if (!outEntry)
            return false;

        const QC::u32 entriesPerSector = 512U / sizeof(FAT32DirEntry);
        const QC::u32 totalEntries = static_cast<QC::u32>(m_bootSector.rootEntryCount);

        QC::u8 sectorBuf[512];
        for (QC::u32 idx = 0; idx < totalEntries; ++idx)
        {
            const QC::u32 sectorIndex = idx / entriesPerSector;
            const QC::u32 within = idx % entriesPerSector;
            if (sectorIndex >= m_rootDirSectors)
                return false;

            if (m_device->readSector(static_cast<QC::u64>(m_rootDirStart) + sectorIndex, sectorBuf) != QC::Status::Success)
                return false;

            auto *entries = reinterpret_cast<FAT32DirEntry *>(sectorBuf);
            FAT32DirEntry &e = entries[within];
            const unsigned char name0 = static_cast<unsigned char>(e.name[0]);
            if (name0 == 0x00)
                return false;
            if (name0 == 0xE5)
                continue;
            if ((e.attributes & FAT32Attr::LongName) == FAT32Attr::LongName)
                continue;

            if (!fatName || QC::String::memcmp(e.name, fatName, 11) == 0)
            {
                QC::String::memcpy(outEntry, &e, sizeof(FAT32DirEntry));
                if (entryIndex)
                    *entryIndex = idx;
                return true;
            }
        }

        return false;
    }

    FAT32DirEntry *FAT16::findEntry(const char *path)
    {
        if (!path || path[0] == '\0')
            return nullptr;
        if (path[0] != '/')
            return nullptr;

        bool currentIsRoot = true;
        QC::u32 currentCluster = 0;

        const char *segment = path;
        while (*segment == '/')
            ++segment;
        if (*segment == '\0')
            return nullptr;

        while (*segment)
        {
            const char *start = segment;
            while (*segment && *segment != '/')
                ++segment;

            QC::usize len = static_cast<QC::usize>(segment - start);
            if (len == 0)
                break;

            char element[256];
            if (len >= sizeof(element))
                len = sizeof(element) - 1;
            for (QC::usize i = 0; i < len; ++i)
                element[i] = start[i];
            element[len] = '\0';

            FAT32DirEntry entry;
            bool found = false;

            auto matchEntry = [&](const FAT32DirEntry &cand, const char *pendingLfn, bool pendingValid, QC::u8 pendingSum) -> bool
            {
                char shortName[64];
                QC::String::memset(shortName, 0, sizeof(shortName));
                parseName(cand.name, shortName);

                const bool lfnMatches = pendingValid && pendingLfn && pendingLfn[0] != '\0' && sfnChecksum(cand.name) == pendingSum && equalsIgnoreCase(element, pendingLfn);
                const bool sfnMatches = equalsIgnoreCase(element, shortName);
                return lfnMatches || sfnMatches;
            };

            if (currentIsRoot)
            {
                const QC::u32 entriesPerSector = 512U / sizeof(FAT32DirEntry);
                const QC::u32 totalEntries = static_cast<QC::u32>(m_bootSector.rootEntryCount);

                char pendingLfn[256];
                QC::u8 pendingSum = 0;
                bool pendingValid = false;
                lfnClear(pendingLfn, sizeof(pendingLfn), pendingSum, pendingValid);

                QC::u8 sectorBuf[512];
                for (QC::u32 idx = 0; idx < totalEntries; ++idx)
                {
                    const QC::u32 sectorIndex = idx / entriesPerSector;
                    const QC::u32 within = idx % entriesPerSector;
                    if (sectorIndex >= m_rootDirSectors)
                        break;

                    if (m_device->readSector(static_cast<QC::u64>(m_rootDirStart) + sectorIndex, sectorBuf) != QC::Status::Success)
                        return nullptr;

                    auto *entries = reinterpret_cast<FAT32DirEntry *>(sectorBuf);
                    FAT32DirEntry &cand = entries[within];
                    const unsigned char n0 = static_cast<unsigned char>(cand.name[0]);
                    if (n0 == 0x00)
                        break;
                    if (n0 == 0xE5)
                    {
                        lfnClear(pendingLfn, sizeof(pendingLfn), pendingSum, pendingValid);
                        continue;
                    }

                    if ((cand.attributes & FAT32Attr::LongName) == FAT32Attr::LongName)
                    {
                        const FATLongNameEntry &lfn = *reinterpret_cast<const FATLongNameEntry *>(&cand);
                        if (lfn.attributes == FAT32Attr::LongName && lfn.type == 0)
                        {
                            const bool isStart = (lfn.order & 0x40) != 0;
                            if (isStart)
                            {
                                lfnClear(pendingLfn, sizeof(pendingLfn), pendingSum, pendingValid);
                                pendingSum = lfn.checksum;
                                pendingValid = true;
                            }
                            if (pendingValid && pendingSum == lfn.checksum)
                                lfnConsume(lfn, pendingLfn, sizeof(pendingLfn));
                            else
                                lfnClear(pendingLfn, sizeof(pendingLfn), pendingSum, pendingValid);
                        }
                        continue;
                    }

                    if ((cand.attributes & FAT32Attr::VolumeId) != 0)
                    {
                        lfnClear(pendingLfn, sizeof(pendingLfn), pendingSum, pendingValid);
                        continue;
                    }
                    if (n0 == static_cast<unsigned char>('.'))
                    {
                        lfnClear(pendingLfn, sizeof(pendingLfn), pendingSum, pendingValid);
                        continue;
                    }

                    if (matchEntry(cand, pendingLfn, pendingValid, pendingSum))
                    {
                        entry = cand;
                        found = true;
                        break;
                    }
                    lfnClear(pendingLfn, sizeof(pendingLfn), pendingSum, pendingValid);
                }
            }
            else
            {
                QC::u32 cluster = currentCluster;
                char pendingLfn[256];
                QC::u8 pendingSum = 0;
                bool pendingValid = false;
                lfnClear(pendingLfn, sizeof(pendingLfn), pendingSum, pendingValid);

                while (!found && cluster >= 2 && cluster < FAT16_BAD)
                {
                    if (!loadCluster(cluster))
                        return nullptr;

                    const QC::u32 entriesPerCluster = m_clusterSize / sizeof(FAT32DirEntry);
                    auto *entries = reinterpret_cast<FAT32DirEntry *>(m_clusterBuffer);
                    for (QC::u32 i = 0; i < entriesPerCluster; ++i)
                    {
                        FAT32DirEntry &cand = entries[i];
                        const unsigned char n0 = static_cast<unsigned char>(cand.name[0]);
                        if (n0 == 0x00)
                            break;
                        if (n0 == 0xE5)
                        {
                            lfnClear(pendingLfn, sizeof(pendingLfn), pendingSum, pendingValid);
                            continue;
                        }

                        if ((cand.attributes & FAT32Attr::LongName) == FAT32Attr::LongName)
                        {
                            const FATLongNameEntry &lfn = *reinterpret_cast<const FATLongNameEntry *>(&cand);
                            if (lfn.attributes == FAT32Attr::LongName && lfn.type == 0)
                            {
                                const bool isStart = (lfn.order & 0x40) != 0;
                                if (isStart)
                                {
                                    lfnClear(pendingLfn, sizeof(pendingLfn), pendingSum, pendingValid);
                                    pendingSum = lfn.checksum;
                                    pendingValid = true;
                                }
                                if (pendingValid && pendingSum == lfn.checksum)
                                    lfnConsume(lfn, pendingLfn, sizeof(pendingLfn));
                                else
                                    lfnClear(pendingLfn, sizeof(pendingLfn), pendingSum, pendingValid);
                            }
                            continue;
                        }

                        if ((cand.attributes & FAT32Attr::VolumeId) != 0)
                        {
                            lfnClear(pendingLfn, sizeof(pendingLfn), pendingSum, pendingValid);
                            continue;
                        }
                        if (n0 == static_cast<unsigned char>('.'))
                        {
                            lfnClear(pendingLfn, sizeof(pendingLfn), pendingSum, pendingValid);
                            continue;
                        }

                        if (matchEntry(cand, pendingLfn, pendingValid, pendingSum))
                        {
                            entry = cand;
                            found = true;
                            break;
                        }
                        lfnClear(pendingLfn, sizeof(pendingLfn), pendingSum, pendingValid);
                    }

                    if (found)
                        break;

                    QC::u16 next = readFAT(cluster);
                    if (next >= FAT16_EOC)
                        break;
                    if (next < 2 || next == FAT16_BAD)
                        break;
                    cluster = next;
                }
            }

            if (!found)
                return nullptr;

            while (*segment == '/')
                ++segment;

            if (*segment == '\0')
            {
                m_entryCache = entry;
                return &m_entryCache;
            }

            if (!(entry.attributes & FAT32Attr::Directory))
                return nullptr;

            QC::u32 nextCluster = entryCluster(entry);
            if (nextCluster < 2)
                return nullptr;

            currentIsRoot = false;
            currentCluster = nextCluster;
        }

        return nullptr;
    }

    File *FAT16::open(const char *path, OpenMode mode)
    {
        if (!path || path[0] == '\0')
            return nullptr;

        if (!(mode & OpenMode::Read) && !(mode & OpenMode::Write))
        {
            QC_LOG_ERROR("QFSFAT16", "open: neither read nor write requested (path=%s)", path);
            return nullptr;
        }

        // Only support create/write/truncate in the root directory for now.
        char parentPath[256];
        char baseName[256];
        Path::dirname(path, parentPath, sizeof(parentPath));
        Path::basename(path, baseName, sizeof(baseName));

        const bool wantsCreate = (mode & OpenMode::Create);
        const bool wantsWrite = (mode & OpenMode::Write);

        QC::u32 entryIndex = 0;
        FAT32DirEntry found;
        bool haveEntry = false;

        if (QC::String::strcmp(parentPath, "/") == 0)
        {
            // Match existing entries by either VFAT long name or 8.3 short name.
            const QC::u32 entriesPerSector = 512U / sizeof(FAT32DirEntry);
            const QC::u32 totalEntries = static_cast<QC::u32>(m_bootSector.rootEntryCount);

            char pendingLfn[256];
            QC::u8 pendingSum = 0;
            bool pendingValid = false;
            lfnClear(pendingLfn, sizeof(pendingLfn), pendingSum, pendingValid);

            QC::u8 sectorBuf[512];
            for (QC::u32 idx = 0; idx < totalEntries; ++idx)
            {
                const QC::u32 sectorIndex = idx / entriesPerSector;
                const QC::u32 within = idx % entriesPerSector;
                if (sectorIndex >= m_rootDirSectors)
                    break;

                if (m_device->readSector(static_cast<QC::u64>(m_rootDirStart) + sectorIndex, sectorBuf) != QC::Status::Success)
                    break;

                auto *entries = reinterpret_cast<FAT32DirEntry *>(sectorBuf);
                FAT32DirEntry &cand = entries[within];
                const unsigned char n0 = static_cast<unsigned char>(cand.name[0]);
                if (n0 == 0x00)
                    break;
                if (n0 == 0xE5)
                {
                    lfnClear(pendingLfn, sizeof(pendingLfn), pendingSum, pendingValid);
                    continue;
                }

                if ((cand.attributes & FAT32Attr::LongName) == FAT32Attr::LongName)
                {
                    const FATLongNameEntry &lfn = *reinterpret_cast<const FATLongNameEntry *>(&cand);
                    if (lfn.attributes == FAT32Attr::LongName && lfn.type == 0)
                    {
                        const bool isStart = (lfn.order & 0x40) != 0;
                        if (isStart)
                        {
                            lfnClear(pendingLfn, sizeof(pendingLfn), pendingSum, pendingValid);
                            pendingSum = lfn.checksum;
                            pendingValid = true;
                        }
                        if (pendingValid && pendingSum == lfn.checksum)
                            lfnConsume(lfn, pendingLfn, sizeof(pendingLfn));
                        else
                            lfnClear(pendingLfn, sizeof(pendingLfn), pendingSum, pendingValid);
                    }
                    continue;
                }

                if ((cand.attributes & FAT32Attr::VolumeId) != 0)
                {
                    lfnClear(pendingLfn, sizeof(pendingLfn), pendingSum, pendingValid);
                    continue;
                }

                char shortName[64];
                QC::String::memset(shortName, 0, sizeof(shortName));
                parseName(cand.name, shortName);

                const bool lfnMatches = pendingValid && pendingLfn[0] != '\0' && sfnChecksum(cand.name) == pendingSum && equalsIgnoreCase(baseName, pendingLfn);
                const bool sfnMatches = equalsIgnoreCase(baseName, shortName);
                if (lfnMatches || sfnMatches)
                {
                    found = cand;
                    entryIndex = idx;
                    haveEntry = true;
                    break;
                }

                lfnClear(pendingLfn, sizeof(pendingLfn), pendingSum, pendingValid);
            }
        }
        else
        {
            // For now, we only support root for create/write.
            if (wantsCreate || wantsWrite)
            {
                QC_LOG_ERROR("QFSFAT16", "open: create/write only supported in root (path=%s parent=%s)", path, parentPath);
                return nullptr;
            }
        }

        if (!haveEntry)
        {
            if (!wantsCreate)
                return nullptr;
            if (QC::String::strcmp(parentPath, "/") != 0)
                return nullptr;

            QC::u32 freeIndex = 0;
            if (!findFreeRootDirectoryEntry(&freeIndex))
            {
                QC_LOG_ERROR("QFSFAT16", "open: no free root directory entry (path=%s)", path);
                return nullptr;
            }

            char fatName[11];
            formatName(baseName, fatName);
            QC::String::memset(&found, 0, sizeof(found));
            QC::String::memcpy(found.name, fatName, 11);
            found.attributes = FAT32Attr::Archive;
            found.reserved = computeNtCaseFlagsForSfnDisplay(baseName);
            found.clusterHigh = 0;
            found.clusterLow = 0;
            found.size = 0;

            if (!updateRootDirectoryEntry(freeIndex, found))
            {
                QC_LOG_ERROR("QFSFAT16", "open: failed to write new root directory entry (path=%s)", path);
                return nullptr;
            }
            entryIndex = freeIndex;
            haveEntry = true;
        }

        if (!haveEntry)
            return nullptr;
        if (found.attributes & FAT32Attr::Directory)
            return nullptr;

        QC::u32 firstCluster = entryCluster(found);

        auto *handle = new FATFileHandle();
        handle->startCluster = firstCluster;
        handle->size = found.size;
        handle->dirEntryIndex = entryIndex;
        handle->dirty = false;

        File *file = new File();
        file->setFileSystem(this);
        file->setHandle(handle);
        file->setMode(mode);
        file->setSize(found.size);
        file->setPosition(0);
        file->setOpen(true);

        if ((mode & OpenMode::Truncate) && (mode & OpenMode::Write))
        {
            if (handle->startCluster >= 2)
                freeClusterChain(handle->startCluster);
            handle->startCluster = 0;
            handle->size = 0;
            handle->dirty = true;
            file->setSize(0);
            file->setPosition(0);

            found.clusterLow = 0;
            found.clusterHigh = 0;
            found.size = 0;
            (void)updateRootDirectoryEntry(entryIndex, found);
        }

        if (mode & OpenMode::Append)
        {
            file->setPosition(file->size());
        }

        return file;
    }

    QC::Status FAT16::close(File *file)
    {
        if (!file)
            return QC::Status::InvalidParam;

        auto *handle = static_cast<FATFileHandle *>(file->handle());
        if (handle)
        {
            if (handle->dirty)
            {
                // Update root directory entry fields.
                FAT32DirEntry entry;
                QC::String::memset(&entry, 0, sizeof(entry));

                // Read the entry's sector.
                const QC::u32 entriesPerSector = 512U / sizeof(FAT32DirEntry);
                const QC::u32 sectorIndex = handle->dirEntryIndex / entriesPerSector;
                const QC::u32 within = handle->dirEntryIndex % entriesPerSector;
                QC::u8 buf[512];
                if (sectorIndex < m_rootDirSectors &&
                    m_device->readSector(static_cast<QC::u64>(m_rootDirStart) + sectorIndex, buf) == QC::Status::Success)
                {
                    auto *entries = reinterpret_cast<FAT32DirEntry *>(buf);
                    entry = entries[within];
                    entry.size = static_cast<QC::u32>(handle->size);
                    entry.clusterHigh = 0;
                    entry.clusterLow = static_cast<QC::u16>(handle->startCluster & 0xFFFF);
                    entries[within] = entry;
                    (void)m_device->writeSector(static_cast<QC::u64>(m_rootDirStart) + sectorIndex, buf);
                }
            }

            delete handle;
            file->setHandle(nullptr);
        }
        file->setOpen(false);
        file->setFileSystem(nullptr);
        return QC::Status::Success;
    }

    QC::isize FAT16::read(File *file, void *buffer, QC::usize size)
    {
        if (!file || !buffer || size == 0)
            return 0;

        auto *handle = static_cast<FATFileHandle *>(file->handle());
        if (!handle)
            return -1;

        QC::u64 position = file->tell();
        if (position >= handle->size)
            return 0;

        QC::usize remaining = static_cast<QC::usize>(handle->size - position);
        if (size > remaining)
            size = remaining;

        if (handle->startCluster < 2)
            return 0;

        QC::u8 *dst = static_cast<QC::u8 *>(buffer);
        QC::usize totalRead = 0;

        while (size > 0)
        {
            const QC::u32 clusterSize = m_clusterSize;
            const QC::u32 clusterIndex = static_cast<QC::u32>(position / clusterSize);
            const QC::u32 offset = static_cast<QC::u32>(position % clusterSize);

            QC::u32 cluster = traverseToCluster(handle->startCluster, clusterIndex);
            if (cluster < 2)
                break;
            QC::u16 marker = readFAT(cluster);
            (void)marker;

            if (!loadCluster(cluster))
                break;

            QC::usize chunk = clusterSize - offset;
            if (chunk > size)
                chunk = size;

            QC::String::memcpy(dst + totalRead, m_clusterBuffer + offset, chunk);

            size -= chunk;
            totalRead += chunk;
            position += chunk;
        }

        return static_cast<QC::isize>(totalRead);
    }

    QC::isize FAT16::write(File *file, const void *buffer, QC::usize size)
    {
        if (!file || !buffer || size == 0)
            return 0;

        auto *handle = static_cast<FATFileHandle *>(file->handle());
        if (!handle)
            return -1;
        if (!(file->mode() & OpenMode::Write))
            return -1;

        QC::u64 position = file->tell();
        if (position > handle->size)
        {
            // No sparse writes.
            return -1;
        }

        const QC::u8 *src = static_cast<const QC::u8 *>(buffer);
        QC::usize totalWritten = 0;

        auto getOrAllocateClusterAt = [&](QC::u32 clusterIndex) -> QC::u32
        {
            if (handle->startCluster < 2)
            {
                QC::u32 first = allocateCluster();
                if (first < 2)
                    return 0;
                handle->startCluster = first;
                handle->dirty = true;
            }

            QC::u32 cluster = handle->startCluster;
            for (QC::u32 i = 0; i < clusterIndex; ++i)
            {
                QC::u16 next = readFAT(cluster);
                if (next >= FAT16_EOC)
                {
                    QC::u32 newCluster = allocateCluster();
                    if (newCluster < 2)
                        return 0;
                    writeFAT(cluster, static_cast<QC::u16>(newCluster & 0xFFFF));
                    cluster = newCluster;
                    handle->dirty = true;
                    continue;
                }
                if (next < 2 || next == FAT16_BAD)
                    return 0;
                cluster = next;
            }
            return cluster;
        };

        while (size > 0)
        {
            const QC::u32 clusterSize = m_clusterSize;
            const QC::u32 clusterIndex = static_cast<QC::u32>(position / clusterSize);
            const QC::u32 offset = static_cast<QC::u32>(position % clusterSize);

            QC::u32 cluster = getOrAllocateClusterAt(clusterIndex);
            if (cluster < 2)
                break;

            if (!loadCluster(cluster))
                break;

            QC::usize chunk = clusterSize - offset;
            if (chunk > size)
                chunk = size;

            QC::String::memcpy(m_clusterBuffer + offset, src + totalWritten, chunk);
            if (!storeCluster(cluster))
                break;

            size -= chunk;
            totalWritten += chunk;
            position += chunk;
        }

        if (totalWritten > 0)
        {
            if (position > handle->size)
            {
                handle->size = position;
                handle->dirty = true;
            }
        }

        return static_cast<QC::isize>(totalWritten);
    }

    Directory *FAT16::openDir(const char *path)
    {
        if (!path)
            return nullptr;

        auto *handle = new FATDirHandle();
        handle->isRoot = false;
        handle->startCluster = 0;
        handle->currentCluster = 0;
        handle->entryIndex = 0;
        handle->rootEntryIndex = 0;
        handle->pendingLongName[0] = '\0';
        handle->pendingLongNameChecksum = 0;
        handle->pendingLongNameValid = false;

        if (path[0] == '/' && path[1] == '\0')
        {
            handle->isRoot = true;
        }
        else
        {
            FAT32DirEntry *entry = findEntry(path);
            if (!entry || !(entry->attributes & FAT32Attr::Directory))
            {
                delete handle;
                return nullptr;
            }
            QC::u32 startCluster = entryCluster(*entry);
            if (startCluster < 2)
            {
                delete handle;
                return nullptr;
            }
            handle->startCluster = startCluster;
            handle->currentCluster = startCluster;
        }

        Directory *dir = new Directory();
        dir->setFileSystem(this);
        dir->setHandle(handle);
        dir->setOpen(true);
        return dir;
    }

    QC::Status FAT16::closeDir(Directory *dir)
    {
        if (!dir)
            return QC::Status::InvalidParam;
        auto *handle = static_cast<FATDirHandle *>(dir->handle());
        if (handle)
        {
            delete handle;
            dir->setHandle(nullptr);
        }
        dir->setOpen(false);
        dir->setFileSystem(nullptr);
        return QC::Status::Success;
    }

    bool FAT16::readDir(Directory *dir, DirEntry *entry)
    {
        if (!dir || !entry)
            return false;

        auto *handle = static_cast<FATDirHandle *>(dir->handle());
        if (!handle)
            return false;

        if (handle->isRoot)
        {
            const QC::u32 totalEntries = static_cast<QC::u32>(m_bootSector.rootEntryCount);
            const QC::u32 entriesPerSector = 512U / sizeof(FAT32DirEntry);
            QC::u8 sectorBuf[512];

            while (handle->rootEntryIndex < totalEntries)
            {
                const QC::u32 idx = handle->rootEntryIndex++;
                const QC::u32 sectorIndex = idx / entriesPerSector;
                const QC::u32 within = idx % entriesPerSector;
                if (sectorIndex >= m_rootDirSectors)
                    return false;

                if (m_device->readSector(static_cast<QC::u64>(m_rootDirStart) + sectorIndex, sectorBuf) != QC::Status::Success)
                    return false;

                auto *entries = reinterpret_cast<FAT32DirEntry *>(sectorBuf);
                FAT32DirEntry &e = entries[within];
                const unsigned char name0 = static_cast<unsigned char>(e.name[0]);
                if (name0 == 0x00)
                    return false;
                if (name0 == 0xE5)
                {
                    lfnClear(handle->pendingLongName, sizeof(handle->pendingLongName), handle->pendingLongNameChecksum, handle->pendingLongNameValid);
                    continue;
                }

                if ((e.attributes & FAT32Attr::LongName) == FAT32Attr::LongName)
                {
                    const FATLongNameEntry &lfn = *reinterpret_cast<const FATLongNameEntry *>(&e);
                    if (lfn.attributes == FAT32Attr::LongName && lfn.type == 0)
                    {
                        const bool isStart = (lfn.order & 0x40) != 0;
                        if (isStart)
                        {
                            lfnClear(handle->pendingLongName, sizeof(handle->pendingLongName), handle->pendingLongNameChecksum, handle->pendingLongNameValid);
                            handle->pendingLongNameChecksum = lfn.checksum;
                            handle->pendingLongNameValid = true;
                        }

                        if (handle->pendingLongNameValid && handle->pendingLongNameChecksum == lfn.checksum)
                        {
                            lfnConsume(lfn, handle->pendingLongName, sizeof(handle->pendingLongName));
                        }
                        else
                        {
                            lfnClear(handle->pendingLongName, sizeof(handle->pendingLongName), handle->pendingLongNameChecksum, handle->pendingLongNameValid);
                        }
                    }
                    continue;
                }

                if (e.attributes & FAT32Attr::VolumeId)
                {
                    lfnClear(handle->pendingLongName, sizeof(handle->pendingLongName), handle->pendingLongNameChecksum, handle->pendingLongNameValid);
                    continue;
                }
                if (name0 == static_cast<unsigned char>('.'))
                {
                    lfnClear(handle->pendingLongName, sizeof(handle->pendingLongName), handle->pendingLongNameChecksum, handle->pendingLongNameValid);
                    continue;
                }

                const bool hasValidLfn = handle->pendingLongNameValid &&
                                         handle->pendingLongName[0] != '\0' &&
                                         sfnChecksum(e.name) == handle->pendingLongNameChecksum;

                if (hasValidLfn)
                {
                    QC::String::strncpy(entry->name, handle->pendingLongName, sizeof(entry->name) - 1);
                    entry->name[sizeof(entry->name) - 1] = '\0';
                }
                else
                {
                    parseName(e, entry->name);
                }

                lfnClear(handle->pendingLongName, sizeof(handle->pendingLongName), handle->pendingLongNameChecksum, handle->pendingLongNameValid);
                entry->type = (e.attributes & FAT32Attr::Directory) ? FileType::Directory : FileType::Regular;
                entry->size = e.size;
                return true;
            }
            return false;
        }

        while (handle->currentCluster >= 2 && handle->currentCluster < FAT16_BAD)
        {
            if (!loadCluster(handle->currentCluster))
                return false;

            const QC::u32 entriesPerCluster = m_clusterSize / sizeof(FAT32DirEntry);
            auto *entries = reinterpret_cast<FAT32DirEntry *>(m_clusterBuffer);

            while (handle->entryIndex < entriesPerCluster)
            {
                FAT32DirEntry &e = entries[handle->entryIndex++];
                const unsigned char name0 = static_cast<unsigned char>(e.name[0]);
                if (name0 == 0x00)
                    return false;
                if (name0 == 0xE5)
                {
                    lfnClear(handle->pendingLongName, sizeof(handle->pendingLongName), handle->pendingLongNameChecksum, handle->pendingLongNameValid);
                    continue;
                }

                if ((e.attributes & FAT32Attr::LongName) == FAT32Attr::LongName)
                {
                    const FATLongNameEntry &lfn = *reinterpret_cast<const FATLongNameEntry *>(&e);
                    if (lfn.attributes == FAT32Attr::LongName && lfn.type == 0)
                    {
                        const bool isStart = (lfn.order & 0x40) != 0;
                        if (isStart)
                        {
                            lfnClear(handle->pendingLongName, sizeof(handle->pendingLongName), handle->pendingLongNameChecksum, handle->pendingLongNameValid);
                            handle->pendingLongNameChecksum = lfn.checksum;
                            handle->pendingLongNameValid = true;
                        }

                        if (handle->pendingLongNameValid && handle->pendingLongNameChecksum == lfn.checksum)
                        {
                            lfnConsume(lfn, handle->pendingLongName, sizeof(handle->pendingLongName));
                        }
                        else
                        {
                            lfnClear(handle->pendingLongName, sizeof(handle->pendingLongName), handle->pendingLongNameChecksum, handle->pendingLongNameValid);
                        }
                    }
                    continue;
                }

                if (e.attributes & FAT32Attr::VolumeId)
                {
                    lfnClear(handle->pendingLongName, sizeof(handle->pendingLongName), handle->pendingLongNameChecksum, handle->pendingLongNameValid);
                    continue;
                }
                if (name0 == static_cast<unsigned char>('.'))
                {
                    lfnClear(handle->pendingLongName, sizeof(handle->pendingLongName), handle->pendingLongNameChecksum, handle->pendingLongNameValid);
                    continue;
                }

                const bool hasValidLfn = handle->pendingLongNameValid &&
                                         handle->pendingLongName[0] != '\0' &&
                                         sfnChecksum(e.name) == handle->pendingLongNameChecksum;

                if (hasValidLfn)
                {
                    QC::String::strncpy(entry->name, handle->pendingLongName, sizeof(entry->name) - 1);
                    entry->name[sizeof(entry->name) - 1] = '\0';
                }
                else
                {
                    parseName(e, entry->name);
                }

                lfnClear(handle->pendingLongName, sizeof(handle->pendingLongName), handle->pendingLongNameChecksum, handle->pendingLongNameValid);
                entry->type = (e.attributes & FAT32Attr::Directory) ? FileType::Directory : FileType::Regular;
                entry->size = e.size;
                return true;
            }

            QC::u16 next = readFAT(handle->currentCluster);
            if (next >= FAT16_EOC)
                break;
            if (next < 2 || next == FAT16_BAD)
                break;
            handle->currentCluster = next;
            handle->entryIndex = 0;
        }

        return false;
    }

    void FAT16::rewindDir(Directory *dir)
    {
        if (!dir)
            return;
        auto *handle = static_cast<FATDirHandle *>(dir->handle());
        if (!handle)
            return;
        if (handle->isRoot)
        {
            handle->rootEntryIndex = 0;
            return;
        }
        handle->currentCluster = handle->startCluster;
        handle->entryIndex = 0;
    }

    QC::Status FAT16::stat(const char *path, FileInfo *info)
    {
        FAT32DirEntry *entry = findEntry(path);
        if (!entry)
            return QC::Status::NotFound;

        parseName(*entry, info->name);
        info->type = (entry->attributes & FAT32Attr::Directory) ? FileType::Directory : FileType::Regular;
        info->size = entry->size;
        info->createdTime = 0;
        info->modifiedTime = 0;
        info->accessedTime = 0;
        info->permissions = 0644;
        info->uid = 0;
        info->gid = 0;
        return QC::Status::Success;
    }

    QC::Status FAT16::createDir(const char *)
    {
        return QC::Status::NotSupported;
    }

    QC::Status FAT16::remove(const char *)
    {
        return QC::Status::NotSupported;
    }

    void FAT16::parseName(const char *fatName, char *outName)
    {
        int i = 0, j = 0;
        for (i = 0; i < 8 && fatName[i] != ' '; ++i)
            outName[j++] = fatName[i];
        if (fatName[8] != ' ')
        {
            outName[j++] = '.';
            for (i = 8; i < 11 && fatName[i] != ' '; ++i)
                outName[j++] = fatName[i];
        }
        outName[j] = '\0';
    }

    void FAT16::parseName(const FAT32DirEntry &entry, char *outName)
    {
        if (!outName)
            return;

        int i = 0, j = 0;
        const bool lowerBase = (entry.reserved & 0x08) != 0;
        const bool lowerExt = (entry.reserved & 0x10) != 0;

        for (i = 0; i < 8 && entry.name[i] != ' '; ++i)
        {
            char c = entry.name[i];
            if (lowerBase && isUpperAlpha(c))
                c = static_cast<char>(c + 32);
            outName[j++] = c;
        }
        if (entry.name[8] != ' ')
        {
            outName[j++] = '.';
            for (i = 8; i < 11 && entry.name[i] != ' '; ++i)
            {
                char c = entry.name[i];
                if (lowerExt && isUpperAlpha(c))
                    c = static_cast<char>(c + 32);
                outName[j++] = c;
            }
        }
        outName[j] = '\0';
    }

    void FAT16::formatName(const char *name, char *fatName)
    {
        QC::String::memset(fatName, ' ', 11);
        int i = 0, j = 0;
        while (name[i] && name[i] != '.' && j < 8)
        {
            fatName[j++] = (name[i] >= 'a' && name[i] <= 'z') ? (name[i] - 32) : name[i];
            ++i;
        }
        while (name[i] && name[i] != '.')
            ++i;
        if (name[i] == '.')
            ++i;
        j = 8;
        while (name[i] && j < 11)
        {
            fatName[j++] = (name[i] >= 'a' && name[i] <= 'z') ? (name[i] - 32) : name[i];
            ++i;
        }
    }

    bool FAT16::updateRootDirectoryEntry(QC::u32 entryIndex, const FAT32DirEntry &entry)
    {
        const QC::u32 entriesPerSector = 512U / sizeof(FAT32DirEntry);
        const QC::u32 sectorIndex = entryIndex / entriesPerSector;
        const QC::u32 within = entryIndex % entriesPerSector;
        if (sectorIndex >= m_rootDirSectors)
            return false;

        QC::u8 buf[512];
        const QC::u64 sector = static_cast<QC::u64>(m_rootDirStart) + sectorIndex;
        QC::Status rs = m_device->readSector(sector, buf);
        if (rs != QC::Status::Success)
        {
            QC_LOG_ERROR("QFSFAT16", "updateRootDirectoryEntry: readSector failed (sector=%llu status=%d)",
                         static_cast<unsigned long long>(sector), static_cast<int>(rs));
            return false;
        }
        auto *entries = reinterpret_cast<FAT32DirEntry *>(buf);
        entries[within] = entry;
        QC::Status ws = m_device->writeSector(sector, buf);
        if (ws != QC::Status::Success)
        {
            QC_LOG_ERROR("QFSFAT16", "updateRootDirectoryEntry: writeSector failed (sector=%llu status=%d)",
                         static_cast<unsigned long long>(sector), static_cast<int>(ws));
            return false;
        }
        return true;
    }

    bool FAT16::findFreeRootDirectoryEntry(QC::u32 *outEntryIndex)
    {
        if (!outEntryIndex)
            return false;
        const QC::u32 totalEntries = static_cast<QC::u32>(m_bootSector.rootEntryCount);
        const QC::u32 entriesPerSector = 512U / sizeof(FAT32DirEntry);

        QC::u8 buf[512];
        for (QC::u32 idx = 0; idx < totalEntries; ++idx)
        {
            const QC::u32 sectorIndex = idx / entriesPerSector;
            const QC::u32 within = idx % entriesPerSector;
            if (sectorIndex >= m_rootDirSectors)
            {
                QC_LOG_ERROR("QFSFAT16", "findFreeRootDirectoryEntry: sectorIndex out of range (idx=%u sectorIndex=%u rootDirSectors=%u)",
                             static_cast<unsigned>(idx), static_cast<unsigned>(sectorIndex), static_cast<unsigned>(m_rootDirSectors));
                return false;
            }
            const QC::u64 sector = static_cast<QC::u64>(m_rootDirStart) + sectorIndex;
            QC::Status rs = m_device->readSector(sector, buf);
            if (rs != QC::Status::Success)
            {
                QC_LOG_ERROR("QFSFAT16", "findFreeRootDirectoryEntry: readSector failed (sector=%llu status=%d)",
                             static_cast<unsigned long long>(sector), static_cast<int>(rs));
                return false;
            }
            auto *entries = reinterpret_cast<FAT32DirEntry *>(buf);
            const unsigned char name0 = static_cast<unsigned char>(entries[within].name[0]);
            if (name0 == 0x00 || name0 == 0xE5)
            {
                *outEntryIndex = idx;
                return true;
            }
        }

        QC_LOG_ERROR("QFSFAT16", "findFreeRootDirectoryEntry: no free entry (totalEntries=%u)", static_cast<unsigned>(totalEntries));
        return false;
    }

} // namespace QFS
