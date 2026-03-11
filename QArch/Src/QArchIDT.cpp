// QArch IDT - Implementation
// Namespace: QArch

#include "QArchIDT.h"
#include "QCLogger.h"
#include "QCString.h"

// Assembly stub declarations - match the actual assembly symbol names
extern "C"
{
    // Exception stubs (isr0-isr31)
    void isr0();
    void isr1();
    void isr2();
    void isr3();
    void isr4();
    void isr5();
    void isr6();
    void isr7();
    void isr8();
    void isr9();
    void isr10();
    void isr11();
    void isr12();
    void isr13();
    void isr14();
    void isr15();
    void isr16();
    void isr17();
    void isr18();
    void isr19();
    void isr20();
    void isr21();
    void isr22();
    void isr23();
    void isr24();
    void isr25();
    void isr26();
    void isr27();
    void isr28();
    void isr29();
    void isr30();
    void isr31();

    // IRQ stubs (irq0-irq15)
    void irq0();
    void irq1();
    void irq2();
    void irq3();
    void irq4();
    void irq5();
    void irq6();
    void irq7();
    void irq8();
    void irq9();
    void irq10();
    void irq11();
    void irq12();
    void irq13();
    void irq14();
    void irq15();
}

namespace QArch
{

    IDT &IDT::instance()
    {
        static IDT instance;
        return instance;
    }

    IDT::IDT()
    {
        QC::String::memset(m_entries, 0, sizeof(m_entries));
    }

    IDT::~IDT()
    {
    }

    void IDT::initialize()
    {
        QC_LOG_INFO("QArchIDT", "Initializing IDT");

        m_pointer.limit = sizeof(m_entries) - 1;
        m_pointer.base = reinterpret_cast<QC::u64>(m_entries);

        installExceptionStubs();
        installIRQStubs();

        // Load the IDT
        load();

        QC_LOG_INFO("QArchIDT", "IDT initialized with %lu entries", IDT_ENTRIES);
    }

    void IDT::load()
    {
        asm volatile("lidt %0" : : "m"(m_pointer));
        QC_LOG_INFO("QArchIDT", "IDT loaded");
    }

    void IDT::setEntry(QC::u8 vector, QC::u64 handler, QC::u16 selector,
                       QC::u8 typeAttr, QC::u8 ist)
    {
        IDTEntry &entry = m_entries[vector];

        entry.offsetLow = handler & 0xFFFF;
        entry.selector = selector;
        entry.ist = ist & 0x07;
        entry.typeAttr = typeAttr;
        entry.offsetMiddle = (handler >> 16) & 0xFFFF;
        entry.offsetHigh = (handler >> 32) & 0xFFFFFFFF;
        entry.reserved = 0;
    }

    void IDT::installExceptionStubs()
    {
        // Install all 32 exception handlers (vectors 0-31)
        // Use 0x28 as selector - matches Limine's kernel code segment
        setEntry(0, reinterpret_cast<QC::u64>(isr0), 0x28, IDT_INTERRUPT_GATE);
        setEntry(1, reinterpret_cast<QC::u64>(isr1), 0x28, IDT_INTERRUPT_GATE);
        setEntry(2, reinterpret_cast<QC::u64>(isr2), 0x28, IDT_INTERRUPT_GATE);
        setEntry(3, reinterpret_cast<QC::u64>(isr3), 0x28, IDT_INTERRUPT_GATE);
        setEntry(4, reinterpret_cast<QC::u64>(isr4), 0x28, IDT_INTERRUPT_GATE);
        setEntry(5, reinterpret_cast<QC::u64>(isr5), 0x28, IDT_INTERRUPT_GATE);
        setEntry(6, reinterpret_cast<QC::u64>(isr6), 0x28, IDT_INTERRUPT_GATE);
        setEntry(7, reinterpret_cast<QC::u64>(isr7), 0x28, IDT_INTERRUPT_GATE);
        setEntry(8, reinterpret_cast<QC::u64>(isr8), 0x28, IDT_INTERRUPT_GATE);
        setEntry(9, reinterpret_cast<QC::u64>(isr9), 0x28, IDT_INTERRUPT_GATE);
        setEntry(10, reinterpret_cast<QC::u64>(isr10), 0x28, IDT_INTERRUPT_GATE);
        setEntry(11, reinterpret_cast<QC::u64>(isr11), 0x28, IDT_INTERRUPT_GATE);
        setEntry(12, reinterpret_cast<QC::u64>(isr12), 0x28, IDT_INTERRUPT_GATE);
        setEntry(13, reinterpret_cast<QC::u64>(isr13), 0x28, IDT_INTERRUPT_GATE);
        setEntry(14, reinterpret_cast<QC::u64>(isr14), 0x28, IDT_INTERRUPT_GATE);
        setEntry(15, reinterpret_cast<QC::u64>(isr15), 0x28, IDT_INTERRUPT_GATE);
        setEntry(16, reinterpret_cast<QC::u64>(isr16), 0x28, IDT_INTERRUPT_GATE);
        setEntry(17, reinterpret_cast<QC::u64>(isr17), 0x28, IDT_INTERRUPT_GATE);
        setEntry(18, reinterpret_cast<QC::u64>(isr18), 0x28, IDT_INTERRUPT_GATE);
        setEntry(19, reinterpret_cast<QC::u64>(isr19), 0x28, IDT_INTERRUPT_GATE);
        setEntry(20, reinterpret_cast<QC::u64>(isr20), 0x28, IDT_INTERRUPT_GATE);
        setEntry(21, reinterpret_cast<QC::u64>(isr21), 0x28, IDT_INTERRUPT_GATE);
        setEntry(22, reinterpret_cast<QC::u64>(isr22), 0x28, IDT_INTERRUPT_GATE);
        setEntry(23, reinterpret_cast<QC::u64>(isr23), 0x28, IDT_INTERRUPT_GATE);
        setEntry(24, reinterpret_cast<QC::u64>(isr24), 0x28, IDT_INTERRUPT_GATE);
        setEntry(25, reinterpret_cast<QC::u64>(isr25), 0x28, IDT_INTERRUPT_GATE);
        setEntry(26, reinterpret_cast<QC::u64>(isr26), 0x28, IDT_INTERRUPT_GATE);
        setEntry(27, reinterpret_cast<QC::u64>(isr27), 0x28, IDT_INTERRUPT_GATE);
        setEntry(28, reinterpret_cast<QC::u64>(isr28), 0x28, IDT_INTERRUPT_GATE);
        setEntry(29, reinterpret_cast<QC::u64>(isr29), 0x28, IDT_INTERRUPT_GATE);
        setEntry(30, reinterpret_cast<QC::u64>(isr30), 0x28, IDT_INTERRUPT_GATE);
        setEntry(31, reinterpret_cast<QC::u64>(isr31), 0x28, IDT_INTERRUPT_GATE);
    }

    void IDT::installIRQStubs()
    {
        // Install all 16 IRQ handlers (vectors 32-47)
        // Use 0x28 as selector - matches Limine's kernel code segment
        setEntry(32, reinterpret_cast<QC::u64>(irq0), 0x28, IDT_INTERRUPT_GATE);
        setEntry(33, reinterpret_cast<QC::u64>(irq1), 0x28, IDT_INTERRUPT_GATE);
        setEntry(34, reinterpret_cast<QC::u64>(irq2), 0x28, IDT_INTERRUPT_GATE);
        setEntry(35, reinterpret_cast<QC::u64>(irq3), 0x28, IDT_INTERRUPT_GATE);
        setEntry(36, reinterpret_cast<QC::u64>(irq4), 0x28, IDT_INTERRUPT_GATE);
        setEntry(37, reinterpret_cast<QC::u64>(irq5), 0x28, IDT_INTERRUPT_GATE);
        setEntry(38, reinterpret_cast<QC::u64>(irq6), 0x28, IDT_INTERRUPT_GATE);
        setEntry(39, reinterpret_cast<QC::u64>(irq7), 0x28, IDT_INTERRUPT_GATE);
        setEntry(40, reinterpret_cast<QC::u64>(irq8), 0x28, IDT_INTERRUPT_GATE);
        setEntry(41, reinterpret_cast<QC::u64>(irq9), 0x28, IDT_INTERRUPT_GATE);
        setEntry(42, reinterpret_cast<QC::u64>(irq10), 0x28, IDT_INTERRUPT_GATE);
        setEntry(43, reinterpret_cast<QC::u64>(irq11), 0x28, IDT_INTERRUPT_GATE);
        setEntry(44, reinterpret_cast<QC::u64>(irq12), 0x28, IDT_INTERRUPT_GATE);
        setEntry(45, reinterpret_cast<QC::u64>(irq13), 0x28, IDT_INTERRUPT_GATE);
        setEntry(46, reinterpret_cast<QC::u64>(irq14), 0x28, IDT_INTERRUPT_GATE);
        setEntry(47, reinterpret_cast<QC::u64>(irq15), 0x28, IDT_INTERRUPT_GATE);
    }

} // namespace QArch
