#include "Boot/Memory/QKBootEarlyMemory.h"

// Provided by Boot/Memory/AddressMapping.cpp
extern "C" QC::PhysAddr kernelVirtToPhys(QC::VirtAddr virt);

namespace
{
    // Early heap buffer - 32MB static allocation for heap before PMM is ready
    alignas(4096) QC::u8 g_earlyHeapBuffer[32 * 1024 * 1024];

    // Early DMA buffer for USB - 1MB, separate from heap
    alignas(4096) QC::u8 g_earlyDmaBuffer[1 * 1024 * 1024];
    QC::usize g_earlyDmaOffset = 0;
}

extern "C" QC::PhysAddr earlyAllocatePage()
{
    if (g_earlyDmaOffset + 4096 > sizeof(g_earlyDmaBuffer))
        return 0;

    QC::VirtAddr virt = reinterpret_cast<QC::VirtAddr>(&g_earlyDmaBuffer[g_earlyDmaOffset]);
    g_earlyDmaOffset += 4096;
    return kernelVirtToPhys(virt);
}

namespace QK::Boot::Memory
{
    EarlyHeap GetEarlyHeap()
    {
        EarlyHeap heap{};
        heap.Buffer = reinterpret_cast<QC::VirtAddr>(g_earlyHeapBuffer);
        heap.Size = sizeof(g_earlyHeapBuffer);
        return heap;
    }
}
