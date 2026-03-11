// QKernel Interrupts - Implementation
// Namespace: QK

#include "QKInterrupts.h"
#include "QCLogger.h"
#include "QCBuiltins.h"

namespace QK
{

    // PIC ports
    constexpr QC::u16 PIC1_COMMAND = 0x20;
    constexpr QC::u16 PIC1_DATA = 0x21;
    constexpr QC::u16 PIC2_COMMAND = 0xA0;
    constexpr QC::u16 PIC2_DATA = 0xA1;

    InterruptManager &InterruptManager::instance()
    {
        static InterruptManager instance;
        return instance;
    }

    InterruptManager::InterruptManager()
    {
        for (int i = 0; i < 256; ++i)
        {
            m_handlers[i] = nullptr;
        }
    }

    InterruptManager::~InterruptManager()
    {
    }

    void InterruptManager::initialize()
    {
        QC_LOG_INFO("QKInt", "Initializing interrupt manager");

        initializePIC();

        QC_LOG_INFO("QKInt", "Interrupt manager initialized");
    }

    void InterruptManager::initializePIC()
    {
        // ICW1: Initialize + ICW4 needed
        QC::outb(PIC1_COMMAND, 0x11);
        QC::outb(PIC2_COMMAND, 0x11);

        // ICW2: Vector offsets
        QC::outb(PIC1_DATA, IRQ_BASE);     // Master: IRQ 0-7 -> INT 32-39
        QC::outb(PIC2_DATA, IRQ_BASE + 8); // Slave: IRQ 8-15 -> INT 40-47

        // ICW3: Master/Slave wiring
        QC::outb(PIC1_DATA, 0x04); // Slave on IRQ2
        QC::outb(PIC2_DATA, 0x02); // Slave ID

        // ICW4: 8086 mode
        QC::outb(PIC1_DATA, 0x01);
        QC::outb(PIC2_DATA, 0x01);

        // Mask all interrupts initially
        QC::outb(PIC1_DATA, 0xFF);
        QC::outb(PIC2_DATA, 0xFF);
    }

    void InterruptManager::initializeAPIC()
    {
        // TODO: Implement APIC initialization for SMP support
    }

    void InterruptManager::registerHandler(QC::u8 vector, InterruptHandler handler)
    {
        m_handlers[vector] = handler;
        QC_LOG_DEBUG("QKInt", "Registered handler for vector %u", vector);
    }

    void InterruptManager::unregisterHandler(QC::u8 vector)
    {
        m_handlers[vector] = nullptr;
    }

    void InterruptManager::enableInterrupt(QC::u8 irq)
    {
        QC::u16 port;
        QC::u8 irqBit = irq;
        if (irq < 8)
        {
            port = PIC1_DATA;
        }
        else
        {
            // For slave PIC interrupts (IRQ 8-15), also enable IRQ2 (cascade)
            // on the master PIC to allow slave interrupts to reach the CPU
            QC::u8 masterMask = QC::inb(PIC1_DATA);
            masterMask &= ~(1 << 2); // Enable IRQ2 (cascade)
            QC::outb(PIC1_DATA, masterMask);

            port = PIC2_DATA;
            irqBit = irq - 8;
        }
        QC::u8 mask = QC::inb(port);
        mask &= ~(1 << irqBit);
        QC::outb(port, mask);
    }

    void InterruptManager::disableInterrupt(QC::u8 irq)
    {
        QC::u16 port;
        if (irq < 8)
        {
            port = PIC1_DATA;
        }
        else
        {
            port = PIC2_DATA;
            irq -= 8;
        }
        QC::u8 mask = QC::inb(port);
        mask |= (1 << irq);
        QC::outb(port, mask);
    }

    void InterruptManager::sendEOI(QC::u8 irq)
    {
        if (irq >= 8)
        {
            QC::outb(PIC2_COMMAND, 0x20);
        }
        QC::outb(PIC1_COMMAND, 0x20);
    }

    void InterruptManager::dispatch(InterruptFrame *frame)
    {
        InterruptManager &mgr = instance();
        QC::u8 vector = static_cast<QC::u8>(frame->vector);

        if (mgr.m_handlers[vector])
        {
            mgr.m_handlers[vector](frame);
        }
        else if (vector < 32)
        {
            // Unhandled exception
            QC_LOG_FATAL("QKInt", "Unhandled exception %u at RIP=%lx", vector, frame->rip);
            QC::halt();
        }

        // Send EOI for hardware interrupts
        if (vector >= IRQ_BASE && vector < IRQ_BASE + 16)
        {
            mgr.sendEOI(vector - IRQ_BASE);
        }
    }

} // namespace QK

// C-callable interrupt handlers for assembly stubs
extern "C"
{

    void isr_handler(QK::InterruptFrame *frame)
    {
        QK::InterruptManager::dispatch(frame);
    }

    void irq_handler(QK::InterruptFrame *frame)
    {
        QK::InterruptManager::dispatch(frame);
    }

} // extern "C"
