// QArch GDT - Implementation
// Namespace: QArch

#include "QArchGDT.h"
#include "QCLogger.h"
#include "QCString.h"

namespace QArch
{

    GDT &GDT::instance()
    {
        static GDT instance;
        return instance;
    }

    GDT::GDT()
    {
        QC::String::memset(&m_tss, 0, sizeof(TSS));
        QC::String::memset(m_entries, 0, sizeof(m_entries));
    }

    GDT::~GDT()
    {
    }

    void GDT::initialize()
    {
        QC_LOG_INFO("QArchGDT", "Initializing GDT");

        // Null descriptor
        setEntry(0, 0, 0, 0, 0);

        // Kernel code segment (64-bit)
        setEntry(1, 0, 0xFFFFF, 0x9A, 0xA0);

        // Kernel data segment
        setEntry(2, 0, 0xFFFFF, 0x92, 0xC0);

        // User code segment (64-bit)
        setEntry(3, 0, 0xFFFFF, 0xFA, 0xA0);

        // User data segment
        setEntry(4, 0, 0xFFFFF, 0xF2, 0xC0);

        // TSS
        m_tss.iopbOffset = sizeof(TSS);
        setTSSEntry(5, reinterpret_cast<QC::u64>(&m_tss), sizeof(TSS) - 1);

        m_pointer.limit = sizeof(m_entries) - 1;
        m_pointer.base = reinterpret_cast<QC::u64>(m_entries);

        QC_LOG_INFO("QArchGDT", "GDT initialized with %lu entries", GDT_ENTRIES);
    }

    void GDT::load()
    {
        asm volatile("lgdt %0" : : "m"(m_pointer));

        // Reload segment registers
        asm volatile(
            "pushq $0x08\n"
            "leaq 1f(%%rip), %%rax\n"
            "pushq %%rax\n"
            "lretq\n"
            "1:\n"
            "mov $0x10, %%ax\n"
            "mov %%ax, %%ds\n"
            "mov %%ax, %%es\n"
            "mov %%ax, %%fs\n"
            "mov %%ax, %%gs\n"
            "mov %%ax, %%ss\n"
            : : : "rax", "memory");

        // Load TSS
        asm volatile("ltr %0" : : "r"(static_cast<QC::u16>(TSS_SELECTOR)));

        QC_LOG_INFO("QArchGDT", "GDT loaded");
    }

    void GDT::setKernelStack(QC::VirtAddr stack)
    {
        m_tss.rsp0 = stack;
    }

    void GDT::setEntry(QC::usize index, QC::u32 base, QC::u32 limit,
                       QC::u8 access, QC::u8 granularity)
    {
        m_entries[index].baseLow = base & 0xFFFF;
        m_entries[index].baseMiddle = (base >> 16) & 0xFF;
        m_entries[index].baseHigh = (base >> 24) & 0xFF;
        m_entries[index].limitLow = limit & 0xFFFF;
        m_entries[index].granularity = ((limit >> 16) & 0x0F) | (granularity & 0xF0);
        m_entries[index].access = access;
    }

    void GDT::setTSSEntry(QC::usize index, QC::u64 base, QC::u32 limit)
    {
        GDTEntry64 *entry = reinterpret_cast<GDTEntry64 *>(&m_entries[index]);

        entry->limitLow = limit & 0xFFFF;
        entry->baseLow = base & 0xFFFF;
        entry->baseMiddle = (base >> 16) & 0xFF;
        entry->access = 0x89; // Present, 64-bit TSS
        entry->granularity = ((limit >> 16) & 0x0F);
        entry->baseHigh = (base >> 24) & 0xFF;
        entry->baseUpper = (base >> 32) & 0xFFFFFFFF;
        entry->reserved = 0;
    }

} // namespace QArch
