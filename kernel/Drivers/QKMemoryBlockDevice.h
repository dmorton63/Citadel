#pragma once

// QKMemoryBlockDevice - Simple memory-backed block device implementation
// Namespace: QKStorage

#include "QFSFAT32.h"
#include "QCString.h"

namespace QKStorage
{

    class MemoryBlockDevice : public QFS::BlockDevice
    {
    public:
        MemoryBlockDevice(QC::u8 *base, QC::u64 size, QC::usize sectorSize = 512)
            : m_base(base), m_size(size), m_sectorSize(sectorSize) {}

        QC::usize sectorSize() const override { return m_sectorSize; }
        QC::u64 sectorCount() const override
        {
            if (m_sectorSize == 0)
                return 0;
            return m_size / m_sectorSize;
        }

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
            QC::u64 offset = sector * m_sectorSize;
            QC::u64 bytes = static_cast<QC::u64>(count) * m_sectorSize;
            if (offset + bytes > m_size)
                return QC::Status::InvalidParam;
            QC::String::memcpy(buffer, m_base + offset, static_cast<QC::usize>(bytes));
            return QC::Status::Success;
        }

        QC::Status writeSectors(QC::u64 sector, QC::usize count, const void *buffer) override
        {
            if (!buffer)
                return QC::Status::InvalidParam;
            QC::u64 offset = sector * m_sectorSize;
            QC::u64 bytes = static_cast<QC::u64>(count) * m_sectorSize;
            if (offset + bytes > m_size)
                return QC::Status::InvalidParam;
            QC::String::memcpy(m_base + offset, buffer, static_cast<QC::usize>(bytes));
            return QC::Status::Success;
        }

    private:
        QC::u8 *m_base;
        QC::u64 m_size;
        QC::usize m_sectorSize;
    };

} // namespace QKStorage
