#include "QJFJitAllocator.h"
#include "QCLogger.h"

namespace QC
{
    namespace JFunc
    {
        namespace
        {
            static constexpr const char* LOG_MODULE = "QJFJitAllocator";
        }

        JitAllocator& JitAllocator::instance()
        {
            static JitAllocator g;
            return g;
        }

        void* JitAllocator::allocateRW(QC::usize size)
        {
            if (size == 0 || m_count >= MAX_BLOCKS)
            {
                QC_LOG_WARN(LOG_MODULE, "allocateRW failed: size=%u count=%u",
                            static_cast<QC::u32>(size),
                            static_cast<QC::u32>(m_count));
                return nullptr;
            }

            void* ptr = operator new[](size);
            if (!ptr)
            {
                QC_LOG_WARN(LOG_MODULE, "allocateRW oom: size=%u", static_cast<QC::u32>(size));
                return nullptr;
            }

            m_blocks[m_count].ptr  = ptr;
            m_blocks[m_count].size = size;
            m_blocks[m_count].isRX = false;
            ++m_count;

            QC_LOG_INFO(LOG_MODULE, "allocateRW ptr=%p size=%u", ptr, static_cast<QC::u32>(size));
            return ptr;
        }

        bool JitAllocator::finalizeToRX(void* ptr, QC::usize size)
        {
            if (!ptr)
                return false;

            Block* b = findBlock(ptr);
            if (!b)
            {
                QC_LOG_WARN(LOG_MODULE, "finalizeToRX unknown ptr=%p", ptr);
                return false;
            }
            if (b->isRX)
            {
                QC_LOG_WARN(LOG_MODULE, "finalizeToRX already RX ptr=%p", ptr);
                return false;
            }

            // v1: record intent; real PTE flipping (W^X) added when codegen lands.
            // On x86_64 bare-metal this will call into the kernel page-table manager
            // to clear the writable bit and set the execute bit on the backing pages.
            b->isRX = true;
            (void)size; // will be used by PTE path when wired

            QC_LOG_INFO(LOG_MODULE, "finalizeToRX ptr=%p size=%u (PTE flip pending codegen)", ptr, static_cast<QC::u32>(size));
            return true;
        }

        void JitAllocator::free(void* ptr)
        {
            if (!ptr)
                return;

            for (QC::usize i = 0; i < m_count; ++i)
            {
                if (m_blocks[i].ptr == ptr)
                {
                    QC_LOG_INFO(LOG_MODULE, "free ptr=%p size=%u isRX=%u",
                                ptr,
                                static_cast<QC::u32>(m_blocks[i].size),
                                static_cast<QC::u32>(m_blocks[i].isRX ? 1 : 0));

                    operator delete[](m_blocks[i].ptr);

                    // Compact the array: move last entry into this slot.
                    if (i < m_count - 1)
                        m_blocks[i] = m_blocks[m_count - 1];
                    m_blocks[m_count - 1] = Block{};
                    --m_count;
                    return;
                }
            }

            QC_LOG_WARN(LOG_MODULE, "free: unknown ptr=%p", ptr);
        }

        void JitAllocator::freeAll()
        {
            for (QC::usize i = 0; i < m_count; ++i)
            {
                if (m_blocks[i].ptr)
                    operator delete[](m_blocks[i].ptr);
                m_blocks[i] = Block{};
            }
            m_count = 0;
            QC_LOG_INFO(LOG_MODULE, "freeAll: all JIT pages released");
        }

        JitAllocator::Block* JitAllocator::findBlock(void* ptr)
        {
            for (QC::usize i = 0; i < m_count; ++i)
            {
                if (m_blocks[i].ptr == ptr)
                    return &m_blocks[i];
            }
            return nullptr;
        }

    } // namespace JFunc
} // namespace QC
