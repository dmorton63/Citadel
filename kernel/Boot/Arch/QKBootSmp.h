#pragma once

#include "QCTypes.h"

namespace QK::Boot::Smp
{
    using FLogFn = void (*)(const char *);

    bool Initialize(QC::u64 MpRequest[], FLogFn Log);
}
