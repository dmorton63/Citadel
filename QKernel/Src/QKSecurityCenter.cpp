#include "QKSecurityCenter.h"

#include "QQExecutor.h"

#include "QSCSecurityCenter.h"

namespace QK
{

    namespace
    {
        static QSC::Mode toQscMode(SecurityCenter::Mode m)
        {
            return (m == SecurityCenter::Mode::Enforce) ? QSC::Mode::Enforce : QSC::Mode::Bypass;
        }
    }

    SecurityCenter::SecurityCenter()
        : m_initialized(false),
          m_mode(Mode::Bypass)
    {
    }

    SecurityCenter &SecurityCenter::instance()
    {
        static SecurityCenter sc;
        return sc;
    }

    void SecurityCenter::initialize(Mode mode)
    {
        m_mode = mode;

        // Delegate policy ownership to QSC.
        QSC::SecurityCenter::instance().initialize(toQscMode(m_mode));
        QQ::Executor::instance().setFlowPolicy(QSC::SecurityCenter::instance().flowPolicy());

        m_initialized = true;
    }

    void SecurityCenter::setFlowEnforcementEnabled(bool enabled)
    {
        m_mode = enabled ? Mode::Enforce : Mode::Bypass;
        QSC::SecurityCenter::instance().setFlowEnforcementEnabled(enabled);
        QQ::Executor::instance().setFlowPolicy(QSC::SecurityCenter::instance().flowPolicy());
    }

    const char *SecurityCenter::modeName(Mode mode)
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

} // namespace QK
