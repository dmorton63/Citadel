#include "QKDebugLimineTerminal.h"

#include "QCString.h"

#define LIMINE_API_REVISION 2
#include "limine.h"

namespace
{
    LIMINE_DEPRECATED_IGNORE_START
    limine_terminal *GTerminal = nullptr;
    limine_terminal_write GTerminalWrite = nullptr;
    LIMINE_DEPRECATED_IGNORE_END
}

namespace QK::Debug::Terminal
{
    bool InitFromLimineRequest(QC::u64 TerminalRequest[])
    {
        if (!TerminalRequest)
        {
            return false;
        }

        LIMINE_DEPRECATED_IGNORE_START
        auto *Response = reinterpret_cast<limine_terminal_response *>(TerminalRequest[5]);
        LIMINE_DEPRECATED_IGNORE_END
        if (!Response)
        {
            return false;
        }

        if (!Response->write)
        {
            return false;
        }

        if (Response->terminal_count == 0 || !Response->terminals)
        {
            return false;
        }

        GTerminal = Response->terminals[0];
        GTerminalWrite = Response->write;
        return true;
    }

    bool IsReady()
    {
        return (GTerminal != nullptr) && (GTerminalWrite != nullptr);
    }

    void Write(const char *Message)
    {
        if (!Message)
        {
            return;
        }

        if (!IsReady())
        {
            return;
        }

        GTerminalWrite(GTerminal, Message, QC::String::strlen(Message));
    }
}
