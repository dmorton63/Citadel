// QKMemory Physical Memory Manager - Implementation
// Namespace: QK::Memory

#include "QKMemPMM.h"
#include "QCLogger.h"
#include "QCString.h"

namespace QK::Memory
{

    PMM &PMM::instance()
    {
        static PMM instance;
        return instance;
    }

    PMM::PMM()
        : m_bitmap(nullptr), m_bitmapSize(0), m_totalMemory(0), m_freeMemory(0), m_totalPages(0), m_freePages(0)
    {
    }

    PMM::~PMM()
    {
    }

    void PMM::initialize(const MemoryRegion *regions, QC::usize count)
    {
        QC_LOG_INFO("QKMemPMM", "Initializing physical memory manager");

        // Calculate total memory and find highest address
        QC::PhysAddr highestAddr = 0;
        for (QC::usize i = 0; i < count; ++i)
        {
            if (regions[i].type == MemoryRegion::Type::Available)
            {
                m_totalMemory += regions[i].size;
                QC::PhysAddr end = regions[i].base + regions[i].size;
                if (end > highestAddr)
                {
                    highestAddr = end;
                }
            }
        }

        m_totalPages = highestAddr / PAGE_SIZE;
        m_bitmapSize = (m_totalPages + 7) / 8;

        QC_LOG_INFO("QKMemPMM", "Total memory: %lu MB, %lu pages",
                    m_totalMemory / (1024 * 1024), m_totalPages);

        initializeBitmap();

        // Mark all regions appropriately
        for (QC::usize i = 0; i < count; ++i)
        {
            bool used = (regions[i].type != MemoryRegion::Type::Available);
            markRegion(regions[i].base, regions[i].size, used);
        }

        m_freeMemory = m_freePages * PAGE_SIZE;

        QC_LOG_INFO("QKMemPMM", "Free memory: %lu MB, %lu pages",
                    m_freeMemory / (1024 * 1024), m_freePages);
    }

    void PMM::initializeBitmap()
    {
        // TODO: Allocate bitmap from bootloader-provided memory
        // For now, bitmap location must be set externally
    }

    void PMM::markRegion(QC::PhysAddr base, QC::usize size, bool used)
    {
        QC::usize startPage = base / PAGE_SIZE;
        QC::usize pageCount = size / PAGE_SIZE;

        for (QC::usize i = 0; i < pageCount; ++i)
        {
            QC::usize page = startPage + i;
            QC::usize byteIndex = page / 8;
            QC::u8 bitIndex = page % 8;

            if (byteIndex < m_bitmapSize)
            {
                if (used)
                {
                    m_bitmap[byteIndex] |= (1 << bitIndex);
                }
                else
                {
                    m_bitmap[byteIndex] &= ~(1 << bitIndex);
                    ++m_freePages;
                }
            }
        }
    }

    QC::PhysAddr PMM::allocatePage()
    {
        for (QC::usize i = 0; i < m_bitmapSize; ++i)
        {
            if (m_bitmap[i] != 0xFF)
            {
                for (QC::u8 bit = 0; bit < 8; ++bit)
                {
                    if ((m_bitmap[i] & (1 << bit)) == 0)
                    {
                        m_bitmap[i] |= (1 << bit);
                        --m_freePages;
                        m_freeMemory = m_freePages * PAGE_SIZE;
                        return (i * 8 + bit) * PAGE_SIZE;
                    }
                }
            }
        }

        QC_LOG_ERROR("QKMemPMM", "Out of physical memory!");
        return 0;
    }

    void PMM::freePage(QC::PhysAddr addr)
    {
        QC::usize page = addr / PAGE_SIZE;
        QC::usize byteIndex = page / 8;
        QC::u8 bitIndex = page % 8;

        if (byteIndex < m_bitmapSize)
        {
            m_bitmap[byteIndex] &= ~(1 << bitIndex);
            ++m_freePages;
            m_freeMemory = m_freePages * PAGE_SIZE;
        }
    }

    QC::PhysAddr PMM::allocatePages(QC::usize count)
    {
        if (count == 0)
            return 0;
        if (count == 1)
            return allocatePage();

        // Find contiguous free pages
        QC::usize consecutive = 0;
        QC::usize startPage = 0;

        for (QC::usize page = 0; page < m_totalPages; ++page)
        {
            QC::usize byteIndex = page / 8;
            QC::u8 bitIndex = page % 8;

            if ((m_bitmap[byteIndex] & (1 << bitIndex)) == 0)
            {
                if (consecutive == 0)
                {
                    startPage = page;
                }
                ++consecutive;
                if (consecutive >= count)
                {
                    // Found enough pages, mark them as used
                    for (QC::usize i = 0; i < count; ++i)
                    {
                        QC::usize p = startPage + i;
                        m_bitmap[p / 8] |= (1 << (p % 8));
                    }
                    m_freePages -= count;
                    m_freeMemory = m_freePages * PAGE_SIZE;
                    return startPage * PAGE_SIZE;
                }
            }
            else
            {
                consecutive = 0;
            }
        }

        QC_LOG_ERROR("QKMemPMM", "Failed to allocate %lu contiguous pages", count);
        return 0;
    }

    void PMM::freePages(QC::PhysAddr addr, QC::usize count)
    {
        for (QC::usize i = 0; i < count; ++i)
        {
            freePage(addr + i * PAGE_SIZE);
        }
    }

} // namespace QK::Memory
