#pragma once

#include "QCTypes.h"

namespace QK::Debug::Terminal
{
    bool InitFromLimineRequest(QC::u64 TerminalRequest[]);
    void Write(const char *Message);
    bool IsReady();
}
