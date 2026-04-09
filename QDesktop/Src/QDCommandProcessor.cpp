#include "QDCommandProcessor.h"

#include "QDCommandMessages.h"

#include "QCCommandRegistry.h"
#include "QCString.h"

#include "QKCommandCenter.h"

#include "QKServiceRegistry.h"
#include "QKMsgBus.h"

#include "QFSVFS.h"
#include "QFSDirectory.h"

#include "QKEventManager.h"
#include "QKShutdownController.h"

namespace
{
    static void destroyOwnedString(void *p)
    {
        char *s = static_cast<char *>(p);
        operator delete[](s);
    }

    static char *dupString(const char *s)
    {
        if (!s)
            s = "";
        const QC::usize len = QC::String::strlen(s);
        char *out = static_cast<char *>(operator new[](len + 1));
        for (QC::usize i = 0; i < len; ++i)
            out[i] = s[i];
        out[len] = '\0';
        return out;
    }

    static bool publishWindowLine(QC::u32 toWindowId, QC::u32 msgId, QC::u64 correlationId, const char *text)
    {
        QK::Msg::Envelope *env = QK::Msg::makeEnvelope(QK::Msg::Topic::WinMsg, correlationId);
        env->senderId = 0;
        env->targetId = toWindowId;
        env->param1 = msgId;
        env->param2 = 0;

        if (text)
        {
            env->payload = dupString(text);
            env->destroyPayload = &destroyOwnedString;
        }

        const bool ok = QK::Msg::Bus::instance().publish(env);
        QK::Msg::release(env);
        return ok;
    }

}

namespace QD
{
    CommandProcessor &CommandProcessor::instance()
    {
        static CommandProcessor proc;
        return proc;
    }

    void CommandProcessor::registerCommands()
    {
        if (m_commandsRegistered)
            return;

        // Shared Command Center MVP (single registry for all front-ends).
        QK::CmdCenter::registerMvpCommands();

        m_commandsRegistered = true;
    }

    void CommandProcessor::initialize()
    {
        if (m_initialized)
            return;

        registerCommands();

        // Register as a named service.
        m_serviceId = QK::Svc::Registry::instance().registerService(QD::CmdMsg::ServiceName, &CommandProcessor::onServiceMessage, this);
        m_initialized = (m_serviceId != 0);
    }

    void CommandProcessor::onServiceMessage(QK::Msg::Envelope *env, void *userData)
    {
        auto *self = static_cast<CommandProcessor *>(userData);
        if (!self || !env)
            return;

        const QC::u32 msgId = static_cast<QC::u32>(env->param1);
        if (msgId != QD::CmdMsg::Request)
            return;

        const QC::u32 replyWindowId = env->senderId;
        const QC::u64 corr = env->correlationId;
        const char *line = env->payload ? static_cast<const char *>(env->payload) : "";

        if (replyWindowId == 0)
            return;

        // Output callback streams back to the terminal window.
        QC::Cmd::Context ctx;
        ctx.out = [](const char *text, void *ud)
        {
            const QC::u64 corrId = reinterpret_cast<QC::u64>(ud);
            // ud packs (windowId in low 32) + corr in high 32 is not enough; so we publish via globals.
            (void)corrId;
        };

        // We need both windowId and correlationId in the callback; store them in a small struct.
        struct OutCtx
        {
            QC::u32 windowId;
            QC::u64 corr;
        };

        OutCtx outCtx{replyWindowId, corr};
        ctx.out = [](const char *text, void *ud)
        {
            OutCtx *c = static_cast<OutCtx *>(ud);
            if (!c)
                return;
            (void)publishWindowLine(c->windowId, QD::CmdMsg::OutputLine, c->corr, text);
        };
        ctx.userData = &outCtx;

        // Terminal encodes QC::Cmd::AccessLevel in env->param2.
        QC::u8 rawAccess = static_cast<QC::u8>(env->param2 & 0xFFu);
        const QC::u8 maxAccess = static_cast<QC::u8>(QC::Cmd::AccessLevel::System);
        if (rawAccess > maxAccess)
            rawAccess = maxAccess;
        ctx.callerAccess = static_cast<QC::Cmd::AccessLevel>(rawAccess);

        QK::CmdCenter::CommandPacket packet;
        QC::String::strncpy(packet.line, line ? line : "", sizeof(packet.line) - 1);
        packet.line[sizeof(packet.line) - 1] = '\0';
        packet.callerAccess = ctx.callerAccess;
        packet.origin = "desktop.terminal";

        // Execute via shared parser/dispatch envelope.
        const bool handled = QK::CmdCenter::executePacket(packet, ctx);
        if (!handled)
        {
            (void)publishWindowLine(replyWindowId, QD::CmdMsg::ErrorLine, corr, "Unknown command. Type 'help'.");
        }

        (void)publishWindowLine(replyWindowId, QD::CmdMsg::Done, corr, nullptr);
    }

}
