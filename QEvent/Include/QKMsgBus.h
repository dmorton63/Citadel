#pragma once

#include "QCTypes.h"
#include "QKEventListener.h"
#include "QKEventTypes.h"

#include "QKEventManager.h"

namespace QK
{
    namespace Msg
    {
        // A tiny message bus built on QEvent custom events.
        // This is intended for in-kernel routing (UI <-> services) and is not a userspace IPC.

        constexpr QC::u16 CustomType = 1; // QK::Event::Type::CustomBase + 1

        constexpr QC::u32 fourCC(char a, char b, char c, char d)
        {
            return (static_cast<QC::u32>(static_cast<QC::u8>(a)) << 24) |
                   (static_cast<QC::u32>(static_cast<QC::u8>(b)) << 16) |
                   (static_cast<QC::u32>(static_cast<QC::u8>(c)) << 8) |
                   (static_cast<QC::u32>(static_cast<QC::u8>(d)));
        }

        // Common topics (extend freely).
        namespace Topic
        {
            constexpr QC::u32 CmdExec = fourCC('C', 'M', 'D', '0');
            constexpr QC::u32 CmdOut = fourCC('C', 'M', 'D', '1');
            constexpr QC::u32 CmdDone = fourCC('C', 'M', 'D', '2');

            constexpr QC::u32 WinMsg = fourCC('W', 'M', 'S', 'G');
            constexpr QC::u32 SvcMsg = fourCC('S', 'V', 'C', 'M');
        }

        struct Envelope
        {
            QC::u32 topic = 0;
            QC::u32 senderId = 0;
            QC::u32 targetId = 0; // 0 = broadcast
            QC::u32 flags = 0;

            QC::u64 correlationId = 0;
            QC::u64 param1 = 0;
            QC::u64 param2 = 0;

            void *payload = nullptr;
            void (*destroyPayload)(void *) = nullptr;

            QC::u32 refCount = 1;
        };

        inline Envelope *retain(Envelope *env)
        {
            if (env)
            {
                env->refCount++;
            }
            return env;
        }

        inline void release(Envelope *env)
        {
            if (!env)
            {
                return;
            }

            if (env->refCount > 1)
            {
                env->refCount--;
                return;
            }

            if (env->destroyPayload && env->payload)
            {
                env->destroyPayload(env->payload);
            }

            operator delete(env);
        }

        using Handler = void (*)(Envelope *env, void *userData);
        using SubscriptionId = QC::u32;

        class Bus : public QK::Event::IEventReceiver
        {
        public:
            static Bus &instance();

            // Subscriptions are lightweight and intended to be stable.
            // Callbacks run on the event-processing thread (kernel main loop).
            SubscriptionId subscribe(QC::u32 topic, Handler handler, void *userData);
            bool unsubscribe(SubscriptionId id);

            // Publishes a message. Bus takes one reference and releases after delivery.
            bool publish(Envelope *env);

            // QK::Event::IEventReceiver
            bool onEvent(const QK::Event::Event &event) override;
            QK::Event::Category getEventMask() const override { return QK::Event::Category::Custom; }

            bool isRegistered() const { return m_receiverId != QK::Event::InvalidListenerId; }

        private:
            Bus();

            void ensureRegistered();

            struct Sub
            {
                SubscriptionId id = 0;
                QC::u32 topic = 0;
                Handler handler = nullptr;
                void *userData = nullptr;
                bool used = false;
            };

            static constexpr QC::usize MaxSubs = 64;
            Sub m_subs[MaxSubs];
            SubscriptionId m_nextId = 1;

            QK::Event::ListenerId m_receiverId = QK::Event::InvalidListenerId;
        };

        inline Envelope *makeEnvelope(QC::u32 topic, QC::u64 correlationId = 0)
        {
            Envelope *env = static_cast<Envelope *>(operator new(sizeof(Envelope)));
            *env = Envelope{};
            env->topic = topic;
            env->correlationId = correlationId;
            env->refCount = 1;
            return env;
        }

    } // namespace Msg
} // namespace QK
