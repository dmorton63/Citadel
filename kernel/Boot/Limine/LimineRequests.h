#pragma once

#include "QCTypes.h"

#ifndef LIMINE_API_REVISION
#define LIMINE_API_REVISION 2
#endif
#include "limine.h"

namespace QK::Boot::Limine
{
    // The Limine "kernel address" (rev < 2) / "executable address" (rev >= 2)
    // response has the same layout across revisions; only the type name differs.
    struct FKernelAddressResponse
    {
        QC::u64 revision;
        QC::u64 physical_base;
        QC::u64 virtual_base;
    };

    struct FKernelMapping
    {
        QC::u64 HhdmOffset = 0;
        QC::u64 KernelPhysBase = 0;
        QC::u64 KernelVirtBase = 0;
    };

    const limine_hhdm_response *GetHhdmResponse(QC::u64 HhdmRequest[]);
    const FKernelAddressResponse *GetKernelAddressResponse(QC::u64 KernelAddressRequest[]);
    const limine_memmap_response *GetMemmapResponse(QC::u64 MemmapRequest[]);
    const limine_firmware_type_response *GetFirmwareTypeResponse(QC::u64 FirmwareTypeRequest[]);
    const limine_rsdp_response *GetRsdpResponse(QC::u64 RsdpRequest[]);

    // Returns the sum of "available after reclaim" ranges in bytes.
    // Counts: USABLE + BOOTLOADER_RECLAIMABLE + ACPI_RECLAIMABLE.
    bool GetAvailableMemoryBytes(QC::u64 MemmapRequest[], QC::u64 &OutBytes);

    bool ReadKernelMapping(QC::u64 HhdmRequest[], QC::u64 KernelAddressRequest[], FKernelMapping &OutMapping);
}
