#pragma once

// QKSystemVolumeCommands - kernel-only command registrations
// Namespace: QK::CmdCenter

namespace QK
{
    namespace CmdCenter
    {
        // Register commands for formatting/mounting the persistent system volume.
        // Safe to call multiple times.
        void registerSystemVolumeCommands();
    }
}
