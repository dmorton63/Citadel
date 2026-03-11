#pragma once

// QKMemory Address Translator
// Namespace: QK::Memory

#include "QCTypes.h"

// External function to get HHDM offset from boot code
extern QC::u64 getHHDMOffset();

namespace QK::Memory
{

    // Higher-half kernel offset (typically 0xFFFF800000000000)
    constexpr QC::VirtAddr KERNEL_OFFSET = 0xFFFF800000000000ULL;

    class Translator
    {
    public:
        static Translator &instance();

        void initialize(QC::VirtAddr physicalBase);

        // Physical to virtual conversion (for kernel-mapped memory)
        template <typename T = void>
        T *physToVirt(QC::PhysAddr phys) const
        {
            // Use m_physicalBase if set, otherwise use HHDM offset from boot
            QC::VirtAddr base = m_physicalBase;
            if (base == 0)
            {
                base = getHHDMOffset();
            }
            return reinterpret_cast<T *>(phys + base);
        }

        // Virtual to physical conversion
        QC::PhysAddr virtToPhys(QC::VirtAddr virt) const;

        // Identity mapping helpers
        bool isIdentityMapped(QC::VirtAddr addr) const;
        bool isHigherHalf(QC::VirtAddr addr) const;

        // MMIO mapping
        QC::VirtAddr mapMMIO(QC::PhysAddr phys, QC::usize size);
        void unmapMMIO(QC::VirtAddr virt, QC::usize size);

    private:
        Translator();
        ~Translator();
        Translator(const Translator &) = delete;
        Translator &operator=(const Translator &) = delete;

        QC::VirtAddr m_physicalBase;
        QC::VirtAddr m_mmioBase;
        bool m_useIdentityMapping;
    };

    // Convenience functions
    template <typename T = void>
    inline T *phys_to_virt(QC::PhysAddr phys)
    {
        return Translator::instance().physToVirt<T>(phys);
    }

    inline QC::PhysAddr virt_to_phys(QC::VirtAddr virt)
    {
        return Translator::instance().virtToPhys(virt);
    }

} // namespace QK::Memory
