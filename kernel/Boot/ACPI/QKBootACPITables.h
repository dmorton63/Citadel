#pragma once

#include "QCTypes.h"

namespace QK::Boot::Acpi
{
    using FLogFn = void (*)(const char *);
    using FTpm2CrbStartupFn = void (*)(QC::u32 StartMethod, QC::PhysAddr ControlAreaPhys, FLogFn Log);

    void EnumerateTables(QC::PhysAddr RsdpPhys, FLogFn Log, FTpm2CrbStartupFn Tpm2CrbStartup);
}
