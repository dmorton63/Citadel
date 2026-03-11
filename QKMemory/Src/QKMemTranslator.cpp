// QKMemory Address Translator - Implementation
// Namespace: QK::Memory

#include "QKMemTranslator.h"
#include "QKMemPMM.h"
#include "QKMemVMM.h"
#include "QCLogger.h"

// External functions for physical/virtual conversion
extern "C" QC::VirtAddr physToVirt(QC::PhysAddr phys);

namespace QK::Memory
{

    Translator &Translator::instance()
    {
        static Translator instance;
        return instance;
    }

    Translator::Translator()
        : m_physicalBase(0), m_mmioBase(0xFFFFE00000000000ULL), m_useIdentityMapping(true)
    {
    }

    Translator::~Translator()
    {
    }

    void Translator::initialize(QC::VirtAddr physicalBase)
    {
        m_physicalBase = physicalBase;
        m_useIdentityMapping = false; // VMM is ready
        QC_LOG_INFO("QKMemTrans", "Address translator initialized, phys base: 0x%lx", physicalBase);
    }

    QC::PhysAddr Translator::virtToPhys(QC::VirtAddr virt) const
    {
        if (m_useIdentityMapping)
        {
            // With identity mapping, virt == phys for lower addresses
            return static_cast<QC::PhysAddr>(virt);
        }

        if (isHigherHalf(virt))
        {
            // Direct mapping in higher half
            return virt - m_physicalBase;
        }

        // Use page tables for other addresses
        return VMM::instance().translate(virt);
    }

    bool Translator::isIdentityMapped(QC::VirtAddr addr) const
    {
        return m_useIdentityMapping || addr < 0x100000000ULL; // First 4GB
    }

    bool Translator::isHigherHalf(QC::VirtAddr addr) const
    {
        return addr >= KERNEL_OFFSET;
    }

    QC::VirtAddr Translator::mapMMIO(QC::PhysAddr phys, QC::usize size)
    {
        // MMIO requires explicit page table mapping with no-cache flags
        // Use the dedicated MMIO virtual address range starting at m_mmioBase

        // Round size up to page boundary
        size = (size + 0xFFF) & ~0xFFFULL;

        QC::VirtAddr virt = m_mmioBase;
        m_mmioBase += size;

        QC_LOG_INFO("QKMemTrans", "Mapping MMIO: phys=0x%lx -> virt=0x%lx, size=0x%lx",
                    phys, virt, size);

        // Map with Present | Writable | NoCache flags for MMIO
        PageFlags flags = PageFlags::Present | PageFlags::Writable |
                          PageFlags::NoCache | PageFlags::WriteThrough;

        QC::Status status = VMM::instance().mapRange(virt, phys, size, flags);
        if (status != QC::Status::Success)
        {
            QC_LOG_ERROR("QKMemTrans", "Failed to map MMIO, status=%d", static_cast<int>(status));
            return 0;
        }

        QC_LOG_INFO("QKMemTrans", "MMIO mapped successfully at 0x%lx", virt);
        return virt;
    }

    void Translator::unmapMMIO(QC::VirtAddr virt, QC::usize size)
    {
        size = (size + 0xFFF) & ~0xFFFULL;
        VMM::instance().unmapRange(virt, size);
    }

} // namespace QK::Memory
