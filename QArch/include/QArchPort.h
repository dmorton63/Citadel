#pragma once

// QArch Port I/O - Low-level port access
// Namespace: QArch

#include "QCBuiltins.h"

namespace QArch
{

    // Port I/O functions
    // NOTE: These forward to QC::out*/in* in QCBuiltins.h so there is a single
    // canonical implementation of the inline-asm semantics.
    inline void outb(QC::u16 port, QC::u8 value)
    {
        QC::outb(port, value);
    }

    inline QC::u8 inb(QC::u16 port)
    {
        return QC::inb(port);
    }

    inline void outw(QC::u16 port, QC::u16 value)
    {
        QC::outw(port, value);
    }

    inline QC::u16 inw(QC::u16 port)
    {
        return QC::inw(port);
    }

    inline void outl(QC::u16 port, QC::u32 value)
    {
        QC::outl(port, value);
    }

    inline QC::u32 inl(QC::u16 port)
    {
        return QC::inl(port);
    }

    // I/O wait (for legacy devices)
    inline void io_wait()
    {
        QC::outb(static_cast<QC::u16>(0x80), static_cast<QC::u8>(0));
    }

    // String I/O
    inline void outsb(QC::u16 port, const void *data, QC::usize count)
    {
        asm volatile("rep outsb" : "+S"(data), "+c"(count) : "d"(port));
    }

    inline void insb(QC::u16 port, void *data, QC::usize count)
    {
        asm volatile("rep insb" : "+D"(data), "+c"(count) : "d"(port) : "memory");
    }

    inline void outsw(QC::u16 port, const void *data, QC::usize count)
    {
        asm volatile("rep outsw" : "+S"(data), "+c"(count) : "d"(port));
    }

    inline void insw(QC::u16 port, void *data, QC::usize count)
    {
        asm volatile("rep insw" : "+D"(data), "+c"(count) : "d"(port) : "memory");
    }

    inline void outsl(QC::u16 port, const void *data, QC::usize count)
    {
        asm volatile("rep outsl" : "+S"(data), "+c"(count) : "d"(port));
    }

    inline void insl(QC::u16 port, void *data, QC::usize count)
    {
        asm volatile("rep insl" : "+D"(data), "+c"(count) : "d"(port) : "memory");
    }

} // namespace QArch
