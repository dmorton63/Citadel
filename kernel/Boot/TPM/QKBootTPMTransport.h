#pragma once

#include "QKBootTPMInternal.h"
#include "QKBootTPMProbePolicy.h"

namespace QK::Boot::Tpm::Transport
{
    using EType = QK::Boot::Tpm::TransportKind;

    struct FInfo
    {
        EType Type = EType::None;
        QC::u32 StartMethod = 0;
        QC::PhysAddr ControlAreaPhys = 0;
        QC::PhysAddr LocalityBasePhys = 0;
        QC::u64 InterfaceId = 0;
        bool bInterfaceIdValid = false;
        bool bCrbLocalityRegs = false;
        // Start methods 2/8 normally require an ACPI _DSM start call, which this
        // kernel cannot evaluate; commands are attempted via the START register.
        bool bAcpiStartRequested = false;
        QC::u16 VendorId = 0;
        QC::u16 DeviceId = 0;
        QC::u8 RevisionId = 0;
        QC::u64 CommandsSubmitted = 0;
        QC::u64 TransportFailures = 0;
    };

    // Detects CRB vs TIS/FIFO from the ACPI TPM2 start method and the
    // TPM_INTERFACE_ID register, then maps the register window.
    bool Init(QC::u32 StartMethod, QC::PhysAddr ControlAreaPhys, FLogFn Log);

    bool IsInitialized();
    const FInfo &Info();
    const char *TypeName(EType Type);
    const char *StartMethodName(QC::u32 StartMethod);

    constexpr QC::usize kMaxResponseSize = 4096;

    // Submits one command and copies the response into Rsp. Returns the TPM
    // response code, or kTpmTransportFailure. VerboseLog may be null.
    // Callers must hold the TPM operation lock.
    QC::u32 Submit(const QC::u8 *Cmd, QC::usize CmdLen, QC::u8 *Rsp, QC::usize RspCap, QC::u32 &RspLen, FLogFn VerboseLog);
}
