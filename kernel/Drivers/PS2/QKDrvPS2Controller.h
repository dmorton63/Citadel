#pragma once

// Shared i8042 helpers. Many UEFI-only PCs have no 8042 controller: port 0x64
// floats to 0xFF, so every poll must be bounded or boot hangs.

#include "QCTypes.h"
#include "QArchPort.h"

namespace QKDrv
{
    namespace PS2
    {
        namespace Controller
        {
            constexpr QC::u16 DATA_PORT = 0x60;
            constexpr QC::u16 STATUS_PORT = 0x64;
            constexpr QC::u32 WAIT_SPINS = 100000;

            inline bool present()
            {
                return QArch::inb(STATUS_PORT) != 0xFF;
            }

            inline bool waitInputClear()
            {
                for (QC::u32 i = 0; i < WAIT_SPINS; ++i)
                {
                    if (!(QArch::inb(STATUS_PORT) & 0x02))
                        return true;
                }
                return false;
            }

            inline bool waitOutputFull()
            {
                for (QC::u32 i = 0; i < WAIT_SPINS; ++i)
                {
                    if (QArch::inb(STATUS_PORT) & 0x01)
                        return true;
                }
                return false;
            }
        }
    }
}
