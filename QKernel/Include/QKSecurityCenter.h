#pragma once

#include "QCTypes.h"

namespace QK
{

    class SecurityCenter
    {
    public:
        enum class Mode : QC::u8
        {
            Bypass,
            Enforce
        };

        static SecurityCenter &instance();

        void initialize(Mode mode);

        bool initialized() const { return m_initialized; }
        Mode mode() const { return m_mode; }
        bool bypassEnabled() const { return m_mode == Mode::Bypass; }

        static const char *modeName(Mode mode);

    private:
        SecurityCenter();

        bool m_initialized;
        Mode m_mode;
    };

} // namespace QK
