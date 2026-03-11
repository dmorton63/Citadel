#pragma once

// QKMemory Heap Manager
// Namespace: QK::Memory

#include "QCTypes.h"

namespace QK::Memory
{

    class Heap
    {
    public:
        static Heap &instance();

        void initialize(QC::VirtAddr base, QC::usize size);

        bool isInitialized() const { return m_totalSize != 0 && m_firstBlock != nullptr; }

        // Allocation
        void *allocate(QC::usize size);
        void *allocateAligned(QC::usize size, QC::usize alignment);
        void *reallocate(void *ptr, QC::usize newSize);
        void free(void *ptr);

        // Statistics
        QC::usize totalSize() const { return m_totalSize; }
        QC::usize usedSize() const { return m_usedSize; }
        QC::usize freeSize() const { return m_totalSize - m_usedSize; }
        QC::usize allocationCount() const { return m_allocationCount; }

    private:
        Heap();
        ~Heap();
        Heap(const Heap &) = delete;
        Heap &operator=(const Heap &) = delete;

        struct BlockHeader
        {
            QC::usize size;
            bool used;
            BlockHeader *next;
            BlockHeader *prev;
        };

        BlockHeader *findFreeBlock(QC::usize size);
        void splitBlock(BlockHeader *block, QC::usize size);
        void mergeBlocks();
        void expandHeap(QC::usize minSize);

        QC::VirtAddr m_base;
        QC::usize m_totalSize;
        QC::usize m_usedSize;
        QC::usize m_allocationCount;
        BlockHeader *m_firstBlock;
    };

} // namespace QK::Memory

// Global operators (must be outside namespace)
void *operator new(QC::usize size);
void *operator new[](QC::usize size);
void operator delete(void *ptr) noexcept;
void operator delete[](void *ptr) noexcept;
void operator delete(void *ptr, QC::usize size) noexcept;
void operator delete[](void *ptr, QC::usize size) noexcept;
