#include "QKBootLog.h"

namespace QK::Boot::Log
{
    namespace
    {
        static constexpr QC::usize kBootLogCapacity = 64u * 1024u;

        alignas(16) static char g_Buf[kBootLogCapacity];
        static QC::usize g_Write = 0;
        static bool g_Full = false;

        // Minimal spin lock (freestanding friendly).
        static int g_Lock = 0;

        static inline void cpuPause()
        {
#if defined(__x86_64__) || defined(__i386__)
            asm volatile("pause");
#else
            asm volatile("");
#endif
        }

        static inline void lock()
        {
            while (__atomic_test_and_set(&g_Lock, __ATOMIC_ACQUIRE))
            {
                cpuPause();
            }
        }

        static inline void unlock()
        {
            __atomic_clear(&g_Lock, __ATOMIC_RELEASE);
        }

        static inline QC::usize sizeUnsafe()
        {
            return g_Full ? kBootLogCapacity : g_Write;
        }

        static inline QC::usize startUnsafe()
        {
            // Oldest byte index.
            return g_Full ? g_Write : 0;
        }
    }

    void Clear()
    {
        lock();
        g_Write = 0;
        g_Full = false;
        unlock();
    }

    void Append(const char *text)
    {
        if (!text)
            return;

        lock();
        const char *p = text;
        while (*p)
        {
            g_Buf[g_Write++] = *p++;
            if (g_Write >= kBootLogCapacity)
            {
                g_Write = 0;
                g_Full = true;
            }
        }
        unlock();
    }

    QC::usize Size()
    {
        lock();
        const QC::usize sz = sizeUnsafe();
        unlock();
        return sz;
    }

    QC::usize CopyOut(QC::usize offset, char *out, QC::usize cap)
    {
        if (!out || cap == 0)
            return 0;

        lock();
        const QC::usize sz = sizeUnsafe();
        if (offset >= sz)
        {
            unlock();
            return 0;
        }

        QC::usize toCopy = sz - offset;
        if (toCopy > cap)
            toCopy = cap;

        const QC::usize start = startUnsafe();
        for (QC::usize i = 0; i < toCopy; ++i)
        {
            QC::usize idx = start + offset + i;
            if (idx >= kBootLogCapacity)
                idx %= kBootLogCapacity;
            out[i] = g_Buf[idx];
        }

        unlock();
        return toCopy;
    }
}
