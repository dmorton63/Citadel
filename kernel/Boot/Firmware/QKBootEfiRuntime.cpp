#include "QKBootEfiRuntime.h"
#include "QKMemVMM.h"

namespace QK::Boot::EfiRuntime
{
    namespace
    {
        constexpr QC::u64 kPageSize = 4096;
        constexpr QC::u64 kEfiMemoryRuntime = 1ULL << 63;
        constexpr QC::u32 kEfiMemoryMappedIo = 11;
        constexpr QC::u32 kEfiMemoryMappedIoPortSpace = 12;
        constexpr QC::u32 kMaxRegions = 64;
        // Bounds the temporary mapping; OVMF and typical PCs need a few MiB.
        constexpr QC::u64 kMaxPages = 64 * 1024 * 1024 / kPageSize;
        // Runtime code lives in the low canonical half; never touch the kernel half.
        constexpr QC::u64 kLowHalfLimit = 0x0000800000000000ULL;

        constexpr QC::u64 kSystemTableSignature = 0x5453595320494249ULL; // "IBI SYST"
        constexpr QC::u64 kRuntimeServicesSignature = 0x56524553544E5552ULL; // "RUNTSERV"
        constexpr QC::u64 kRuntimeServicesOffset = 0x58;
        constexpr QC::u64 kGetVariableOffset = 0x48;

        constexpr QC::u64 kEfiSuccess = 0;
        constexpr QC::u64 kEfiBufferTooSmall = 0x8000000000000005ULL;

        // EFI_GLOBAL_VARIABLE {8BE4DF61-93CA-11D2-AA0D-00E098032B8C}.
        constexpr QC::u8 kEfiGlobalVariableGuid[16] = {
            0x61, 0xDF, 0xE4, 0x8B, 0xCA, 0x93, 0xD2, 0x11,
            0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C};

        constexpr QC::u16 kSecureBootName[] = {'S', 'e', 'c', 'u', 'r', 'e', 'B', 'o', 'o', 't', 0};
        constexpr QC::u16 kSetupModeName[] = {'S', 'e', 't', 'u', 'p', 'M', 'o', 'd', 'e', 0};
        constexpr QC::u16 kPkName[] = {'P', 'K', 0};

        using FGetVariable = QC::u64(__attribute__((ms_abi)) *)(const QC::u16 *Name, const void *Guid,
                                                               QC::u32 *Attributes, QC::u64 *DataSize,
                                                               void *Data);

        struct FRegion
        {
            QC::u64 Phys;
            QC::u64 Pages;
            bool bMmio;
        };

        struct FEfiMemoryDescriptor
        {
            QC::u32 Type;
            QC::u32 Pad;
            QC::u64 PhysicalStart;
            QC::u64 VirtualStart;
            QC::u64 NumberOfPages;
            QC::u64 Attribute;
        };

        void LogStr(FLogFn Log, const char *S)
        {
            if (Log)
                Log(S);
        }

        bool AnyMapped(const FRegion *Regions, QC::u32 Count)
        {
            auto &Vmm = QK::Memory::VMM::instance();
            for (QC::u32 r = 0; r < Count; ++r)
                for (QC::u64 p = 0; p < Regions[r].Pages; ++p)
                    if (Vmm.isMapped(Regions[r].Phys + p * kPageSize))
                        return true;
            return false;
        }

        void UnmapRegions(const FRegion *Regions, QC::u32 Count)
        {
            auto &Vmm = QK::Memory::VMM::instance();
            for (QC::u32 r = 0; r < Count; ++r)
                for (QC::u64 p = 0; p < Regions[r].Pages; ++p)
                {
                    const QC::u64 Addr = Regions[r].Phys + p * kPageSize;
                    if (Vmm.isMapped(Addr))
                        Vmm.unmap(Addr);
                }
        }

        bool MapRegions(const FRegion *Regions, QC::u32 Count)
        {
            using QK::Memory::PageFlags;
            auto &Vmm = QK::Memory::VMM::instance();
            for (QC::u32 r = 0; r < Count; ++r)
            {
                PageFlags Flags = PageFlags::Present | PageFlags::Writable;
                if (Regions[r].bMmio)
                    Flags = Flags | PageFlags::NoCache;
                for (QC::u64 p = 0; p < Regions[r].Pages; ++p)
                {
                    const QC::u64 Addr = Regions[r].Phys + p * kPageSize;
                    if (Vmm.map(Addr, Addr, Flags) != QC::Status::Success)
                        return false;
                }
            }
            return true;
        }

        bool InRegions(const FRegion *Regions, QC::u32 Count, QC::u64 Addr, QC::u64 Len)
        {
            for (QC::u32 r = 0; r < Count; ++r)
            {
                const QC::u64 End = Regions[r].Phys + Regions[r].Pages * kPageSize;
                if (Addr >= Regions[r].Phys && Addr <= End && Len <= End - Addr)
                    return true;
            }
            return false;
        }

        QC::u64 SaveAndDisableInterrupts()
        {
            QC::u64 Flags;
            asm volatile("pushfq; pop %0; cli" : "=r"(Flags) : : "memory");
            return Flags;
        }

        void RestoreInterrupts(QC::u64 Flags)
        {
            if (Flags & (1ULL << 9))
                asm volatile("sti" : : : "memory");
        }

        QC::u64 g_LastRc = 0;

        void LogHex(FLogFn Log, QC::u64 V)
        {
            char Buf[19] = {'0', 'x'};
            for (int i = 0; i < 16; ++i)
                Buf[2 + i] = "0123456789abcdef"[(V >> ((15 - i) * 4)) & 0xF];
            Buf[18] = 0;
            LogStr(Log, Buf);
        }

        // Returns true when the variable exists; Value receives its first byte.
        bool QueryByte(FGetVariable GetVariable, const QC::u16 *Name, QC::u8 &Value)
        {
            QC::u8 Buffer[16]{};
            QC::u64 Size = sizeof(Buffer);
            QC::u32 Attributes = 0;
            const QC::u64 Rc = GetVariable(Name, kEfiGlobalVariableGuid, &Attributes, &Size, Buffer);
            g_LastRc = Rc;
            if (Rc != kEfiSuccess || Size == 0)
                return false;
            Value = Buffer[0];
            return true;
        }

        bool QueryPresent(FGetVariable GetVariable, const QC::u16 *Name)
        {
            QC::u64 Size = 0;
            QC::u32 Attributes = 0;
            const QC::u64 Rc = GetVariable(Name, kEfiGlobalVariableGuid, &Attributes, &Size, nullptr);
            return Rc == kEfiBufferTooSmall || (Rc == kEfiSuccess && Size > 0);
        }
    }

    bool ReadGlobalVariables(QC::u64 SystemTablePhys, const void *Memmap, QC::u64 MemmapSize,
                             QC::u64 DescSize, FGlobalVariables &Out, FLogFn Log)
    {
        Out = FGlobalVariables{};
        if (!SystemTablePhys || !Memmap || DescSize < sizeof(FEfiMemoryDescriptor) || MemmapSize < DescSize)
        {
            LogStr(Log, "EFI runtime: no system table or memory map\r\n");
            return false;
        }

        // Copy the runtime descriptors first: mapping allocates page tables,
        // which may reuse bootloader-reclaimable memory holding the map.
        FRegion Regions[kMaxRegions];
        QC::u32 Count = 0;
        QC::u64 TotalPages = 0;
        const auto *Base = static_cast<const QC::u8 *>(Memmap);
        for (QC::u64 Off = 0; Off + DescSize <= MemmapSize; Off += DescSize)
        {
            const auto *D = reinterpret_cast<const FEfiMemoryDescriptor *>(Base + Off);
            if (!(D->Attribute & kEfiMemoryRuntime) || D->NumberOfPages == 0)
                continue;
            if (Count == kMaxRegions || D->PhysicalStart >= kLowHalfLimit ||
                D->NumberOfPages > kMaxPages - TotalPages)
            {
                LogStr(Log, "EFI runtime: runtime regions exceed mapping limits\r\n");
                return false;
            }
            Regions[Count++] = FRegion{D->PhysicalStart & ~(kPageSize - 1), D->NumberOfPages,
                                       D->Type == kEfiMemoryMappedIo || D->Type == kEfiMemoryMappedIoPortSpace};
            TotalPages += D->NumberOfPages;
        }
        if (Count == 0)
        {
            LogStr(Log, "EFI runtime: no runtime regions\r\n");
            return false;
        }
        if (AnyMapped(Regions, Count))
        {
            LogStr(Log, "EFI runtime: runtime regions overlap existing mappings\r\n");
            return false;
        }
        if (!InRegions(Regions, Count, SystemTablePhys, 0x60))
        {
            LogStr(Log, "EFI runtime: system table outside runtime memory\r\n");
            return false;
        }

        QC::u64 SecureBootRc = 0;
        const QC::u64 IrqFlags = SaveAndDisableInterrupts();
        bool bOk = MapRegions(Regions, Count);
        if (bOk)
        {
            const auto *SystemTable = reinterpret_cast<const QC::u8 *>(SystemTablePhys);
            const QC::u64 RuntimeServices = *reinterpret_cast<const QC::u64 *>(SystemTable + kRuntimeServicesOffset);
            bOk = *reinterpret_cast<const QC::u64 *>(SystemTable) == kSystemTableSignature &&
                  InRegions(Regions, Count, RuntimeServices, kGetVariableOffset + 8) &&
                  *reinterpret_cast<const QC::u64 *>(RuntimeServices) == kRuntimeServicesSignature;
            if (bOk)
            {
                const QC::u64 Fn = *reinterpret_cast<const QC::u64 *>(RuntimeServices + kGetVariableOffset);
                bOk = InRegions(Regions, Count, Fn, 1);
                if (bOk)
                {
                    const auto GetVariable = reinterpret_cast<FGetVariable>(Fn);
                    Out.bSecureBootPresent = QueryByte(GetVariable, kSecureBootName, Out.SecureBoot);
                    SecureBootRc = g_LastRc;
                    Out.bSetupModePresent = QueryByte(GetVariable, kSetupModeName, Out.SetupMode);
                    Out.bPkPresent = QueryPresent(GetVariable, kPkName);
                }
            }
        }
        UnmapRegions(Regions, Count);
        RestoreInterrupts(IrqFlags);

        if (!bOk)
        {
            LogStr(Log, "EFI runtime: GetVariable unavailable\r\n");
            return false;
        }
        LogStr(Log, "EFI runtime: SecureBoot=");
        LogStr(Log, !Out.bSecureBootPresent ? "absent" : (Out.SecureBoot ? "1" : "0"));
        LogStr(Log, " SetupMode=");
        LogStr(Log, !Out.bSetupModePresent ? "absent" : (Out.SetupMode ? "1" : "0"));
        LogStr(Log, Out.bPkPresent ? " PK=present rc=" : " PK=absent rc=");
        LogHex(Log, SecureBootRc);
        LogStr(Log, "\r\n");
        return true;
    }
}
