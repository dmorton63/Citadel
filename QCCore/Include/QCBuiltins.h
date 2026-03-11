#pragma once

// QCommon Builtins - Compiler intrinsics and low-level utilities
// Namespace: QC

#include "QCTypes.h"

namespace QC
{

    // Port I/O
    inline void outb(u16 port, u8 value)
    {
        asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
    }

    inline u8 inb(u16 port)
    {
        u8 value;
        asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }

    inline void outw(u16 port, u16 value)
    {
        asm volatile("outw %0, %1" : : "a"(value), "Nd"(port));
    }

    inline u16 inw(u16 port)
    {
        u16 value;
        asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }

    inline void outl(u16 port, u32 value)
    {
        asm volatile("outl %0, %1" : : "a"(value), "Nd"(port));
    }

    inline u32 inl(u16 port)
    {
        u32 value;
        asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }

    // MMIO
    inline void mmio_write8(VirtAddr addr, u8 value)
    {
        *reinterpret_cast<volatile u8 *>(addr) = value;
    }

    inline u8 mmio_read8(VirtAddr addr)
    {
        return *reinterpret_cast<volatile u8 *>(addr);
    }

    inline void mmio_write16(VirtAddr addr, u16 value)
    {
        *reinterpret_cast<volatile u16 *>(addr) = value;
    }

    inline u16 mmio_read16(VirtAddr addr)
    {
        return *reinterpret_cast<volatile u16 *>(addr);
    }

    inline void mmio_write32(VirtAddr addr, u32 value)
    {
        *reinterpret_cast<volatile u32 *>(addr) = value;
    }

    inline u32 mmio_read32(VirtAddr addr)
    {
        return *reinterpret_cast<volatile u32 *>(addr);
    }

    inline void mmio_write64(VirtAddr addr, u64 value)
    {
        *reinterpret_cast<volatile u64 *>(addr) = value;
    }

    inline u64 mmio_read64(VirtAddr addr)
    {
        return *reinterpret_cast<volatile u64 *>(addr);
    }

    // CPU control
    inline void halt()
    {
        asm volatile("hlt");
    }

    inline void cli()
    {
        asm volatile("cli");
    }

    inline void sti()
    {
        asm volatile("sti");
    }

    inline void pause()
    {
        asm volatile("pause");
    }

    inline void wbinvd()
    {
        // Write back and invalidate the entire CPU cache hierarchy.
        // Useful as a blunt tool when writing to device VRAM through a cacheable mapping.
        asm volatile("wbinvd" ::: "memory");
    }

    // MSR access
    inline u64 read_msr(u32 msr)
    {
        u32 low, high;
        asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
        return (static_cast<u64>(high) << 32) | low;
    }

    inline void write_msr(u32 msr, u64 value)
    {
        u32 low = static_cast<u32>(value);
        u32 high = static_cast<u32>(value >> 32);
        asm volatile("wrmsr" : : "a"(low), "d"(high), "c"(msr));
    }

    // Memory barriers
    inline void memory_barrier()
    {
        asm volatile("mfence" ::: "memory");
    }

    inline void read_barrier()
    {
        asm volatile("lfence" ::: "memory");
    }

    inline void write_barrier()
    {
        asm volatile("sfence" ::: "memory");
    }

    // Simple serial debug output (COM1 at 0x3F8)
    inline void serial_putc(char c)
    {
        // Wait for transmit buffer to be empty
        while ((inb(0x3F8 + 5) & 0x20) == 0)
        {
        }
        outb(0x3F8, static_cast<u8>(c));
    }

    inline void serial_puts(const char *str)
    {
        while (*str)
        {
            serial_putc(*str++);
        }
    }

} // namespace QC
