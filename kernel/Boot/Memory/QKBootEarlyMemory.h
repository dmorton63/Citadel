#pragma once

#include "QCTypes.h"

namespace QK::Boot::Memory
{
    struct EarlyHeap
    {
        QC::VirtAddr Buffer = 0;
        QC::usize Size = 0;
    };

    EarlyHeap GetEarlyHeap();
}

// Keep this exported symbol stable; it is used by drivers and early VMM.
extern "C" QC::PhysAddr earlyAllocatePage();
