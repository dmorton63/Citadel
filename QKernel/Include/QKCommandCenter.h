#pragma once

// QKernel CommandCenter - shared command registration for terminals
// Namespace: QK::CmdCenter

#include "QCTypes.h"
#include "QCCommandRegistry.h"

namespace QK::CmdCenter
{

    struct Session
    {
        static constexpr QC::usize CwdSize = 128;
        char cwd[CwdSize];
    };

    // Frontends may embed this as the first field of their own ctx structs.
    struct UserContext
    {
        Session *session = nullptr;
    };

    // Shared command packet for frontends that want a stable execution envelope.
    struct CommandPacket
    {
        char line[256] = {0};
        QC::Cmd::AccessLevel callerAccess = QC::Cmd::AccessLevel::Everyone;
        const char *origin = nullptr;
    };

    using IpcHookFn = void (*)(const CommandPacket &packet, void *userData);

    void initSession(Session &session);

    // Registers the Command Center MVP commands into QC::Cmd::Registry.
    // Safe to call multiple times.
    void registerMvpCommands();

    // IPC hook: lets GUI shells observe outgoing command packets without
    // re-implementing parser/dispatch logic.
    void setIpcHook(IpcHookFn hook, void *userData);

    // Execute a structured command packet through the shared registry.
    bool executePacket(const CommandPacket &packet, const QC::Cmd::Context &ctx);

}
