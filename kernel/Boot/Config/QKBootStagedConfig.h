#pragma once

#include "QCTypes.h"
#include "QKBootConfigTier.h"
#include "QCJson.h"

namespace QK::Boot::Config
{
    struct StagedEarlyModule
    {
        char id[32] = {0};
        char type[32] = {0};
        char resolvedPath[320] = {0};
        bool required = false;

        // Layer-2 module trust metadata (from <modulePath>.module.json)
        char role[16] = {0};
        char status[16] = {0};
        char contentValidationCode[72] = {0}; // sha256 hex (64) + optional prefix + NUL
        bool hashRequired = false;
        bool signatureRequired = false;

        bool hasJson = false;
        QC::JSON::Value json{};

        void clear()
        {
            id[0] = 0;
            type[0] = 0;
            resolvedPath[0] = 0;
            required = false;

            role[0] = 0;
            status[0] = 0;
            contentValidationCode[0] = 0;
            hashRequired = false;
            signatureRequired = false;

            hasJson = false;
            json = QC::JSON::Value{};
        }
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
                modules[i].clear();
        }
    };

    void ClearCommittedEarlyConfig();
    void CommitEarlyConfig(StagedEarlyConfig &&Stage);
    const StagedEarlyConfig *GetCommittedEarlyConfig();
}
