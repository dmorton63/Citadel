#include "QKBootConfigTier.h"

#include "QCString.h"

namespace QK::Boot::Config
{
    namespace
    {
        static ConfigTier g_Tier = ConfigTier::Unknown;
        static char g_Root[160] = {0};
    }

    void SetActiveConfigTier(ConfigTier tier, const char *root)
    {
        g_Tier = tier;
        g_Root[0] = 0;
        if (root && root[0])
        {
            QC::String::strncpy(g_Root, root, sizeof(g_Root));
            g_Root[sizeof(g_Root) - 1] = 0;
        }
    }

    ConfigTier GetActiveConfigTier()
    {
        return g_Tier;
    }

    const char *GetActiveConfigTierRoot()
    {
        return g_Root;
    }

    const char *GetActiveConfigTierName()
    {
        switch (g_Tier)
        {
        case ConfigTier::Production:
            return "production";
        case ConfigTier::Golden:
            return "golden";
        default:
            return "unknown";
        }
    }
}
