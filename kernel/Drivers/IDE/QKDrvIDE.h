#pragma once

// QKDrvIDE - Minimal legacy IDE/ATA PIO probing for block devices
// Namespace: QKDrv::IDE

#include "QCTypes.h"

namespace QKDrv
{
    namespace IDE
    {
        struct DetectedDeviceInfo
        {
            QC::u16 base = 0;
            QC::u16 ctrl = 0;
            QC::u32 sectors = 0;
            QC::u8 channelIndex = 0;
            bool present = false;
            bool slave = false;
            bool mountableFat = false;
            bool hasMbrSignature = false;
            bool hasPartitionTable = false;
            bool hasFatBootSector = false;
            bool isSystemBinding = false;
            bool isSharedBinding = false;
            char model[41] = {};
        };

        // Enable/disable shared-volume probe (default disabled for boot safety).
        void setSharedProbeEnabled(bool enabled);

        // Enumerate legacy IDE disks that Citadel can currently see.
        // Returns the number of present disks written to outDevices.
        QC::usize enumerateDetectedDevices(DetectedDeviceInfo *outDevices, QC::usize capacity);

        // Probe legacy primary/secondary IDE channels for an ATA disk that looks like FAT32
        // and register it as QFS_SHARED mounted at /shared.
        void probeAndRegisterSharedVolume();

        // Probe legacy IDE for a persistent system volume and mount it at /system.
        // Intended for QEMU disk images (e.g., qcow2/raw) containing a FAT16/32 partition.
        void probeAndRegisterSystemVolume();

        // Probe legacy IDE for additional FAT volumes and mount them under /mnt/diskN.
        // These are discovered beyond the fixed /system and /shared bindings.
        void probeAndRegisterDataVolumes();

        // Reset internal system probe state so probeAndRegisterSystemVolume() can run again.
        // This is useful after formatting a disk at runtime.
        void resetSystemProbe();

        // Partition and format the first detected system disk candidate as a FAT32 volume.
        // Safety: refuses to format if the disk already appears to contain an MBR partition table
        // or an existing FAT boot sector.
        QC::Status formatSystemVolumeFAT32(bool force = false);

        // Partition and format the selected detected disk as a FAT32 volume.
        // The deviceIndex matches the numbering printed by sysdisks (disk0, disk1, ...).
        QC::Status formatDetectedDeviceFAT32(QC::usize deviceIndex, bool force = false);
    }
}
