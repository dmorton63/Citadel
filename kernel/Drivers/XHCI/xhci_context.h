#pragma once

// xHCI context structures
// Namespace: QKDrv::XHCI

#include "QCTypes.h"

namespace QKDrv
{
    namespace XHCI
    {
        struct SlotContext
        {
            QC::u32 routeString : 20;
            QC::u32 speed : 4;
            QC::u32 reserved1 : 1;
            QC::u32 mtt : 1;
            QC::u32 hub : 1;
            QC::u32 contextEntries : 5;
            QC::u16 maxExitLatency;
            QC::u8 rootHubPort;
            QC::u8 portCount;
            QC::u32 parentHubSlot : 8;
            QC::u32 parentPort : 8;
            QC::u32 ttThinkTime : 2;
            QC::u32 reserved2 : 4;
            QC::u32 interrupterTarget : 10;
            QC::u32 usbDeviceAddr : 8;
            QC::u32 reserved3 : 19;
            QC::u32 slotState : 5;
            QC::u32 reserved4[4];
        } __attribute__((packed));

        struct EndpointContext
        {
            QC::u32 epState : 3;
            QC::u32 reserved1 : 5;
            QC::u32 mult : 2;
            QC::u32 maxPStreams : 5;
            QC::u32 lsa : 1;
            QC::u32 interval : 8;
            QC::u32 maxESITPayloadHi : 8;
            QC::u32 reserved2 : 1;
            QC::u32 errorCount : 2;
            QC::u32 epType : 3;
            QC::u32 reserved3 : 1;
            QC::u32 hid : 1;
            QC::u32 maxBurstSize : 8;
            QC::u32 maxPacketSize : 16;
            QC::u64 trDequeuePtr;
            QC::u32 avgTRBLength : 16;
            QC::u32 maxESITPayloadLo : 16;
            QC::u32 reserved4[3];
        } __attribute__((packed));

        struct InputControlContext
        {
            QC::u32 dropFlags;
            QC::u32 addFlags;
            QC::u32 reserved[5];
            QC::u8 configValue;
            QC::u8 interfaceNum;
            QC::u8 alternateSetting;
            QC::u8 reserved2;
        } __attribute__((packed));
    }
} // namespace QKDrv
