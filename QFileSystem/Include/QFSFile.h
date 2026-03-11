#pragma once

// QFileSystem File - File handle
// Namespace: QFS

#include "QCTypes.h"
#include "QFSVFS.h"

namespace QFS
{

    enum class SeekOrigin : QC::u8
    {
        Begin,
        Current,
        End
    };

    class File
    {
    public:
        File();
        ~File();

        // Read/Write
        QC::isize read(void *buffer, QC::usize size);
        QC::isize write(const void *buffer, QC::usize size);

        // Positioning
        QC::i64 seek(QC::i64 offset, SeekOrigin origin);
        QC::u64 tell() const { return m_position; }
        void rewind() { m_position = 0; }

        // Status
        bool isOpen() const { return m_open; }
        bool isEOF() const;
        QC::u64 size() const { return m_size; }
        OpenMode mode() const { return m_mode; }
        FileSystem *fileSystem() const { return m_fs; }

        // Misc
        QC::Status flush();
        QC::Status sync();

        // Implementation details (set by filesystem)
        void setHandle(void *handle) { m_fsHandle = handle; }
        void *handle() const { return m_fsHandle; }
        void setFileSystem(FileSystem *fs) { m_fs = fs; }
        void setOpen(bool open) { m_open = open; }
        void setMode(OpenMode mode) { m_mode = mode; }
        void setSize(QC::u64 size) { m_size = size; }
        void setPosition(QC::u64 position) { m_position = position; }

    private:
        bool m_open;
        OpenMode m_mode;
        QC::u64 m_position;
        QC::u64 m_size;
        FileSystem *m_fs;
        void *m_fsHandle;
    };

} // namespace QFS
