#pragma once

#include "QCTypes.h"

namespace QQ
{
    struct TaskDescriptor;

    enum class FlowDecisionType : QC::u8
    {
        Allow = 0,
        ThrottleDelay = 1,
        IsolateSuspend = 2,
        IsolateCancel = 3,
    };

    struct FlowDecision
    {
        FlowDecisionType type = FlowDecisionType::Allow;
        QC::u64 throttleDelayMs = 0; // used when type==ThrottleDelay
    };

    using FlowPolicyFn = FlowDecision (*)(const TaskDescriptor &task);
}
