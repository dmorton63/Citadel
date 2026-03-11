#pragma once

#include "QCTypes.h"

namespace QK::Boot::Config
{
    using FLogFn = void (*)(const char *);

    // Reads a file from the Limine ramdisk module (FAT32), returning a null-terminated buffer.
    // This is limited to short 8.3 names (no LFN). Subdirectories are supported only if each
    // path segment is also 8.3.
    //
    // Path examples: "/BOOT.JSN", "SYSCFG.JSN", "/DESKTOP.JSN".
    //
    // On success, caller owns OutBuf (delete[]).
    bool ReadFileFromLimineRamdiskModule(FLogFn Log, QC::u64 ModuleRequest[], const char *Path, char *&OutBuf, QC::usize &OutLen);
}
