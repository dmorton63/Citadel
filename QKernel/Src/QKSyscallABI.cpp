#include "QKSyscallABI.h"

namespace QK::Syscall
{

    namespace
    {
        constexpr QC::i64 kErrNotImplemented = static_cast<QC::i64>(Error::NotImplemented);
    }

    QC::i64 dispatch(QC::u16 sysId,
                     QC::u64 a0,
                     QC::u64 a1,
                     QC::u64 a2,
                     QC::u64 a3,
                     QC::u64 a4,
                     QC::u64 a5)
    {
        (void)a0;
        (void)a1;
        (void)a2;
        (void)a3;
        (void)a4;
        (void)a5;

        switch (sysId)
        {
        case Id::ProcessSelf:
        case Id::ProcessExit:
        case Id::ProcessSpawn:
        case Id::ProcessWait:
        case Id::ProcessSleep:
        case Id::ThreadSelf:
        case Id::ThreadCreate:
        case Id::ThreadJoin:
        case Id::ThreadYield:
        case Id::MemoryMap:
        case Id::MemoryUnmap:
        case Id::MemoryProtect:
        case Id::MemoryMapShared:
        case Id::FileOpen:
        case Id::FileClose:
        case Id::FileRead:
        case Id::FileWrite:
        case Id::FileSeek:
        case Id::FileStat:
        case Id::FileList:
        case Id::FileRemove:
        case Id::FileRename:
        case Id::TimeMonotonicNs:
        case Id::TimeRealtimeNs:
        case Id::TimeTimerCreate:
        case Id::TimeTimerWait:
        case Id::IpcChannelCreate:
        case Id::IpcSend:
        case Id::IpcRecv:
        case Id::NetSocket:
        case Id::NetConnect:
        case Id::NetBind:
        case Id::NetListen:
        case Id::NetAccept:
        case Id::NetSend:
        case Id::NetRecv:
        case Id::NetClose:
        case Id::UiWindowCreate:
        case Id::UiWindowDestroy:
        case Id::UiWindowShow:
        case Id::UiEventPoll:
        case Id::UiWindowSetTitle:
        case Id::GfxSurfaceCreate:
        case Id::GfxSurfacePresent:
        case Id::GfxUploadBuffer:
        case Id::GfxResize:
        case Id::SecCapQuery:
        case Id::SecTokenInfo:
        case Id::SecAttestSelf:
            return kErrNotImplemented;
        default:
            return static_cast<QC::i64>(Error::InvalidArg);
        }
    }

} // namespace QK::Syscall