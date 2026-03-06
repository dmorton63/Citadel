#pragma once

// QArch CPU - CPU detection and control
// Namespace: QArch

#include "QCTypes.h"

namespace QArch
{

    struct CPUIDResult
    {
        QC::u32 eax;
        QC::u32 ebx;
        QC::u32 ecx;
        QC::u32 edx;
    };

    struct CPUFeatures
    {
        // Basic features (EDX from CPUID 1)
        bool fpu : 1;
        bool vme : 1;
        bool de : 1;
        bool pse : 1;
        bool tsc : 1;
        bool msr : 1;
        bool pae : 1;
        bool mce : 1;
        bool cx8 : 1;
        bool apic : 1;
        bool sep : 1;
        bool mtrr : 1;
        bool pge : 1;
        bool mca : 1;
        bool cmov : 1;
        bool pat : 1;
        bool pse36 : 1;
        bool psn : 1;
        bool clfsh : 1;
        bool ds : 1;
        bool acpi : 1;
        bool mmx : 1;
        bool fxsr : 1;
        bool sse : 1;
        bool sse2 : 1;
        bool ss : 1;
        bool htt : 1;
        bool tm : 1;
        bool ia64 : 1;
        bool pbe : 1;

        // Extended features (ECX from CPUID 1)
        bool sse3 : 1;
        bool pclmulqdq : 1;
        bool dtes64 : 1;
        bool monitor : 1;
        bool dscpl : 1;
        bool vmx : 1;
        bool smx : 1;
        bool est : 1;
        bool tm2 : 1;
        bool ssse3 : 1;
        bool cnxtid : 1;
        bool fma : 1;
        bool cx16 : 1;
        bool xtpr : 1;
        bool pdcm : 1;
        bool pcid : 1;
        bool dca : 1;
        bool sse4_1 : 1;
        bool sse4_2 : 1;
        bool x2apic : 1;
        bool movbe : 1;
        bool popcnt : 1;
        bool tscdeadline : 1;
        bool aes : 1;
        bool xsave : 1;
        bool osxsave : 1;
        bool avx : 1;
        bool f16c : 1;
        bool rdrand : 1;
    };

    class CPU
    {
    public:
        static CPU &instance();

        void initialize();

        // CPUID
        CPUIDResult cpuid(QC::u32 leaf, QC::u32 subleaf = 0);

        // CPU info
        const char *vendorString() const { return m_vendorString; }
        const char *brandString() const { return m_brandString; }
        QC::u32 family() const { return m_family; }
        QC::u32 model() const { return m_model; }
        QC::u32 stepping() const { return m_stepping; }

        const CPUFeatures &features() const { return m_features; }

        // Control registers
        QC::u64 readCR0();
        QC::u64 readCR2();
        QC::u64 readCR3();
        QC::u64 readCR4();
        void writeCR0(QC::u64 value);
        void writeCR3(QC::u64 value);
        void writeCR4(QC::u64 value);

        // CPU operations
        void halt();
        void enableInterrupts();
        void disableInterrupts();
        bool interruptsEnabled();

    private:
        CPU();
        ~CPU();
        CPU(const CPU &) = delete;
        CPU &operator=(const CPU &) = delete;

        void detectCPU();
        void detectFeatures();

        char m_vendorString[13];
        char m_brandString[49];
        QC::u32 m_family;
        QC::u32 m_model;
        QC::u32 m_stepping;
        CPUFeatures m_features;
    };

} // namespace QArch

// Low-level spin-wait hint (x86 PAUSE).
// Implemented with C linkage (see QArchRelax.cpp).
extern "C" void cpu_relax();
