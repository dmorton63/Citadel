#pragma once

#include "QCTypes.h"

namespace QK::Boot::Memory
{
    using FLogFn = void (*)(const char *);

    // Initializes the global physical/virtual translation parameters used by
    // physToVirt/kernelVirtToPhys/getHHDMOffset.
    bool InitFromLimineRequests(QC::u64 HhdmRequest[], QC::u64 KernelAddressRequest[], FLogFn Log);
}

// Keep these exported symbols stable; they are used across the kernel.
QC::u64 getHHDMOffset();

extern "C" QC::VirtAddr physToVirt(QC::PhysAddr phys);
extern "C" QC::PhysAddr kernelVirtToPhys(QC::VirtAddr virt);
