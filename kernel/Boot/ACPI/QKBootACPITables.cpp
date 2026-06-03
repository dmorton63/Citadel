#include "QKBootACPITables.h"

#include "QCBuiltins.h"
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

        struct AcpiGenericAddress
        {
            QC::u8 spaceId;
            QC::u8 bitWidth;
            QC::u8 bitOffset;
            QC::u8 accessSize;
            QC::u64 address;
        };

        struct AcpiFadtTable
        {
            AcpiSdtHeader header;
            QC::u32 firmwareCtrl;
            QC::u32 dsdt;
            QC::u8 reserved0;
            QC::u8 preferredPmProfile;
            QC::u16 sciInt;
            QC::u32 smiCmd;
            QC::u8 acpiEnable;
            QC::u8 acpiDisable;
            QC::u8 s4BiosReq;
            QC::u8 pstateCnt;
            QC::u32 pm1aEventBlock;
            QC::u32 pm1bEventBlock;
            QC::u32 pm1aControlBlock;
            QC::u32 pm1bControlBlock;
            QC::u32 pm2ControlBlock;
            QC::u32 pmTimerBlock;
            QC::u32 gpe0Block;
            QC::u32 gpe1Block;
            QC::u8 pm1EventLength;
            QC::u8 pm1ControlLength;
            QC::u8 pm2ControlLength;
            QC::u8 pmTimerLength;
            QC::u8 gpe0BlockLength;
            QC::u8 gpe1BlockLength;
            QC::u8 gpe1Base;
            QC::u8 cstCnt;
            QC::u16 pLvl2Lat;
            QC::u16 pLvl3Lat;
            QC::u16 flushSize;
            QC::u16 flushStride;
            QC::u8 dutyOffset;
            QC::u8 dutyWidth;
            QC::u8 dayAlarm;
            QC::u8 monthAlarm;
            QC::u8 century;
            QC::u16 iapcBootArch;
            QC::u8 reserved1;
            QC::u32 flags;
            AcpiGenericAddress resetReg;
            QC::u8 resetValue;
            QC::u16 armBootArch;
            QC::u8 minorVersion;
            QC::u64 xFirmwareCtrl;
            QC::u64 xDsdt;
            AcpiGenericAddress xPm1aEventBlock;
            AcpiGenericAddress xPm1bEventBlock;
            AcpiGenericAddress xPm1aControlBlock;
            AcpiGenericAddress xPm1bControlBlock;
            AcpiGenericAddress xPm2ControlBlock;
            AcpiGenericAddress xPmTimerBlock;
            AcpiGenericAddress xGpe0Block;
            AcpiGenericAddress xGpe1Block;
        };
#pragma pack(pop)

        struct ShutdownRegister
        {
            AcpiGenericAddress gas{};
            QC::u8 length = 0;
        };

        struct ShutdownProfile
        {
            bool valid = false;
            bool hasSleepTypes = false;
            QC::u32 smiCmdPort = 0;
            QC::u8 acpiEnable = 0;
            ShutdownRegister pm1aControl{};
            ShutdownRegister pm1bControl{};
            QC::u16 sleepTypeA = 0;
            QC::u16 sleepTypeB = 0;
        };

        static ShutdownProfile g_ShutdownProfile;

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

        static void LogHex16Fixed(FLogFn Log, QC::u16 Value)
        {
            char HexBuf[5];
            for (int i = 3; i >= 0; --i)
            {
                int nibble = static_cast<int>((Value >> (i * 4)) & 0xF);
                HexBuf[3 - i] = nibble < 10 ? static_cast<char>('0' + nibble) : static_cast<char>('a' + nibble - 10);
            }
            HexBuf[4] = 0;
            LogStr(Log, HexBuf);
        }

        static void LogHex64Fixed(FLogFn Log, QC::u64 Value)
        {
            char HexBuf[17];
            for (int i = 15; i >= 0; --i)
            {
                int nibble = static_cast<int>((Value >> (i * 4)) & 0xF);
                HexBuf[15 - i] = nibble < 10 ? static_cast<char>('0' + nibble) : static_cast<char>('a' + nibble - 10);
            }
            HexBuf[16] = 0;
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

        static bool IsSupportedAddressSpace(QC::u8 SpaceId)
        {
            return SpaceId == 0 || SpaceId == 1;
        }

        static QC::u8 AccessSizeFromLength(QC::u8 Length, QC::u8 BitWidth)
        {
            if (Length >= 4 || BitWidth > 16)
                return 4;
            if (Length >= 2 || BitWidth > 8)
                return 2;
            return 1;
        }

        static bool IsRegisterUsable(const ShutdownRegister &Reg)
        {
            return Reg.gas.address != 0 && Reg.length != 0 && IsSupportedAddressSpace(Reg.gas.spaceId);
        }

        static const char *AddressSpaceName(QC::u8 SpaceId)
        {
            switch (SpaceId)
            {
            case 0:
                return "system-memory";
            case 1:
                return "system-io";
            default:
                return "unsupported";
            }
        }

        static ShutdownRegister MakeLegacyIoRegister(QC::u32 Address, QC::u8 Length)
        {
            ShutdownRegister Reg{};
            if (Address == 0 || Length == 0)
                return Reg;

            Reg.gas.spaceId = 1;
            Reg.gas.bitWidth = static_cast<QC::u8>(Length * 8u);
            Reg.gas.accessSize = Length >= 4 ? 3 : (Length >= 2 ? 2 : 1);
            Reg.gas.address = Address;
            Reg.length = Length;
            return Reg;
        }

        static ShutdownRegister SelectRegister(const AcpiGenericAddress &Extended, QC::u32 LegacyAddress, QC::u8 Length)
        {
            if (Extended.address != 0 && IsSupportedAddressSpace(Extended.spaceId))
            {
                ShutdownRegister Reg{};
                Reg.gas = Extended;
                Reg.length = Length != 0 ? Length : static_cast<QC::u8>((Extended.bitWidth + 7u) / 8u);
                return Reg;
            }
            return MakeLegacyIoRegister(LegacyAddress, Length);
        }

        static bool DecodePkgLength(const QC::u8 *Table, QC::usize Length, QC::usize &Offset)
        {
            if (Offset >= Length)
                return false;

            const QC::u8 Lead = Table[Offset++];
            const QC::u8 FollowCount = static_cast<QC::u8>((Lead >> 6) & 0x3u);
            if (Offset + FollowCount > Length)
                return false;

            Offset += FollowCount;
            return true;
        }

        static bool DecodeAmlInteger(const QC::u8 *Table, QC::usize Length, QC::usize &Offset, QC::u64 &Value)
        {
            if (Offset >= Length)
                return false;

            const QC::u8 Op = Table[Offset++];
            switch (Op)
            {
            case 0x00:
                Value = 0;
                return true;
            case 0x01:
                Value = 1;
                return true;
            case 0x0A:
                if (Offset + 1 > Length)
                    return false;
                Value = Table[Offset++];
                return true;
            case 0x0B:
                if (Offset + 2 > Length)
                    return false;
                Value = static_cast<QC::u64>(Table[Offset]) |
                        (static_cast<QC::u64>(Table[Offset + 1]) << 8);
                Offset += 2;
                return true;
            case 0x0C:
                if (Offset + 4 > Length)
                    return false;
                Value = static_cast<QC::u64>(Table[Offset]) |
                        (static_cast<QC::u64>(Table[Offset + 1]) << 8) |
                        (static_cast<QC::u64>(Table[Offset + 2]) << 16) |
                        (static_cast<QC::u64>(Table[Offset + 3]) << 24);
                Offset += 4;
                return true;
            case 0x0E:
                if (Offset + 8 > Length)
                    return false;
                Value = static_cast<QC::u64>(Table[Offset]) |
                        (static_cast<QC::u64>(Table[Offset + 1]) << 8) |
                        (static_cast<QC::u64>(Table[Offset + 2]) << 16) |
                        (static_cast<QC::u64>(Table[Offset + 3]) << 24) |
                        (static_cast<QC::u64>(Table[Offset + 4]) << 32) |
                        (static_cast<QC::u64>(Table[Offset + 5]) << 40) |
                        (static_cast<QC::u64>(Table[Offset + 6]) << 48) |
                        (static_cast<QC::u64>(Table[Offset + 7]) << 56);
                Offset += 8;
                return true;
            default:
                return false;
            }
        }

        static bool ExtractSleepTypesFromAml(const QC::u8 *Table, QC::usize Length, QC::u16 &SleepTypeA, QC::u16 &SleepTypeB)
        {
            if (!Table || Length < 8)
                return false;

            for (QC::usize i = 1; i + 5 < Length; ++i)
            {
                if (QC::String::memcmp(Table + i, "_S5_", 4) != 0)
                    continue;

                bool NamedObject = false;
                if (Table[i - 1] == 0x08)
                    NamedObject = true;
                else if (i >= 2 && Table[i - 2] == 0x08 && Table[i - 1] == '\\')
                    NamedObject = true;

                if (!NamedObject)
                    continue;

                QC::usize Offset = i + 4;
                if (Offset >= Length || Table[Offset++] != 0x12)
                    continue;
                if (!DecodePkgLength(Table, Length, Offset))
                    continue;
                if (Offset >= Length)
                    continue;

                ++Offset;

                QC::u64 ValueA = 0;
                QC::u64 ValueB = 0;
                if (!DecodeAmlInteger(Table, Length, Offset, ValueA))
                    continue;
                if (!DecodeAmlInteger(Table, Length, Offset, ValueB))
                    ValueB = ValueA;

                SleepTypeA = static_cast<QC::u16>((ValueA & 0x7u) << 10);
                SleepTypeB = static_cast<QC::u16>((ValueB & 0x7u) << 10);
                return true;
            }

            return false;
        }

        static bool ExtractSleepTypesFromTable(FLogFn Log, QC::PhysAddr TablePhys, const char *Label, QC::u16 &SleepTypeA, QC::u16 &SleepTypeB)
        {
            if (TablePhys == 0)
                return false;
            if (!EnsureHhdmMapped(Log, TablePhys, sizeof(AcpiSdtHeader)))
                return false;

            auto *Hdr = reinterpret_cast<const AcpiSdtHeader *>(physToVirt(TablePhys));
            if (Hdr->length < sizeof(AcpiSdtHeader))
                return false;
            if (!EnsureHhdmMapped(Log, TablePhys, Hdr->length))
                return false;

            const QC::u8 *TableBytes = reinterpret_cast<const QC::u8 *>(physToVirt(TablePhys));
            const bool Found = ExtractSleepTypesFromAml(TableBytes + sizeof(AcpiSdtHeader), Hdr->length - sizeof(AcpiSdtHeader), SleepTypeA, SleepTypeB);
            if (Found)
            {
                LogStr(Log, "ACPI: found _S5_ in ");
                LogStr(Log, Label);
                LogStr(Log, "\r\n");
            }
            return Found;
        }

        static bool ReadRegisterValue(FLogFn Log, const ShutdownRegister &Reg, QC::u32 &Value)
        {
            if (!IsRegisterUsable(Reg))
                return false;

            const QC::u8 Width = AccessSizeFromLength(Reg.length, Reg.gas.bitWidth);
            if (Reg.gas.spaceId == 1)
            {
                if (Reg.gas.address > 0xFFFFu)
                    return false;

                const QC::u16 Port = static_cast<QC::u16>(Reg.gas.address & 0xFFFFu);
                if (Width >= 4)
                    Value = QC::inl(Port);
                else if (Width >= 2)
                    Value = QC::inw(Port);
                else
                    Value = QC::inb(Port);
                return true;
            }

            const QC::PhysAddr Phys = static_cast<QC::PhysAddr>(Reg.gas.address);
            if (!EnsureHhdmMappedMmio(Log, Phys, Width))
                return false;

            const QC::VirtAddr Virt = physToVirt(Phys);
            if (Width >= 4)
                Value = QC::mmio_read32(Virt);
            else if (Width >= 2)
                Value = QC::mmio_read16(Virt);
            else
                Value = QC::mmio_read8(Virt);
            return true;
        }

        static bool WriteRegisterValue(FLogFn Log, const ShutdownRegister &Reg, QC::u32 Value)
        {
            if (!IsRegisterUsable(Reg))
                return false;

            const QC::u8 Width = AccessSizeFromLength(Reg.length, Reg.gas.bitWidth);
            if (Reg.gas.spaceId == 1)
            {
                if (Reg.gas.address > 0xFFFFu)
                    return false;

                const QC::u16 Port = static_cast<QC::u16>(Reg.gas.address & 0xFFFFu);
                if (Width >= 4)
                    QC::outl(Port, Value);
                else if (Width >= 2)
                    QC::outw(Port, static_cast<QC::u16>(Value));
                else
                    QC::outb(Port, static_cast<QC::u8>(Value));
                return true;
            }

            const QC::PhysAddr Phys = static_cast<QC::PhysAddr>(Reg.gas.address);
            if (!EnsureHhdmMappedMmio(Log, Phys, Width))
                return false;

            const QC::VirtAddr Virt = physToVirt(Phys);
            if (Width >= 4)
                QC::mmio_write32(Virt, Value);
            else if (Width >= 2)
                QC::mmio_write16(Virt, static_cast<QC::u16>(Value));
            else
                QC::mmio_write8(Virt, static_cast<QC::u8>(Value));
            return true;
        }

        static bool EnsureSciEnabled(FLogFn Log)
        {
            if (!IsRegisterUsable(g_ShutdownProfile.pm1aControl))
                return false;

            QC::u32 Control = 0;
            if (!ReadRegisterValue(Log, g_ShutdownProfile.pm1aControl, Control))
                return false;
            if ((Control & 0x1u) != 0)
                return true;

            if (g_ShutdownProfile.smiCmdPort == 0 || g_ShutdownProfile.acpiEnable == 0 || g_ShutdownProfile.smiCmdPort > 0xFFFFu)
            {
                LogStr(Log, "ACPI: cannot enable ACPI mode (missing SMI_CMD/ACPI_ENABLE)\r\n");
                return false;
            }

            QC::outb(static_cast<QC::u16>(g_ShutdownProfile.smiCmdPort & 0xFFFFu), g_ShutdownProfile.acpiEnable);
            for (QC::usize Spin = 0; Spin < 200000; ++Spin)
            {
                if (!ReadRegisterValue(Log, g_ShutdownProfile.pm1aControl, Control))
                    return false;
                if ((Control & 0x1u) != 0)
                    return true;
                QC::pause();
            }

            LogStr(Log, "ACPI: SCI_EN did not assert after ACPI_ENABLE\r\n");
            return false;
        }

        static bool BuildShutdownProfile(FLogFn Log, QC::PhysAddr FadtPhys, const QC::PhysAddr *SsdtTables, QC::usize SsdtCount)
        {
            if (FadtPhys == 0)
                return false;
            if (!EnsureHhdmMapped(Log, FadtPhys, sizeof(AcpiSdtHeader)))
                return false;

            auto *Hdr = reinterpret_cast<const AcpiSdtHeader *>(physToVirt(FadtPhys));
            if (Hdr->length < sizeof(AcpiFadtTable))
            {
                LogStr(Log, "ACPI: FADT too small for shutdown profile\r\n");
                return false;
            }
            if (!EnsureHhdmMapped(Log, FadtPhys, Hdr->length))
                return false;

            auto *Fadt = reinterpret_cast<const AcpiFadtTable *>(physToVirt(FadtPhys));
            ShutdownProfile Profile{};
            Profile.smiCmdPort = Fadt->smiCmd;
            Profile.acpiEnable = Fadt->acpiEnable;
            Profile.pm1aControl = SelectRegister(Fadt->xPm1aControlBlock, Fadt->pm1aControlBlock, Fadt->pm1ControlLength);
            Profile.pm1bControl = SelectRegister(Fadt->xPm1bControlBlock, Fadt->pm1bControlBlock, Fadt->pm1ControlLength);

            const QC::PhysAddr DsdtPhys = Fadt->xDsdt != 0 ? static_cast<QC::PhysAddr>(Fadt->xDsdt)
                                                            : static_cast<QC::PhysAddr>(Fadt->dsdt);

            if (IsRegisterUsable(Profile.pm1aControl))
            {
                Profile.valid = true;
            }

            if (DsdtPhys != 0)
            {
                Profile.hasSleepTypes = ExtractSleepTypesFromTable(Log, DsdtPhys, "DSDT", Profile.sleepTypeA, Profile.sleepTypeB);
            }
            for (QC::usize i = 0; !Profile.hasSleepTypes && i < SsdtCount; ++i)
            {
                Profile.hasSleepTypes = ExtractSleepTypesFromTable(Log, SsdtTables[i], "SSDT", Profile.sleepTypeA, Profile.sleepTypeB);
            }

            if (!Profile.valid)
            {
                LogStr(Log, "ACPI: no usable PM1 control register for shutdown\r\n");
                return false;
            }
            if (!Profile.hasSleepTypes)
            {
                LogStr(Log, "ACPI: _S5_ not found; ACPI shutdown unavailable\r\n");
                return false;
            }

            g_ShutdownProfile = Profile;
            LogStr(Log, "ACPI: shutdown PM1A ");
            LogStr(Log, AddressSpaceName(g_ShutdownProfile.pm1aControl.gas.spaceId));
            LogStr(Log, " 0x");
            LogHex64Fixed(Log, g_ShutdownProfile.pm1aControl.gas.address);
            LogStr(Log, " S5A=0x");
            LogHex16Fixed(Log, g_ShutdownProfile.sleepTypeA);
            LogStr(Log, "\r\n");
            return true;
        }
    }

    void EnumerateTables(QC::PhysAddr RsdpPhys, FLogFn Log, FTpm2CrbStartupFn Tpm2CrbStartup)
    {
        g_ShutdownProfile = ShutdownProfile{};

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
        QC::PhysAddr FadtPhys = 0;
        QC::PhysAddr SsdtTables[32]{};
        QC::usize SsdtCount = 0;
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
            else if (QC::String::memcmp(Hdr->signature, "FACP", 4) == 0)
            {
                FadtPhys = TablePhys;
            }
            else if (QC::String::memcmp(Hdr->signature, "SSDT", 4) == 0)
            {
                if (SsdtCount < (sizeof(SsdtTables) / sizeof(SsdtTables[0])))
                    SsdtTables[SsdtCount++] = TablePhys;
            }
        }

        if (FadtPhys != 0)
            (void)BuildShutdownProfile(Log, FadtPhys, SsdtTables, SsdtCount);
        else
            LogStr(Log, "ACPI: FADT not present; ACPI shutdown unavailable\r\n");

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

    bool TryAcpiShutdown(FLogFn Log)
    {
        constexpr QC::u32 kSleepMask = 0x1C00u;
        constexpr QC::u32 kSleepEnable = 0x2000u;

        if (!g_ShutdownProfile.valid || !g_ShutdownProfile.hasSleepTypes)
        {
            LogStr(Log, "ACPI: shutdown profile unavailable\r\n");
            return false;
        }

        QC::u32 Pm1aValue = 0;
        if (!ReadRegisterValue(Log, g_ShutdownProfile.pm1aControl, Pm1aValue))
        {
            LogStr(Log, "ACPI: failed to read PM1A control register\r\n");
            return false;
        }

        QC::u32 Pm1bValue = 0;
        const bool HasPm1b = IsRegisterUsable(g_ShutdownProfile.pm1bControl);
        if (HasPm1b && !ReadRegisterValue(Log, g_ShutdownProfile.pm1bControl, Pm1bValue))
        {
            LogStr(Log, "ACPI: failed to read PM1B control register\r\n");
            return false;
        }

        if ((Pm1aValue & 0x1u) == 0 && !EnsureSciEnabled(Log))
        {
            LogStr(Log, "ACPI: failed to enable ACPI mode\r\n");
            return false;
        }

        // Firmware may set SCI_EN or other PM1 control bits as part of ACPI_ENABLE.
        // Re-read after the mode transition so the shutdown write preserves the live state.
        if (!ReadRegisterValue(Log, g_ShutdownProfile.pm1aControl, Pm1aValue))
        {
            LogStr(Log, "ACPI: failed to re-read PM1A control register\r\n");
            return false;
        }
        if (HasPm1b && !ReadRegisterValue(Log, g_ShutdownProfile.pm1bControl, Pm1bValue))
        {
            LogStr(Log, "ACPI: failed to re-read PM1B control register\r\n");
            return false;
        }

        const QC::u32 SleepPrepA = (Pm1aValue & ~kSleepMask) | g_ShutdownProfile.sleepTypeA;
        const QC::u32 SleepPrepB = (Pm1bValue & ~kSleepMask) | g_ShutdownProfile.sleepTypeB;

        if (!WriteRegisterValue(Log, g_ShutdownProfile.pm1aControl, SleepPrepA))
        {
            LogStr(Log, "ACPI: failed to program PM1A sleep type\r\n");
            return false;
        }
        if (HasPm1b && !WriteRegisterValue(Log, g_ShutdownProfile.pm1bControl, SleepPrepB))
        {
            LogStr(Log, "ACPI: failed to program PM1B sleep type\r\n");
            return false;
        }

        if (HasPm1b)
            (void)WriteRegisterValue(Log, g_ShutdownProfile.pm1bControl, SleepPrepB | kSleepEnable);
        if (!WriteRegisterValue(Log, g_ShutdownProfile.pm1aControl, SleepPrepA | kSleepEnable))
        {
            LogStr(Log, "ACPI: failed to assert SLP_EN\r\n");
            return false;
        }

        return true;
    }
}
