#pragma once

// QKMemory Virtual Memory Manager
// Namespace: QK::Memory

#include "QCTypes.h"

namespace QK::Memory
{

    // Memory protection flags
    enum class PageFlags : QC::u64
    {
        None = 0,
        Present = 1 << 0,
        Writable = 1 << 1,
        User = 1 << 2,
        WriteThrough = 1 << 3,
        NoCache = 1 << 4,
        Accessed = 1 << 5,
        Dirty = 1 << 6,
        Large = 1 << 7,
        Global = 1 << 8,
        NoExecute = 1ULL << 63
    };

    inline PageFlags operator|(PageFlags a, PageFlags b)
    {
        return static_cast<PageFlags>(static_cast<QC::u64>(a) | static_cast<QC::u64>(b));
    }

    inline PageFlags operator&(PageFlags a, PageFlags b)
    {
        return static_cast<PageFlags>(static_cast<QC::u64>(a) & static_cast<QC::u64>(b));
    }

    struct VirtualMemoryRegion
    {
        QC::VirtAddr base;
        QC::usize size;
        PageFlags flags;
    };

    class VMM
    {
    public:
        static VMM &instance();

        void initialize();

        // Page table management
        QC::PhysAddr createAddressSpace();
        void destroyAddressSpace(QC::PhysAddr pml4);
        void switchAddressSpace(QC::PhysAddr pml4);
        QC::PhysAddr currentAddressSpace() const;

        // Mapping operations
        QC::Status map(QC::VirtAddr virt, QC::PhysAddr phys, PageFlags flags);
        QC::Status mapRange(QC::VirtAddr virt, QC::PhysAddr phys, QC::usize size, PageFlags flags);
        QC::Status unmap(QC::VirtAddr virt);
        QC::Status unmapRange(QC::VirtAddr virt, QC::usize size);

        // Query operations
        QC::PhysAddr translate(QC::VirtAddr virt) const;
        PageFlags getFlags(QC::VirtAddr virt) const;
        bool isMapped(QC::VirtAddr virt) const;

        // Virtual address allocation
        QC::VirtAddr allocate(QC::usize size, PageFlags flags);
        void free(QC::VirtAddr addr, QC::usize size);

    private:
        VMM();
        ~VMM();
        VMM(const VMM &) = delete;
        VMM &operator=(const VMM &) = delete;

        QC::u64 *getOrCreateTable(QC::u64 *parent, QC::usize index);
        void invalidatePage(QC::VirtAddr addr);

        QC::PhysAddr m_kernelPML4;
        QC::VirtAddr m_nextVirtualAddress;
    };

} // namespace QK::Memory
