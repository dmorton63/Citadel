#pragma once

#include "QCTypes.h"

#include "QCJson.h"

#include "QKBootConfigTier.h"

namespace QK::Boot::Config
{
    struct StagedEarlyModule
    {
        char id[32] = {0};
        char type[32] = {0};
        char resolvedPath[320] = {0};
        bool required = false;
        bool hasJson = false;

        QC::JSON::Value json;
    };

    struct StagedEarlyConfig
    {
        ConfigTier tier = ConfigTier::Unknown;
        char root[160] = {0};

        QC::u32 moduleCount = 0;
        StagedEarlyModule modules[16] = {};

        void clear()
        {
            tier = ConfigTier::Unknown;
            root[0] = 0;
            moduleCount = 0;
            for (QC::u32 i = 0; i < 16; ++i)
            {
                modules[i] = StagedEarlyModule{};
            }
        }
    };

    // Committed staging snapshot for the active tier.
    void ClearCommittedEarlyConfig();
    void CommitEarlyConfig(StagedEarlyConfig &&Stage);
    const StagedEarlyConfig *GetCommittedEarlyConfig();
}
