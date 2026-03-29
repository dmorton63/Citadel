// QKDrvIDE - Minimal legacy IDE/ATA PIO probing for block devices
// Namespace: QKDrv::IDE

#include "IDE/QKDrvIDE.h"

#include "QArchPort.h"
#include "QCLogger.h"
#include "QCString.h"

#include "QFSFAT32.h"
#include "QFSFATProbe.h"

#include "QKStorageRegistry.h"

#include "QArchPCI.h"

namespace
{
    static bool g_sharedProbeEnabled = false;
    static bool g_sharedProbeCompleted = false;
    static bool g_systemProbeCompleted = false;

    constexpr QC::u16 kPrimaryBase = 0x1F0;
    constexpr QC::u16 kPrimaryCtrl = 0x3F6;
    constexpr QC::u16 kSecondaryBase = 0x170;
    constexpr QC::u16 kSecondaryCtrl = 0x376;

    struct IdeIoPorts
    {
        QC::u16 primaryBase = kPrimaryBase;
        QC::u16 primaryCtrl = kPrimaryCtrl;
        QC::u16 secondaryBase = kSecondaryBase;
        QC::u16 secondaryCtrl = kSecondaryCtrl;
        bool fromPci = false;
    };

    static IdeIoPorts detectIdePortsFromPci()
    {
        IdeIoPorts out{};

        const auto ioBaseFromBar = [](QC::u64 bar) -> QC::u16 {
            // PCI I/O BAR: bit0=1 indicates I/O space; bits1-0 are attributes.
            // Mask them off to get the base port.
            return static_cast<QC::u16>(bar & ~0x3ULL);
        };

        auto &pci = QArch::PCI::instance();
        const auto &devs = pci.devices();
        for (QC::usize i = 0; i < devs.size(); ++i)
        {
            const auto &d = devs[i];
            if (d.classCode != QArch::PCIClass::MassStorage)
                continue;
            if (d.subclass != 0x01) // IDE
                continue;

            const QC::u16 cmdBefore = pci.readConfig16(d.address, 0x04);

            // Ensure controller I/O space is enabled.
            pci.enableIOSpace(d.address);
            pci.enableBusMastering(d.address);

            const QC::u16 cmdAfter = pci.readConfig16(d.address, 0x04);

            // IDE controller BAR mapping:
            // BAR0 primary command, BAR1 primary control, BAR2 secondary command, BAR3 secondary control.
            // If a channel is in compatibility mode, BAR may be 0 and legacy ports should be used.
            const QC::u64 bar0 = d.bar[0];
            const QC::u64 bar1 = d.bar[1];
            const QC::u64 bar2 = d.bar[2];
            const QC::u64 bar3 = d.bar[3];

            QC_LOG_INFO("QKDrvIDE", "IDE PCI dev %u: vid=%x did=%x cmd=%04x->%04x progIF=%x BAR0=%llx BAR1=%llx BAR2=%llx BAR3=%llx",
                        static_cast<unsigned>(i), d.vendorId, d.deviceId,
                        cmdBefore, cmdAfter, d.progIF,
                        static_cast<unsigned long long>(bar0), static_cast<unsigned long long>(bar1),
                        static_cast<unsigned long long>(bar2), static_cast<unsigned long long>(bar3));

            if (bar0 != 0 && bar0 <= 0xFFFF)
                out.primaryBase = ioBaseFromBar(bar0);
            if (bar1 != 0 && bar1 <= 0xFFFF)
                out.primaryCtrl = static_cast<QC::u16>(ioBaseFromBar(bar1) + 2); // altstatus/devctl at +2
            if (bar2 != 0 && bar2 <= 0xFFFF)
                out.secondaryBase = ioBaseFromBar(bar2);
            if (bar3 != 0 && bar3 <= 0xFFFF)
                out.secondaryCtrl = static_cast<QC::u16>(ioBaseFromBar(bar3) + 2);

            out.fromPci = true;

            QC_LOG_INFO("QKDrvIDE", "IDE PCI ports: pri=%x/%x sec=%x/%x (progIF=%x)",
                        out.primaryBase, out.primaryCtrl, out.secondaryBase, out.secondaryCtrl, d.progIF);
            return out;
        }

        return out;
    }

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

    static void ataSoftReset(QC::u16 ctrl)
    {
        // Device control register shares the same port as altstatus.
        // bit1=nIEN (disable IRQ), bit2=SRST.
        QArch::outb(static_cast<QC::u16>(ctrl + 0), 0x06);
        QArch::io_wait();
        (void)ioReadAltStatus(ctrl);
        (void)ioReadAltStatus(ctrl);
        (void)ioReadAltStatus(ctrl);
        (void)ioReadAltStatus(ctrl);

        QArch::outb(static_cast<QC::u16>(ctrl + 0), 0x02);
        QArch::io_wait();
        (void)ioReadAltStatus(ctrl);
        (void)ioReadAltStatus(ctrl);
        (void)ioReadAltStatus(ctrl);
        (void)ioReadAltStatus(ctrl);
    }

    static bool waitNotBusy(QC::u16 ctrl, QC::usize spins)
    {
        while (spins--)
        {
            QC::u8 s = ioReadAltStatus(ctrl);
            // QEMU (and some real hardware) can transiently return 0x00/0xFF.
            // Treat those as "not ready yet" instead of hard failure.
            if (s != 0x00 && s != 0xFF)
            {
                if ((s & STATUS_BSY) == 0)
                    return true;
            }
        }
        return false;
    }

    static bool waitNotBusyStatus(QC::u16 base, QC::usize spins)
    {
        while (spins--)
        {
            QC::u8 s = ioReadStatusReg(base);
            if (s != 0x00 && s != 0xFF)
            {
                if ((s & STATUS_BSY) == 0)
                    return true;
            }
        }
        return false;
    }

    static bool waitDrqOrErr(QC::u16 ctrl, QC::u16 base, QC::usize spins)
    {
        while (spins--)
        {
            QC::u8 s = ioReadAltStatus(ctrl);
            if (s == 0x00 || s == 0xFF)
                continue;
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
                continue;
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
        auto issueIdentify = [&](QC::u8 &outStatus) -> bool {
            selectDrive(base, ctrl, slave);

            // Zero out task file regs
            QArch::outb(static_cast<QC::u16>(base + REG_SECCOUNT0), 0);
            QArch::outb(static_cast<QC::u16>(base + REG_LBA0), 0);
            QArch::outb(static_cast<QC::u16>(base + REG_LBA1), 0);
            QArch::outb(static_cast<QC::u16>(base + REG_LBA2), 0);
            QArch::outb(static_cast<QC::u16>(base + REG_COMMAND), CMD_IDENTIFY);

            // Read status until it becomes stable (not 0x00/0xFF) or we time out.
            bool hasStatus = false;
            for (QC::usize i = 0; i < 256; ++i)
            {
                outStatus = ioReadStatus(base);
                if (outStatus != 0xFF && outStatus != 0x00)
                {
                    hasStatus = true;
                    break;
                }
                if ((i & 0x1F) == 0)
                    QArch::io_wait();
            }
            return hasStatus;
        };

        QC::u8 status = 0;
        if (!issueIdentify(status))
        {
            // One retry after soft reset - helps on some emulations.
            ataSoftReset(ctrl);
            if (!issueIdentify(status))
            {
                QC_LOG_INFO("QKDrvIDE", "IDENTIFY: no response (base=%x ctrl=%x %s st=%02x)",
                            base, ctrl, slave ? "slave" : "master", status);
                return false;
            }
        }

        if (!waitNotBusy(ctrl, kSpinsNotBusy))
        {
            QC::u8 sAlt = ioReadAltStatus(ctrl);
            QC_LOG_INFO("QKDrvIDE", "IDENTIFY: timeout waiting not-busy (base=%x ctrl=%x %s st=%02x alt=%02x)",
                        base, ctrl, slave ? "slave" : "master", status, sAlt);
            return false;
        }

        // Check for ATAPI signature (LBA1=0x14, LBA2=0xEB or LBA1=0x69, LBA2=0x96)
        QC::u8 lba1 = QArch::inb(static_cast<QC::u16>(base + REG_LBA1));
        QC::u8 lba2 = QArch::inb(static_cast<QC::u16>(base + REG_LBA2));
        if ((lba1 == 0x14 && lba2 == 0xEB) || (lba1 == 0x69 && lba2 == 0x96))
            return false;

        if (!waitDrqOrErr(ctrl, base, kSpinsDrq))
        {
            const QC::u8 sNow = ioReadStatus(base);
            const QC::u8 err = QArch::inb(static_cast<QC::u16>(base + REG_ERROR));
            QC_LOG_INFO("QKDrvIDE", "IDENTIFY: no DRQ/ERR (base=%x ctrl=%x %s st=%02x err=%02x)",
                        base, ctrl, slave ? "slave" : "master", sNow, err);
            return false;
        }

        QArch::insw(static_cast<QC::u16>(base + REG_DATA), outWords, 256);
        return true;
    }

    static QC::u32 sectorCount28FromIdentify(const QC::u16 id[256])
    {
        // Words 60-61: total number of user addressable sectors for 28-bit LBA
        return static_cast<QC::u32>(id[60]) | (static_cast<QC::u32>(id[61]) << 16);
    }

    static QC::u32 sectorCountFromIdentify(const QC::u16 id[256])
    {
        // Prefer 48-bit LBA total sectors when present.
        // Words 100-103: total number of user addressable logical sectors for 48-bit LBA.
        const QC::u64 count48 = static_cast<QC::u64>(id[100]) |
                                (static_cast<QC::u64>(id[101]) << 16) |
                                (static_cast<QC::u64>(id[102]) << 32) |
                                (static_cast<QC::u64>(id[103]) << 48);
        if (count48 != 0)
        {
            if (count48 > 0xFFFFFFFFULL)
                return 0; // Too large for our u32 sectorCount.
            return static_cast<QC::u32>(count48);
        }

        return sectorCount28FromIdentify(id);
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

    static bool isFatPartitionType(QC::u8 type)
    {
        // Common FAT types (including FAT32 variants)
        return type == 0x0B || type == 0x0C || type == 0x06 || type == 0x0E || type == 0x01 || type == 0x04;
    }

    static bool findFatPartition(QFS::BlockDevice *dev, QC::u64 &outOffset, QC::u64 &outSize)
    {
        QC::u8 sector0[512];
        const QC::Status st0 = dev->readSector(0, sector0);
        if (st0 != QC::Status::Success)
        {
            QC_LOG_WARN("QKDrvIDE", "findFatPartition: read sector0 failed (status=%d)", static_cast<int>(st0));
            return false;
        }

        // If this looks like an MBR with an actual partition table, prefer partition scanning.
        // Our FAT probe is intentionally permissive and can false-positive on MBR bootstrap code.
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

                outOffset = parts[i].lbaFirst;
                outSize = parts[i].lbaCount;
                return true;
            }

            return false;
        }

        // No real partition table found: treat as a superfloppy FAT volume.
        if (looksLikeFatBootSector(sector0))
        {
            outOffset = 0;
            outSize = dev->sectorCount();
            return true;
        }

        return false;
    }

    static bool tryRegisterAsShared(QC::u16 base, QC::u16 ctrl, bool slave)
    {
        QC::u16 id[256];
        if (!identify(base, ctrl, slave, id))
            return false;

        QC::u32 sectors = sectorCountFromIdentify(id);
        if (sectors == 0)
        {
            const QC::u32 s28 = sectorCount28FromIdentify(id);
            const QC::u64 s48 = static_cast<QC::u64>(id[100]) |
                                (static_cast<QC::u64>(id[101]) << 16) |
                                (static_cast<QC::u64>(id[102]) << 32) |
                                (static_cast<QC::u64>(id[103]) << 48);
            QC_LOG_WARN("QKDrvIDE", "IDENTIFY ok but sectorCount=0 (base=%x ctrl=%x %s s28=%u s48=%llu)",
                        base, ctrl, slave ? "slave" : "master", s28, static_cast<unsigned long long>(s48));
            return false;
        }

        QC_LOG_INFO("QKDrvIDE", "IDENTIFY ok (base=%x ctrl=%x %s sectors=%u)",
                    base, ctrl, slave ? "slave" : "master", sectors);

        auto *rawDev = new AtaPioBlockDevice(base, ctrl, slave, sectors);

        QC::u64 offset = 0;
        QC::u64 size = 0;
        if (!findFatPartition(rawDev, offset, size))
        {
            QC_LOG_INFO("QKDrvIDE", "Disk present but no FAT volume found (base=%x ctrl=%x %s)",
                        base, ctrl, slave ? "slave" : "master");
            return false;
        }

        QFS::BlockDevice *mountDev = rawDev;
        if (offset != 0 || size != rawDev->sectorCount())
        {
            mountDev = new OffsetBlockDevice(rawDev, offset, size);
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

    static bool tryRegisterAsSystem(QC::u16 base, QC::u16 ctrl, bool slave)
    {
        QC::u16 id[256];
        if (!identify(base, ctrl, slave, id))
            return false;

        QC::u32 sectors = sectorCountFromIdentify(id);
        if (sectors == 0)
        {
            const QC::u32 s28 = sectorCount28FromIdentify(id);
            const QC::u64 s48 = static_cast<QC::u64>(id[100]) |
                                (static_cast<QC::u64>(id[101]) << 16) |
                                (static_cast<QC::u64>(id[102]) << 32) |
                                (static_cast<QC::u64>(id[103]) << 48);
            QC_LOG_WARN("QKDrvIDE", "IDENTIFY ok but sectorCount=0 (base=%x ctrl=%x %s s28=%u s48=%llu)",
                        base, ctrl, slave ? "slave" : "master", s28, static_cast<unsigned long long>(s48));
            return false;
        }

        QC_LOG_INFO("QKDrvIDE", "IDENTIFY ok (base=%x ctrl=%x %s sectors=%u)",
                    base, ctrl, slave ? "slave" : "master", sectors);

        auto *rawDev = new AtaPioBlockDevice(base, ctrl, slave, sectors);

        QC::u64 offset = 0;
        QC::u64 size = 0;
        if (!findFatPartition(rawDev, offset, size))
        {
            QC_LOG_INFO("QKDrvIDE", "Disk present but no FAT volume found (base=%x ctrl=%x %s)",
                        base, ctrl, slave ? "slave" : "master");
            return false;
        }

        QFS::BlockDevice *mountDev = rawDev;
        if (offset != 0 || size != rawDev->sectorCount())
        {
            mountDev = new OffsetBlockDevice(rawDev, offset, size);
        }

        QKStorage::BlockDeviceRegistration reg{};
        reg.name = "QFS_SYSTEM";
        reg.mountPath = "/system";
        reg.fsKind = QFS::FileSystemKind::FAT_AUTO;
        reg.device = mountDev;
        reg.autoMount = true;

        QC::Status st = QKStorage::registerBlockDevice(reg);
        if (st == QC::Status::Success || st == QC::Status::Busy)
        {
            QC_LOG_INFO("QKDrvIDE", "Registered system volume (base=%x ctrl=%x %s, offset=%llu)",
                        base, ctrl, slave ? "slave" : "master", static_cast<unsigned long long>(offset));
            return true;
        }

        QC_LOG_WARN("QKDrvIDE", "Failed to register system volume (status=%d)", static_cast<int>(st));
        return false;
    }

    static QC::Status writeZeroSectors(QFS::BlockDevice *dev, QC::u64 startSector, QC::u32 count)
    {
        if (!dev)
            return QC::Status::InvalidParam;
        if (count == 0)
            return QC::Status::Success;

        // Small chunked writer to avoid large stack buffers.
        constexpr QC::u32 kChunkSectors = 8;
        QC::u8 zeros[512 * kChunkSectors];
        QC::String::memset(zeros, 0, sizeof(zeros));

        QC::u64 s = startSector;
        QC::u32 remaining = count;
        while (remaining)
        {
            const QC::u32 n = (remaining > kChunkSectors) ? kChunkSectors : remaining;
            QC::Status st = dev->writeSectors(s, n, zeros);
            if (st != QC::Status::Success)
                return st;
            s += n;
            remaining -= n;
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
        parts[0].type = 0x0C; // FAT32 LBA
        QC::String::memset(parts[0].chsLast, 0, sizeof(parts[0].chsLast));
        parts[0].lbaFirst = lbaFirst;
        parts[0].lbaCount = lbaCount;

        mbr[510] = 0x55;
        mbr[511] = 0xAA;
        return dev->writeSector(0, mbr);
    }

    static QC::Status formatFat32At(QFS::BlockDevice *dev, QC::u32 partStartLba, QC::u32 partSectors)
    {
        if (!dev)
            return QC::Status::InvalidParam;
        if (dev->sectorSize() != 512)
            return QC::Status::NotSupported;
        if (partStartLba == 0 || partSectors < 4096)
        {
            // Keep a little slack for reserved+FATs+data.
            // (We could support smaller, but this is a practical minimum for our use-case.)
            return QC::Status::InvalidParam;
        }

        constexpr QC::u16 kReserved = 32;
        constexpr QC::u8 kFatCount = 2;
        constexpr QC::u8 kMedia = 0xF8;
        constexpr QC::u32 kRootCluster = 2;
        constexpr QC::u16 kFsInfoSector = 1;
        constexpr QC::u16 kBackupBoot = 6;

        // Pick a reasonable default (4KiB clusters), but clamp for very small images.
        QC::u8 sectorsPerCluster = 8;
        if (partSectors < 131072)
            sectorsPerCluster = 4;
        if (partSectors < 65536)
            sectorsPerCluster = 2;
        if (partSectors < 32768)
            sectorsPerCluster = 1;

        // Compute sectors per FAT iteratively.
        QC::u32 sectorsPerFat = 1;
        for (int iter = 0; iter < 8; ++iter)
        {
            const QC::u32 fatArea = static_cast<QC::u32>(kFatCount) * sectorsPerFat;
            if (partSectors <= static_cast<QC::u32>(kReserved) + fatArea)
                return QC::Status::InvalidParam;

            const QC::u32 dataSectors = partSectors - static_cast<QC::u32>(kReserved) - fatArea;
            const QC::u32 totalClusters = dataSectors / sectorsPerCluster;
            const QC::u32 fatEntries = totalClusters + 2;
            const QC::u32 fatBytes = fatEntries * 4;
            const QC::u32 newSpf = (fatBytes + 511U) / 512U;
            if (newSpf == sectorsPerFat)
                break;
            sectorsPerFat = (newSpf == 0) ? 1 : newSpf;
        }

        const QC::u32 totalSectors = partSectors;
        const QC::u32 fatStart = static_cast<QC::u32>(kReserved);
        const QC::u32 dataStart = fatStart + (static_cast<QC::u32>(kFatCount) * sectorsPerFat);
        if (totalSectors <= dataStart)
            return QC::Status::InvalidParam;

        // --- Boot sector ---
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
        QC::String::memset(bpb->reserved, 0, sizeof(bpb->reserved));
        bpb->driveNumber = 0x80;
        bpb->reserved1 = 0;
        bpb->bootSignature = 0x29;
        bpb->volumeId = 0xC1A0D00D;
        QC::String::memcpy(bpb->volumeLabel, "CITADEL SYS ", 11);
        QC::String::memcpy(bpb->fsType, "FAT32   ", 8);
        boot[510] = 0x55;
        boot[511] = 0xAA;

        QC::Status st = dev->writeSector(partStartLba + 0, boot);
        if (st != QC::Status::Success)
            return st;

        // --- FSInfo sector (best-effort; mount doesn't require it today) ---
        QC::u8 fsinfo[512];
        QC::String::memset(fsinfo, 0, sizeof(fsinfo));
        // Lead signature
        fsinfo[0] = 0x52;
        fsinfo[1] = 0x52;
        fsinfo[2] = 0x61;
        fsinfo[3] = 0x41;
        // Struct signature at 484
        fsinfo[484] = 0x72;
        fsinfo[485] = 0x72;
        fsinfo[486] = 0x41;
        fsinfo[487] = 0x61;
        // Free count + next free (unknown)
        fsinfo[488] = 0xFF;
        fsinfo[489] = 0xFF;
        fsinfo[490] = 0xFF;
        fsinfo[491] = 0xFF;
        fsinfo[492] = 0xFF;
        fsinfo[493] = 0xFF;
        fsinfo[494] = 0xFF;
        fsinfo[495] = 0xFF;
        // Trail signature 0xAA550000 at 508
        fsinfo[508] = 0x00;
        fsinfo[509] = 0x00;
        fsinfo[510] = 0x55;
        fsinfo[511] = 0xAA;
        st = dev->writeSector(partStartLba + kFsInfoSector, fsinfo);
        if (st != QC::Status::Success)
            return st;

        // --- Backup boot sector ---
        st = dev->writeSector(partStartLba + kBackupBoot, boot);
        if (st != QC::Status::Success)
            return st;

        // Zero the rest of reserved sectors (best-effort).
        if (kReserved > 0)
        {
            // Skip 0 (boot), 1 (fsinfo), 6 (backup boot)
            for (QC::u16 i = 0; i < kReserved; ++i)
            {
                if (i == 0 || i == kFsInfoSector || i == kBackupBoot)
                    continue;
                st = writeZeroSectors(dev, partStartLba + i, 1);
                if (st != QC::Status::Success)
                    return st;
            }
        }

        // --- FAT tables ---
        QC::u8 fat0[512];
        QC::String::memset(fat0, 0, sizeof(fat0));
        // FAT[0] = media + EOC
        fat0[0] = kMedia;
        fat0[1] = 0xFF;
        fat0[2] = 0xFF;
        fat0[3] = 0x0F;
        // FAT[1] = EOC
        fat0[4] = 0xFF;
        fat0[5] = 0xFF;
        fat0[6] = 0xFF;
        fat0[7] = 0x0F;
        // FAT[2] (root dir cluster) = EOC
        fat0[8] = 0xFF;
        fat0[9] = 0xFF;
        fat0[10] = 0xFF;
        fat0[11] = 0x0F;

        const QC::u32 fat1Lba = partStartLba + fatStart;
        const QC::u32 fat2Lba = fat1Lba + sectorsPerFat;

        st = dev->writeSector(fat1Lba, fat0);
        if (st != QC::Status::Success)
            return st;
        if (sectorsPerFat > 1)
        {
            st = writeZeroSectors(dev, fat1Lba + 1, sectorsPerFat - 1);
            if (st != QC::Status::Success)
                return st;
        }

        st = dev->writeSector(fat2Lba, fat0);
        if (st != QC::Status::Success)
            return st;
        if (sectorsPerFat > 1)
        {
            st = writeZeroSectors(dev, fat2Lba + 1, sectorsPerFat - 1);
            if (st != QC::Status::Success)
                return st;
        }

        // --- Root directory cluster ---
        const QC::u32 rootLba = partStartLba + dataStart;
        st = writeZeroSectors(dev, rootLba, sectorsPerCluster);
        if (st != QC::Status::Success)
            return st;

        return QC::Status::Success;
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

            const IdeIoPorts ports = detectIdePortsFromPci();

            // Prefer primary slave first (QEMU often maps the extra -drive index=1 there),
            // but fall back to the other positions.
            struct Candidate
            {
                QC::u16 base;
                QC::u16 ctrl;
                bool slave;
            };

            const Candidate candidates[] = {
                {ports.primaryBase, ports.primaryCtrl, true},
                {ports.primaryBase, ports.primaryCtrl, false},
                {ports.secondaryBase, ports.secondaryCtrl, false},
                {ports.secondaryBase, ports.secondaryCtrl, true},
            };

            for (const auto &c : candidates)
            {
                if (tryRegisterAsShared(c.base, c.ctrl, c.slave))
                    return;
            }

            QC_LOG_WARN("QKDrvIDE", "No mountable FAT32 shared volume detected");
        }

        void probeAndRegisterSystemVolume()
        {
            if (g_systemProbeCompleted)
                return;
            g_systemProbeCompleted = true;

            QC_LOG_INFO("QKDrvIDE", "Probing legacy IDE for system volume");

            const IdeIoPorts ports = detectIdePortsFromPci();

            struct Candidate
            {
                QC::u16 base;
                QC::u16 ctrl;
                bool slave;
            };

            // Prefer primary master for the first disk (-drive index=0), then other positions.
            const Candidate candidates[] = {
                {ports.primaryBase, ports.primaryCtrl, false},
                {ports.primaryBase, ports.primaryCtrl, true},
                {ports.secondaryBase, ports.secondaryCtrl, false},
                {ports.secondaryBase, ports.secondaryCtrl, true},
            };

            for (const auto &c : candidates)
            {
                if (tryRegisterAsSystem(c.base, c.ctrl, c.slave))
                    return;
            }

            QC_LOG_WARN("QKDrvIDE", "No mountable FAT system volume detected");
        }

        void resetSystemProbe()
        {
            g_systemProbeCompleted = false;
        }

        QC::Status formatSystemVolumeFAT32()
        {
            // Format the first system-disk candidate (same ordering as system probe).
            struct Candidate
            {
                QC::u16 base;
                QC::u16 ctrl;
                bool slave;
            };

            const IdeIoPorts ports = detectIdePortsFromPci();

            const Candidate candidates[] = {
                {ports.primaryBase, ports.primaryCtrl, false},
                {ports.primaryBase, ports.primaryCtrl, true},
                {ports.secondaryBase, ports.secondaryCtrl, false},
                {ports.secondaryBase, ports.secondaryCtrl, true},
            };

            QC::Status lastError = QC::Status::NotFound;
            for (const auto &c : candidates)
            {
                QC::u16 id[256];
                if (!identify(c.base, c.ctrl, c.slave, id))
                {
                    lastError = QC::Status::NotFound;
                    continue;
                }

                const QC::u32 sectors = sectorCountFromIdentify(id);
                if (sectors == 0)
                {
                    lastError = QC::Status::NotFound;
                    continue;
                }

                AtaPioBlockDevice dev(c.base, c.ctrl, c.slave, sectors);

                QC::u8 sector0[512];
                QC::Status st = dev.readSector(0, sector0);
                if (st != QC::Status::Success)
                {
                    QC_LOG_WARN("QKDrvIDE", "formatSystemVolumeFAT32: read sector0 failed (status=%d)", static_cast<int>(st));
                    lastError = st;
                    continue;
                }

                // Safety: refuse to clobber a disk that already looks formatted/partitioned.
                if (mbrHasPartitionTable(sector0) || looksLikeFatBootSector(sector0) || looksLikeMbr(sector0))
                {
                    QC_LOG_WARN("QKDrvIDE", "Refusing to format: disk already appears partitioned/formatted");
                    return QC::Status::Busy;
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

                QC_LOG_INFO("QKDrvIDE", "Formatting system disk as FAT32 (base=%x ctrl=%x %s, lba=%u sectors=%u)",
                            c.base, c.ctrl, c.slave ? "slave" : "master", partStart, partSectors);

                st = writeMbrSingleFat32(&dev, partStart, partSectors);
                if (st != QC::Status::Success)
                {
                    QC_LOG_WARN("QKDrvIDE", "formatSystemVolumeFAT32: write MBR failed (status=%d)", static_cast<int>(st));
                    return st;
                }

                st = formatFat32At(&dev, partStart, partSectors);
                if (st != QC::Status::Success)
                {
                    QC_LOG_WARN("QKDrvIDE", "formatSystemVolumeFAT32: mkfs failed (status=%d)", static_cast<int>(st));
                    return st;
                }

                QC_LOG_INFO("QKDrvIDE", "System disk FAT32 format complete");
                return QC::Status::Success;
            }

            QC_LOG_WARN("QKDrvIDE", "formatSystemVolumeFAT32: no suitable disk found");
            return lastError;
        }
    }
}
