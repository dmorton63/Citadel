// QKDrvAHCI - Minimal AHCI SATA probing for block devices
// Namespace: QKDrv::AHCI

#include "AHCI/QKDrvAHCI.h"

#include "QArchPCI.h"
#include "QCBuiltins.h"
#include "QCLogger.h"
#include "QCString.h"
#include "QFSFAT32.h"
#include "QFSFATProbe.h"
#include "../../QKConsole.h"
#include "QKMemTranslator.h"
#include "QKStorageRegistry.h"

extern "C" QC::PhysAddr earlyAllocatePage();
extern "C" QC::VirtAddr physToVirt(QC::PhysAddr phys);

namespace
{
    static bool g_systemProbeCompleted = false;
    static QKDrv::AHCI::LastFailureInfo g_lastFailure{};

    constexpr QC::u32 kAhciGlobalHostControlAe = (1u << 31);
    constexpr QC::u32 kAhciPortCmdSt = (1u << 0);
    constexpr QC::u32 kAhciPortCmdClo = (1u << 3);
    constexpr QC::u32 kAhciPortCmdFre = (1u << 4);
    constexpr QC::u32 kAhciPortCmdFr = (1u << 14);
    constexpr QC::u32 kAhciPortCmdCr = (1u << 15);
    constexpr QC::u32 kAhciPortIsTfe = (1u << 30);
    constexpr QC::u32 kAhciPortSigAta = 0x00000101u;
    constexpr QC::u32 kAtaStatusBusy = (1u << 7);
    constexpr QC::u32 kAtaStatusDrq = (1u << 3);
    constexpr QC::u8 kFisTypeRegH2D = 0x27;
    constexpr QC::u8 kAtaCmdIdentify = 0xEC;
    constexpr QC::u8 kAtaCmdReadDma = 0xC8;
    constexpr QC::u8 kAtaCmdReadDmaExt = 0x25;
    constexpr QC::u8 kAtaCmdWriteDma = 0xCA;
    constexpr QC::u8 kAtaCmdWriteDmaExt = 0x35;
    constexpr QC::u8 kAtaCmdFlushCache = 0xE7;
    constexpr QC::u8 kAtaCmdFlushCacheExt = 0xEA;
    constexpr QC::u32 kDmaPageSize = 4096;
    constexpr QC::u32 kMaxTransferSectors = kDmaPageSize / 512;

    struct AhciHbaRegs
    {
        QC::u32 cap;
        QC::u32 ghc;
        QC::u32 is;
        QC::u32 pi;
        QC::u32 vs;
        QC::u32 cccCtl;
        QC::u32 cccPorts;
        QC::u32 emLoc;
        QC::u32 emCtl;
        QC::u32 cap2;
        QC::u32 bohc;
        QC::u8 reserved[0xA0 - 0x2C];
        QC::u8 vendor[0x100 - 0xA0];
    };

    struct AhciPortRegs
    {
        QC::u32 clb;
        QC::u32 clbu;
        QC::u32 fb;
        QC::u32 fbu;
        QC::u32 is;
        QC::u32 ie;
        QC::u32 cmd;
        QC::u32 reserved0;
        QC::u32 tfd;
        QC::u32 sig;
        QC::u32 ssts;
        QC::u32 sctl;
        QC::u32 serr;
        QC::u32 sact;
        QC::u32 ci;
        QC::u32 sntf;
        QC::u32 fbs;
        QC::u32 reserved1[11];
        QC::u32 vendor[4];
    };

    struct AhciCommandHeader
    {
        QC::u8 cfl;
        QC::u8 flags;
        QC::u16 prdtl;
        QC::u32 prdbc;
        QC::u32 ctba;
        QC::u32 ctbau;
        QC::u32 reserved[4];
    } __attribute__((packed));

    struct AhciPrdtEntry
    {
        QC::u32 dba;
        QC::u32 dbau;
        QC::u32 reserved;
        QC::u32 dbcInterrupt;
    } __attribute__((packed));

    struct AhciCommandTable
    {
        QC::u8 cfis[64];
        QC::u8 acmd[16];
        QC::u8 reserved[48];
        AhciPrdtEntry prdt[1];
    } __attribute__((packed));

    struct AhciFisRegH2D
    {
        QC::u8 fisType;
        QC::u8 pmPortAndFlags;
        QC::u8 command;
        QC::u8 featureLow;
        QC::u8 lba0;
        QC::u8 lba1;
        QC::u8 lba2;
        QC::u8 device;
        QC::u8 lba3;
        QC::u8 lba4;
        QC::u8 lba5;
        QC::u8 featureHigh;
        QC::u8 countLow;
        QC::u8 countHigh;
        QC::u8 icc;
        QC::u8 control;
        QC::u8 reserved[4];
    } __attribute__((packed));

    struct MbrPartitionEntry
    {
        QC::u8 status;
        QC::u8 chsFirst[3];
        QC::u8 type;
        QC::u8 chsLast[3];
        QC::u32 lbaFirst;
        QC::u32 lbaCount;
    } __attribute__((packed));

    static bool looksLikeMbr(const QC::u8 sector[512])
    {
        return sector[510] == 0x55 && sector[511] == 0xAA;
    }

    static bool mbrHasPartitionTable(const QC::u8 sector[512])
    {
        if (!looksLikeMbr(sector))
            return false;

        const auto *parts = reinterpret_cast<const MbrPartitionEntry *>(&sector[446]);
        for (int i = 0; i < 4; ++i)
        {
            if (parts[i].type != 0 && parts[i].lbaFirst != 0 && parts[i].lbaCount != 0)
                return true;
        }
        return false;
    }

    static bool looksLikeFatBootSector(const QC::u8 sector[512])
    {
        QFS::FATProbeResult probe{};
        if (!QFS::probeFATBootSector(sector, probe))
            return false;
        return probe.kind == QFS::FATKind::FAT16 || probe.kind == QFS::FATKind::FAT32;
    }

    static bool isFatPartitionType(QC::u8 type)
    {
        return type == 0x0B || type == 0x0C || type == 0x06 || type == 0x0E || type == 0x01 || type == 0x04;
    }

    static QC::u32 sectorCount28FromIdentify(const QC::u16 id[256])
    {
        return static_cast<QC::u32>(id[60]) | (static_cast<QC::u32>(id[61]) << 16);
    }

    static QC::u32 sectorCountFromIdentify(const QC::u16 id[256])
    {
        const QC::u64 count48 = static_cast<QC::u64>(id[100]) |
                                (static_cast<QC::u64>(id[101]) << 16) |
                                (static_cast<QC::u64>(id[102]) << 32) |
                                (static_cast<QC::u64>(id[103]) << 48);
        if (count48 != 0)
        {
            if (count48 > 0xFFFFFFFFULL)
                return 0;
            return static_cast<QC::u32>(count48);
        }

        return sectorCount28FromIdentify(id);
    }

    static void extractModelString(const QC::u16 id[256], char out[41])
    {
        if (!out)
            return;

        QC::String::memset(out, 0, 41);
        QC::usize pos = 0;
        for (QC::usize word = 27; word <= 46 && pos + 1 < 41; ++word)
        {
            const QC::u16 value = id[word];
            const char hi = static_cast<char>((value >> 8) & 0xFF);
            const char lo = static_cast<char>(value & 0xFF);
            if (hi != '\0' && pos + 1 < 41)
                out[pos++] = hi;
            if (lo != '\0' && pos + 1 < 41)
                out[pos++] = lo;
        }

        while (pos > 0 && (out[pos - 1] == ' ' || out[pos - 1] == '\t'))
            out[--pos] = '\0';
    }

    static bool canUseLba28Dma(QC::u64 lba, QC::u16 sectorCount)
    {
        return lba < 0x10000000ULL && sectorCount != 0 && sectorCount <= 0xFF;
    }

    static void writeFormatStage(const char *stage)
    {
        if (!stage)
            return;
        QK::Console::write("sysformat: ahci ");
        QK::Console::write(stage);
        QK::Console::write("\r\n");
        QC_LOG_INFO("QKDrvAHCI", "format stage: %s", stage);
    }

    static QC::u8 selectAtaDmaCommand(bool write, QC::u64 lba, QC::u16 sectorCount)
    {
        if (canUseLba28Dma(lba, sectorCount))
            return write ? kAtaCmdWriteDma : kAtaCmdReadDma;
        return write ? kAtaCmdWriteDmaExt : kAtaCmdReadDmaExt;
    }

    class OffsetBlockDevice final : public QFS::BlockDevice
    {
    public:
        OffsetBlockDevice(QFS::BlockDevice *inner, QC::u64 offsetSectors, QC::u64 visibleSectors)
            : m_inner(inner), m_offset(offsetSectors), m_visible(visibleSectors)
        {
        }

        QC::usize sectorSize() const override { return m_inner ? m_inner->sectorSize() : 512; }
        QC::u64 sectorCount() const override { return m_visible; }

        QC::Status readSector(QC::u64 sector, void *buffer) override
        {
            return readSectors(sector, 1, buffer);
        }

        QC::Status writeSector(QC::u64 sector, const void *buffer) override
        {
            return writeSectors(sector, 1, buffer);
        }

        QC::Status readSectors(QC::u64 sector, QC::usize count, void *buffer) override
        {
            if (!m_inner)
                return QC::Status::InvalidParam;
            if (sector + count > m_visible)
                return QC::Status::InvalidParam;
            return m_inner->readSectors(m_offset + sector, count, buffer);
        }

        QC::Status writeSectors(QC::u64 sector, QC::usize count, const void *buffer) override
        {
            if (!m_inner)
                return QC::Status::InvalidParam;
            if (sector + count > m_visible)
                return QC::Status::InvalidParam;
            return m_inner->writeSectors(m_offset + sector, count, buffer);
        }

    private:
        QFS::BlockDevice *m_inner;
        QC::u64 m_offset;
        QC::u64 m_visible;
    };

    class AhciPortBlockDevice final : public QFS::BlockDevice
    {
    public:
        AhciPortBlockDevice(QArch::PCIDevice *pciDevice,
                            QC::VirtAddr abar,
                            volatile AhciHbaRegs *hba,
                            volatile AhciPortRegs *portRegs,
                            QC::u8 portIndex,
                            QC::u32 sectors)
            : m_pciDevice(pciDevice),
              m_abar(abar),
              m_hba(hba),
              m_port(portRegs),
              m_portIndex(portIndex),
              m_sectorCount(sectors),
              m_cmdListPhys(0),
              m_cmdList(nullptr),
              m_receivedFisPhys(0),
              m_receivedFis(nullptr),
              m_cmdTablePhys(0),
              m_cmdTable(nullptr),
              m_dmaPhys(0),
              m_dmaBuffer(nullptr),
              m_initialized(false)
        {
        }

        QC::usize sectorSize() const override { return 512; }
        QC::u64 sectorCount() const override { return m_sectorCount; }

        QC::Status readSector(QC::u64 sector, void *buffer) override
        {
            return readSectors(sector, 1, buffer);
        }

        QC::Status writeSector(QC::u64 sector, const void *buffer) override
        {
            return writeSectors(sector, 1, buffer);
        }

        QC::Status readSectors(QC::u64 sector, QC::usize count, void *buffer) override
        {
            if (!buffer)
                return QC::Status::InvalidParam;
            return transfer(sector, count, buffer, false);
        }

        QC::Status writeSectors(QC::u64 sector, QC::usize count, const void *buffer) override
        {
            if (!buffer)
                return QC::Status::InvalidParam;
            return transfer(sector, count, const_cast<void *>(buffer), true);
        }

        QC::Status identify(QC::u16 outWords[256])
        {
            if (!outWords)
                return QC::Status::InvalidParam;
            QC::Status st = ensureInitialized();
            if (st != QC::Status::Success)
                return st;
            st = issueAtaCommand(kAtaCmdIdentify, 0, 1, false, m_dmaPhys, 512);
            if (st != QC::Status::Success)
                return st;
            QC::String::memcpy(outWords, m_dmaBuffer, 512);
            return QC::Status::Success;
        }

        void setSectorCount(QC::u32 sectors)
        {
            m_sectorCount = sectors;
        }

    private:
        static bool waitForClear(volatile QC::u32 &reg, QC::u32 mask, QC::usize spins)
        {
            while (spins--)
            {
                if ((reg & mask) == 0)
                    return true;
            }
            return false;
        }

        static bool waitForSet(volatile QC::u32 &reg, QC::u32 mask, QC::usize spins)
        {
            while (spins--)
            {
                if ((reg & mask) != 0)
                    return true;
            }
            return false;
        }

        QC::Status restartCommandEngine(bool requestClo)
        {
            m_port->cmd &= ~(kAhciPortCmdSt | kAhciPortCmdFre);
            if (!waitForClear(m_port->cmd, kAhciPortCmdCr | kAhciPortCmdFr, 1000000))
                return QC::Status::Timeout;

            m_port->is = 0xFFFFFFFFU;
            m_port->serr = 0xFFFFFFFFU;
            m_port->cmd |= kAhciPortCmdFre;
            if (!waitForSet(m_port->cmd, kAhciPortCmdFr, 1000000))
                return QC::Status::Timeout;

            if (requestClo)
            {
                m_port->cmd |= kAhciPortCmdClo;
                if (!waitForClear(m_port->cmd, kAhciPortCmdClo, 1000000))
                    return QC::Status::Timeout;
            }

            m_port->cmd |= kAhciPortCmdSt;
            return QC::Status::Success;
        }

        QC::Status ensureInitialized()
        {
            if (m_initialized)
                return QC::Status::Success;

            if (!m_pciDevice || !m_hba || !m_port)
                return QC::Status::InvalidParam;

            QArch::PCI::instance().enableBusMastering(m_pciDevice->address);
            QArch::PCI::instance().enableMemorySpace(m_pciDevice->address);
            m_hba->ghc |= kAhciGlobalHostControlAe;

            m_cmdListPhys = earlyAllocatePage();
            m_receivedFisPhys = earlyAllocatePage();
            m_cmdTablePhys = earlyAllocatePage();
            m_dmaPhys = earlyAllocatePage();
            if (!m_cmdListPhys || !m_receivedFisPhys || !m_cmdTablePhys || !m_dmaPhys)
                return QC::Status::OutOfMemory;

            m_cmdList = reinterpret_cast<AhciCommandHeader *>(physToVirt(m_cmdListPhys));
            m_receivedFis = reinterpret_cast<QC::u8 *>(physToVirt(m_receivedFisPhys));
            m_cmdTable = reinterpret_cast<AhciCommandTable *>(physToVirt(m_cmdTablePhys));
            m_dmaBuffer = reinterpret_cast<QC::u8 *>(physToVirt(m_dmaPhys));
            if (!m_cmdList || !m_receivedFis || !m_cmdTable || !m_dmaBuffer)
                return QC::Status::OutOfMemory;

            QC::String::memset(m_cmdList, 0, 1024);
            QC::String::memset(m_receivedFis, 0, 256);
            QC::String::memset(m_cmdTable, 0, 256);
            QC::String::memset(m_dmaBuffer, 0, kDmaPageSize);

            m_port->clb = static_cast<QC::u32>(m_cmdListPhys & 0xFFFFFFFFULL);
            m_port->clbu = static_cast<QC::u32>((m_cmdListPhys >> 32) & 0xFFFFFFFFULL);
            m_port->fb = static_cast<QC::u32>(m_receivedFisPhys & 0xFFFFFFFFULL);
            m_port->fbu = static_cast<QC::u32>((m_receivedFisPhys >> 32) & 0xFFFFFFFFULL);

            const QC::Status startSt = restartCommandEngine(false);
            if (startSt != QC::Status::Success)
                return startSt;

            m_initialized = true;
            return QC::Status::Success;
        }

        QC::Status issueAtaCommand(QC::u8 command,
                                   QC::u64 lba,
                                   QC::u16 sectorCount,
                                   bool write,
                                   QC::PhysAddr dataPhys,
                                   QC::u32 byteCount)
        {
            const bool hasData = (byteCount != 0);

            g_lastFailure = QKDrv::AHCI::LastFailureInfo{};

            QC::Status st = ensureInitialized();
            if (st != QC::Status::Success)
                return st;

            if (write)
            {
                st = restartCommandEngine(true);
                if (st != QC::Status::Success)
                    return st;
            }

            if (!waitForClear(m_port->tfd, kAtaStatusBusy | kAtaStatusDrq, 1000000))
                return QC::Status::Timeout;
            if (!waitForClear(m_port->ci, 0x1, 1000000))
                return QC::Status::Busy;

            AhciCommandHeader &header = m_cmdList[0];
            QC::String::memset(&header, 0, sizeof(header));
            header.cfl = sizeof(AhciFisRegH2D) / sizeof(QC::u32);
            if (write)
                header.cfl |= static_cast<QC::u8>(1u << 6);
            header.flags = 0;
            header.prdtl = hasData ? 1 : 0;
            header.ctba = static_cast<QC::u32>(m_cmdTablePhys & 0xFFFFFFFFULL);
            header.ctbau = static_cast<QC::u32>((m_cmdTablePhys >> 32) & 0xFFFFFFFFULL);

            QC::String::memset(m_cmdTable, 0, sizeof(AhciCommandTable));
            if (hasData)
            {
                m_cmdTable->prdt[0].dba = static_cast<QC::u32>(dataPhys & 0xFFFFFFFFULL);
                m_cmdTable->prdt[0].dbau = static_cast<QC::u32>((dataPhys >> 32) & 0xFFFFFFFFULL);
                m_cmdTable->prdt[0].dbcInterrupt = ((byteCount - 1u) & 0x3FFFFFu) | (1u << 31);
            }

            auto *fis = reinterpret_cast<AhciFisRegH2D *>(m_cmdTable->cfis);
            QC::String::memset(fis, 0, sizeof(AhciFisRegH2D));
            fis->fisType = kFisTypeRegH2D;
            fis->pmPortAndFlags = (1u << 7);
            fis->command = command;
            fis->device = (1u << 6);
            fis->lba0 = static_cast<QC::u8>(lba & 0xFF);
            fis->lba1 = static_cast<QC::u8>((lba >> 8) & 0xFF);
            fis->lba2 = static_cast<QC::u8>((lba >> 16) & 0xFF);
            fis->lba3 = static_cast<QC::u8>((lba >> 24) & 0xFF);
            fis->lba4 = static_cast<QC::u8>((lba >> 32) & 0xFF);
            fis->lba5 = static_cast<QC::u8>((lba >> 40) & 0xFF);
            fis->countLow = static_cast<QC::u8>(sectorCount & 0xFF);
            fis->countHigh = static_cast<QC::u8>((sectorCount >> 8) & 0xFF);

            QC::write_barrier();
            QC::memory_barrier();
            m_port->is = 0xFFFFFFFFU;
            m_port->ci = 0x1;
            QC::memory_barrier();
            (void)m_port->ci;

            QC::usize spins = 2000000;
            while (spins--)
            {
                if ((m_port->ci & 0x1) == 0)
                    break;
                if ((m_port->is & kAhciPortIsTfe) != 0)
                {
                    g_lastFailure.valid = true;
                    g_lastFailure.timeout = false;
                    g_lastFailure.portIndex = m_portIndex;
                    g_lastFailure.command = command;
                    g_lastFailure.lba = lba;
                    g_lastFailure.sectorCount = sectorCount;
                    g_lastFailure.interruptStatus = m_port->is;
                    g_lastFailure.taskFileData = m_port->tfd;
                    g_lastFailure.commandIssue = m_port->ci;
                    g_lastFailure.sataError = m_port->serr;
                    QC_LOG_WARN("QKDrvAHCI", "ATA cmd 0x%x failed on AHCI port %u (is=%08x tfd=%08x ci=%08x serr=%08x)",
                                static_cast<unsigned>(command),
                                static_cast<unsigned>(m_portIndex),
                                static_cast<unsigned>(m_port->is),
                                static_cast<unsigned>(m_port->tfd),
                                static_cast<unsigned>(m_port->ci),
                                static_cast<unsigned>(m_port->serr));
                    return QC::Status::Error;
                }
            }

            if ((m_port->ci & 0x1) != 0)
            {
                g_lastFailure.valid = true;
                g_lastFailure.timeout = true;
                g_lastFailure.portIndex = m_portIndex;
                g_lastFailure.command = command;
                g_lastFailure.lba = lba;
                g_lastFailure.sectorCount = sectorCount;
                g_lastFailure.interruptStatus = m_port->is;
                g_lastFailure.taskFileData = m_port->tfd;
                g_lastFailure.commandIssue = m_port->ci;
                g_lastFailure.sataError = m_port->serr;
                QC_LOG_WARN("QKDrvAHCI", "ATA cmd 0x%x timeout on AHCI port %u (lba=%llu count=%u is=%08x tfd=%08x ci=%08x serr=%08x)",
                            static_cast<unsigned>(command),
                            static_cast<unsigned>(m_portIndex),
                            static_cast<unsigned long long>(lba),
                            static_cast<unsigned>(sectorCount),
                            static_cast<unsigned>(m_port->is),
                            static_cast<unsigned>(m_port->tfd),
                            static_cast<unsigned>(m_port->ci),
                            static_cast<unsigned>(m_port->serr));
                (void)restartCommandEngine(true);
                return QC::Status::Timeout;
            }
            if ((m_port->is & kAhciPortIsTfe) != 0)
            {
                g_lastFailure.valid = true;
                g_lastFailure.timeout = false;
                g_lastFailure.portIndex = m_portIndex;
                g_lastFailure.command = command;
                g_lastFailure.lba = lba;
                g_lastFailure.sectorCount = sectorCount;
                g_lastFailure.interruptStatus = m_port->is;
                g_lastFailure.taskFileData = m_port->tfd;
                g_lastFailure.commandIssue = m_port->ci;
                g_lastFailure.sataError = m_port->serr;
                QC_LOG_WARN("QKDrvAHCI", "ATA cmd 0x%x completed with task-file error on AHCI port %u (is=%08x tfd=%08x serr=%08x)",
                            static_cast<unsigned>(command),
                            static_cast<unsigned>(m_portIndex),
                            static_cast<unsigned>(m_port->is),
                            static_cast<unsigned>(m_port->tfd),
                            static_cast<unsigned>(m_port->serr));
                return QC::Status::Error;
            }
            return QC::Status::Success;
        }

        QC::Status flushWriteCache(bool usedLba48)
        {
            const QC::u8 flushCmd = usedLba48 ? kAtaCmdFlushCacheExt : kAtaCmdFlushCache;
            return issueAtaCommand(flushCmd,
                                   0,
                                   0,
                                   false,
                                   0,
                                   0);
        }

        QC::Status transfer(QC::u64 sector, QC::usize count, void *buffer, bool write)
        {
            const QC::u64 startSector = sector;
            const QC::usize initialCount = count;
            if (count == 0)
                return QC::Status::Success;
            if (sector + count > m_sectorCount)
                return QC::Status::InvalidParam;

            QC::Status st = ensureInitialized();
            if (st != QC::Status::Success)
                return st;

            QC::u8 *bytes = static_cast<QC::u8 *>(buffer);
            QC::u64 currentSector = sector;
            QC::usize remaining = count;
            while (remaining)
            {
                const QC::u16 chunk = static_cast<QC::u16>((remaining > kMaxTransferSectors) ? kMaxTransferSectors : remaining);
                const QC::u32 byteCount = static_cast<QC::u32>(chunk) * 512u;

                if (write)
                    QC::String::memcpy(m_dmaBuffer, bytes, byteCount);

                st = issueAtaCommand(selectAtaDmaCommand(write, currentSector, chunk),
                                     currentSector,
                                     chunk,
                                     write,
                                     m_dmaPhys,
                                     byteCount);
                if (st != QC::Status::Success)
                    return st;

                if (!write)
                    QC::String::memcpy(bytes, m_dmaBuffer, byteCount);

                bytes += byteCount;
                currentSector += chunk;
                remaining -= chunk;
            }

            if (write)
            {
                const QC::u64 lastSector = startSector + static_cast<QC::u64>(initialCount) - 1;
                const bool usedLba48 = (lastSector >= 0x10000000ULL);
                st = flushWriteCache(usedLba48);
                if (st != QC::Status::Success)
                    return st;
            }

            return QC::Status::Success;
        }

        QArch::PCIDevice *m_pciDevice;
        QC::VirtAddr m_abar;
        volatile AhciHbaRegs *m_hba;
        volatile AhciPortRegs *m_port;
        QC::u8 m_portIndex;
        QC::u32 m_sectorCount;
        QC::PhysAddr m_cmdListPhys;
        AhciCommandHeader *m_cmdList;
        QC::PhysAddr m_receivedFisPhys;
        QC::u8 *m_receivedFis;
        QC::PhysAddr m_cmdTablePhys;
        AhciCommandTable *m_cmdTable;
        QC::PhysAddr m_dmaPhys;
        QC::u8 *m_dmaBuffer;
        bool m_initialized;
    };

    static bool isActiveSataPort(volatile AhciPortRegs *port)
    {
        if (!port)
            return false;
        const QC::u32 ssts = port->ssts;
        const QC::u32 det = ssts & 0x0F;
        const QC::u32 ipm = (ssts >> 8) & 0x0F;
        if (det != 0x3)
            return false;
        if (ipm != 0x1 && ipm != 0x2 && ipm != 0x6)
            return false;
        return port->sig == kAhciPortSigAta;
    }

    struct ResolvedAhciDisk
    {
        QArch::PCIDevice *pciDevice = nullptr;
        QC::VirtAddr abar = 0;
        volatile AhciHbaRegs *hba = nullptr;
        volatile AhciPortRegs *portRegs = nullptr;
        QC::u8 portIndex = 0;
    };

    static bool resolveDetectedDeviceIndex(QC::usize deviceIndex, ResolvedAhciDisk &out)
    {
        auto &pci = QArch::PCI::instance();
        const auto &devices = pci.devices();
        QC::usize detectedIndex = 0;

        for (QC::usize i = 0; i < devices.size(); ++i)
        {
            auto &dev = const_cast<QArch::PCIDevice &>(devices[i]);
            if (dev.classCode != QArch::PCIClass::MassStorage || dev.subclass != 0x06 || dev.progIF != 0x01)
                continue;

            const QC::PhysAddr abarPhys = dev.bar[5];
            if (abarPhys == 0)
                continue;

            pci.enableBusMastering(dev.address);
            pci.enableMemorySpace(dev.address);

            const QC::VirtAddr abar = QK::Memory::Translator::instance().mapMMIO(abarPhys, 0x2000);
            if (!abar)
                continue;

            auto *hba = reinterpret_cast<volatile AhciHbaRegs *>(abar);
            hba->ghc |= kAhciGlobalHostControlAe;

            const QC::u32 implementedPorts = hba->pi;
            for (QC::u32 port = 0; port < 32; ++port)
            {
                if ((implementedPorts & (1u << port)) == 0)
                    continue;

                auto *portRegs = reinterpret_cast<volatile AhciPortRegs *>(abar + 0x100 + (port * 0x80));
                if (!isActiveSataPort(portRegs))
                    continue;

                if (detectedIndex == deviceIndex)
                {
                    out.pciDevice = &dev;
                    out.abar = abar;
                    out.hba = hba;
                    out.portRegs = portRegs;
                    out.portIndex = static_cast<QC::u8>(port);
                    return true;
                }

                ++detectedIndex;
            }
        }

        return false;
    }

    static bool findFatPartition(QFS::BlockDevice *dev, QC::u64 &outOffset, QC::u64 &outSize, QFS::FATKind *outKind = nullptr)
    {
        QC::u8 sector0[512];
        const QC::Status st0 = dev->readSector(0, sector0);
        if (st0 != QC::Status::Success)
        {
            QC_LOG_WARN("QKDrvAHCI", "findFatPartition: read sector0 failed (status=%d)", static_cast<int>(st0));
            return false;
        }

        if (mbrHasPartitionTable(sector0))
        {
            const auto *parts = reinterpret_cast<const MbrPartitionEntry *>(&sector0[446]);
            for (int i = 0; i < 4; ++i)
            {
                if (!isFatPartitionType(parts[i].type))
                    continue;
                if (parts[i].lbaFirst == 0 || parts[i].lbaCount == 0)
                    continue;

                QC::u8 bs[512];
                const QC::Status st = dev->readSector(parts[i].lbaFirst, bs);
                if (st != QC::Status::Success)
                    continue;
                if (!looksLikeFatBootSector(bs))
                    continue;

                if (outKind)
                {
                    QFS::FATProbeResult probe{};
                    if (!QFS::probeFATBootSector(bs, probe))
                        continue;
                    *outKind = probe.kind;
                }

                outOffset = parts[i].lbaFirst;
                outSize = parts[i].lbaCount;
                return true;
            }

            return false;
        }

        if (looksLikeFatBootSector(sector0))
        {
            if (outKind)
            {
                QFS::FATProbeResult probe{};
                if (!QFS::probeFATBootSector(sector0, probe))
                    return false;
                *outKind = probe.kind;
            }
            outOffset = 0;
            outSize = dev->sectorCount();
            return true;
        }

        return false;
    }

    static QC::Status writeZeroSectors(QFS::BlockDevice *dev, QC::u64 startSector, QC::u32 count)
    {
        if (!dev)
            return QC::Status::InvalidParam;
        if (count == 0)
            return QC::Status::Success;

        constexpr QC::u32 kChunkSectors = 8;
        QC::u8 zeros[512 * kChunkSectors];
        QC::String::memset(zeros, 0, sizeof(zeros));

        QC::u64 sector = startSector;
        QC::u32 remaining = count;
        while (remaining)
        {
            const QC::u32 chunk = (remaining > kChunkSectors) ? kChunkSectors : remaining;
            const QC::Status st = dev->writeSectors(sector, chunk, zeros);
            if (st != QC::Status::Success)
                return st;
            sector += chunk;
            remaining -= chunk;
        }

        return QC::Status::Success;
    }

    static QC::Status writeMbrSingleFat32(QFS::BlockDevice *dev, QC::u32 lbaFirst, QC::u32 lbaCount)
    {
        if (!dev)
            return QC::Status::InvalidParam;
        if (lbaFirst == 0 || lbaCount == 0)
            return QC::Status::InvalidParam;

        QC::u8 mbr[512];
        QC::String::memset(mbr, 0, sizeof(mbr));

        auto *parts = reinterpret_cast<MbrPartitionEntry *>(&mbr[446]);
        parts[0].status = 0x00;
        QC::String::memset(parts[0].chsFirst, 0, sizeof(parts[0].chsFirst));
        parts[0].type = 0x0C;
        QC::String::memset(parts[0].chsLast, 0, sizeof(parts[0].chsLast));
        parts[0].lbaFirst = lbaFirst;
        parts[0].lbaCount = lbaCount;

        mbr[510] = 0x55;
        mbr[511] = 0xAA;
        return dev->writeSector(0, mbr);
    }

    static bool chooseFat32Layout(QC::u32 partSectors, QC::u8 &outSectorsPerCluster, QC::u32 &outSectorsPerFat)
    {
        constexpr QC::u16 kReserved = 32;
        constexpr QC::u8 kFatCount = 2;
        constexpr QC::u32 kMinFat32Clusters = 65525;
        constexpr QC::u32 kMaxFat32Clusters = 0x0FFFFFF5U;
        // Prefer larger clusters first; tiny clusters dramatically increase FAT allocation
        // work during installer payload copies.
        const QC::u8 candidates[] = {64, 32, 16, 8, 4, 2, 1};

        for (QC::u8 sectorsPerCluster : candidates)
        {
            QC::u32 sectorsPerFat = 1;
            for (int iter = 0; iter < 8; ++iter)
            {
                const QC::u32 fatArea = static_cast<QC::u32>(kFatCount) * sectorsPerFat;
                if (partSectors <= static_cast<QC::u32>(kReserved) + fatArea)
                    return false;

                const QC::u32 dataSectors = partSectors - static_cast<QC::u32>(kReserved) - fatArea;
                const QC::u32 totalClusters = dataSectors / sectorsPerCluster;
                const QC::u32 fatEntries = totalClusters + 2;
                const QC::u32 fatBytes = fatEntries * 4;
                const QC::u32 newSpf = (fatBytes + 511U) / 512U;
                if (newSpf == sectorsPerFat)
                    break;
                sectorsPerFat = (newSpf == 0) ? 1 : newSpf;
            }

            const QC::u32 fatArea = static_cast<QC::u32>(kFatCount) * sectorsPerFat;
            if (partSectors <= static_cast<QC::u32>(kReserved) + fatArea)
                continue;

            const QC::u32 dataSectors = partSectors - static_cast<QC::u32>(kReserved) - fatArea;
            const QC::u32 totalClusters = dataSectors / sectorsPerCluster;
            if (totalClusters < kMinFat32Clusters || totalClusters > kMaxFat32Clusters)
                continue;

            outSectorsPerCluster = sectorsPerCluster;
            outSectorsPerFat = sectorsPerFat;
            return true;
        }

        return false;
    }

    static bool hasMountableFatLayout(QFS::BlockDevice *dev)
    {
        if (!dev)
            return false;

        QC::u64 offset = 0;
        QC::u64 size = 0;
        return findFatPartition(dev, offset, size, nullptr);
    }

    static QC::Status formatFat32At(QFS::BlockDevice *dev, QC::u32 partStartLba, QC::u32 partSectors)
    {
        if (!dev)
            return QC::Status::InvalidParam;
        if (dev->sectorSize() != 512)
            return QC::Status::NotSupported;
        if (partStartLba == 0 || partSectors < 4096)
            return QC::Status::InvalidParam;

        constexpr QC::u16 kReserved = 32;
        constexpr QC::u8 kFatCount = 2;
        constexpr QC::u8 kMedia = 0xF8;
        constexpr QC::u32 kRootCluster = 2;
        constexpr QC::u16 kFsInfoSector = 1;
        constexpr QC::u16 kBackupBoot = 6;

        QC::u8 sectorsPerCluster = 0;
        QC::u32 sectorsPerFat = 0;
        if (!chooseFat32Layout(partSectors, sectorsPerCluster, sectorsPerFat))
            return QC::Status::InvalidParam;

        const QC::u32 totalSectors = partSectors;
        const QC::u32 fatStart = static_cast<QC::u32>(kReserved);
        const QC::u32 dataStart = fatStart + (static_cast<QC::u32>(kFatCount) * sectorsPerFat);
        if (totalSectors <= dataStart)
            return QC::Status::InvalidParam;

        QC::u8 boot[512];
        QC::String::memset(boot, 0, sizeof(boot));
        auto *bpb = reinterpret_cast<QFS::FAT32BootSector *>(boot);
        bpb->jump[0] = 0xEB;
        bpb->jump[1] = 0x58;
        bpb->jump[2] = 0x90;
        QC::String::memcpy(bpb->oemName, "MSWIN4.1", 8);
        bpb->bytesPerSector = 512;
        bpb->sectorsPerCluster = sectorsPerCluster;
        bpb->reservedSectors = kReserved;
        bpb->fatCount = kFatCount;
        bpb->rootEntryCount = 0;
        bpb->totalSectors16 = 0;
        bpb->mediaType = kMedia;
        bpb->sectorsPerFat16 = 0;
        bpb->sectorsPerTrack = 63;
        bpb->heads = 255;
        bpb->hiddenSectors = partStartLba;
        bpb->totalSectors32 = totalSectors;
        bpb->sectorsPerFat32 = sectorsPerFat;
        bpb->extFlags = 0;
        bpb->version = 0;
        bpb->rootCluster = kRootCluster;
        bpb->fsInfoSector = kFsInfoSector;
        bpb->backupBootSector = kBackupBoot;
        bpb->driveNumber = 0x80;
        bpb->bootSignature = 0x29;
        bpb->volumeId = 0x43495444U;
        QC::String::memcpy(bpb->volumeLabel, "CITADEL SYS", 11);
        QC::String::memcpy(bpb->fsType, "FAT32   ", 8);
        boot[510] = 0x55;
        boot[511] = 0xAA;

        QC::u8 fsinfo[512];
        QC::String::memset(fsinfo, 0, sizeof(fsinfo));
        fsinfo[0] = 'R';
        fsinfo[1] = 'R';
        fsinfo[2] = 'a';
        fsinfo[3] = 'A';
        fsinfo[484] = 'r';
        fsinfo[485] = 'r';
        fsinfo[486] = 'A';
        fsinfo[487] = 'a';
        *reinterpret_cast<QC::u32 *>(&fsinfo[488]) = 0xFFFFFFFFU;
        *reinterpret_cast<QC::u32 *>(&fsinfo[492]) = kRootCluster;
        fsinfo[508] = 0x00;
        fsinfo[509] = 0x00;
        fsinfo[510] = 0x55;
        fsinfo[511] = 0xAA;

        writeFormatStage("write boot");
        QC::Status st = dev->writeSector(partStartLba + 0, boot);
        if (st != QC::Status::Success)
            return st;

        writeFormatStage("write fsinfo");
        st = dev->writeSector(partStartLba + kFsInfoSector, fsinfo);
        if (st != QC::Status::Success)
            return st;

        writeFormatStage("write backup boot");
        st = dev->writeSector(partStartLba + kBackupBoot, boot);
        if (st != QC::Status::Success)
            return st;

        writeFormatStage("write backup fsinfo");
        st = dev->writeSector(partStartLba + kBackupBoot + kFsInfoSector, fsinfo);
        if (st != QC::Status::Success)
            return st;

        // Match the proven IDE formatter flow: keep reserved-sector cleanup
        // narrow, then seed each FAT with the header sector before zeroing the
        // remainder.
        writeFormatStage("zero reserved");
        for (QC::u16 i = 0; i < kReserved; ++i)
        {
            if (i == 0 || i == kFsInfoSector || i == kBackupBoot || i == static_cast<QC::u16>(kBackupBoot + kFsInfoSector))
                continue;
            st = writeZeroSectors(dev, partStartLba + i, 1);
            if (st != QC::Status::Success)
                return st;
        }

        QC::u8 fat0[512];
        QC::String::memset(fat0, 0, sizeof(fat0));
        fat0[0] = kMedia;
        fat0[1] = 0xFF;
        fat0[2] = 0xFF;
        fat0[3] = 0x0F;
        fat0[4] = 0xFF;
        fat0[5] = 0xFF;
        fat0[6] = 0xFF;
        fat0[7] = 0x0F;
        fat0[8] = 0xFF;
        fat0[9] = 0xFF;
        fat0[10] = 0xFF;
        fat0[11] = 0x0F;

        const QC::u32 fat1Lba = partStartLba + fatStart;
        const QC::u32 fat2Lba = fat1Lba + sectorsPerFat;

        writeFormatStage("write fat1 header");
        st = dev->writeSector(fat1Lba, fat0);
        if (st != QC::Status::Success)
            return st;
        if (sectorsPerFat > 1)
        {
            writeFormatStage("zero fat1 tail");
            st = writeZeroSectors(dev, fat1Lba + 1, sectorsPerFat - 1);
            if (st != QC::Status::Success)
                return st;
        }

        writeFormatStage("write fat2 header");
        st = dev->writeSector(fat2Lba, fat0);
        if (st != QC::Status::Success)
            return st;
        if (sectorsPerFat > 1)
        {
            writeFormatStage("zero fat2 tail");
            st = writeZeroSectors(dev, fat2Lba + 1, sectorsPerFat - 1);
            if (st != QC::Status::Success)
                return st;
        }

        writeFormatStage("zero root");
        const QC::u32 rootLba = partStartLba + dataStart;
        st = writeZeroSectors(dev, rootLba, sectorsPerCluster);
        if (st != QC::Status::Success)
            return st;

        writeFormatStage("format structures complete");
        return QC::Status::Success;
    }
}

namespace QKDrv
{
    namespace AHCI
    {
        QC::usize enumerateDetectedDevices(DetectedDeviceInfo *outDevices, QC::usize capacity)
        {
            if (!outDevices || capacity == 0)
                return 0;

            auto &pci = QArch::PCI::instance();
            const auto &devices = pci.devices();
            QC::usize written = 0;

            for (QC::usize i = 0; i < devices.size() && written < capacity; ++i)
            {
                auto &dev = const_cast<QArch::PCIDevice &>(devices[i]);
                if (dev.classCode != QArch::PCIClass::MassStorage || dev.subclass != 0x06 || dev.progIF != 0x01)
                    continue;

                const QC::PhysAddr abarPhys = dev.bar[5];
                if (abarPhys == 0)
                    continue;

                pci.enableBusMastering(dev.address);
                pci.enableMemorySpace(dev.address);

                const QC::VirtAddr abar = QK::Memory::Translator::instance().mapMMIO(abarPhys, 0x2000);
                if (!abar)
                    continue;

                auto *hba = reinterpret_cast<volatile AhciHbaRegs *>(abar);
                hba->ghc |= kAhciGlobalHostControlAe;

                const QC::u32 implementedPorts = hba->pi;
                for (QC::u32 port = 0; port < 32 && written < capacity; ++port)
                {
                    if ((implementedPorts & (1u << port)) == 0)
                        continue;

                    auto *portRegs = reinterpret_cast<volatile AhciPortRegs *>(abar + 0x100 + (port * 0x80));
                    if (!isActiveSataPort(portRegs))
                        continue;

                    auto &info = outDevices[written];
                    info = DetectedDeviceInfo{};
                    info.controllerBus = dev.address.bus;
                    info.controllerDevice = dev.address.device;
                    info.controllerFunction = dev.address.function;
                    info.controllerVendorId = dev.vendorId;
                    info.controllerDeviceId = dev.deviceId;
                    info.portIndex = static_cast<QC::u8>(port);
                    info.present = true;

                    AhciPortBlockDevice ahciDev(&dev, abar, hba, portRegs, static_cast<QC::u8>(port), 0);
                    QC::u16 id[256];
                    if (ahciDev.identify(id) == QC::Status::Success)
                    {
                        const QC::u32 sectors = sectorCountFromIdentify(id);
                        info.sectors = sectors;
                        extractModelString(id, info.model);
                        ahciDev.setSectorCount(sectors);

                        if (sectors != 0)
                        {
                            QC::u8 sector0[512];
                            if (ahciDev.readSector(0, sector0) == QC::Status::Success)
                            {
                                info.hasMbrSignature = looksLikeMbr(sector0);
                                info.hasPartitionTable = mbrHasPartitionTable(sector0);
                                info.hasFatBootSector = looksLikeFatBootSector(sector0);
                                QC::u64 offset = 0;
                                QC::u64 size = 0;
                                info.mountableFat = findFatPartition(&ahciDev, offset, size, nullptr);
                            }
                        }
                    }

                    ++written;
                }
            }

            return written;
        }

        QC::Status formatSystemVolumeFAT32(bool force)
        {
            auto &pci = QArch::PCI::instance();
            const auto &devices = pci.devices();
            QC::Status lastError = QC::Status::NotFound;

            for (QC::usize i = 0; i < devices.size(); ++i)
            {
                auto &dev = const_cast<QArch::PCIDevice &>(devices[i]);
                if (dev.classCode != QArch::PCIClass::MassStorage || dev.subclass != 0x06 || dev.progIF != 0x01)
                    continue;

                const QC::PhysAddr abarPhys = dev.bar[5];
                if (abarPhys == 0)
                    continue;

                pci.enableBusMastering(dev.address);
                pci.enableMemorySpace(dev.address);

                const QC::VirtAddr abar = QK::Memory::Translator::instance().mapMMIO(abarPhys, 0x2000);
                if (!abar)
                {
                    lastError = QC::Status::OutOfMemory;
                    continue;
                }

                auto *hba = reinterpret_cast<volatile AhciHbaRegs *>(abar);
                hba->ghc |= kAhciGlobalHostControlAe;

                const QC::u32 implementedPorts = hba->pi;
                for (QC::u32 port = 0; port < 32; ++port)
                {
                    if ((implementedPorts & (1u << port)) == 0)
                        continue;

                    auto *portRegs = reinterpret_cast<volatile AhciPortRegs *>(abar + 0x100 + (port * 0x80));
                    if (!isActiveSataPort(portRegs))
                        continue;

                    auto *rawDev = new AhciPortBlockDevice(&dev, abar, hba, portRegs, static_cast<QC::u8>(port), 0);
                    QC::u16 id[256];
                    QC::Status st = rawDev->identify(id);
                    if (st != QC::Status::Success)
                    {
                        lastError = st;
                        continue;
                    }

                    const QC::u32 sectors = sectorCountFromIdentify(id);
                    if (sectors == 0)
                    {
                        lastError = QC::Status::NotSupported;
                        continue;
                    }
                    rawDev->setSectorCount(sectors);

                    QC::u8 sector0[512];
                    st = rawDev->readSector(0, sector0);
                    if (st != QC::Status::Success)
                    {
                        QC_LOG_WARN("QKDrvAHCI", "formatSystemVolumeFAT32: read sector0 failed on port %u (status=%d)",
                                    static_cast<unsigned>(port), static_cast<int>(st));
                        lastError = st;
                        continue;
                    }

                    if (hasMountableFatLayout(rawDev))
                    {
                        if (force)
                        {
                            QC_LOG_WARN("QKDrvAHCI", "Forcing format of AHCI port %u despite existing mountable FAT layout",
                                        static_cast<unsigned>(port));
                        }
                        else
                        {
                        QC_LOG_WARN("QKDrvAHCI", "Refusing to format AHCI port %u: disk already appears partitioned/formatted",
                                    static_cast<unsigned>(port));
                        return QC::Status::Busy;
                        }
                    }

                    if (mbrHasPartitionTable(sector0) || looksLikeFatBootSector(sector0) || looksLikeMbr(sector0))
                    {
                        QC_LOG_WARN("QKDrvAHCI", "AHCI port %u has stale partition/boot signatures without a mountable FAT volume; reformatting",
                                    static_cast<unsigned>(port));
                    }

                    const QC::u64 total = rawDev->sectorCount();
                    QC::u32 partStart = 2048;
                    if (total <= static_cast<QC::u64>(partStart + 4096))
                        partStart = 1;
                    if (total <= static_cast<QC::u64>(partStart + 4096))
                        return QC::Status::InvalidParam;

                    const QC::u64 partSectors64 = total - partStart;
                    if (partSectors64 > 0xFFFFFFFFULL)
                        return QC::Status::NotSupported;
                    const QC::u32 partSectors = static_cast<QC::u32>(partSectors64);

                    QC_LOG_INFO("QKDrvAHCI", "Formatting AHCI SATA disk on port %u as FAT32 (lba=%u sectors=%u)",
                                static_cast<unsigned>(port), partStart, partSectors);

                    writeFormatStage("write mbr");
                    st = writeMbrSingleFat32(rawDev, partStart, partSectors);
                    if (st != QC::Status::Success)
                        return st;

                    writeFormatStage("format fat32");
                    st = formatFat32At(rawDev, partStart, partSectors);
                    if (st != QC::Status::Success)
                        return st;

                    QC_LOG_INFO("QKDrvAHCI", "AHCI SATA disk on port %u FAT32 format complete", static_cast<unsigned>(port));
                    return QC::Status::Success;
                }
            }

            QC_LOG_WARN("QKDrvAHCI", "formatSystemVolumeFAT32: no suitable AHCI SATA disk found");
            return lastError;
        }

        QC::Status formatDetectedDeviceFAT32(QC::usize deviceIndex, bool force)
        {
            ResolvedAhciDisk resolved{};
            if (!resolveDetectedDeviceIndex(deviceIndex, resolved))
            {
                QC_LOG_WARN("QKDrvAHCI", "formatDetectedDeviceFAT32: device index %u not found", static_cast<unsigned>(deviceIndex));
                return QC::Status::NotFound;
            }

            AhciPortBlockDevice dev(resolved.pciDevice,
                                    resolved.abar,
                                    resolved.hba,
                                    resolved.portRegs,
                                    resolved.portIndex,
                                    0);
            QC::u16 id[256];
            QC::Status st = dev.identify(id);
            if (st != QC::Status::Success)
                return st;

            const QC::u32 sectors = sectorCountFromIdentify(id);
            if (sectors == 0)
                return QC::Status::NotSupported;
            dev.setSectorCount(sectors);

            QC::u8 sector0[512];
            st = dev.readSector(0, sector0);
            if (st != QC::Status::Success)
                return st;

            if (hasMountableFatLayout(&dev))
            {
                if (force)
                {
                    QC_LOG_WARN("QKDrvAHCI", "Forcing format of disk%u despite existing mountable FAT layout", static_cast<unsigned>(deviceIndex));
                }
                else
                {
                    QC_LOG_WARN("QKDrvAHCI", "Refusing to format disk%u: AHCI device already appears partitioned/formatted", static_cast<unsigned>(deviceIndex));
                    return QC::Status::Busy;
                }
            }

            if (mbrHasPartitionTable(sector0) || looksLikeFatBootSector(sector0) || looksLikeMbr(sector0))
            {
                QC_LOG_WARN("QKDrvAHCI", "disk%u has stale partition/boot signatures without a mountable FAT volume; reformatting",
                            static_cast<unsigned>(deviceIndex));
            }

            const QC::u64 total = dev.sectorCount();
            QC::u32 partStart = 2048;
            if (total <= static_cast<QC::u64>(partStart + 4096))
                partStart = 1;
            if (total <= static_cast<QC::u64>(partStart + 4096))
                return QC::Status::InvalidParam;

            const QC::u64 partSectors64 = total - partStart;
            if (partSectors64 > 0xFFFFFFFFULL)
                return QC::Status::NotSupported;
            const QC::u32 partSectors = static_cast<QC::u32>(partSectors64);

            QC_LOG_INFO("QKDrvAHCI", "Formatting disk%u (AHCI port %u) as FAT32 (lba=%u sectors=%u)",
                        static_cast<unsigned>(deviceIndex),
                        static_cast<unsigned>(resolved.portIndex),
                        partStart,
                        partSectors);

            writeFormatStage("write mbr");
            st = writeMbrSingleFat32(&dev, partStart, partSectors);
            if (st != QC::Status::Success)
                return st;

            writeFormatStage("format fat32");
            st = formatFat32At(&dev, partStart, partSectors);
            if (st != QC::Status::Success)
                return st;

            QC_LOG_INFO("QKDrvAHCI", "disk%u (AHCI) FAT32 format complete", static_cast<unsigned>(deviceIndex));
            return QC::Status::Success;
        }

        bool probeAndRegisterSystemVolume()
        {
            if (g_systemProbeCompleted)
                return false;
            g_systemProbeCompleted = true;

            QC_LOG_INFO("QKDrvAHCI", "Probing AHCI controllers for system volume");

            auto &pci = QArch::PCI::instance();
            const auto &devices = pci.devices();
            for (QC::usize i = 0; i < devices.size(); ++i)
            {
                auto &dev = const_cast<QArch::PCIDevice &>(devices[i]);
                if (dev.classCode != QArch::PCIClass::MassStorage || dev.subclass != 0x06 || dev.progIF != 0x01)
                    continue;

                const QC::PhysAddr abarPhys = dev.bar[5];
                if (abarPhys == 0)
                    continue;

                pci.enableBusMastering(dev.address);
                pci.enableMemorySpace(dev.address);

                const QC::VirtAddr abar = QK::Memory::Translator::instance().mapMMIO(abarPhys, 0x2000);
                if (!abar)
                {
                    QC_LOG_WARN("QKDrvAHCI", "Failed to map ABAR for %04x:%04x", dev.vendorId, dev.deviceId);
                    continue;
                }

                auto *hba = reinterpret_cast<volatile AhciHbaRegs *>(abar);
                hba->ghc |= kAhciGlobalHostControlAe;

                const QC::u32 implementedPorts = hba->pi;
                for (QC::u32 port = 0; port < 32; ++port)
                {
                    if ((implementedPorts & (1u << port)) == 0)
                        continue;

                    auto *portRegs = reinterpret_cast<volatile AhciPortRegs *>(abar + 0x100 + (port * 0x80));
                    if (!isActiveSataPort(portRegs))
                        continue;

                    auto *rawDev = new AhciPortBlockDevice(&dev, abar, hba, portRegs, static_cast<QC::u8>(port), 0);
                    QC::u16 id[256];
                    QC::Status idSt = rawDev->identify(id);
                    if (idSt != QC::Status::Success)
                    {
                        QC_LOG_WARN("QKDrvAHCI", "AHCI port %u identify failed (status=%d)", static_cast<unsigned>(port), static_cast<int>(idSt));
                        continue;
                    }

                    const QC::u32 sectors = sectorCountFromIdentify(id);
                    if (sectors == 0)
                    {
                        QC_LOG_WARN("QKDrvAHCI", "AHCI port %u identify succeeded but sector count is zero", static_cast<unsigned>(port));
                        continue;
                    }

                    rawDev->setSectorCount(sectors);

                    QC::u64 offset = 0;
                    QC::u64 size = 0;
                    QFS::FATKind fatKind = QFS::FATKind::Unknown;
                    if (!findFatPartition(rawDev, offset, size, &fatKind))
                    {
                        QC_LOG_INFO("QKDrvAHCI", "AHCI SATA disk present on port %u but no FAT volume found", static_cast<unsigned>(port));
                        continue;
                    }

                    if (fatKind != QFS::FATKind::FAT32)
                    {
                        QC_LOG_INFO("QKDrvAHCI", "AHCI SATA disk present on port %u but not FAT32 (kind=%u)",
                                    static_cast<unsigned>(port), static_cast<unsigned>(fatKind));
                        continue;
                    }

                    QFS::BlockDevice *mountDev = rawDev;
                    if (offset != 0 || size != rawDev->sectorCount())
                        mountDev = new OffsetBlockDevice(rawDev, offset, size);

                    QKStorage::BlockDeviceRegistration reg{};
                    reg.name = "QFS_SYSTEM";
                    reg.mountPath = "/system";
                    reg.fsKind = QFS::FileSystemKind::FAT_AUTO;
                    reg.device = mountDev;
                    reg.autoMount = true;
                    reg.sourceKind = "ahci";
                    reg.sourceDetail = "sata port";
                    reg.persistent = true;

                    const QC::Status st = QKStorage::registerBlockDevice(reg);
                    if (st == QC::Status::Success || st == QC::Status::Busy)
                    {
                        QC_LOG_INFO("QKDrvAHCI", "Registered AHCI system volume on port %u (offset=%llu)",
                                    static_cast<unsigned>(port),
                                    static_cast<unsigned long long>(offset));
                        return true;
                    }

                    QC_LOG_WARN("QKDrvAHCI", "Failed to register AHCI system volume (status=%d)", static_cast<int>(st));
                }
            }

            QC_LOG_WARN("QKDrvAHCI", "No mountable AHCI FAT32 system volume detected");
            return false;
        }

        void resetSystemProbe()
        {
            g_systemProbeCompleted = false;
        }

        bool getLastFailure(LastFailureInfo &out)
        {
            if (!g_lastFailure.valid)
                return false;
            out = g_lastFailure;
            return true;
        }
    }
}