#pragma once

// CiteLang runtime syscall wrappers (v0.1).
// Wrapper signatures align with docs/CITADEL_SYSCALL_ABI_V0_1.md.

#include "QCTypes.h"

namespace QC::JFunc::Sys
{

    using Handle = QC::u64;
    using Pid = QC::u64;
    using Tid = QC::u64;

    using SyscallInvoker = QC::i64 (*)(QC::u16,
                                       QC::u64,
                                       QC::u64,
                                       QC::u64,
                                       QC::u64,
                                       QC::u64,
                                       QC::u64);

    // Install the platform-specific syscall bridge used by wrappers.
    void setInvoker(SyscallInvoker invoker);

    // Generic entry for wrappers when typed helpers are not available yet.
    QC::i64 call(QC::u16 sysId,
                 QC::u64 a0 = 0,
                 QC::u64 a1 = 0,
                 QC::u64 a2 = 0,
                 QC::u64 a3 = 0,
                 QC::u64 a4 = 0,
                 QC::u64 a5 = 0);

    // Process
    QC::i64 processSelf(Pid* outPid);
    QC::i64 processExit(QC::i32 code);
    QC::i64 processSleep(QC::u64 durationNs);

    // FileSystem
    QC::i64 fileOpen(const char* path, QC::u32 flags, QC::u32 mode, Handle* outHandle);
    QC::i64 fileClose(Handle handle);
    QC::i64 fileRead(Handle handle, void* buffer, QC::u64 len, QC::u64* outRead);
    QC::i64 fileWrite(Handle handle, const void* buffer, QC::u64 len, QC::u64* outWritten);
    QC::i64 fileSeek(Handle handle, QC::i64 offset, QC::u32 whence, QC::u64* outPos);

    // Time
    QC::i64 timeMonotonicNs(QC::u64* outNs);
    QC::i64 timeRealtimeNs(QC::u64* outNs);

    // UI
    QC::i64 uiWindowCreate(const void* createReq, Handle* outWindow);
    QC::i64 uiWindowShow(Handle window, QC::u32 show);
    QC::i64 uiEventPoll(Handle window, void* outEvent, QC::u64 timeoutNs);

    // Net
    QC::i64 netSocket(QC::u32 domain, QC::u32 type, QC::u32 protocol, Handle* outSocket);
    QC::i64 netConnect(Handle socket, const void* sockaddrBytes, QC::u32 sockaddrLen);
    QC::i64 netSend(Handle socket, const void* buffer, QC::u64 len, QC::u32 flags, QC::u64* outWritten);
    QC::i64 netRecv(Handle socket, void* buffer, QC::u64 cap, QC::u32 flags, QC::u64* outRead);

    QC::i64 netClose(Handle socket);

} // namespace QC::JFunc::Sys
