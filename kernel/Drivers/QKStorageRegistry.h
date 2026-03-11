#pragma once

// QKStorageRegistry - Helper for registering block devices with QFileSystem
// Namespace: QKStorage

#include "QFSVolumeManager.h"

namespace QKStorage
{

    struct BlockDeviceRegistration
    {
        const char *name;      // Must follow QFS_* naming convention
        const char *mountPath; // Target mount path (e.g., "/", "/mnt/usb0")
        QFS::FileSystemKind fsKind;
        QFS::BlockDevice *device;
        bool autoMount = true;
    };

    QC::Status registerBlockDevice(const BlockDeviceRegistration &registration);

} // namespace QKStorage
