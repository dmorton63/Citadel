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

        // Provision/load SST after persistent /system mount.
        QC::Status ensureSst();

        // Runtime flow governance toggle (MVP).
        void setFlowEnforcementEnabled(bool enabled);
        bool flowEnforcementEnabled() const { return m_mode == Mode::Enforce; }

        bool initialized() const { return m_initialized; }
        Mode mode() const { return m_mode; }
        bool bypassEnabled() const { return m_mode == Mode::Bypass; }

        // Owner credentials (minimal v1, single-owner).
        // Persisted via SecureStore sealed blobs; no DB.
        QC::Status ownerEnroll(const char *username, const char *secret);
        QC::Status ownerUnlock(const char *username, const char *secret);
        void ownerLock();
        bool ownerIsEnrolled() const;
        bool ownerUnlocked() const { return m_ownerUnlocked; }
        QC::u32 ownerUnlockBackoffMs() const { return m_ownerBackoffMs; }

        static const char *modeName(Mode mode);

    private:
        SecurityCenter();

        bool m_initialized;
        Mode m_mode;

        // Owner auth session state.
        bool m_ownerUnlocked = false;
        QC::u32 m_ownerFailCount = 0;
        QC::u32 m_ownerBackoffMs = 0;
    };

} // namespace QK
