#include "QWMessageBus.h"

#include "QWWindowManager.h"
#include "QWWindow.h"
#include "QKMsgBus.h"

namespace QW
{

    namespace
    {
        void onWinMsg(QK::Msg::Envelope *env, void *)
        {
            if (!env)
                return;

            Message msg{};
            msg.fromWindowId = env->senderId;
            msg.toWindowId = env->targetId;
            msg.msgId = static_cast<QC::u32>(env->param1);
            msg.flags = env->flags;
            msg.param1 = env->param2;
            msg.param2 = env->correlationId;
            msg.payload = env->payload;

            auto &wm = WindowManager::instance();

            if (msg.toWindowId != 0)
            {
                if (Window *target = wm.windowById(msg.toWindowId))
                {
                    target->handleMessage(msg);
                }
                return;
            }

            // Broadcast
            const QC::usize count = wm.windowCount();
            for (QC::usize i = 0; i < count; ++i)
            {
                if (Window *w = wm.windowAtIndex(i))
                {
                    w->handleMessage(msg);
                }
            }
        }
    }

    MessageBus &MessageBus::instance()
    {
        static MessageBus bus;
        return bus;
    }

    void MessageBus::initialize()
    {
        if (m_initialized)
            return;

        // Subscribe to window message topic.
        QK::Msg::Bus::instance().subscribe(QK::Msg::Topic::WinMsg, &onWinMsg, nullptr);
        m_initialized = true;
    }

    bool MessageBus::send(const Message &msg)
    {
        if (!m_initialized)
        {
            initialize();
        }

        QK::Msg::Envelope *env = QK::Msg::makeEnvelope(QK::Msg::Topic::WinMsg, 0);
        env->senderId = msg.fromWindowId;
        env->targetId = msg.toWindowId;
        env->flags = msg.flags;
        env->param1 = msg.msgId;
        env->param2 = msg.param1;
        env->correlationId = msg.param2;
        env->payload = msg.payload;
        env->destroyPayload = nullptr;

        const bool ok = QK::Msg::Bus::instance().publish(env);
        QK::Msg::release(env);
        return ok;
    }

} // namespace QW
