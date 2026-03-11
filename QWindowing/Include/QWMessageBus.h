#pragma once

#include "QCTypes.h"

namespace QW
{
    struct Message
    {
        QC::u32 fromWindowId = 0;
        QC::u32 toWindowId = 0; // 0 = broadcast
        QC::u32 msgId = 0;
        QC::u32 flags = 0;

        QC::u64 param1 = 0;
        QC::u64 param2 = 0;

        void *payload = nullptr;
    };

    class Window;

    using MessageHandler = bool (*)(Window *window, const Message &msg, void *userData);

    /// Window messaging layer (distinct from QEvent input/system events).
    /// Routes addressed messages between windows using windowId.
    class MessageBus
    {
    public:
        static MessageBus &instance();

        void initialize();

        // Send a message to a window (or broadcast if toWindowId==0).
        bool send(const Message &msg);

    private:
        MessageBus() = default;

        bool m_initialized = false;
    };

} // namespace QW
