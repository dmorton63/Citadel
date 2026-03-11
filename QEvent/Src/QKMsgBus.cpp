#include "QKMsgBus.h"

#include "QKEventManager.h"

namespace QK
{
    namespace Msg
    {
        namespace
        {
            inline bool isMsgBusEvent(const QK::Event::Event &event)
            {
                if (event.category() != QK::Event::Category::Custom)
                    return false;

                const QC::u16 t = static_cast<QC::u16>(event.type());
                const QC::u16 expected = static_cast<QC::u16>(QK::Event::Type::CustomBase) + CustomType;
                return t == expected;
            }
        }

        Bus &Bus::instance()
        {
            static Bus bus;
            return bus;
        }

        Bus::Bus()
        {
            for (QC::usize i = 0; i < MaxSubs; ++i)
            {
                m_subs[i] = Sub{};
            }

            ensureRegistered();
        }

        void Bus::ensureRegistered()
        {
            if (m_receiverId != QK::Event::InvalidListenerId)
            {
                return;
            }

            auto &eventMgr = QK::Event::EventManager::instance();
            if (!eventMgr.isInitialized())
            {
                return;
            }

            m_receiverId = eventMgr.addReceiver(this);
        }

        SubscriptionId Bus::subscribe(QC::u32 topic, Handler handler, void *userData)
        {
            ensureRegistered();
            if (!handler || topic == 0)
                return 0;

            for (QC::usize i = 0; i < MaxSubs; ++i)
            {
                if (!m_subs[i].used)
                {
                    const SubscriptionId id = m_nextId++;
                    m_subs[i].used = true;
                    m_subs[i].id = id;
                    m_subs[i].topic = topic;
                    m_subs[i].handler = handler;
                    m_subs[i].userData = userData;
                    return id;
                }
            }

            return 0;
        }

        bool Bus::unsubscribe(SubscriptionId id)
        {
            if (id == 0)
                return false;

            for (QC::usize i = 0; i < MaxSubs; ++i)
            {
                if (m_subs[i].used && m_subs[i].id == id)
                {
                    m_subs[i] = Sub{};
                    return true;
                }
            }

            return false;
        }

        bool Bus::publish(Envelope *env)
        {
            ensureRegistered();
            if (!env || env->topic == 0)
                return false;

            // The queued event owns one ref until delivery finishes.
            retain(env);

            bool ok = QK::Event::EventManager::instance().postCustomEvent(
                CustomType,
                static_cast<QC::u64>(env->topic),
                env->correlationId,
                env);

            if (!ok)
            {
                release(env);
                return false;
            }

            return true;
        }

        bool Bus::onEvent(const QK::Event::Event &event)
        {
            if (!isMsgBusEvent(event))
            {
                return false;
            }

            Envelope *env = static_cast<Envelope *>(event.data.custom.userData);
            if (!env)
            {
                return true;
            }

            // For convenience, reflect event params into the envelope as well.
            env->topic = static_cast<QC::u32>(event.data.custom.param1);
            env->correlationId = event.data.custom.param2;

            for (QC::usize i = 0; i < MaxSubs; ++i)
            {
                if (!m_subs[i].used)
                    continue;

                if (m_subs[i].topic != env->topic)
                    continue;

                if (m_subs[i].handler)
                {
                    m_subs[i].handler(env, m_subs[i].userData);
                }
            }

            // Release the queue's reference.
            release(env);
            return true;
        }

    } // namespace Msg
} // namespace QK
