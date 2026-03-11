#pragma once

#include "QCTypes.h"

namespace QK::Boot::Config
{
    enum class ConfigTier : QC::u8
    {
        Unknown = 0,
        Production = 1,
        Golden = 2,
    };

    // Set once during boot after tier validation.
    void SetActiveConfigTier(ConfigTier tier, const char *root);

    ConfigTier GetActiveConfigTier();
    const char *GetActiveConfigTierRoot();
    const char *GetActiveConfigTierName();
}
