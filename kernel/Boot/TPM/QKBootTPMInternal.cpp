#include "QKBootTPMInternal.h"

#include "QCBuiltins.h"
#include "QKMemVMM.h"

extern "C" QC::VirtAddr physToVirt(QC::PhysAddr phys);

namespace QK::Boot::Tpm::Internal
{
    void LogHex32Fixed(FLogFn Log, QC::u32 Value)
    {
        char HexBuf[9];
        for (int i = 7; i >= 0; --i)
        {
            int nibble = static_cast<int>((Value >> (i * 4)) & 0xF);
            HexBuf[7 - i] = nibble < 10 ? static_cast<char>('0' + nibble) : static_cast<char>('a' + nibble - 10);
        }
        HexBuf[8] = 0;
        LogStr(Log, HexBuf);
    }

    void LogHex64(FLogFn Log, const char *Label, QC::u64 Value)
    {
        LogStr(Log, Label);
        LogStr(Log, "0x");

        char HexBuf[17];
        for (int i = 15; i >= 0; --i)
        {
            int nibble = static_cast<int>((Value >> (i * 4)) & 0xF);
            HexBuf[15 - i] = nibble < 10 ? static_cast<char>('0' + nibble) : static_cast<char>('a' + nibble - 10);
        }
        HexBuf[16] = 0;
        LogStr(Log, HexBuf);
        LogStr(Log, "\r\n");
    }

    void LogDecU32(FLogFn Log, const char *Label, QC::u32 Value)
    {
        LogStr(Log, Label);

        char Buf[11];
        QC::usize Pos = 0;
        if (Value == 0)
        {
            Buf[Pos++] = '0';
        }
        else
        {
            char Tmp[10];
            QC::usize TmpPos = 0;
            while (Value > 0 && TmpPos < sizeof(Tmp))
            {
                Tmp[TmpPos++] = static_cast<char>('0' + (Value % 10));
                Value /= 10;
            }
            while (TmpPos > 0)
                Buf[Pos++] = Tmp[--TmpPos];
        }

        Buf[Pos] = 0;
        LogStr(Log, Buf);
        LogStr(Log, "\r\n");
    }

    static bool EnsureHhdmMappedWithFlags(FLogFn Log, QC::PhysAddr Phys, QC::usize Size, QK::Memory::PageFlags Flags)
    {
        if (Phys == 0 || Size == 0)
            return false;

        constexpr QC::usize kPageSize = 4096;
        QC::PhysAddr Start = Phys & ~(static_cast<QC::PhysAddr>(kPageSize - 1));
        QC::PhysAddr End = (Phys + Size + (kPageSize - 1)) & ~(static_cast<QC::PhysAddr>(kPageSize - 1));

        for (QC::PhysAddr P = Start; P < End; P += kPageSize)
        {
            QC::VirtAddr V = physToVirt(P);
            if (!QK::Memory::VMM::instance().isMapped(V))
            {
                QC::Status Status = QK::Memory::VMM::instance().map(V, P, Flags);
                if (Status != QC::Status::Success)
                {
                    LogStr(Log, "TPM2: failed to map physical page\r\n");
                    return false;
                }
            }
        }

        return true;
    }

    bool EnsureHhdmMapped(FLogFn Log, QC::PhysAddr Phys, QC::usize Size)
    {
        auto Flags = QK::Memory::PageFlags::Present | QK::Memory::PageFlags::Writable | QK::Memory::PageFlags::NoExecute;
        return EnsureHhdmMappedWithFlags(Log, Phys, Size, Flags);
    }

    bool EnsureHhdmMappedMmio(FLogFn Log, QC::PhysAddr Phys, QC::usize Size)
    {
        auto Flags = QK::Memory::PageFlags::Present | QK::Memory::PageFlags::Writable |
                     QK::Memory::PageFlags::NoExecute | QK::Memory::PageFlags::NoCache |
                     QK::Memory::PageFlags::WriteThrough;
        return EnsureHhdmMappedWithFlags(Log, Phys, Size, Flags);
    }

    void BusyWaitMilliseconds(QC::u32 Milliseconds)
    {
        constexpr QC::usize kPauseIterationsPerMillisecond = 200000;
        for (QC::u32 ms = 0; ms < Milliseconds; ++ms)
        {
            for (QC::usize i = 0; i < kPauseIterationsPerMillisecond; ++i)
                QC::pause();
        }
    }
}
