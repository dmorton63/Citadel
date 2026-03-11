#pragma once

// QArch IDT - Interrupt Descriptor Table
// Namespace: QArch

#include "QCTypes.h"

namespace QArch
{

    struct IDTEntry
    {
        QC::u16 offsetLow;
        QC::u16 selector;
        QC::u8 ist;
        QC::u8 typeAttr;
        QC::u16 offsetMiddle;
        QC::u32 offsetHigh;
        QC::u32 reserved;
    } __attribute__((packed));

    struct IDTPointer
    {
        QC::u16 limit;
        QC::u64 base;
    } __attribute__((packed));

    // Gate types
    constexpr QC::u8 IDT_INTERRUPT_GATE = 0x8E; // Present, DPL=0, 64-bit interrupt gate
    constexpr QC::u8 IDT_TRAP_GATE = 0x8F;      // Present, DPL=0, 64-bit trap gate
    constexpr QC::u8 IDT_USER_INTERRUPT = 0xEE; // Present, DPL=3, 64-bit interrupt gate

    class IDT
    {
    public:
        static IDT &instance();

        void initialize();
        void load();

        void setEntry(QC::u8 vector, QC::u64 handler, QC::u16 selector,
                      QC::u8 typeAttr, QC::u8 ist = 0);

        // Install interrupt stubs
        void installExceptionStubs();
        void installIRQStubs();

    private:
        IDT();
        ~IDT();
        IDT(const IDT &) = delete;
        IDT &operator=(const IDT &) = delete;

        static constexpr QC::usize IDT_ENTRIES = 256;

        IDTEntry m_entries[IDT_ENTRIES];
        IDTPointer m_pointer;
    };

    // Interrupt stub declarations (implemented in assembly)
    extern "C"
    {
        void isr_stub_0();
        void isr_stub_1();
        void isr_stub_2();
        void isr_stub_3();
        void isr_stub_4();
        void isr_stub_5();
        void isr_stub_6();
        void isr_stub_7();
        void isr_stub_8();
        void isr_stub_9();
        void isr_stub_10();
        void isr_stub_11();
        void isr_stub_12();
        void isr_stub_13();
        void isr_stub_14();
        void isr_stub_15();
        void isr_stub_16();
        void isr_stub_17();
        void isr_stub_18();
        void isr_stub_19();
        void isr_stub_20();
        void isr_stub_21();
        // ... up to 255

        void irq_stub_0();
        void irq_stub_1();
        // ... etc
    }

} // namespace QArch
