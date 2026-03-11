#pragma once

namespace QK::Boot::Arch
{
    using FLogFn = void (*)(const char *);

    void InitCpuGdtIdtAndInterrupts(FLogFn Log);
}
