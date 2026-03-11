#include "QKBootEventLog.h"

#include "QCString.h"
#include "QDrvTimer.h"

namespace QK::Boot::Events
{
    namespace
    {
        static constexpr QC::usize kEventCapacity = 256;

        alignas(16) static Record g_Ring[kEventCapacity];
        static QC::usize g_Write = 0;
        static bool g_Full = false;
        static QC::u32 g_Seq = 1;

        static LogFn g_Sink = nullptr;

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

        static inline QC::usize countUnsafe()
        {
            return g_Full ? kEventCapacity : g_Write;
        }

        static inline QC::usize startUnsafe()
        {
            // Oldest record index.
            return g_Full ? g_Write : 0;
        }

        static inline void copyTrunc(char *dst, QC::usize dstCap, const char *src)
        {
            if (!dst || dstCap == 0)
                return;
            dst[0] = 0;
            if (!src)
                return;
            QC::String::strncpy(dst, src, dstCap - 1);
            dst[dstCap - 1] = 0;
        }

        static inline bool appendStr(char *dst, QC::usize dstCap, const char *s)
        {
            if (!dst || dstCap == 0 || !s)
                return false;
            const QC::usize cur = QC::String::strlen(dst);
            if (cur + 1 >= dstCap)
                return false;
            QC::String::strncpy(dst + cur, s, dstCap - 1 - cur);
            dst[dstCap - 1] = 0;
            return true;
        }

        static inline bool appendU64Dec(char *dst, QC::usize dstCap, QC::u64 v)
        {
            char num[32];
            QC::String::memset(num, 0, sizeof(num));

            int numIdx = 0;
            if (v == 0)
            {
                num[numIdx++] = '0';
            }
            else
            {
                char tmp[32];
                int tmpIdx = 0;
                while (v > 0 && tmpIdx < 31)
                {
                    tmp[tmpIdx++] = static_cast<char>('0' + (v % 10));
                    v /= 10;
                }
                for (int i = tmpIdx - 1; i >= 0; --i)
                    num[numIdx++] = tmp[i];
            }
            num[numIdx] = '\0';
            return appendStr(dst, dstCap, num);
        }

        static void emitOneLine(LogFn sink, const Record &r)
        {
            if (!sink)
                return;

            char line[512];
            QC::String::memset(line, 0, sizeof(line));

            (void)appendStr(line, sizeof(line), "EV seq=");
            (void)appendU64Dec(line, sizeof(line), static_cast<QC::u64>(r.seq));
            (void)appendStr(line, sizeof(line), " t_ms=");
            (void)appendU64Dec(line, sizeof(line), r.t_ms);
            (void)appendStr(line, sizeof(line), " stage=");
            (void)appendStr(line, sizeof(line), r.stage[0] ? r.stage : "(none)");
            (void)appendStr(line, sizeof(line), " type=");
            (void)appendStr(line, sizeof(line), r.type[0] ? r.type : "(none)");
            if (r.details[0])
            {
                (void)appendStr(line, sizeof(line), " ");
                (void)appendStr(line, sizeof(line), r.details);
            }
            (void)appendStr(line, sizeof(line), "\r\n");

            sink(line);
        }
    }

    void Clear()
    {
        lock();
        for (QC::usize i = 0; i < kEventCapacity; ++i)
            g_Ring[i] = Record{};
        g_Write = 0;
        g_Full = false;
        g_Seq = 1;
        unlock();
    }

    void SetSerialSink(LogFn sink)
    {
        lock();
        g_Sink = sink;
        unlock();
    }

    QC::u32 Emit(const char *stage, const char *type, const char *details)
    {
        lock();

        Record r{};
        r.seq = g_Seq++;
        r.t_ms = QDrv::Timer::instance().milliseconds();
        copyTrunc(r.stage, sizeof(r.stage), stage);
        copyTrunc(r.type, sizeof(r.type), type);
        copyTrunc(r.details, sizeof(r.details), details);

        g_Ring[g_Write] = r;
        g_Write++;
        if (g_Write >= kEventCapacity)
        {
            g_Write = 0;
            g_Full = true;
        }

        const LogFn sink = g_Sink;
        const Record local = r;
        unlock();

        emitOneLine(sink, local);
        return r.seq;
    }

    QC::usize Count()
    {
        lock();
        const QC::usize n = countUnsafe();
        unlock();
        return n;
    }

    QC::usize CopyOut(QC::usize offset, Record *out, QC::usize cap)
    {
        if (!out || cap == 0)
            return 0;

        lock();
        const QC::usize n = countUnsafe();
        if (offset >= n)
        {
            unlock();
            return 0;
        }

        QC::usize toCopy = n - offset;
        if (toCopy > cap)
            toCopy = cap;

        const QC::usize start = startUnsafe();
        for (QC::usize i = 0; i < toCopy; ++i)
        {
            QC::usize idx = start + offset + i;
            if (idx >= kEventCapacity)
                idx %= kEventCapacity;
            out[i] = g_Ring[idx];
        }

        unlock();
        return toCopy;
    }
}
