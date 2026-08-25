#include "QJFCitadelSyscalls.h"

namespace QC::JFunc::Sys
{

    namespace
    {
        constexpr QC::i64 kErrNotImplemented = -2;

        // v0.1 syscall IDs from docs/CITADEL_SYSCALL_ABI_V0_1.md
        constexpr QC::u16 kProcessSelf = 0x0101;
        constexpr QC::u16 kProcessExit = 0x0102;
        constexpr QC::u16 kProcessSleep = 0x0105;

        constexpr QC::u16 kFileOpen = 0x0401;
        constexpr QC::u16 kFileClose = 0x0402;
        constexpr QC::u16 kFileRead = 0x0403;
        constexpr QC::u16 kFileWrite = 0x0404;
        constexpr QC::u16 kFileSeek = 0x0405;

        constexpr QC::u16 kTimeMonotonicNs = 0x0501;
        constexpr QC::u16 kTimeRealtimeNs = 0x0502;

        constexpr QC::u16 kUiWindowCreate = 0x0801;
        constexpr QC::u16 kUiWindowShow = 0x0803;
        constexpr QC::u16 kUiEventPoll = 0x0804;

        constexpr QC::u16 kNetSocket = 0x0701;
        constexpr QC::u16 kNetConnect = 0x0702;
        constexpr QC::u16 kNetSend = 0x0706;
        constexpr QC::u16 kNetRecv = 0x0707;
        constexpr QC::u16 kNetClose = 0x0708;

        SyscallInvoker g_invoker = nullptr;
    } // namespace

    void setInvoker(SyscallInvoker invoker)
    {
        g_invoker = invoker;
    }

    QC::i64 call(QC::u16 sysId,
                 QC::u64 a0,
                 QC::u64 a1,
                 QC::u64 a2,
                 QC::u64 a3,
                 QC::u64 a4,
                 QC::u64 a5)
    {
        if (!g_invoker)
        {
            return kErrNotImplemented;
        }

        return g_invoker(sysId, a0, a1, a2, a3, a4, a5);
    }

    QC::i64 processSelf(Pid *outPid)
    {
        return call(kProcessSelf, reinterpret_cast<QC::u64>(outPid));
    }

    QC::i64 processExit(QC::i32 code)
    {
        return call(kProcessExit, static_cast<QC::u64>(static_cast<QC::i64>(code)));
    }

    QC::i64 processSleep(QC::u64 durationNs)
    {
        return call(kProcessSleep, durationNs);
    }

    QC::i64 fileOpen(const char *path, QC::u32 flags, QC::u32 mode, Handle *outHandle)
    {
        return call(kFileOpen,
                    reinterpret_cast<QC::u64>(path),
                    static_cast<QC::u64>(flags),
                    static_cast<QC::u64>(mode),
                    reinterpret_cast<QC::u64>(outHandle));
    }

    QC::i64 fileClose(Handle handle)
    {
        return call(kFileClose, handle);
    }

    QC::i64 fileRead(Handle handle, void *buffer, QC::u64 len, QC::u64 *outRead)
    {
        return call(kFileRead,
                    handle,
                    reinterpret_cast<QC::u64>(buffer),
                    len,
                    reinterpret_cast<QC::u64>(outRead));
    }

    QC::i64 fileWrite(Handle handle, const void *buffer, QC::u64 len, QC::u64 *outWritten)
    {
        return call(kFileWrite,
                    handle,
                    reinterpret_cast<QC::u64>(buffer),
                    len,
                    reinterpret_cast<QC::u64>(outWritten));
    }

    QC::i64 fileSeek(Handle handle, QC::i64 offset, QC::u32 whence, QC::u64 *outPos)
    {
        return call(kFileSeek,
                    handle,
                    static_cast<QC::u64>(offset),
                    static_cast<QC::u64>(whence),
                    reinterpret_cast<QC::u64>(outPos));
    }

    QC::i64 timeMonotonicNs(QC::u64 *outNs)
    {
        return call(kTimeMonotonicNs, reinterpret_cast<QC::u64>(outNs));
    }

    QC::i64 timeRealtimeNs(QC::u64 *outNs)
    {
        return call(kTimeRealtimeNs, reinterpret_cast<QC::u64>(outNs));
    }

    QC::i64 uiWindowCreate(const void *createReq, Handle *outWindow)
    {
        return call(kUiWindowCreate,
                    reinterpret_cast<QC::u64>(createReq),
                    reinterpret_cast<QC::u64>(outWindow));
    }

    QC::i64 uiWindowShow(Handle window, QC::u32 show)
    {
        return call(kUiWindowShow, window, static_cast<QC::u64>(show));
    }

    QC::i64 uiEventPoll(Handle window, void *outEvent, QC::u64 timeoutNs)
    {
        return call(kUiEventPoll,
                    window,
                    reinterpret_cast<QC::u64>(outEvent),
                    timeoutNs);
    }

    QC::i64 netSocket(QC::u32 domain, QC::u32 type, QC::u32 protocol, Handle *outSocket)
    {
        return call(kNetSocket,
                    static_cast<QC::u64>(domain),
                    static_cast<QC::u64>(type),
                    static_cast<QC::u64>(protocol),
                    reinterpret_cast<QC::u64>(outSocket));
    }

    QC::i64 netConnect(Handle socket, const void *sockaddrBytes, QC::u32 sockaddrLen)
    {
        return call(kNetConnect,
                    socket,
                    reinterpret_cast<QC::u64>(sockaddrBytes),
                    static_cast<QC::u64>(sockaddrLen));
    }

    QC::i64 netSend(Handle socket, const void *buffer, QC::u64 len, QC::u32 flags, QC::u64 *outWritten)
    {
        return call(kNetSend,
                    socket,
                    reinterpret_cast<QC::u64>(buffer),
                    len,
                    static_cast<QC::u64>(flags),
                    reinterpret_cast<QC::u64>(outWritten));
    }

    QC::i64 netRecv(Handle socket, void *buffer, QC::u64 cap, QC::u32 flags, QC::u64 *outRead)
    {
        return call(kNetRecv,
                    socket,
                    reinterpret_cast<QC::u64>(buffer),
                    cap,
                    static_cast<QC::u64>(flags),
                    reinterpret_cast<QC::u64>(outRead));
    }

    QC::i64 netClose(Handle socket)
    {
        return call(kNetClose, socket);
    }

} // namespace QC::JFunc::Sys