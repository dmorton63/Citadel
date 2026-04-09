#pragma once

#include "QFSVFS.h"

namespace QFS
{
    // VFS-backed stat helper so tooling can query metadata without touching filesystem internals.
    QC::Status statPath(const char *path, FileInfo *info);
}
