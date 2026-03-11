#pragma once

// QKMemory Paging Structures
// Namespace: QK::Memory

#include "QCTypes.h"

namespace QK::Memory
{

    // x86_64 page table entry structure
    struct PageTableEntry
    {
        QC::u64 value;

        bool present() const { return value & (1ULL << 0); }
        bool writable() const { return value & (1ULL << 1); }
        bool user() const { return value & (1ULL << 2); }
        bool writeThrough() const { return value & (1ULL << 3); }
        bool noCache() const { return value & (1ULL << 4); }
        bool accessed() const { return value & (1ULL << 5); }
        bool dirty() const { return value & (1ULL << 6); }
        bool large() const { return value & (1ULL << 7); }
        bool global() const { return value & (1ULL << 8); }
        bool noExecute() const { return value & (1ULL << 63); }

        QC::PhysAddr address() const
        {
            return value & 0x000FFFFFFFFFF000ULL;
        }

        void setAddress(QC::PhysAddr addr)
        {
            value = (value & ~0x000FFFFFFFFFF000ULL) | (addr & 0x000FFFFFFFFFF000ULL);
        }

        void setPresent(bool v) { v ? (value |= 1ULL << 0) : (value &= ~(1ULL << 0)); }
        void setWritable(bool v) { v ? (value |= 1ULL << 1) : (value &= ~(1ULL << 1)); }
        void setUser(bool v) { v ? (value |= 1ULL << 2) : (value &= ~(1ULL << 2)); }
        void setNoExecute(bool v) { v ? (value |= 1ULL << 63) : (value &= ~(1ULL << 63)); }
    };

    struct PageTable
    {
        PageTableEntry entries[512];
    };

    // Page table indices
    inline QC::usize pml4Index(QC::VirtAddr addr) { return (addr >> 39) & 0x1FF; }
    inline QC::usize pdptIndex(QC::VirtAddr addr) { return (addr >> 30) & 0x1FF; }
    inline QC::usize pdIndex(QC::VirtAddr addr) { return (addr >> 21) & 0x1FF; }
    inline QC::usize ptIndex(QC::VirtAddr addr) { return (addr >> 12) & 0x1FF; }
    inline QC::usize pageOffset(QC::VirtAddr addr) { return addr & 0xFFF; }

    class Paging
    {
    public:
        static Paging &instance();

        void initialize();

        // Page table operations
        PageTable *createPageTable();
        void freePageTable(PageTable *table);

        // TLB management
        void flushTLB();
        void flushPage(QC::VirtAddr addr);
        void flushRange(QC::VirtAddr start, QC::usize size);

    private:
        Paging();
        ~Paging();
        Paging(const Paging &) = delete;
        Paging &operator=(const Paging &) = delete;
    };

} // namespace QK::Memory
