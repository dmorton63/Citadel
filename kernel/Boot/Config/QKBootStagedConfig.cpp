#include "QKBootStagedConfig.h"

namespace QK::Boot::Config
{
    namespace
    {
        bool g_HaveCommitted = false;
        StagedEarlyConfig g_Committed{};
    }

    void ClearCommittedEarlyConfig()
    {
        g_Committed.clear();
        g_HaveCommitted = false;
    }

    void CommitEarlyConfig(StagedEarlyConfig &&Stage)
    {
        g_Committed = static_cast<StagedEarlyConfig &&>(Stage);
        g_HaveCommitted = true;
    }

    const StagedEarlyConfig *GetCommittedEarlyConfig()
    {
        return g_HaveCommitted ? &g_Committed : nullptr;
    }
}
