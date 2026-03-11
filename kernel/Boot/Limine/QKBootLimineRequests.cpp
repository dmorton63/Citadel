#include "QKBootLimineRequests.h"

namespace
{
    template <typename T>
    const T *GetResponse(QC::u64 Request[])
    {
        if (!Request)
        {
            return nullptr;
        }

        return reinterpret_cast<const T *>(Request[5]);
    }
}

namespace QK::Boot::Limine
{
    const limine_hhdm_response *GetHhdmResponse(QC::u64 HhdmRequest[])
    {
        return GetResponse<limine_hhdm_response>(HhdmRequest);
    }

    const FKernelAddressResponse *GetKernelAddressResponse(QC::u64 KernelAddressRequest[])
    {
        return GetResponse<FKernelAddressResponse>(KernelAddressRequest);
    }

    const limine_memmap_response *GetMemmapResponse(QC::u64 MemmapRequest[])
    {
        return GetResponse<limine_memmap_response>(MemmapRequest);
    }

    const limine_firmware_type_response *GetFirmwareTypeResponse(QC::u64 FirmwareTypeRequest[])
    {
        return GetResponse<limine_firmware_type_response>(FirmwareTypeRequest);
    }

    const limine_rsdp_response *GetRsdpResponse(QC::u64 RsdpRequest[])
    {
        return GetResponse<limine_rsdp_response>(RsdpRequest);
    }

    bool GetAvailableMemoryBytes(QC::u64 MemmapRequest[], QC::u64 &OutBytes)
    {
        OutBytes = 0;

        const limine_memmap_response *Resp = GetMemmapResponse(MemmapRequest);
        if (!Resp || Resp->entry_count == 0 || !Resp->entries)
        {
            return false;
        }

        QC::u64 sum = 0;
        for (QC::u64 i = 0; i < Resp->entry_count; ++i)
        {
            const limine_memmap_entry *Entry = Resp->entries[i];
            if (!Entry)
                continue;

            switch (Entry->type)
            {
            case LIMINE_MEMMAP_USABLE:
            case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
            case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
                sum += Entry->length;
                break;
            default:
                break;
            }
        }

        OutBytes = sum;
        return true;
    }

    bool ReadKernelMapping(QC::u64 HhdmRequest[], QC::u64 KernelAddressRequest[], FKernelMapping &OutMapping)
    {
        const limine_hhdm_response *Hhdm = GetHhdmResponse(HhdmRequest);
        if (!Hhdm)
        {
            return false;
        }

        const FKernelAddressResponse *KernelAddr = GetKernelAddressResponse(KernelAddressRequest);
        if (!KernelAddr)
        {
            return false;
        }

        OutMapping.HhdmOffset = Hhdm->offset;
        OutMapping.KernelPhysBase = KernelAddr->physical_base;
        OutMapping.KernelVirtBase = KernelAddr->virtual_base;
        return true;
    }
}
