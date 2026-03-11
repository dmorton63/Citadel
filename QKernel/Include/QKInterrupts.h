#pragma once

// QKernel Interrupts - Interrupt handling
// Namespace: QK

#include "QCTypes.h"

namespace QK
{

    // Interrupt vector numbers
    constexpr QC::u8 INT_DIVIDE_ERROR = 0;
    constexpr QC::u8 INT_DEBUG = 1;
    constexpr QC::u8 INT_NMI = 2;
    constexpr QC::u8 INT_BREAKPOINT = 3;
    constexpr QC::u8 INT_OVERFLOW = 4;
    constexpr QC::u8 INT_BOUND_RANGE = 5;
    constexpr QC::u8 INT_INVALID_OPCODE = 6;
    constexpr QC::u8 INT_DEVICE_NOT_AVAILABLE = 7;
    constexpr QC::u8 INT_DOUBLE_FAULT = 8;
    constexpr QC::u8 INT_INVALID_TSS = 10;
    constexpr QC::u8 INT_SEGMENT_NOT_PRESENT = 11;
    constexpr QC::u8 INT_STACK_FAULT = 12;
    constexpr QC::u8 INT_GENERAL_PROTECTION = 13;
    constexpr QC::u8 INT_PAGE_FAULT = 14;
    constexpr QC::u8 INT_X87_FPU_ERROR = 16;
    constexpr QC::u8 INT_ALIGNMENT_CHECK = 17;
    constexpr QC::u8 INT_MACHINE_CHECK = 18;
    constexpr QC::u8 INT_SIMD_FP_EXCEPTION = 19;

    // IRQ offsets (remapped)
    constexpr QC::u8 IRQ_BASE = 32;
    constexpr QC::u8 IRQ_TIMER = IRQ_BASE + 0;
    constexpr QC::u8 IRQ_KEYBOARD = IRQ_BASE + 1;
    constexpr QC::u8 IRQ_CASCADE = IRQ_BASE + 2;
    constexpr QC::u8 IRQ_COM2 = IRQ_BASE + 3;
    constexpr QC::u8 IRQ_COM1 = IRQ_BASE + 4;
    constexpr QC::u8 IRQ_MOUSE = IRQ_BASE + 12;

    struct InterruptFrame
    {
        QC::u64 r15, r14, r13, r12, r11, r10, r9, r8;
        QC::u64 rdi, rsi, rbp, rbx, rdx, rcx, rax;
        QC::u64 vector, errorCode;
        QC::u64 rip, cs, rflags, rsp, ss;
    };

    using InterruptHandler = void (*)(InterruptFrame *frame);

    class InterruptManager
    {
    public:
        static InterruptManager &instance();

        void initialize();

        void registerHandler(QC::u8 vector, InterruptHandler handler);
        void unregisterHandler(QC::u8 vector);

        void enableInterrupt(QC::u8 irq);
        void disableInterrupt(QC::u8 irq);

        void sendEOI(QC::u8 irq);

        // Called from assembly interrupt stubs
        static void dispatch(InterruptFrame *frame);

    private:
        InterruptManager();
        ~InterruptManager();
        InterruptManager(const InterruptManager &) = delete;
        InterruptManager &operator=(const InterruptManager &) = delete;

        void initializePIC();
        void initializeAPIC();

        InterruptHandler m_handlers[256];
    };

} // namespace QK
