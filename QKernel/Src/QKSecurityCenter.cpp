#include "QKSecurityCenter.h"

namespace QK
{

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
        m_initialized = true;
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
