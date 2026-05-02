#pragma once

// QJFunctions - JIT page allocator (RW→RX, W^X enforced).
// Namespace: QC::JFunc
//
// Allocates executable memory for JIT-compiled function code.
//
// W^X contract:
//   1. allocateRW()   — returns a writable (non-executable) block.
//   2. finalizeToRX() — transitions the block to execute-only (no write).
//   3. free()         — releases the block.
//
// v1 note: On bare-metal x86_64, flipping page attributes requires PTE
// manipulation.  The v1 implementation tracks the intent (RW vs RX) and
// satisfies the API contract.  Actual PTE flipping is wired in once the
// codegen backend requires it.  Until then, the allocator serves as the
// lifecycle manager for JIT-page ownership.

#include "QCTypes.h"

namespace QC
{
    namespace JFunc
    {
        class JitAllocator
        {
        public:
            static JitAllocator& instance();

            // Allocate at least 'size' bytes for codegen (RW, not executable).
            // Returns nullptr on failure.
            void* allocateRW(QC::usize size);

            // Finalize: transition a previously allocated block from RW to RX.
            // Returns false if ptr is unknown or already finalized.
            bool finalizeToRX(void* ptr, QC::usize size);

            // Release a block (RW or RX).  Safe to call with nullptr.
            void free(void* ptr);

            // Release all blocks.  Called on SST rotation or policy revocation.
            void freeAll();

            // Number of live allocations.
            QC::usize count() const { return m_count; }

        private:
            JitAllocator() = default;
            JitAllocator(const JitAllocator&) = delete;
            JitAllocator& operator=(const JitAllocator&) = delete;

            struct Block
            {
                void*      ptr  = nullptr;
                QC::usize  size = 0;
                bool       isRX = false; // false = RW (writeable/not-yet-exec)
            };

            static constexpr QC::usize MAX_BLOCKS = 64;
            Block     m_blocks[MAX_BLOCKS] = {};
            QC::usize m_count = 0;

            Block* findBlock(void* ptr);
        };

    } // namespace JFunc
} // namespace QC
