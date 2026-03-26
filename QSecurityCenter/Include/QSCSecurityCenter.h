#pragma once

#include "QCTypes.h"

#include "QQFlowPolicy.h"

namespace QSC
{

    enum class Mode : QC::u8
    {
        Bypass,
        Enforce
    };

    class SecurityCenter
    {
    public:
        static SecurityCenter &instance();

        void initialize(Mode mode);

        // Runtime flow governance toggle (MVP).
        void setFlowEnforcementEnabled(bool enabled);
        bool flowEnforcementEnabled() const { return m_mode == Mode::Enforce; }

        bool initialized() const { return m_initialized; }
        Mode mode() const { return m_mode; }

        // Flow policy used by QQ::Executor.
        QQ::FlowPolicyFn flowPolicy() const { return m_flowPolicy; }

    private:
        SecurityCenter();

        bool m_initialized;
        Mode m_mode;
        QQ::FlowPolicyFn m_flowPolicy;
    };

    const char *modeName(Mode mode);

} // namespace QSC
