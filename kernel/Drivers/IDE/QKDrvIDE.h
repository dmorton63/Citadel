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
    }
}
