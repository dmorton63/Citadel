#pragma once

// Shared helpers for the boot TPM modules (transport, commands, measurement,
// SecureStore sealing, diagnostics). Not part of the public TPM API.

#include "QKBootTPMSecureStore.h"

#include "QCTypes.h"

namespace QK::Boot::Tpm::Internal
{
    inline void LogStr(FLogFn Log, const char *Msg)
    {
        if (Log && Msg)
            Log(Msg);
    }

    void LogHex32Fixed(FLogFn Log, QC::u32 Value);
    void LogHex64(FLogFn Log, const char *Label, QC::u64 Value);
    void LogDecU32(FLogFn Log, const char *Label, QC::u32 Value);

    inline QC::u16 ReadBe16(const QC::u8 *p)
    {
        return static_cast<QC::u16>((static_cast<QC::u16>(p[0]) << 8) | static_cast<QC::u16>(p[1]));
    }

    inline QC::u32 ReadBe32(const QC::u8 *p)
    {
        return (static_cast<QC::u32>(p[0]) << 24) |
               (static_cast<QC::u32>(p[1]) << 16) |
               (static_cast<QC::u32>(p[2]) << 8) |
               static_cast<QC::u32>(p[3]);
    }

    inline void WriteBe16(QC::u8 *p, QC::u16 v)
    {
        p[0] = static_cast<QC::u8>((v >> 8) & 0xFF);
        p[1] = static_cast<QC::u8>(v & 0xFF);
    }

    inline void WriteBe32(QC::u8 *p, QC::u32 v)
    {
        p[0] = static_cast<QC::u8>((v >> 24) & 0xFF);
        p[1] = static_cast<QC::u8>((v >> 16) & 0xFF);
        p[2] = static_cast<QC::u8>((v >> 8) & 0xFF);
        p[3] = static_cast<QC::u8>(v & 0xFF);
    }

    struct FTpmBufWriter
    {
        QC::u8 *Buf;
        QC::usize Cap;
        QC::usize Len;

        bool Push(const void *Data, QC::usize N)
        {
            if (!Data || Len + N > Cap)
                return false;
            const QC::u8 *P = static_cast<const QC::u8 *>(Data);
            for (QC::usize i = 0; i < N; ++i)
                Buf[Len + i] = P[i];
            Len += N;
            return true;
        }

        bool U8(QC::u8 V)
        {
            return Push(&V, 1);
        }

        bool Be16(QC::u16 V)
        {
            QC::u8 Tmp[2];
            WriteBe16(Tmp, V);
            return Push(Tmp, 2);
        }

        bool Be32(QC::u32 V)
        {
            QC::u8 Tmp[4];
            WriteBe32(Tmp, V);
            return Push(Tmp, 4);
        }
    };

    bool EnsureHhdmMapped(FLogFn Log, QC::PhysAddr Phys, QC::usize Size);
    bool EnsureHhdmMappedMmio(FLogFn Log, QC::PhysAddr Phys, QC::usize Size);

    // Bounded pause loop; usable before timers exist and inside IRQ context.
    void BusyWaitMilliseconds(QC::u32 Milliseconds);
}
