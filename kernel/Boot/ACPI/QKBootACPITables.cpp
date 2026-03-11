#include "QKBootACPITables.h"

#include "QCString.h"
#include "QKMemVMM.h"

extern "C" QC::VirtAddr physToVirt(QC::PhysAddr phys);

namespace QK::Boot::Acpi
{
    namespace
    {
#pragma pack(push, 1)
        struct AcpiRsdp
        {
            char signature[8];
            QC::u8 checksum;
            char oemId[6];
            QC::u8 revision;
            QC::u32 rsdtAddress;
            QC::u32 length;
            QC::u64 xsdtAddress;
            QC::u8 extendedChecksum;
            QC::u8 reserved[3];
        };

        struct AcpiSdtHeader
        {
            char signature[4];
            QC::u32 length;
            QC::u8 revision;
            QC::u8 checksum;
            char oemId[6];
            char oemTableId[8];
            QC::u32 oemRevision;
            QC::u32 creatorId;
            QC::u32 creatorRevision;
        };

        struct AcpiTpm2TableBase
        {
            AcpiSdtHeader header;
            QC::u16 platformClass;
            QC::u16 reserved;
            QC::u64 controlArea;
            QC::u32 startMethod;
            QC::u8 startMethodParameters[12];
        };
#pragma pack(pop)

        static void LogStr(FLogFn Log, const char *Msg)
        {
            if (Log)
                Log(Msg);
        }

        static void LogHex64(FLogFn Log, const char *Label, QC::u64 Value)
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

        static void LogDecU32(FLogFn Log, const char *Label, QC::u32 Value)
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
                {
                    Buf[Pos++] = Tmp[--TmpPos];
                }
            }

            Buf[Pos] = 0;
            LogStr(Log, Buf);
            LogStr(Log, "\r\n");
        }

        static void LogHex32Fixed(FLogFn Log, QC::u32 Value)
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
                        LogStr(Log, "ACPI: failed to map physical page\r\n");
                        return false;
                    }
                }
            }

            return true;
        }

        static bool EnsureHhdmMapped(FLogFn Log, QC::PhysAddr Phys, QC::usize Size)
        {
            auto Flags = QK::Memory::PageFlags::Present | QK::Memory::PageFlags::Writable | QK::Memory::PageFlags::NoExecute;
            return EnsureHhdmMappedWithFlags(Log, Phys, Size, Flags);
        }

        static bool EnsureHhdmMappedMmio(FLogFn Log, QC::PhysAddr Phys, QC::usize Size)
        {
            auto Flags = QK::Memory::PageFlags::Present | QK::Memory::PageFlags::Writable |
                         QK::Memory::PageFlags::NoExecute | QK::Memory::PageFlags::NoCache |
                         QK::Memory::PageFlags::WriteThrough;
            return EnsureHhdmMappedWithFlags(Log, Phys, Size, Flags);
        }
    }

    void EnumerateTables(QC::PhysAddr RsdpPhys, FLogFn Log, FTpm2CrbStartupFn Tpm2CrbStartup)
    {
        if (RsdpPhys == 0)
        {
            LogStr(Log, "ACPI: no RSDP address\r\n");
            return;
        }

        LogHex64(Log, "ACPI: RSDP phys ", RsdpPhys);
        if (!EnsureHhdmMapped(Log, RsdpPhys, sizeof(AcpiRsdp)))
        {
            LogStr(Log, "ACPI: RSDP mapping failed\r\n");
            return;
        }

        auto *Rsdp = reinterpret_cast<const AcpiRsdp *>(physToVirt(RsdpPhys));
        if (QC::String::memcmp(Rsdp->signature, "RSD PTR ", 8) != 0)
        {
            LogStr(Log, "ACPI: invalid RSDP signature\r\n");
            return;
        }

        LogStr(Log, "ACPI: RSDP OK\r\n");
        LogStr(Log, "ACPI: using ");

        bool bUseXsdt = (Rsdp->revision >= 2) && (Rsdp->xsdtAddress != 0);
        QC::PhysAddr SdtPhys = bUseXsdt ? static_cast<QC::PhysAddr>(Rsdp->xsdtAddress)
                                        : static_cast<QC::PhysAddr>(Rsdp->rsdtAddress);

        LogStr(Log, bUseXsdt ? "XSDT\r\n" : "RSDT\r\n");
        if (SdtPhys == 0)
        {
            LogStr(Log, "ACPI: SDT address is null\r\n");
            return;
        }

        if (!EnsureHhdmMapped(Log, SdtPhys, sizeof(AcpiSdtHeader)))
        {
            LogStr(Log, "ACPI: SDT header mapping failed\r\n");
            return;
        }

        auto *Sdt = reinterpret_cast<const AcpiSdtHeader *>(physToVirt(SdtPhys));
        LogHex64(Log, "ACPI: SDT phys ", SdtPhys);

        if (!EnsureHhdmMapped(Log, SdtPhys, Sdt->length))
        {
            LogStr(Log, "ACPI: SDT mapping failed\r\n");
            return;
        }

        QC::usize EntrySize = bUseXsdt ? sizeof(QC::u64) : sizeof(QC::u32);
        if (Sdt->length < sizeof(AcpiSdtHeader) || ((Sdt->length - sizeof(AcpiSdtHeader)) % EntrySize) != 0)
        {
            LogStr(Log, "ACPI: SDT length invalid\r\n");
            return;
        }

        QC::usize EntryCount = (Sdt->length - sizeof(AcpiSdtHeader)) / EntrySize;
        LogStr(Log, "ACPI: table signatures:\r\n");

        bool bFoundTpm2 = false;
        QC::PhysAddr Tpm2Phys = 0;
        const QC::u8 *Entries = reinterpret_cast<const QC::u8 *>(Sdt) + sizeof(AcpiSdtHeader);

        for (QC::usize i = 0; i < EntryCount; ++i)
        {
            QC::PhysAddr TablePhys = 0;
            if (bUseXsdt)
                TablePhys = static_cast<QC::PhysAddr>(reinterpret_cast<const QC::u64 *>(Entries)[i]);
            else
                TablePhys = static_cast<QC::PhysAddr>(reinterpret_cast<const QC::u32 *>(Entries)[i]);

            if (TablePhys == 0)
                continue;

            if (!EnsureHhdmMapped(Log, TablePhys, sizeof(AcpiSdtHeader)))
                continue;

            auto *Hdr = reinterpret_cast<const AcpiSdtHeader *>(physToVirt(TablePhys));

            char Sig[5] = {Hdr->signature[0], Hdr->signature[1], Hdr->signature[2], Hdr->signature[3], 0};
            LogStr(Log, "  - ");
            LogStr(Log, Sig);
            LogStr(Log, "\r\n");

            if (QC::String::memcmp(Hdr->signature, "TPM2", 4) == 0)
            {
                bFoundTpm2 = true;
                Tpm2Phys = TablePhys;
            }
        }

        LogStr(Log, bFoundTpm2 ? "ACPI: TPM2 table present\r\n" : "ACPI: TPM2 table NOT present\r\n");
        if (!bFoundTpm2 || !Tpm2Phys)
            return;

        LogStr(Log, "ACPI: TPM2 details\r\n");
        if (!EnsureHhdmMapped(Log, Tpm2Phys, sizeof(AcpiSdtHeader)))
        {
            LogStr(Log, "ACPI: TPM2 header mapping failed\r\n");
            return;
        }

        auto *Tpm2Hdr = reinterpret_cast<const AcpiSdtHeader *>(physToVirt(Tpm2Phys));
        if (Tpm2Hdr->length < sizeof(AcpiTpm2TableBase))
        {
            LogStr(Log, "ACPI: TPM2 length too small\r\n");
            return;
        }

        if (!EnsureHhdmMapped(Log, Tpm2Phys, Tpm2Hdr->length))
        {
            LogStr(Log, "ACPI: TPM2 mapping failed\r\n");
            return;
        }

        auto *Tpm2 = reinterpret_cast<const AcpiTpm2TableBase *>(physToVirt(Tpm2Phys));
        LogDecU32(Log, "  platformClass: ", static_cast<QC::u32>(Tpm2->platformClass));
        LogDecU32(Log, "  startMethod: ", Tpm2->startMethod);
        if (Tpm2->startMethod == 6 || Tpm2->startMethod == 7)
            LogStr(Log, "  startMethodHint: CRB\r\n");
        LogHex64(Log, "  controlArea phys ", Tpm2->controlArea);

        if (Tpm2->controlArea)
        {
            QC::PhysAddr ControlPhys = static_cast<QC::PhysAddr>(Tpm2->controlArea);
            if (EnsureHhdmMappedMmio(Log, ControlPhys & ~static_cast<QC::PhysAddr>(0xFFF), 4096))
            {
                LogStr(Log, "  controlArea mapped\r\n");
                if (Tpm2->startMethod == 6 || Tpm2->startMethod == 7)
                {
                    LogStr(Log, "TPM2: CRB control area dump (first 0x100 bytes)\r\n");
                    volatile const QC::u8 *Base = reinterpret_cast<volatile const QC::u8 *>(
                        physToVirt(ControlPhys & ~static_cast<QC::PhysAddr>(0xFFF)));
                    QC::usize StartOff = static_cast<QC::usize>(ControlPhys & 0xFFF);

                    for (QC::usize Off = 0; Off < 0x100; Off += 16)
                    {
                        LogStr(Log, "  +0x");
                        QC::u32 O = static_cast<QC::u32>(Off);
                        char Obuf[4];
                        for (int i = 2; i >= 0; --i)
                        {
                            int nibble = static_cast<int>((O >> (i * 4)) & 0xF);
                            Obuf[2 - i] = nibble < 10 ? static_cast<char>('0' + nibble) : static_cast<char>('a' + nibble - 10);
                        }
                        Obuf[3] = 0;
                        LogStr(Log, Obuf);
                        LogStr(Log, ": ");

                        for (QC::usize w = 0; w < 4; ++w)
                        {
                            const QC::u32 *P = reinterpret_cast<const QC::u32 *>(
                                reinterpret_cast<QC::VirtAddr>(Base + StartOff + Off + w * 4));
                            QC::u32 V = *P;
                            LogHex32Fixed(Log, V);
                            if (w != 3)
                                LogStr(Log, " ");
                        }
                        LogStr(Log, "\r\n");
                    }

                    if (Tpm2CrbStartup)
                        Tpm2CrbStartup(Tpm2->startMethod, ControlPhys, Log);
                }
            }
            else
            {
                LogStr(Log, "  controlArea map failed\r\n");
            }
        }

        constexpr QC::usize kOptionalOffset = sizeof(AcpiTpm2TableBase);
        if (Tpm2Hdr->length >= kOptionalOffset + sizeof(QC::u32) + sizeof(QC::u64))
        {
            const QC::u8 *Base = reinterpret_cast<const QC::u8 *>(Tpm2);
            QC::u32 Laml = *reinterpret_cast<const QC::u32 *>(Base + kOptionalOffset);
            QC::u64 Lasa = *reinterpret_cast<const QC::u64 *>(Base + kOptionalOffset + sizeof(QC::u32));
            LogDecU32(Log, "  laml: ", Laml);
            LogHex64(Log, "  lasa phys ", Lasa);
        }
        else
        {
            LogStr(Log, "  eventLog: none\r\n");
        }
    }
}
