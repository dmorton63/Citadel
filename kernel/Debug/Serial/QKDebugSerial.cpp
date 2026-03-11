#include "QKDebugSerial.h"

#include "QCBuiltins.h"

namespace
{
    QK::Debug::Serial::FMirrorCallback GMirrorCallback = nullptr;
}

namespace QK::Debug::Serial
{
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
        if (!Message)
        {
            return;
        }

        if (GMirrorCallback)
        {
            GMirrorCallback(Message);
        }

        while (*Message)
        {
            while ((QC::inb(0x3F8 + 5) & 0x20) == 0)
            {
            }
            QC::outb(0x3F8, static_cast<QC::u8>(*Message));
            ++Message;
        }
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
}
