#include "QDCommandProcessor.h"

#include "QDCommandMessages.h"

#include "QDDesktopDocumentIO.h"
#include "QDDesktopDocumentValidation.h"

#include "QCCommandRegistry.h"
#include "QCQLEngine.h"
#include "QCQLRuntime.h"
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
    constexpr const char *CMMS_DB_PATH = "/system/CMMS.QDB";

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

    static const char *skipSpaces(const char *text)
    {
        while (text && (*text == ' ' || *text == '\t'))
            ++text;
        return text;
    }

    static bool equalsIgnoreCaseAscii(const char *a, const char *b)
    {
        if (!a || !b)
            return false;
        while (*a && *b)
        {
            char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a + 32) : *a;
            char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b + 32) : *b;
            if (ca != cb)
                return false;
            ++a;
            ++b;
        }
        return *a == '\0' && *b == '\0';
    }

    static bool readToken(const char *&text, char *out, QC::usize outSize)
    {
        if (!out || outSize == 0)
            return false;
        out[0] = '\0';
        text = skipSpaces(text);
        if (!text || !*text)
            return false;

        QC::usize i = 0;
        while (*text && *text != ' ' && *text != '\t')
        {
            if (i + 1 < outSize)
                out[i++] = *text;
            ++text;
        }
        out[i] = '\0';
        return i > 0;
    }

    static void appendText(char *dst, QC::usize dstSize, const char *text)
    {
        if (!dst || dstSize == 0 || !text)
            return;
        const QC::usize used = QC::String::strlen(dst);
        if (used + 1 >= dstSize)
            return;
        QC::String::strncpy(dst + used, text, dstSize - used - 1);
        dst[dstSize - 1] = '\0';
    }

    static void appendUnsigned(char *dst, QC::usize dstSize, QC::u32 value)
    {
        char reversed[16]{};
        QC::usize count = 0;
        do
        {
            reversed[count++] = static_cast<char>('0' + (value % 10u));
            value /= 10u;
        } while (value != 0 && count + 1 < sizeof(reversed));

        char text[16]{};
        for (QC::usize i = 0; i < count; ++i)
            text[i] = reversed[count - 1 - i];
        text[count] = '\0';
        appendText(dst, dstSize, text);
    }

    static bool cmdDeskDoc(const char *args, const QC::Cmd::Context &ctx, void *)
    {
        char verb[24]{};
        char documentId[24]{};
        char formatText[24]{};
        char modeText[24]{};
        const char *cursor = args;

        if (!readToken(cursor, verb, sizeof(verb)) || !equalsIgnoreCaseAscii(verb, "validate"))
        {
            ctx.writeLine("usage: deskdoc validate [production|golden] [json|cuiml] [save|publish]");
            ctx.writeLine("example: deskdoc validate production cuiml publish");
            return true;
        }

        if (!readToken(cursor, documentId, sizeof(documentId)))
            QC::String::strncpy(documentId, "production", sizeof(documentId) - 1);
        if (!readToken(cursor, formatText, sizeof(formatText)))
            QC::String::strncpy(formatText, "cuiml", sizeof(formatText) - 1);
        if (!readToken(cursor, modeText, sizeof(modeText)))
            QC::String::strncpy(modeText, "save", sizeof(modeText) - 1);

        QCQL::DbHandle handle{};
        const QCQL::Status openSt = QCQL::Runtime::openHandle(CMMS_DB_PATH, handle, false);
        if (openSt != QCQL::Status::Success)
        {
            ctx.writeLine("deskdoc: failed to open /system/CMMS.QDB");
            return true;
        }

        QCQL::Database *databasePtr = nullptr;
        if (QCQL::Runtime::borrowDatabase(handle, databasePtr) != QCQL::Status::Success || !databasePtr)
        {
            (void)QCQL::Runtime::closeHandle(handle);
            ctx.writeLine("deskdoc: failed to borrow database handle");
            return true;
        }
        QCQL::Database &database = *databasePtr;

        QD::DesktopDocumentImportResult importResult{};
        bool imported = false;
        if (equalsIgnoreCaseAscii(formatText, "json"))
            imported = QD::DesktopDocumentIO::importCmmsJson(database, documentId, importResult);
        else
            imported = QD::DesktopDocumentIO::importCmmsCuiml(database, documentId, importResult);

        if (!imported)
        {
            ctx.writeLine(importResult.error[0] ? importResult.error : "deskdoc: import failed");
            (void)QCQL::Runtime::closeHandle(handle);
            return true;
        }

        QD::DesktopDocumentValidationResult validation{};
        const bool publishMode = equalsIgnoreCaseAscii(modeText, "publish");
        const bool valid = publishMode
                               ? QD::DesktopDocumentValidation::validateForPublish(importResult.document, validation)
                               : QD::DesktopDocumentValidation::validateForSave(importResult.document, validation);

        char line[256]{};
        appendText(line, sizeof(line), "deskdoc: document=");
        appendText(line, sizeof(line), importResult.document.documentId);
        appendText(line, sizeof(line), " format=");
        appendText(line, sizeof(line), formatText);
        appendText(line, sizeof(line), " mode=");
        appendText(line, sizeof(line), publishMode ? "publish" : "save");
        appendText(line, sizeof(line), " controls=");
        appendUnsigned(line, sizeof(line), static_cast<QC::u32>(importResult.document.controls.size()));
        ctx.writeLine(line);

        QC::String::memset(line, 0, sizeof(line));
        appendText(line, sizeof(line), "deskdoc: valid=");
        appendText(line, sizeof(line), valid ? "yes" : "no");
        appendText(line, sizeof(line), " errors=");
        appendUnsigned(line, sizeof(line), validation.errorCount);
        appendText(line, sizeof(line), " warnings=");
        appendUnsigned(line, sizeof(line), validation.warningCount);
        ctx.writeLine(line);

        for (QC::usize i = 0; i < validation.issues.size(); ++i)
        {
            QC::String::memset(line, 0, sizeof(line));
            appendText(line, sizeof(line), validation.issues[i].severity == QD::DesktopValidationSeverity::Error ? "error: " : "warn: ");
            if (validation.issues[i].controlId[0])
            {
                appendText(line, sizeof(line), validation.issues[i].controlId);
                appendText(line, sizeof(line), ": ");
            }
            appendText(line, sizeof(line), validation.issues[i].message);
            ctx.writeLine(line);
        }

        (void)QCQL::Runtime::closeHandle(handle);
        return true;
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
        (void)QC::Cmd::Registry::instance().registerCommandExAccess(
            "deskdoc",
            QC::Cmd::AccessLevel::User,
            &cmdDeskDoc,
            nullptr,
            "Validate CMMS desktop documents through the canonical desktop model (deskdoc validate [production|golden] [json|cuiml] [save|publish])");

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
