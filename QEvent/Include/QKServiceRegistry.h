#pragma once

#include "QCTypes.h"
#include "QKMsgBus.h"

namespace QK
{
    namespace Svc
    {
        using ServiceHandler = void (*)(QK::Msg::Envelope *env, void *userData);
        using ServiceId = QC::u32;

        class Registry
        {
        public:
            static Registry &instance();

            // Convenience aliases so callers can write Registry::ServiceId.
            using ServiceId = QK::Svc::ServiceId;
            using ServiceID = ServiceId;

            void initialize();

            // Register a service by human-readable name (e.g. "CommandProcessor").
            // Messages are delivered on Topic::SvcMsg where env->targetId == hash(name).
            ServiceId registerService(const char *name, ServiceHandler handler, void *userData);
            bool unregisterService(ServiceId id);

            // Send to a named service (name is hashed internally).
            bool sendTo(const char *name, QK::Msg::Envelope *env);

            // Hash helper (FNV-1a 32-bit)
            static QC::u32 nameHash(const char *name);

        private:
            Registry() = default;

            struct Entry
            {
                bool used = false;
                ServiceId id = 0;
                QC::u32 hash = 0;
                char name[48] = {0};
                ServiceHandler handler = nullptr;
                void *userData = nullptr;
            };

            static constexpr QC::usize MaxServices = 64;
            Entry m_entries[MaxServices];
            ServiceId m_nextId = 1;
            bool m_initialized = false;
            QK::Msg::SubscriptionId m_subId = 0;

            static void onSvcMsg(QK::Msg::Envelope *env, void *userData);
        };

    } // namespace Svc
} // namespace QK
