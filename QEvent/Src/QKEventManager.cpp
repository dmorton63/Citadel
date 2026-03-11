// QEvent - Event Manager Implementation
// QKEventManager.cpp

#include "QKEventManager.h"
#include "QCLogger.h"
#include "QCBuiltins.h"

namespace QK
{
    namespace Event
    {

        // ==================== Singleton ====================

        EventManager &EventManager::instance()
        {
            static EventManager s_instance;
            return s_instance;
        }

        // ==================== Initialization ====================

        void EventManager::initialize()
        {
            if (m_initialized)
            {
                return;
            }

            QC_LOG_INFO("QKEvent", "Initializing Event Manager");

            m_mainQueue.initialize();
            m_immediateQueue.initialize();

            m_listenerCount = 0;
            m_nextListenerId = 1;
            m_receiverCount = 0;

            m_totalDispatched = 0;
            m_totalDropped = 0;
            m_dispatching = false;

            // Clear listener array - direct member init (copy assignment crashes in freestanding env)
            for (QC::usize i = 0; i < MaxListeners; i++)
            {
                m_listeners[i].id = 0;
                m_listeners[i].eventType = Type::None;
                m_listeners[i].categoryMask = Category::All;
                m_listeners[i].minPriority = Priority::Low;
                m_listeners[i].handler = nullptr;
                m_listeners[i].userData = nullptr;
                m_listeners[i].enabled = true;
                m_receivers[i] = nullptr;
            }

            m_initialized = true;
            QC_LOG_INFO("QKEvent", "Event Manager initialized");
        }

        void EventManager::shutdown()
        {
            if (!m_initialized)
            {
                return;
            }

            QC_LOG_INFO("QKEvent", "Shutting down Event Manager");

            clearEvents();
            m_listenerCount = 0;
            m_receiverCount = 0;
            m_initialized = false;
        }

        // ==================== Event Posting ====================

        bool EventManager::postEvent(const Event &event)
        {
            if (!m_initialized)
            {
                return false;
            }

            // Immediate priority bypasses the queue
            if (event.priority() == Priority::Immediate)
            {
                if (!m_immediateQueue.push(event))
                {
                    m_totalDropped++;
                    return false;
                }
                return true;
            }

            if (!m_mainQueue.push(event))
            {
                m_totalDropped++;
                QC_LOG_DEBUG("QKEvent", "Event dropped - queue full");
                return false;
            }

            return true;
        }

        void EventManager::dispatchImmediate(const Event &event)
        {
            if (!m_initialized)
            {
                return;
            }

            dispatchEvent(event);
        }

        void EventManager::postKeyEvent(Type type, QC::u8 scancode, QC::u8 keycode,
                                        char character, Modifiers mods, bool isRepeat)
        {
            Event event;
            event.data.key.type = type;
            event.data.key.category = Category::Input;
            event.data.key.priority = Priority::High;
            event.data.key.timestamp = getTimestamp();
            event.data.key.scancode = scancode;
            event.data.key.keycode = keycode;
            event.data.key.character = character;
            event.data.key.modifiers = mods;
            event.data.key.isRepeat = isRepeat;

            postEvent(event);
        }

        void EventManager::postMouseMove(QC::i32 x, QC::i32 y, QC::i32 deltaX, QC::i32 deltaY)
        {
            Event event;
            event.data.mouse.type = Type::MouseMove;
            event.data.mouse.category = Category::Input;
            event.data.mouse.priority = Priority::Normal;
            event.data.mouse.timestamp = getTimestamp();
            event.data.mouse.x = x;
            event.data.mouse.y = y;
            event.data.mouse.deltaX = deltaX;
            event.data.mouse.deltaY = deltaY;

            postEvent(event);
        }

        void EventManager::postMouseButton(Type type, MouseButton button,
                                           QC::i32 x, QC::i32 y, Modifiers mods)
        {
            Event event;
            event.data.mouse.type = type;
            event.data.mouse.category = Category::Input;
            event.data.mouse.priority = Priority::High;
            event.data.mouse.timestamp = getTimestamp();
            event.data.mouse.x = x;
            event.data.mouse.y = y;
            event.data.mouse.button = button;
            event.data.mouse.modifiers = mods;

            postEvent(event);
        }

        void EventManager::postMouseScroll(QC::i32 delta, QC::i32 x, QC::i32 y)
        {
            Event event;
            event.data.mouse.type = Type::MouseScroll;
            event.data.mouse.category = Category::Input;
            event.data.mouse.priority = Priority::Normal;
            event.data.mouse.timestamp = getTimestamp();
            event.data.mouse.x = x;
            event.data.mouse.y = y;
            event.data.mouse.scrollDelta = delta;

            postEvent(event);
        }

        void EventManager::postTimerEvent(QC::u32 timerId, QC::u64 elapsedMs, QC::u64 intervalMs)
        {
            Event event;
            event.data.timer.type = Type::Timer;
            event.data.timer.category = Category::System;
            event.data.timer.priority = Priority::Normal;
            event.data.timer.timestamp = getTimestamp();
            event.data.timer.timerId = timerId;
            event.data.timer.elapsedMs = elapsedMs;
            event.data.timer.intervalMs = intervalMs;

            postEvent(event);
        }

        void EventManager::postWindowEvent(Type type, QC::u32 windowId,
                                           QC::i32 x, QC::i32 y, QC::u32 w, QC::u32 h)
        {
            Event event;
            event.data.window.type = type;
            event.data.window.category = Category::Window;
            event.data.window.priority = Priority::Normal;
            event.data.window.timestamp = getTimestamp();
            event.data.window.windowId = windowId;
            event.data.window.x = x;
            event.data.window.y = y;
            event.data.window.width = w;
            event.data.window.height = h;

            postEvent(event);
        }

        bool EventManager::postCustomEvent(QC::u16 customType, QC::u64 param1,
                                           QC::u64 param2, void *userData)
        {
            Event event;
            event.data.custom.type = static_cast<Type>(static_cast<QC::u16>(Type::CustomBase) + customType);
            event.data.custom.category = Category::Custom;
            event.data.custom.priority = Priority::Normal;
            event.data.custom.timestamp = getTimestamp();
            event.data.custom.param1 = param1;
            event.data.custom.param2 = param2;
            event.data.custom.userData = userData;

            return postEvent(event);
        }

        void EventManager::postShutdownEvent(Type type, QC::u32 reasonCode,
                                             void *context, Priority priority)
        {
            if (!m_initialized)
            {
                return;
            }

            Event event;
            event.data.shutdown.type = type;
            event.data.shutdown.category = Category::System;
            event.data.shutdown.priority = priority;
            event.data.shutdown.timestamp = getTimestamp();
            event.data.shutdown.reasonCode = reasonCode;
            event.data.shutdown.context = context;

            postEvent(event);
        }

        // ==================== Event Processing ====================

        QC::usize EventManager::processEvents(QC::usize maxEvents)
        {
            if (!m_initialized || m_dispatching)
            {
                return 0;
            }

            m_dispatching = true;
            QC::usize processed = 0;

            // Process immediate queue first
            Event event;
            while (m_immediateQueue.pop(event))
            {
                dispatchEvent(event);
                processed++;
            }

            // Process main queue
            while (m_mainQueue.pop(event))
            {
                dispatchEvent(event);
                processed++;

                if (maxEvents > 0 && processed >= maxEvents)
                {
                    break;
                }
            }

            m_dispatching = false;
            return processed;
        }

        QC::usize EventManager::processEventsUntil(QC::u64 timeoutMs)
        {
            // TODO: Implement proper timing when timer is available
            // For now, just process all events
            return processEvents(0);
        }

        bool EventManager::hasPendingEvents() const
        {
            return !m_mainQueue.isEmpty() || !m_immediateQueue.isEmpty();
        }

        QC::usize EventManager::pendingEventCount() const
        {
            return m_mainQueue.count() + (m_immediateQueue.isEmpty() ? 0 : 1);
        }

        void EventManager::clearEvents()
        {
            m_mainQueue.clear();
            m_immediateQueue.clear();
        }

        void EventManager::clearEventsOfType(Type type)
        {
            m_mainQueue.clearType(type);
        }

        // ==================== Listener Management ====================

        ListenerId EventManager::addListener(const EventListener &listener)
        {
            if (!m_initialized || m_listenerCount >= MaxListeners)
            {
                return InvalidListenerId;
            }

            // Find empty slot
            for (QC::usize i = 0; i < MaxListeners; i++)
            {
                if (m_listeners[i].id == InvalidListenerId)
                {
                    m_listeners[i] = listener;
                    m_listeners[i].id = nextListenerId();
                    m_listenerCount++;
                    return m_listeners[i].id;
                }
            }

            return InvalidListenerId;
        }

        ListenerId EventManager::addListener(Type type, EventHandler handler, void *userData)
        {
            EventListener listener(type, handler, userData);
            return addListener(listener);
        }

        ListenerId EventManager::addListener(Category category, EventHandler handler, void *userData)
        {
            EventListener listener(category, handler, userData);
            return addListener(listener);
        }

        ListenerId EventManager::addReceiver(IEventReceiver *receiver)
        {
            if (!m_initialized || receiver == nullptr || m_receiverCount >= MaxListeners)
            {
                return InvalidListenerId;
            }

            // Find empty slot
            for (QC::usize i = 0; i < MaxListeners; i++)
            {
                if (m_receivers[i] == nullptr)
                {
                    m_receivers[i] = receiver;
                    m_receiverCount++;
                    // Return a pseudo-ID for receivers (high bit set)
                    return static_cast<ListenerId>(0x80000000 | i);
                }
            }

            return InvalidListenerId;
        }

        void EventManager::removeListener(ListenerId id)
        {
            if (id == InvalidListenerId)
            {
                return;
            }

            // Check if it's a receiver ID
            if (id & 0x80000000)
            {
                QC::usize idx = id & 0x7FFFFFFF;
                if (idx < MaxListeners && m_receivers[idx] != nullptr)
                {
                    m_receivers[idx] = nullptr;
                    m_receiverCount--;
                }
                return;
            }

            // Find and remove listener
            for (QC::usize i = 0; i < MaxListeners; i++)
            {
                if (m_listeners[i].id == id)
                {
                    m_listeners[i] = EventListener();
                    m_listenerCount--;
                    return;
                }
            }
        }

        void EventManager::removeListenersForHandler(EventHandler handler)
        {
            for (QC::usize i = 0; i < MaxListeners; i++)
            {
                if (m_listeners[i].handler == handler)
                {
                    m_listeners[i] = EventListener();
                    m_listenerCount--;
                }
            }
        }

        void EventManager::setListenerEnabled(ListenerId id, bool enabled)
        {
            EventListener *listener = findListener(id);
            if (listener)
            {
                listener->enabled = enabled;
            }
        }

        bool EventManager::hasListener(ListenerId id) const
        {
            return findListener(id) != nullptr;
        }

        // ==================== Statistics ====================

        void EventManager::resetStats()
        {
            m_totalDispatched = 0;
            m_totalDropped = 0;
        }

        // ==================== Private Methods ====================

        void EventManager::dispatchEvent(const Event &event)
        {
            m_totalDispatched++;

            // Dispatch to function listeners
            for (QC::usize i = 0; i < MaxListeners; i++)
            {
                const EventListener &listener = m_listeners[i];

                if (listener.id == InvalidListenerId || !listener.enabled)
                {
                    continue;
                }

                // Check type filter
                if (listener.eventType != Type::None && listener.eventType != event.type())
                {
                    continue;
                }

                // Check category filter
                if (!hasCategory(event.category(), listener.categoryMask))
                {
                    continue;
                }

                // Check priority filter
                if (event.priority() < listener.minPriority)
                {
                    continue;
                }

                // Call handler
                if (listener.handler)
                {
                    bool handled = listener.handler(event, listener.userData);
                    if (handled)
                    {
                        return; // Event consumed
                    }
                }
            }

            // Dispatch to class receivers
            for (QC::usize i = 0; i < MaxListeners; i++)
            {
                IEventReceiver *receiver = m_receivers[i];

                if (receiver == nullptr || !receiver->isEnabled())
                {
                    continue;
                }

                // Check category filter
                if (!hasCategory(event.category(), receiver->getEventMask()))
                {
                    continue;
                }

                bool handled = receiver->onEvent(event);
                if (handled)
                {
                    return; // Event consumed
                }
            }
        }

        QC::u64 EventManager::getTimestamp() const
        {
            // TODO: Get actual system tick count when timer is available
            static QC::u64 s_counter = 0;
            return ++s_counter;
        }

        ListenerId EventManager::nextListenerId()
        {
            return m_nextListenerId++;
        }

        EventListener *EventManager::findListener(ListenerId id)
        {
            if (id == InvalidListenerId || (id & 0x80000000))
            {
                return nullptr;
            }

            for (QC::usize i = 0; i < MaxListeners; i++)
            {
                if (m_listeners[i].id == id)
                {
                    return &m_listeners[i];
                }
            }

            return nullptr;
        }

        const EventListener *EventManager::findListener(ListenerId id) const
        {
            if (id == InvalidListenerId || (id & 0x80000000))
            {
                return nullptr;
            }

            for (QC::usize i = 0; i < MaxListeners; i++)
            {
                if (m_listeners[i].id == id)
                {
                    return &m_listeners[i];
                }
            }

            return nullptr;
        }

    } // namespace Event
} // namespace QK
