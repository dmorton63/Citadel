#include "Debug/QKDebugPanic.h"

#include "QCTypes.h"

namespace
{
    void earlyPrint(const char *msg)
    {
        if (!msg)
            return;

        volatile QC::u16 *video = reinterpret_cast<volatile QC::u16 *>(0xB8000);
        static int pos = 0;

        while (*msg)
        {
            if (*msg == '\n')
            {
                pos = ((pos / 80) + 1) * 80;
            }
            else
            {
                video[pos++] = static_cast<QC::u16>(*msg) | 0x0F00;
            }
            ++msg;
        }
    }
}

extern "C" [[noreturn]] void kernel_panic(const char *message)
{
    asm volatile("cli");

    earlyPrint("\n\n*** KERNEL PANIC ***\n");
    if (message)
    {
        earlyPrint(message);
    }

    for (;;)
    {
        asm volatile("hlt");
    }
}
