#pragma once

// QKDrvIDE - Minimal legacy IDE/ATA PIO probing for block devices
// Namespace: QKDrv::IDE

#include "QCTypes.h"

namespace QKDrv
{
    namespace IDE
    {
        // Enable/disable shared-volume probe (default disabled for boot safety).
        void setSharedProbeEnabled(bool enabled);

        // Probe legacy primary/secondary IDE channels for an ATA disk that looks like FAT32
        // and register it as QFS_SHARED mounted at /shared.
        void probeAndRegisterSharedVolume();

        // Probe legacy IDE for a persistent system volume and mount it at /system.
        // Intended for QEMU disk images (e.g., qcow2/raw) containing a FAT16/32 partition.
        void probeAndRegisterSystemVolume();

        // Reset internal system probe state so probeAndRegisterSystemVolume() can run again.
        // This is useful after formatting a disk at runtime.
        void resetSystemProbe();

        // Partition and format the first detected system disk candidate as a FAT32 volume.
        // Safety: refuses to format if the disk already appears to contain an MBR partition table
        // or an existing FAT boot sector.
        QC::Status formatSystemVolumeFAT32();
    }
}
