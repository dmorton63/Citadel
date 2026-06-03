#pragma once

// QKDrvAHCI - Minimal AHCI SATA probing for system volumes
// Namespace: QKDrv::AHCI

#include "QCTypes.h"

namespace QKDrv
{
    namespace AHCI
    {
        struct LastFailureInfo
        {
            bool valid = false;
            bool timeout = false;
            QC::u8 portIndex = 0;
            QC::u8 command = 0;
            QC::u16 sectorCount = 0;
            QC::u64 lba = 0;
            QC::u32 interruptStatus = 0;
            QC::u32 taskFileData = 0;
            QC::u32 commandIssue = 0;
            QC::u32 sataError = 0;
        };

        struct DetectedDeviceInfo
        {
            QC::u8 controllerBus = 0;
            QC::u8 controllerDevice = 0;
            QC::u8 controllerFunction = 0;
            QC::u16 controllerVendorId = 0;
            QC::u16 controllerDeviceId = 0;
            QC::u8 portIndex = 0;
            QC::u32 sectors = 0;
            bool present = false;
            bool mountableFat = false;
            bool hasMbrSignature = false;
            bool hasPartitionTable = false;
            bool hasFatBootSector = false;
            char model[41] = {};
        };

        // Enumerate AHCI SATA disks that Citadel can currently see.
        // Returns the number of present disks written to outDevices.
        QC::usize enumerateDetectedDevices(DetectedDeviceInfo *outDevices, QC::usize capacity);

        // Partition and format the first detected blank AHCI SATA disk as FAT32.
        // Safety: refuses to format if the disk already appears partitioned/formatted.
        QC::Status formatSystemVolumeFAT32();

        // Partition and format the selected detected AHCI disk as FAT32.
        // The deviceIndex matches the AHCI portion of sysdisks numbering.
        QC::Status formatDetectedDeviceFAT32(QC::usize deviceIndex);

        // Probe AHCI SATA ports for a FAT32 system volume and register it at /system.
        // Returns true if a candidate volume was registered.
        bool probeAndRegisterSystemVolume();

        // Reset probe state so probeAndRegisterSystemVolume() can run again.
        void resetSystemProbe();

        // Copy the last AHCI failure details captured by the driver.
        bool getLastFailure(LastFailureInfo &out);
    }
}