// QKMemory Heap Manager - Implementation
// Namespace: QK::Memory

#include "QKMemHeap.h"
#include "QKMemVMM.h"
#include "QCLogger.h"
#include "QCString.h"

namespace QK::Memory
{

    Heap &Heap::instance()
    {
        static Heap instance;
        return instance;
    }

    Heap::Heap()
        : m_base(0), m_totalSize(0), m_usedSize(0), m_allocationCount(0), m_firstBlock(nullptr)
    {
    }

    Heap::~Heap()
    {
    }

    void Heap::initialize(QC::VirtAddr base, QC::usize size)
    {
        if (isInitialized())
        {
            if (m_base == base && m_totalSize == size)
            {
                // Already initialized with the same arena.
                return;
            }
            QC_LOG_WARN("QKMemHeap", "Heap already initialized (base=0x%lx size=%lu KB); ignoring reinit to base=0x%lx size=%lu KB",
                        m_base, m_totalSize / 1024, base, size / 1024);
            return;
        }

        QC_LOG_INFO("QKMemHeap", "Initializing heap at 0x%lx, size %lu KB", base, size / 1024);

        if (base == 0 || size <= sizeof(BlockHeader))
        {
            QC_LOG_ERROR("QKMemHeap", "Invalid heap arena base=0x%lx size=%lu", base, size);
            return;
        }

        m_base = base;
        m_totalSize = size;
        m_usedSize = sizeof(BlockHeader);
        m_allocationCount = 0;

        // Create initial free block
        m_firstBlock = reinterpret_cast<BlockHeader *>(base);
        m_firstBlock->size = size - sizeof(BlockHeader);
        m_firstBlock->used = false;
        m_firstBlock->next = nullptr;
        m_firstBlock->prev = nullptr;

        QC_LOG_INFO("QKMemHeap", "Heap initialized with %lu KB free", m_firstBlock->size / 1024);
    }

    void *Heap::allocate(QC::usize size)
    {
        if (size == 0)
            return nullptr;

        // Align size to 16 bytes
        size = (size + 15) & ~15ULL;

        BlockHeader *block = findFreeBlock(size);
        if (!block)
        {
            expandHeap(size);
            block = findFreeBlock(size);
            if (!block)
            {
                QC_LOG_ERROR("QKMemHeap", "Failed to allocate %lu bytes", size);
                return nullptr;
            }
        }

        if (block->size > size + sizeof(BlockHeader) + 16)
        {
            splitBlock(block, size);
        }

        block->used = true;
        m_usedSize += block->size + sizeof(BlockHeader);
        ++m_allocationCount;

        return reinterpret_cast<void *>(reinterpret_cast<QC::VirtAddr>(block) + sizeof(BlockHeader));
    }

    void *Heap::allocateAligned(QC::usize size, QC::usize alignment)
    {
        // TODO: Implement aligned allocation
        return allocate(size);
    }

    void *Heap::reallocate(void *ptr, QC::usize newSize)
    {
        if (!ptr)
            return allocate(newSize);
        if (newSize == 0)
        {
            free(ptr);
            return nullptr;
        }

        BlockHeader *block = reinterpret_cast<BlockHeader *>(
            reinterpret_cast<QC::VirtAddr>(ptr) - sizeof(BlockHeader));

        if (block->size >= newSize)
        {
            return ptr;
        }

        void *newPtr = allocate(newSize);
        if (newPtr)
        {
            QC::String::memcpy(newPtr, ptr, block->size);
            free(ptr);
        }

        return newPtr;
    }

    void Heap::free(void *ptr)
    {
        if (!ptr)
            return;

        BlockHeader *block = reinterpret_cast<BlockHeader *>(
            reinterpret_cast<QC::VirtAddr>(ptr) - sizeof(BlockHeader));

        if (!block->used)
        {
            QC_LOG_WARN("QKMemHeap", "Double free detected at %p", ptr);
            return;
        }

        block->used = false;
        m_usedSize -= block->size + sizeof(BlockHeader);
        --m_allocationCount;

        mergeBlocks();
    }

    Heap::BlockHeader *Heap::findFreeBlock(QC::usize size)
    {
        BlockHeader *block = m_firstBlock;

        while (block)
        {
            if (!block->used && block->size >= size)
            {
                return block;
            }
            block = block->next;
        }

        return nullptr;
    }

    void Heap::splitBlock(BlockHeader *block, QC::usize size)
    {
        QC::VirtAddr newBlockAddr = reinterpret_cast<QC::VirtAddr>(block) + sizeof(BlockHeader) + size;

        BlockHeader *newBlock = reinterpret_cast<BlockHeader *>(newBlockAddr);
        newBlock->size = block->size - size - sizeof(BlockHeader);
        newBlock->used = false;
        newBlock->next = block->next;
        newBlock->prev = block;

        if (block->next)
        {
            block->next->prev = newBlock;
        }
        block->next = newBlock;
        block->size = size;
    }

    void Heap::mergeBlocks()
    {
        BlockHeader *block = m_firstBlock;

        while (block && block->next)
        {
            if (!block->used && !block->next->used)
            {
                block->size += block->next->size + sizeof(BlockHeader);
                block->next = block->next->next;
                if (block->next)
                {
                    block->next->prev = block;
                }
            }
            else
            {
                block = block->next;
            }
        }
    }

    void Heap::expandHeap(QC::usize minSize)
    {
        // TODO: Allocate more pages from VMM
        QC_LOG_WARN("QKMemHeap", "Heap expansion not implemented");
    }

} // namespace QK::Memory

// Global operators (must be outside namespace)
void *operator new(QC::usize size)
{
    return QK::Memory::Heap::instance().allocate(size);
}

void *operator new[](QC::usize size)
{
    return QK::Memory::Heap::instance().allocate(size);
}

void operator delete(void *ptr) noexcept
{
    QK::Memory::Heap::instance().free(ptr);
}

void operator delete[](void *ptr) noexcept
{
    QK::Memory::Heap::instance().free(ptr);
}

void operator delete(void *ptr, QC::usize) noexcept
{
    QK::Memory::Heap::instance().free(ptr);
}

void operator delete[](void *ptr, QC::usize) noexcept
{
    QK::Memory::Heap::instance().free(ptr);
}
