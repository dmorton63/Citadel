// QArch CPU - Implementation
// Namespace: QArch

#include "QArchCPU.h"
#include "QCLogger.h"
#include "QCString.h"

namespace QArch
{

    namespace
    {
        // Enable x87 FPU + SSE/SSE2 instruction usage in kernel mode.
        // Without this, any compiler-emitted SSE instruction (e.g. for double math)
        // will raise #UD (invalid opcode) on many setups.
        void enableFpuAndSse()
        {
            // Clear TS to avoid #NM on FPU/SSE instructions.
            asm volatile("clts" ::: "memory");

            QC::u64 cr0;
            asm volatile("mov %%cr0, %0" : "=r"(cr0));

            // CR0:
            // - Clear EM (bit 2): disable FPU emulation
            // - Set MP (bit 1): monitor coprocessor
            // - Clear TS (bit 3): task switched
            cr0 &= ~(1ULL << 2);
            cr0 |= (1ULL << 1);
            cr0 &= ~(1ULL << 3);
            // Optionally enable native x87 error reporting (bit 5)
            cr0 |= (1ULL << 5);

            asm volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");

            QC::u64 cr4;
            asm volatile("mov %%cr4, %0" : "=r"(cr4));

            // CR4:
            // - OSFXSR (bit 9): enable FXSR/SSE instructions
            // - OSXMMEXCPT (bit 10): enable unmasked SSE exceptions
            cr4 |= (1ULL << 9);
            cr4 |= (1ULL << 10);

            asm volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");

            // Initialize FPU state.
            asm volatile("fninit" ::: "memory");
        }
    }

    CPU &CPU::instance()
    {
        static CPU instance;
        return instance;
    }

    CPU::CPU() : m_family(0), m_model(0), m_stepping(0)
    {
        QC::String::memset(m_vendorString, 0, sizeof(m_vendorString));
        QC::String::memset(m_brandString, 0, sizeof(m_brandString));
        QC::String::memset(&m_features, 0, sizeof(m_features));
    }

    CPU::~CPU()
    {
    }

    void CPU::initialize()
    {
        QC_LOG_INFO("QArchCPU", "Detecting CPU");
        detectCPU();
        detectFeatures();

        // Enable FPU/SSE early so freestanding code can safely use double/float math.
        if (m_features.fpu && m_features.sse && m_features.sse2)
        {
            enableFpuAndSse();
            QC_LOG_INFO("QArchCPU", "FPU/SSE enabled");
        }
        else
        {
            QC_LOG_WARN("QArchCPU", "CPU lacks SSE2; floating-point math may fault");
        }

        QC_LOG_INFO("QArchCPU", "CPU: %s", m_brandString[0] ? m_brandString : m_vendorString);
        QC_LOG_INFO("QArchCPU", "Family: %u, Model: %u, Stepping: %u",
                    m_family, m_model, m_stepping);
    }

    CPUIDResult CPU::cpuid(QC::u32 leaf, QC::u32 subleaf)
    {
        CPUIDResult result;
        asm volatile("cpuid"
                     : "=a"(result.eax), "=b"(result.ebx), "=c"(result.ecx), "=d"(result.edx)
                     : "a"(leaf), "c"(subleaf));
        return result;
    }

    void CPU::detectCPU()
    {
        // Get vendor string
        CPUIDResult vendor = cpuid(0);
        *reinterpret_cast<QC::u32 *>(&m_vendorString[0]) = vendor.ebx;
        *reinterpret_cast<QC::u32 *>(&m_vendorString[4]) = vendor.edx;
        *reinterpret_cast<QC::u32 *>(&m_vendorString[8]) = vendor.ecx;
        m_vendorString[12] = '\0';

        // Get processor info
        CPUIDResult info = cpuid(1);
        m_stepping = info.eax & 0xF;
        m_model = (info.eax >> 4) & 0xF;
        m_family = (info.eax >> 8) & 0xF;

        // Extended model/family for modern CPUs
        if (m_family == 0xF)
        {
            m_family += (info.eax >> 20) & 0xFF;
        }
        if (m_family == 0x6 || m_family == 0xF)
        {
            m_model += ((info.eax >> 16) & 0xF) << 4;
        }

        // Get brand string
        CPUIDResult extLeaf = cpuid(0x80000000);
        if (extLeaf.eax >= 0x80000004)
        {
            CPUIDResult brand1 = cpuid(0x80000002);
            CPUIDResult brand2 = cpuid(0x80000003);
            CPUIDResult brand3 = cpuid(0x80000004);

            QC::String::memcpy(&m_brandString[0], &brand1, 16);
            QC::String::memcpy(&m_brandString[16], &brand2, 16);
            QC::String::memcpy(&m_brandString[32], &brand3, 16);
            m_brandString[48] = '\0';
        }
    }

    void CPU::detectFeatures()
    {
        CPUIDResult info = cpuid(1);

        // EDX features
        m_features.fpu = info.edx & (1 << 0);
        m_features.vme = info.edx & (1 << 1);
        m_features.de = info.edx & (1 << 2);
        m_features.pse = info.edx & (1 << 3);
        m_features.tsc = info.edx & (1 << 4);
        m_features.msr = info.edx & (1 << 5);
        m_features.pae = info.edx & (1 << 6);
        m_features.apic = info.edx & (1 << 9);
        m_features.mmx = info.edx & (1 << 23);
        m_features.fxsr = info.edx & (1 << 24);
        m_features.sse = info.edx & (1 << 25);
        m_features.sse2 = info.edx & (1 << 26);

        // ECX features
        m_features.sse3 = info.ecx & (1 << 0);
        m_features.ssse3 = info.ecx & (1 << 9);
        m_features.sse4_1 = info.ecx & (1 << 19);
        m_features.sse4_2 = info.ecx & (1 << 20);
        m_features.aes = info.ecx & (1 << 25);
        m_features.avx = info.ecx & (1 << 28);
    }

    QC::u64 CPU::readCR0()
    {
        QC::u64 value;
        asm volatile("mov %%cr0, %0" : "=r"(value));
        return value;
    }

    QC::u64 CPU::readCR2()
    {
        QC::u64 value;
        asm volatile("mov %%cr2, %0" : "=r"(value));
        return value;
    }

    QC::u64 CPU::readCR3()
    {
        QC::u64 value;
        asm volatile("mov %%cr3, %0" : "=r"(value));
        return value;
    }

    QC::u64 CPU::readCR4()
    {
        QC::u64 value;
        asm volatile("mov %%cr4, %0" : "=r"(value));
        return value;
    }

    void CPU::writeCR0(QC::u64 value)
    {
        asm volatile("mov %0, %%cr0" : : "r"(value) : "memory");
    }

    void CPU::writeCR3(QC::u64 value)
    {
        asm volatile("mov %0, %%cr3" : : "r"(value) : "memory");
    }

    void CPU::writeCR4(QC::u64 value)
    {
        asm volatile("mov %0, %%cr4" : : "r"(value) : "memory");
    }

    void CPU::halt()
    {
        asm volatile("hlt");
    }

    void CPU::enableInterrupts()
    {
        asm volatile("sti");
    }

    void CPU::disableInterrupts()
    {
        asm volatile("cli");
    }

    bool CPU::interruptsEnabled()
    {
        QC::u64 flags;
        asm volatile("pushfq; pop %0" : "=r"(flags));
        return (flags & (1 << 9)) != 0;
    }

} // namespace QArch
