#pragma once

// Citadel syscall ABI declarations (v0.1).
// Mirrors docs/CITADEL_SYSCALL_ABI_V0_1.md.

#include "QCTypes.h"

namespace QK::Syscall
{

    using Handle = QC::u64;
    using Pid = QC::u64;
    using Tid = QC::u64;

    enum class Family : QC::u8
    {
        Process  = 0x01,
        Thread   = 0x02,
        Memory   = 0x03,
        File     = 0x04,
        Time     = 0x05,
        IPC      = 0x06,
        Net      = 0x07,
        UI       = 0x08,
        Graphics = 0x09,
        Security = 0x0A,
    };

    constexpr QC::u16 makeId(Family family, QC::u8 op)
    {
        return static_cast<QC::u16>((static_cast<QC::u16>(family) << 8) | op);
    }

    enum class Error : QC::i64
    {
        Unknown            = -1,
        NotImplemented     = -2,
        InvalidArg         = -3,
        BadHandle          = -4,
        AccessDenied       = -5,
        NotFound           = -6,
        AlreadyExists      = -7,
        Timeout            = -8,
        WouldBlock         = -9,
        Io                 = -10,
        NoMemory           = -11,
        BadState           = -12,
        Truncated          = -13,
        CapabilityDenied   = -14,
        AbiMismatch        = -15,
    };

    namespace Id
    {
        // 0x01xx Process
        constexpr QC::u16 ProcessSelf  = makeId(Family::Process, 0x01);
        constexpr QC::u16 ProcessExit  = makeId(Family::Process, 0x02);
        constexpr QC::u16 ProcessSpawn = makeId(Family::Process, 0x03);
        constexpr QC::u16 ProcessWait  = makeId(Family::Process, 0x04);
        constexpr QC::u16 ProcessSleep = makeId(Family::Process, 0x05);

        // 0x02xx Thread
        constexpr QC::u16 ThreadSelf   = makeId(Family::Thread, 0x01);
        constexpr QC::u16 ThreadCreate = makeId(Family::Thread, 0x02);
        constexpr QC::u16 ThreadJoin   = makeId(Family::Thread, 0x03);
        constexpr QC::u16 ThreadYield  = makeId(Family::Thread, 0x04);

        // 0x03xx Memory
        constexpr QC::u16 MemoryMap       = makeId(Family::Memory, 0x01);
        constexpr QC::u16 MemoryUnmap     = makeId(Family::Memory, 0x02);
        constexpr QC::u16 MemoryProtect   = makeId(Family::Memory, 0x03);
        constexpr QC::u16 MemoryMapShared = makeId(Family::Memory, 0x04);

        // 0x04xx FileSystem
        constexpr QC::u16 FileOpen   = makeId(Family::File, 0x01);
        constexpr QC::u16 FileClose  = makeId(Family::File, 0x02);
        constexpr QC::u16 FileRead   = makeId(Family::File, 0x03);
        constexpr QC::u16 FileWrite  = makeId(Family::File, 0x04);
        constexpr QC::u16 FileSeek   = makeId(Family::File, 0x05);
        constexpr QC::u16 FileStat   = makeId(Family::File, 0x06);
        constexpr QC::u16 FileList   = makeId(Family::File, 0x07);
        constexpr QC::u16 FileRemove = makeId(Family::File, 0x08);
        constexpr QC::u16 FileRename = makeId(Family::File, 0x09);

        // 0x05xx Time
        constexpr QC::u16 TimeMonotonicNs = makeId(Family::Time, 0x01);
        constexpr QC::u16 TimeRealtimeNs  = makeId(Family::Time, 0x02);
        constexpr QC::u16 TimeTimerCreate = makeId(Family::Time, 0x03);
        constexpr QC::u16 TimeTimerWait   = makeId(Family::Time, 0x04);

        // 0x06xx IPC
        constexpr QC::u16 IpcChannelCreate = makeId(Family::IPC, 0x01);
        constexpr QC::u16 IpcSend          = makeId(Family::IPC, 0x02);
        constexpr QC::u16 IpcRecv          = makeId(Family::IPC, 0x03);

        // 0x07xx Net
        constexpr QC::u16 NetSocket  = makeId(Family::Net, 0x01);
        constexpr QC::u16 NetConnect = makeId(Family::Net, 0x02);
        constexpr QC::u16 NetBind    = makeId(Family::Net, 0x03);
        constexpr QC::u16 NetListen  = makeId(Family::Net, 0x04);
        constexpr QC::u16 NetAccept  = makeId(Family::Net, 0x05);
        constexpr QC::u16 NetSend    = makeId(Family::Net, 0x06);
        constexpr QC::u16 NetRecv    = makeId(Family::Net, 0x07);
        constexpr QC::u16 NetClose   = makeId(Family::Net, 0x08);

        // 0x08xx UI
        constexpr QC::u16 UiWindowCreate   = makeId(Family::UI, 0x01);
        constexpr QC::u16 UiWindowDestroy  = makeId(Family::UI, 0x02);
        constexpr QC::u16 UiWindowShow     = makeId(Family::UI, 0x03);
        constexpr QC::u16 UiEventPoll      = makeId(Family::UI, 0x04);
        constexpr QC::u16 UiWindowSetTitle = makeId(Family::UI, 0x05);

        // 0x09xx Graphics
        constexpr QC::u16 GfxSurfaceCreate = makeId(Family::Graphics, 0x01);
        constexpr QC::u16 GfxSurfacePresent = makeId(Family::Graphics, 0x02);
        constexpr QC::u16 GfxUploadBuffer  = makeId(Family::Graphics, 0x03);
        constexpr QC::u16 GfxResize        = makeId(Family::Graphics, 0x04);

        // 0x0Axx Security
        constexpr QC::u16 SecCapQuery   = makeId(Family::Security, 0x01);
        constexpr QC::u16 SecTokenInfo  = makeId(Family::Security, 0x02);
        constexpr QC::u16 SecAttestSelf = makeId(Family::Security, 0x03);
    } // namespace Id

    struct SpawnReq
    {
        const char* path = nullptr;
        const char* const* argv = nullptr;
        const char* const* envv = nullptr;
        QC::u32 flags = 0;
        Handle stdIn = 0;
        Handle stdOut = 0;
        Handle stdErr = 0;
    };

    struct ThreadCreateReq
    {
        QC::u64 entryFn = 0;
        QC::u64 arg0 = 0;
        QC::u64 stackSize = 0;
        QC::u32 flags = 0;
    };

    struct MemMapReq
    {
        QC::u64 hintAddr = 0;
        QC::u64 size = 0;
        QC::u32 prot = 0;
        QC::u32 flags = 0;
    };

    struct FileStat
    {
        QC::u64 size = 0;
        QC::u64 mtimeNs = 0;
        QC::u32 mode = 0;
        QC::u32 type = 0;
    };

    struct DirListReq
    {
        void* outBuffer = nullptr;
        QC::u64 outCapacity = 0;
        QC::u64* outUsed = nullptr;
    };

    struct TimerReq
    {
        QC::u64 intervalNs = 0;
        QC::u64 startDelayNs = 0;
        QC::u32 flags = 0;
    };

    // Central kernel dispatcher entry. Implementation is kernel-specific.
    QC::i64 dispatch(QC::u16 sysId,
                     QC::u64 a0 = 0,
                     QC::u64 a1 = 0,
                     QC::u64 a2 = 0,
                     QC::u64 a3 = 0,
                     QC::u64 a4 = 0,
                     QC::u64 a5 = 0);

} // namespace QK::Syscall
