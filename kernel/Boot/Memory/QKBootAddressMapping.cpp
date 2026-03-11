#include "Boot/Memory/QKBootAddressMapping.h"

#include "Boot/Limine/QKBootLimineRequests.h"

namespace
{
    // Global HHDM offset (physical to virtual mapping)
    QC::u64 g_hhdmOffset = 0;

    // Kernel address mapping from Limine
    QC::u64 g_kernelPhysBase = 0;
    QC::u64 g_kernelVirtBase = 0;
}

QC::u64 getHHDMOffset()
{
    return g_hhdmOffset;
}

extern "C" QC::VirtAddr physToVirt(QC::PhysAddr phys)
{
    return static_cast<QC::VirtAddr>(phys + g_hhdmOffset);
}

extern "C" QC::PhysAddr kernelVirtToPhys(QC::VirtAddr virt)
{
    return static_cast<QC::PhysAddr>(virt - g_kernelVirtBase + g_kernelPhysBase);
}

namespace QK::Boot::Memory
{
    bool InitFromLimineRequests(QC::u64 HhdmRequest[], QC::u64 KernelAddressRequest[], FLogFn Log)
    {
        QK::Boot::Limine::FKernelMapping Mapping{};
        if (!QK::Boot::Limine::ReadKernelMapping(HhdmRequest, KernelAddressRequest, Mapping))
        {
            if (Log)
                Log("WARNING: Missing Limine HHDM/kernel address response(s)\r\n");
            return false;
        }

        g_hhdmOffset = Mapping.HhdmOffset;
        g_kernelPhysBase = Mapping.KernelPhysBase;
        g_kernelVirtBase = Mapping.KernelVirtBase;
        return true;
    }
}
