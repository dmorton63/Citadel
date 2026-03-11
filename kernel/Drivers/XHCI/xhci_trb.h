#pragma once

// xHCI TRB formats and enums
// Namespace: QKDrv::XHCI

#include "QCTypes.h"

namespace QKDrv
{
    namespace XHCI
    {
        enum class TRBType : QC::u8
        {
            Normal = 1,
            SetupStage = 2,
            DataStage = 3,
            StatusStage = 4,
            Link = 6,
            NoOp = 8,
            EnableSlot = 9,
            DisableSlot = 10,
            AddressDevice = 11,
            ConfigureEndpoint = 12,
            EvaluateContext = 13,
            ResetEndpoint = 14,
            StopEndpoint = 15,
            SetTRDequeue = 16,
            ResetDevice = 17,
            TransferEvent = 32,
            CommandComplete = 33,
            PortStatusChange = 34
        };

        enum class CompletionCode : QC::u8
        {
            Invalid = 0,
            Success = 1,
            DataBuffer = 2,
            BabbleDetected = 3,
            USBTransaction = 4,
            TRBError = 5,
            Stall = 6,
            ResourceError = 7,
            NoSlotsAvailable = 9,
            ShortPacket = 13
        };

        enum class EndpointType : QC::u8
        {
            Invalid = 0,
            IsochOut = 1,
            BulkOut = 2,
            InterruptOut = 3,
            Control = 4,
            IsochIn = 5,
            BulkIn = 6,
            InterruptIn = 7
        };

        struct TRB
        {
            QC::u64 parameter;
            QC::u32 status;
            QC::u32 control;
        } __attribute__((packed));

        constexpr QC::u32 TRB_CYCLE = 1 << 0;
        constexpr QC::u32 TRB_TC = 1 << 1;
        constexpr QC::u32 TRB_IOC = 1 << 5;
        constexpr QC::u32 TRB_IDT = 1 << 6;
        constexpr QC::u32 TRB_BSR = 1 << 9;
        constexpr QC::u32 TRB_DIR_IN = 1 << 16;

        inline QC::u32 makeTRBControl(TRBType type, QC::u32 flags)
        {
            return (static_cast<QC::u32>(type) << 10) | flags;
        }

        inline TRBType getTRBType(const TRB &trb)
        {
            return static_cast<TRBType>((trb.control >> 10) & 0x3F);
        }

        inline CompletionCode getCompletionCode(const TRB &trb)
        {
            return static_cast<CompletionCode>((trb.status >> 24) & 0xFF);
        }

        inline QC::u8 getSlotId(const TRB &trb)
        {
            return (trb.control >> 24) & 0xFF;
        }
    }
} // namespace QKDrv
