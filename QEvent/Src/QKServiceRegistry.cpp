#include "QKServiceRegistry.h"

#include "QCString.h"

namespace QK
{
    namespace Svc
    {

        Registry &Registry::instance()
        {
            static Registry reg;
            return reg;
        }

        void Registry::initialize()
        {
            if (m_initialized)
            {
                return;
            }

            for (QC::usize i = 0; i < MaxServices; ++i)
            {
                m_entries[i] = Entry{};
            }

            m_subId = QK::Msg::Bus::instance().subscribe(QK::Msg::Topic::SvcMsg, &Registry::onSvcMsg, this);
            m_initialized = true;
        }

        QC::u32 Registry::nameHash(const char *name)
        {
            if (!name)
                return 0;

            // FNV-1a 32-bit
            QC::u32 h = 2166136261u;
            for (const unsigned char *p = reinterpret_cast<const unsigned char *>(name); *p; ++p)
            {
                h ^= static_cast<QC::u32>(*p);
                h *= 16777619u;
            }
            return h;
        }

        ServiceId Registry::registerService(const char *name, ServiceHandler handler, void *userData)
        {
            if (!name || !handler)
                return 0;

            if (!m_initialized)
                initialize();

            const QC::u32 h = nameHash(name);
            if (h == 0)
                return 0;

            // Reject duplicates (same hash) to avoid ambiguity.
            for (QC::usize i = 0; i < MaxServices; ++i)
            {
                if (m_entries[i].used && m_entries[i].hash == h)
                {
                    return 0;
                }
            }

            for (QC::usize i = 0; i < MaxServices; ++i)
            {
                if (!m_entries[i].used)
                {
                    const ServiceId id = m_nextId++;
                    m_entries[i].used = true;
                    m_entries[i].id = id;
                    m_entries[i].hash = h;
                    m_entries[i].handler = handler;
                    m_entries[i].userData = userData;

                    // Store name for debugging only (truncate).
                    QC::String::strncpy(m_entries[i].name, name, sizeof(m_entries[i].name) - 1);
                    m_entries[i].name[sizeof(m_entries[i].name) - 1] = '\0';

                    return id;
                }
            }

            return 0;
        }

        bool Registry::unregisterService(ServiceId id)
        {
            if (id == 0)
                return false;

            for (QC::usize i = 0; i < MaxServices; ++i)
            {
                if (m_entries[i].used && m_entries[i].id == id)
                {
                    m_entries[i] = Entry{};
                    return true;
                }
            }

            return false;
        }

        bool Registry::sendTo(const char *name, QK::Msg::Envelope *env)
        {
            if (!name || !env)
                return false;

            if (!m_initialized)
                initialize();

            const QC::u32 h = nameHash(name);
            if (h == 0)
                return false;

            env->topic = QK::Msg::Topic::SvcMsg;
            env->targetId = h;
            return QK::Msg::Bus::instance().publish(env);
        }

        void Registry::onSvcMsg(QK::Msg::Envelope *env, void *userData)
        {
            Registry *self = static_cast<Registry *>(userData);
            if (!self || !env)
                return;

            const QC::u32 target = env->targetId;
            if (target == 0)
                return;

            for (QC::usize i = 0; i < MaxServices; ++i)
            {
                Entry &e = self->m_entries[i];
                if (!e.used)
                    continue;
                if (e.hash != target)
                    continue;

                if (e.handler)
                {
                    e.handler(env, e.userData);
                }
                return;
            }
        }

    } // namespace Svc
} // namespace QK
