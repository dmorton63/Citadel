// QFileSystem File - Implementation
// Namespace: QFS

#include "QFSFile.h"
#include "QFSFAT32.h"

namespace QFS
{

    File::File()
        : m_open(false), m_mode(OpenMode::Read), m_position(0), m_size(0), m_fs(nullptr), m_fsHandle(nullptr)
    {
    }

    File::~File()
    {
        if (m_open && m_fs)
        {
            m_fs->close(this);
        }
    }

    QC::isize File::read(void *buffer, QC::usize size)
    {
        if (!m_open || !m_fs || !(m_mode & OpenMode::Read))
        {
            return -1;
        }

        QC::isize bytesRead = m_fs->read(this, buffer, size);
        if (bytesRead > 0)
        {
            m_position += static_cast<QC::u64>(bytesRead);
        }
        return bytesRead;
    }

    QC::isize File::write(const void *buffer, QC::usize size)
    {
        if (!m_open || !m_fs || !(m_mode & OpenMode::Write))
        {
            return -1;
        }

        QC::isize bytesWritten = m_fs->write(this, buffer, size);
        if (bytesWritten > 0)
        {
            m_position += static_cast<QC::u64>(bytesWritten);
            if (m_position > m_size)
            {
                m_size = m_position;
            }
        }
        return bytesWritten;
    }

    QC::i64 File::seek(QC::i64 offset, SeekOrigin origin)
    {
        QC::i64 newPos;

        switch (origin)
        {
        case SeekOrigin::Begin:
            newPos = offset;
            break;
        case SeekOrigin::Current:
            newPos = static_cast<QC::i64>(m_position) + offset;
            break;
        case SeekOrigin::End:
            newPos = static_cast<QC::i64>(m_size) + offset;
            break;
        default:
            return -1;
        }

        if (newPos < 0)
        {
            return -1;
        }

        m_position = static_cast<QC::u64>(newPos);
        return static_cast<QC::i64>(m_position);
    }

    bool File::isEOF() const
    {
        return m_position >= m_size;
    }

    QC::Status File::flush()
    {
        // TODO: Implement flush
        return QC::Status::Success;
    }

    QC::Status File::sync()
    {
        // TODO: Implement sync
        return QC::Status::Success;
    }

} // namespace QFS
