#include "QFSStat.h"

namespace QFS
{
    QC::Status statPath(const char *path, FileInfo *info)
    {
        return VFS::instance().stat(path, info);
    }
}
