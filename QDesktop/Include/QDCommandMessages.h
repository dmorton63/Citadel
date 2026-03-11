#pragma once

#include "QCTypes.h"

namespace QD
{
    namespace CmdMsg
    {
        // Service messages (Terminal -> CommandProcessor)
        enum : QC::u32
        {
            Request = 1, // payload: const char* line
        };

        // Window messages (CommandProcessor -> Terminal window)
        enum : QC::u32
        {
            OutputLine = 100, // payload: const char* line
            Done = 101,       // no payload
            ErrorLine = 102,  // payload: const char* line
        };

        constexpr const char *ServiceName = "QDesktop.CommandProcessor";
    }
}
