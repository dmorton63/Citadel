#pragma once

// QKernel CommandCenter - shared command registration for terminals
// Namespace: QK::CmdCenter

#include "QCTypes.h"

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

    void initSession(Session &session);

    // Registers the Command Center MVP commands into QC::Cmd::Registry.
    // Safe to call multiple times.
    void registerMvpCommands();

}
