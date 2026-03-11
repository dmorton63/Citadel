#pragma once

// QFileSystem Directory - Directory handle
// Namespace: QFS

#include "QCTypes.h"
#include "QFSVFS.h"

namespace QFS
{

    struct DirEntry
    {
        char name[256];
        FileType type;
        QC::u64 size;
    };

    class Directory
    {
    public:
        Directory();
        ~Directory();

        // Enumeration
        bool read(DirEntry *entry);
        void rewind();

        // Status
        bool isOpen() const { return m_open; }
        FileSystem *fileSystem() const { return m_fs; }

        // Implementation details
        void setHandle(void *handle) { m_fsHandle = handle; }
        void *handle() const { return m_fsHandle; }
        void setFileSystem(FileSystem *fs) { m_fs = fs; }
        void setOpen(bool open) { m_open = open; }

    private:
        bool m_open;
        FileSystem *m_fs;
        void *m_fsHandle;
    };

} // namespace QFS
