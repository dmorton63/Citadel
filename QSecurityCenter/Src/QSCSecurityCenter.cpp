#include "QSCSecurityCenter.h"

#include "QQExecutor.h"

namespace QSC
{

    namespace
    {
        static QQ::FlowDecision securityCenterFlowPolicy(const QQ::TaskDescriptor &td)
        {
            const char *name = td.name;
            const char *origin = td.origin[0] ? td.origin : nullptr;
            const char *moduleId = td.moduleId[0] ? td.moduleId : nullptr;

            auto lower = [](char c) -> char {
                if (c >= 'A' && c <= 'Z')
                    return static_cast<char>(c - 'A' + 'a');
                return c;
            };

            auto containsIgnoreCase = [&](const char *hay, const char *needle) -> bool {
                if (!hay || !needle)
                    return false;
                for (const char *h = hay; *h; ++h)
                {
                    const char *a = h;
                    const char *b = needle;
                    while (*a && *b && (lower(*a) == lower(*b)))
                    {
                        ++a;
                        ++b;
                    }
                    if (*b == 0)
                        return true;
                }
                return false;
            };

            // Prefer moduleId/origin gates (real inputs).
            if (moduleId && containsIgnoreCase(moduleId, "cancel"))
                return QQ::FlowDecision{QQ::FlowDecisionType::IsolateCancel, 0};
            if (origin && containsIgnoreCase(origin, "cancel"))
                return QQ::FlowDecision{QQ::FlowDecisionType::IsolateCancel, 0};
            if (moduleId && containsIgnoreCase(moduleId, "suspend"))
                return QQ::FlowDecision{QQ::FlowDecisionType::IsolateSuspend, 0};
            if (origin && containsIgnoreCase(origin, "suspend"))
                return QQ::FlowDecision{QQ::FlowDecisionType::IsolateSuspend, 0};
            if (moduleId && containsIgnoreCase(moduleId, "delay"))
                return QQ::FlowDecision{QQ::FlowDecisionType::ThrottleDelay, 250};
            if (origin && containsIgnoreCase(origin, "delay"))
                return QQ::FlowDecision{QQ::FlowDecisionType::ThrottleDelay, 250};

            // Fallback to name substring (dev/testing).
            if (name && containsIgnoreCase(name, "cancel"))
                return QQ::FlowDecision{QQ::FlowDecisionType::IsolateCancel, 0};
            if (name && containsIgnoreCase(name, "suspend"))
                return QQ::FlowDecision{QQ::FlowDecisionType::IsolateSuspend, 0};
            if (name && containsIgnoreCase(name, "delay"))
                return QQ::FlowDecision{QQ::FlowDecisionType::ThrottleDelay, 250};

            return QQ::FlowDecision{};
        }
    }

    SecurityCenter::SecurityCenter()
        : m_initialized(false),
          m_mode(Mode::Bypass),
          m_flowPolicy(nullptr)
    {
    }

    SecurityCenter &SecurityCenter::instance()
    {
        static SecurityCenter inst;
        return inst;
    }

    void SecurityCenter::initialize(Mode mode)
    {
        m_mode = mode;
        m_flowPolicy = (m_mode == Mode::Enforce) ? &securityCenterFlowPolicy : nullptr;
        m_initialized = true;
    }

    void SecurityCenter::setFlowEnforcementEnabled(bool enabled)
    {
        m_mode = enabled ? Mode::Enforce : Mode::Bypass;
        m_flowPolicy = (m_mode == Mode::Enforce) ? &securityCenterFlowPolicy : nullptr;
    }

    const char *modeName(Mode mode)
    {
        switch (mode)
        {
        case Mode::Bypass:
            return "BYPASS";
        case Mode::Enforce:
            return "ENFORCE";
        default:
            return "UNKNOWN";
        }
    }

} // namespace QSC
