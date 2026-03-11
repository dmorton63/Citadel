#pragma once

// xHCI hardware registers and bit definitions
// Namespace: QKDrv::XHCI

#include "QCTypes.h"

namespace QKDrv
{
    namespace XHCI
    {
        struct CapRegs
        {
            QC::u8 capLength;
            QC::u8 reserved;
            QC::u16 hciVersion;
            QC::u32 hcsParams1;
            QC::u32 hcsParams2;
            QC::u32 hcsParams3;
            QC::u32 hccParams1;
            QC::u32 dbOffset;
            QC::u32 rtsOffset;
            QC::u32 hccParams2;
        } __attribute__((packed));

        struct OpRegs
        {
            QC::u32 usbcmd;
            QC::u32 usbsts;
            QC::u32 pageSize;
            QC::u32 reserved1[2];
            QC::u32 dnctrl;
            QC::u64 crcr;
            QC::u32 reserved2[4];
            QC::u64 dcbaap;
            QC::u32 config;
        } __attribute__((packed));

        struct PortRegs
        {
            QC::u32 portsc;
            QC::u32 portpmsc;
            QC::u32 portli;
            QC::u32 porthlpmc;
        } __attribute__((packed));

        struct InterrupterRegs
        {
            QC::u32 iman;
            QC::u32 imod;
            QC::u32 erstsz;
            QC::u32 reserved;
            QC::u64 erstba;
            QC::u64 erdp;
        } __attribute__((packed));

        struct ERSTEntry
        {
            QC::u64 ringSegmentBase;
            QC::u16 ringSegmentSize;
            QC::u16 reserved1;
            QC::u32 reserved2;
        } __attribute__((packed));

        // Command register bits
        constexpr QC::u32 CMD_RUN = 1 << 0;
        constexpr QC::u32 CMD_HCRST = 1 << 1;
        constexpr QC::u32 CMD_INTE = 1 << 2;
        constexpr QC::u32 CMD_HSEE = 1 << 3;

        // Status register bits
        constexpr QC::u32 STS_HCH = 1 << 0;
        constexpr QC::u32 STS_HSE = 1 << 2;
        constexpr QC::u32 STS_EINT = 1 << 3;
        constexpr QC::u32 STS_PCD = 1 << 4;
        constexpr QC::u32 STS_CNR = 1 << 11;

        // Port status bits
        constexpr QC::u32 PORTSC_CCS = 1 << 0;
        constexpr QC::u32 PORTSC_PED = 1 << 1;
        constexpr QC::u32 PORTSC_OCA = 1 << 3;
        constexpr QC::u32 PORTSC_PR = 1 << 4;
        constexpr QC::u32 PORTSC_PP = 1 << 9;
        constexpr QC::u32 PORTSC_SPEED_MASK = 0xF << 10;
        constexpr QC::u32 PORTSC_CSC = 1 << 17;
        constexpr QC::u32 PORTSC_WRC = 1 << 19;
        constexpr QC::u32 PORTSC_PRC = 1 << 21;
    }
} // namespace QKDrv
