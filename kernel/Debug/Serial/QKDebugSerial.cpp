#include "QKDebugSerial.h"

#include "QCBuiltins.h"
#include "QFSFile.h"
#include "QFSVFS.h"

namespace
{
    QK::Debug::Serial::FMirrorCallback GMirrorCallback = nullptr;
    static constexpr QC::usize GCaptureCapacity = 256u * 1024u;
    static char GCaptureBuffer[GCaptureCapacity] = {0};
    static QC::usize GCaptureLength = 0;
    static bool GCaptureTruncated = false;
}

namespace QK::Debug::Serial
{
    namespace
    {
        void appendCapture(const char *message)
        {
            if (!message)
                return;

            while (*message)
            {
                if (GCaptureLength + 1 < GCaptureCapacity)
                {
                    GCaptureBuffer[GCaptureLength++] = *message;
                    GCaptureBuffer[GCaptureLength] = '\0';
                }
                else
                {
                    GCaptureTruncated = true;
                }
                ++message;
            }
        }

        void writeImpl(const char *message, bool includeMirror)
        {
            if (!message)
            {
                return;
            }

            appendCapture(message);

            if (includeMirror && GMirrorCallback)
            {
                GMirrorCallback(message);
            }

            while (*message)
            {
                while ((QC::inb(0x3F8 + 5) & 0x20) == 0)
                {
                }
                QC::outb(0x3F8, static_cast<QC::u8>(*message));
                ++message;
            }
        }
    }

    void Init()
    {
        // Initialize COM1 at 0x3F8
        QC::outb(0x3F8 + 1, 0x00); // Disable interrupts
        QC::outb(0x3F8 + 3, 0x80); // Enable DLAB
        QC::outb(0x3F8 + 0, 0x03); // Baud divisor low (38400 baud)
        QC::outb(0x3F8 + 1, 0x00); // Baud divisor high
        QC::outb(0x3F8 + 3, 0x03); // 8N1
        QC::outb(0x3F8 + 2, 0xC7); // Enable FIFO
        QC::outb(0x3F8 + 4, 0x0B); // IRQs enabled, RTS/DSR set
    }

    void SetMirror(FMirrorCallback MirrorCallback)
    {
        GMirrorCallback = MirrorCallback;
    }

    void Write(const char *Message)
    {
        writeImpl(Message, false);
    }

    void WriteMirrored(const char *Message)
    {
        writeImpl(Message, true);
    }

    void WriteInt(QC::i32 Value)
    {
        char Buffer[16];
        int Pos = 0;
        bool bNegative = Value < 0;
        QC::u32 Magnitude = bNegative ? static_cast<QC::u32>(-Value) : static_cast<QC::u32>(Value);

        do
        {
            Buffer[Pos++] = static_cast<char>('0' + (Magnitude % 10));
            Magnitude /= 10;
        } while (Magnitude != 0 && Pos < static_cast<int>(sizeof(Buffer)) - 1);

        if (bNegative && Pos < static_cast<int>(sizeof(Buffer)) - 1)
        {
            Buffer[Pos++] = '-';
        }

        Buffer[Pos] = '\0';

        for (int i = 0; i < Pos / 2; ++i)
        {
            char Tmp = Buffer[i];
            Buffer[i] = Buffer[Pos - 1 - i];
            Buffer[Pos - 1 - i] = Tmp;
        }

        Write(Buffer);
    }

    const char *CaptureData()
    {
        return GCaptureBuffer;
    }

    QC::usize CaptureSize()
    {
        return GCaptureLength;
    }

    bool CaptureTruncated()
    {
        return GCaptureTruncated;
    }

    bool SaveCaptureToFile(const char *path)
    {
        if (!path || !*path)
            return false;

        auto &vfs = QFS::VFS::instance();
        QFS::File *file = vfs.open(path, QFS::OpenMode::Write | QFS::OpenMode::Create | QFS::OpenMode::Truncate);
        if (!file)
            return false;

        const char *capture = CaptureData();
        const QC::usize captureLen = CaptureSize();
        bool wroteAll = true;
        if (capture && captureLen > 0)
        {
            QC::usize offset = 0;
            while (offset < captureLen)
            {
                const QC::usize chunk = (captureLen - offset > 4096u) ? 4096u : (captureLen - offset);
                const QC::isize written = file->write(capture + offset, chunk);
                if (written <= 0)
                {
                    wroteAll = false;
                    break;
                }
                offset += static_cast<QC::usize>(written);
            }
        }

        if (wroteAll)
        {
            (void)file->sync();
        }

        vfs.close(file);
        return wroteAll;
    }
}
