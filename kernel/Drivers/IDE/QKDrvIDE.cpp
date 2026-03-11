// QKDrvIDE - Minimal legacy IDE/ATA PIO probing for block devices
// Namespace: QKDrv::IDE

#include "IDE/QKDrvIDE.h"

#include "QArchPort.h"
#include "QCLogger.h"
#include "QCString.h"

#include "QFSFAT32.h"
#include "QFSFATProbe.h"

#include "QKStorageRegistry.h"

namespace
{
    static bool g_sharedProbeEnabled = false;
    static bool g_sharedProbeCompleted = false;

    constexpr QC::u16 kPrimaryBase = 0x1F0;
    constexpr QC::u16 kPrimaryCtrl = 0x3F6;
    constexpr QC::u16 kSecondaryBase = 0x170;
    constexpr QC::u16 kSecondaryCtrl = 0x376;

    constexpr QC::usize kSpinsNotBusy = 20000;
    constexpr QC::usize kSpinsDrq = 60000;

    enum StatusBits : QC::u8
    {
        STATUS_ERR = 1 << 0,
        STATUS_DRQ = 1 << 3,
        STATUS_SRV = 1 << 4,
        STATUS_DF = 1 << 5,
        STATUS_RDY = 1 << 6,
        STATUS_BSY = 1 << 7,
    };

    enum Reg : QC::u16
    {
        REG_DATA = 0,
        REG_ERROR = 1,
        REG_FEATURES = 1,
        REG_SECCOUNT0 = 2,
        REG_LBA0 = 3,
        REG_LBA1 = 4,
        REG_LBA2 = 5,
        REG_HDDEVSEL = 6,
        REG_STATUS = 7,
        REG_COMMAND = 7,
    };

    enum Command : QC::u8
    {
        CMD_READ_SECTORS = 0x20,
        CMD_WRITE_SECTORS = 0x30,
        CMD_IDENTIFY = 0xEC,
    };

    static inline QC::u8 ioReadStatus(QC::u16 base)
    {
        return QArch::inb(static_cast<QC::u16>(base + REG_STATUS));
    }

    static inline QC::u8 ioReadAltStatus(QC::u16 ctrl)
    {
        return QArch::inb(static_cast<QC::u16>(ctrl + 0));
    }

    static inline QC::u8 ioReadStatusReg(QC::u16 base)
    {
        return QArch::inb(static_cast<QC::u16>(base + REG_STATUS));
    }

    static bool waitNotBusy(QC::u16 ctrl, QC::usize spins)
    {
        while (spins--)
        {
            QC::u8 s = ioReadAltStatus(ctrl);
            if (s == 0x00 || s == 0xFF)
                return false;
            if ((s & STATUS_BSY) == 0)
                return true;
        }
        return false;
    }

    static bool waitNotBusyStatus(QC::u16 base, QC::usize spins)
    {
        while (spins--)
        {
            QC::u8 s = ioReadStatusReg(base);
            if (s == 0x00 || s == 0xFF)
                return false;
            if ((s & STATUS_BSY) == 0)
                return true;
        }
        return false;
    }

    static bool waitDrqOrErr(QC::u16 ctrl, QC::u16 base, QC::usize spins)
    {
        while (spins--)
        {
            QC::u8 s = ioReadAltStatus(ctrl);
            if (s == 0x00 || s == 0xFF)
                return false;
            if (s & STATUS_ERR)
                return false;
            if ((s & STATUS_BSY) == 0 && (s & STATUS_DRQ))
                return true;
        }
        // Fall back to regular status read (some emulations are quirky)
        QC::u8 s = ioReadStatus(base);
        return ((s & STATUS_BSY) == 0) && ((s & STATUS_DRQ) != 0) && ((s & STATUS_ERR) == 0);
    }

    static bool waitDrqOrErrStatus(QC::u16 base, QC::usize spins)
    {
        while (spins--)
        {
            QC::u8 s = ioReadStatusReg(base);
            if (s == 0x00 || s == 0xFF)
                return false;
            if (s & STATUS_ERR)
                return false;
            if ((s & STATUS_BSY) == 0 && (s & STATUS_DRQ))
                return true;
        }
        return false;
    }

    static bool waitNotBusyThenDrq(QC::u16 ctrl, QC::u16 base, QC::usize spins)
    {
        if (!waitNotBusy(ctrl, spins))
            return false;
        return waitDrqOrErr(ctrl, base, spins);
    }

    static void selectDrive(QC::u16 base, QC::u16 ctrl, bool slave)
    {
        // 0xA0 = CHS, 0xE0 = LBA; keep LBA bit set.
        // For IDENTIFY, upper LBA bits are ignored.
        QArch::outb(static_cast<QC::u16>(base + REG_HDDEVSEL), static_cast<QC::u8>(0xE0 | (slave ? 0x10 : 0x00)));
        QArch::io_wait();
        (void)ioReadAltStatus(ctrl);
        (void)ioReadAltStatus(ctrl);
        (void)ioReadAltStatus(ctrl);
        (void)ioReadAltStatus(ctrl);
    }

    static bool identify(QC::u16 base, QC::u16 ctrl, bool slave, QC::u16 outWords[256])
    {
        selectDrive(base, ctrl, slave);

        // If status is 0xFF (floating bus) or 0x00 (no device), bail immediately.
        QC::u8 status = ioReadStatus(base);
        if (status == 0xFF || status == 0x00)
            return false;

        // Zero out task file regs
        QArch::outb(static_cast<QC::u16>(base + REG_SECCOUNT0), 0);
        QArch::outb(static_cast<QC::u16>(base + REG_LBA0), 0);
        QArch::outb(static_cast<QC::u16>(base + REG_LBA1), 0);
        QArch::outb(static_cast<QC::u16>(base + REG_LBA2), 0);
        QArch::outb(static_cast<QC::u16>(base + REG_COMMAND), CMD_IDENTIFY);

        // If device doesn't exist, status may become 0.
        status = ioReadStatus(base);
        if (status == 0 || status == 0xFF)
            return false;

        if (!waitNotBusy(ctrl, kSpinsNotBusy))
            return false;

        // Check for ATAPI signature (LBA1=0x14, LBA2=0xEB or LBA1=0x69, LBA2=0x96)
        QC::u8 lba1 = QArch::inb(static_cast<QC::u16>(base + REG_LBA1));
        QC::u8 lba2 = QArch::inb(static_cast<QC::u16>(base + REG_LBA2));
        if ((lba1 == 0x14 && lba2 == 0xEB) || (lba1 == 0x69 && lba2 == 0x96))
            return false;

        if (!waitDrqOrErr(ctrl, base, kSpinsDrq))
            return false;

        QArch::insw(static_cast<QC::u16>(base + REG_DATA), outWords, 256);
        return true;
    }

    static QC::u32 sectorCount28FromIdentify(const QC::u16 id[256])
    {
        // Words 60-61: total number of user addressable sectors for 28-bit LBA
        return static_cast<QC::u32>(id[60]) | (static_cast<QC::u32>(id[61]) << 16);
    }

    class AtaPioBlockDevice final : public QFS::BlockDevice
    {
    public:
        AtaPioBlockDevice(QC::u16 ioBase, QC::u16 ctrlBase, bool slave, QC::u32 sectors)
            : m_base(ioBase), m_ctrl(ctrlBase), m_slave(slave), m_sectorCount(sectors)
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
            if (count == 0)
                return QC::Status::Success;
            if (sector + count > m_sectorCount)
                return QC::Status::InvalidParam;
            if (sector >= 0x10000000ULL)
                return QC::Status::NotSupported; // LBA28 limit

            QC::u8 *out = static_cast<QC::u8 *>(buffer);
            QC::u64 lba = sector;
            QC::usize remaining = count;

            while (remaining)
            {
                QC::u8 thisCount = static_cast<QC::u8>((remaining > 255) ? 255 : remaining);

                selectDrive(m_base, m_ctrl, m_slave);
                if (!waitNotBusyStatus(m_base, kSpinsNotBusy))
                    return QC::Status::Timeout;

                QArch::outb(static_cast<QC::u16>(m_base + REG_HDDEVSEL),
                            static_cast<QC::u8>(0xE0 | (m_slave ? 0x10 : 0x00) | ((lba >> 24) & 0x0F)));
                QArch::outb(static_cast<QC::u16>(m_base + REG_SECCOUNT0), thisCount);
                QArch::outb(static_cast<QC::u16>(m_base + REG_LBA0), static_cast<QC::u8>(lba & 0xFF));
                QArch::outb(static_cast<QC::u16>(m_base + REG_LBA1), static_cast<QC::u8>((lba >> 8) & 0xFF));
                QArch::outb(static_cast<QC::u16>(m_base + REG_LBA2), static_cast<QC::u8>((lba >> 16) & 0xFF));
                QArch::outb(static_cast<QC::u16>(m_base + REG_COMMAND), CMD_READ_SECTORS);

                for (QC::u8 i = 0; i < thisCount; ++i)
                {
                    if (!waitDrqOrErr(m_ctrl, m_base, kSpinsDrq))
                        return QC::Status::Error;

                    QArch::insw(static_cast<QC::u16>(m_base + REG_DATA), out, 256);
                    out += 512;
                }

                lba += thisCount;
                remaining -= thisCount;
            }

            return QC::Status::Success;
        }

        QC::Status writeSectors(QC::u64 sector, QC::usize count, const void *buffer) override
        {
            if (!buffer)
                return QC::Status::InvalidParam;
            if (count == 0)
                return QC::Status::Success;
            if (sector + count > m_sectorCount)
                return QC::Status::InvalidParam;
            if (sector >= 0x10000000ULL)
                return QC::Status::NotSupported; // LBA28 limit

            const QC::u8 *in = static_cast<const QC::u8 *>(buffer);
            QC::u64 lba = sector;
            QC::usize remaining = count;

            while (remaining)
            {
                QC::u8 thisCount = static_cast<QC::u8>((remaining > 255) ? 255 : remaining);

                selectDrive(m_base, m_ctrl, m_slave);
                if (!waitNotBusy(m_ctrl, kSpinsNotBusy))
                    return QC::Status::Timeout;

                QArch::outb(static_cast<QC::u16>(m_base + REG_HDDEVSEL),
                            static_cast<QC::u8>(0xE0 | (m_slave ? 0x10 : 0x00) | ((lba >> 24) & 0x0F)));
                QArch::outb(static_cast<QC::u16>(m_base + REG_SECCOUNT0), thisCount);
                QArch::outb(static_cast<QC::u16>(m_base + REG_LBA0), static_cast<QC::u8>(lba & 0xFF));
                QArch::outb(static_cast<QC::u16>(m_base + REG_LBA1), static_cast<QC::u8>((lba >> 8) & 0xFF));
                QArch::outb(static_cast<QC::u16>(m_base + REG_LBA2), static_cast<QC::u8>((lba >> 16) & 0xFF));
                QArch::outb(static_cast<QC::u16>(m_base + REG_COMMAND), CMD_WRITE_SECTORS);

                for (QC::u8 i = 0; i < thisCount; ++i)
                {
                    if (!waitDrqOrErrStatus(m_base, kSpinsDrq))
                        return QC::Status::Error;

                    QArch::outsw(static_cast<QC::u16>(m_base + REG_DATA), in, 256);
                    in += 512;

                    (void)ioReadStatus(m_base);
                }

                lba += thisCount;
                remaining -= thisCount;

                if (!waitNotBusyStatus(m_base, kSpinsNotBusy))
                    return QC::Status::Timeout;
            }

            return QC::Status::Success;
        }

    private:
        QC::u16 m_base;
        QC::u16 m_ctrl;
        bool m_slave;
        QC::u32 m_sectorCount;
    };

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

    static bool looksLikeFatBootSector(const QC::u8 sector[512])
    {
        // Minimal sanity checks; filesystem mount will do deeper parsing.
        QFS::FATProbeResult probe;
        if (!QFS::probeFATBootSector(sector, probe))
            return false;
        return probe.kind == QFS::FATKind::FAT16 || probe.kind == QFS::FATKind::FAT32;
    }

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

    static bool isFatPartitionType(QC::u8 type)
    {
        // Common FAT types (including FAT32 variants)
        return type == 0x0B || type == 0x0C || type == 0x06 || type == 0x0E || type == 0x01 || type == 0x04;
    }

    static bool findFatPartition(QFS::BlockDevice *dev, QC::u64 &outOffset, QC::u64 &outSize)
    {
        QC::u8 sector0[512];
        if (dev->readSector(0, sector0) != QC::Status::Success)
            return false;

        if (looksLikeFatBootSector(sector0))
        {
            outOffset = 0;
            outSize = dev->sectorCount();
            return true;
        }

        if (!looksLikeMbr(sector0))
            return false;

        const auto *parts = reinterpret_cast<const MbrPartitionEntry *>(&sector0[446]);
        for (int i = 0; i < 4; ++i)
        {
            if (!isFatPartitionType(parts[i].type))
                continue;
            if (parts[i].lbaFirst == 0 || parts[i].lbaCount == 0)
                continue;

            QC::u8 bs[512];
            if (dev->readSector(parts[i].lbaFirst, bs) != QC::Status::Success)
                continue;
            if (!looksLikeFatBootSector(bs))
                continue;

            outOffset = parts[i].lbaFirst;
            outSize = parts[i].lbaCount;
            return true;
        }

        return false;
    }

    static bool tryRegisterAsShared(QC::u16 base, QC::u16 ctrl, bool slave)
    {
        QC::u16 id[256];
        if (!identify(base, ctrl, slave, id))
            return false;

        QC::u32 sectors = sectorCount28FromIdentify(id);
        if (sectors == 0)
            return false;

        auto *rawDev = new AtaPioBlockDevice(base, ctrl, slave, sectors);

        QC::u64 offset = 0;
        QC::u64 size = 0;
        if (!findFatPartition(rawDev, offset, size))
        {
            QC_LOG_INFO("QKDrvIDE", "ATA device found but not FAT16/32 (base=%x ctrl=%x %s)", base, ctrl, slave ? "slave" : "master");
            return false;
        }

        QFS::BlockDevice *mountDev = rawDev;
        if (offset != 0 || size != rawDev->sectorCount())
        {
            mountDev = new OffsetBlockDevice(rawDev, offset, size);
        }

        // Write-probe: write back the boot sector unchanged to validate write support.
        {
            QC::u8 sectorBuf[512];
            QC::Status rs = mountDev->readSector(0, sectorBuf);
            QC::Status ws = QC::Status::Error;
            if (rs == QC::Status::Success)
            {
                ws = mountDev->writeSector(0, sectorBuf);
            }
            QC_LOG_INFO("QKDrvIDE", "Shared write probe: read=%d write=%d", static_cast<int>(rs), static_cast<int>(ws));
        }

        QKStorage::BlockDeviceRegistration reg{};
        reg.name = "QFS_SHARED";
        reg.mountPath = "/shared";
        reg.fsKind = QFS::FileSystemKind::FAT_AUTO;
        reg.device = mountDev;
        reg.autoMount = true;

        QC::Status st = QKStorage::registerBlockDevice(reg);
        if (st == QC::Status::Success || st == QC::Status::Busy)
        {
            QC_LOG_INFO("QKDrvIDE", "Registered shared volume (base=%x ctrl=%x %s, offset=%llu)",
                        base, ctrl, slave ? "slave" : "master", static_cast<unsigned long long>(offset));
            return true;
        }

        QC_LOG_WARN("QKDrvIDE", "Failed to register shared volume (status=%d)", static_cast<int>(st));
        return false;
    }
}

namespace QKDrv
{
    namespace IDE
    {
        void setSharedProbeEnabled(bool enabled)
        {
            g_sharedProbeEnabled = enabled;
        }

        void probeAndRegisterSharedVolume()
        {
            if (!g_sharedProbeEnabled)
                return;
            if (g_sharedProbeCompleted)
                return;
            g_sharedProbeCompleted = true;

            QC_LOG_INFO("QKDrvIDE", "Probing legacy IDE for shared volume");

            // Prefer primary slave first (QEMU often maps the extra -drive index=1 there),
            // but fall back to the other positions.
            struct Candidate
            {
                QC::u16 base;
                QC::u16 ctrl;
                bool slave;
            };

            const Candidate candidates[] = {
                {kPrimaryBase, kPrimaryCtrl, true},
                {kPrimaryBase, kPrimaryCtrl, false},
                {kSecondaryBase, kSecondaryCtrl, false},
                {kSecondaryBase, kSecondaryCtrl, true},
            };

            for (const auto &c : candidates)
            {
                if (tryRegisterAsShared(c.base, c.ctrl, c.slave))
                    return;
            }

            QC_LOG_WARN("QKDrvIDE", "No mountable FAT32 shared volume detected");
        }
    }
}
