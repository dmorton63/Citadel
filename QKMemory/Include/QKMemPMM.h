#pragma once

// QKMemory Physical Memory Manager
// Namespace: QK::Memory

#include "QCTypes.h"

namespace QK::Memory
{

    constexpr QC::usize PAGE_SIZE = 4096;
    constexpr QC::usize LARGE_PAGE_SIZE = 2 * 1024 * 1024; // 2MB

    struct MemoryRegion
    {
        QC::PhysAddr base;
        QC::usize size;
        enum class Type : QC::u8
        {
            Available,
            Reserved,
            ACPI,
            NVS,
            BadMemory,
            Kernel,
            BootloaderReclaimable
        } type;
    };

    class PMM
    {
    public:
        static PMM &instance();

        void initialize(const MemoryRegion *regions, QC::usize count);

        // Single page allocation
        QC::PhysAddr allocatePage();
        void freePage(QC::PhysAddr addr);

        // Multiple contiguous pages
        QC::PhysAddr allocatePages(QC::usize count);
        void freePages(QC::PhysAddr addr, QC::usize count);

        // Statistics
        QC::usize totalMemory() const { return m_totalMemory; }
        QC::usize freeMemory() const { return m_freeMemory; }
        QC::usize usedMemory() const { return m_totalMemory - m_freeMemory; }
        QC::usize totalPages() const { return m_totalPages; }
        QC::usize freePages() const { return m_freePages; }

    private:
        PMM();
        ~PMM();
        PMM(const PMM &) = delete;
        PMM &operator=(const PMM &) = delete;

        void initializeBitmap();
        void markRegion(QC::PhysAddr base, QC::usize size, bool used);

        QC::u8 *m_bitmap;
        QC::usize m_bitmapSize;
        QC::usize m_totalMemory;
        QC::usize m_freeMemory;
        QC::usize m_totalPages;
        QC::usize m_freePages;
    };

} // namespace QK::Memory
