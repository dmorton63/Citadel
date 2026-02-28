#pragma once

#include "QCTypes.h"

namespace QK::Boot::Config
{
    using FLogFn = void (*)(const char *);

    // Reads a file from the Limine ramdisk module (FAT32), returning a null-terminated buffer.
    // This is limited to short 8.3 names in the root directory, since the early FAT reader
    // does not support LFN or subdirectories.
    //
    // Path examples: "/BOOT.JSN", "SYSCFG.JSN", "/DESKTOP.JSN".
    //
    // On success, caller owns OutBuf (delete[]).
    bool ReadFileFromLimineRamdiskModule(FLogFn Log, QC::u64 ModuleRequest[], const char *Path, char *&OutBuf, QC::usize &OutLen);
}
