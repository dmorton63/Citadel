#pragma once

// QEvent - Central Event Manager
// Namespace: QK::Event

#include "QCTypes.h"
#include "QKEventTypes.h"
#include "QKEventQueue.h"
#include "QKEventListener.h"

namespace QK
{
    namespace Event
    {

        /// Central event manager - singleton
        /// Handles event queuing, dispatch, and listener management
        class EventManager
        {
        public:
            /// Maximum number of registered listeners
            static constexpr QC::usize MaxListeners = 64;

            /// Get the singleton instance
            static EventManager &instance();

            // Non-copyable singleton
            EventManager(const EventManager &) = delete;
            EventManager &operator=(const EventManager &) = delete;

            /// Initialize the event system
            void initialize();

            /// Shutdown the event system
            void shutdown();

            /// Check if the event system is initialized
            bool isInitialized() const { return m_initialized; }

            // ==================== Event Posting ====================

            /// Post an event to the queue
            /// @param event The event to post
            /// @return true if the event was queued
            bool postEvent(const Event &event);

            /// Post an event and dispatch immediately (sync)
            /// @param event The event to dispatch
            void dispatchImmediate(const Event &event);

            /// Create and post a key event
            void postKeyEvent(Type type, QC::u8 scancode, QC::u8 keycode,
                              char character, Modifiers mods, bool isRepeat = false);

            /// Create and post a mouse move event
            void postMouseMove(QC::i32 x, QC::i32 y, QC::i32 deltaX, QC::i32 deltaY);

            /// Create and post a mouse button event
            void postMouseButton(Type type, MouseButton button,
                                 QC::i32 x, QC::i32 y, Modifiers mods);

            /// Create and post a mouse scroll event
            void postMouseScroll(QC::i32 delta, QC::i32 x, QC::i32 y);

            /// Create and post a timer event
            void postTimerEvent(QC::u32 timerId, QC::u64 elapsedMs, QC::u64 intervalMs);

            /// Create and post a window event
            void postWindowEvent(Type type, QC::u32 windowId,
                                 QC::i32 x, QC::i32 y, QC::u32 w, QC::u32 h);

            /// Create and post a custom event
            bool postCustomEvent(QC::u16 customType, QC::u64 param1 = 0,
                                 QC::u64 param2 = 0, void *userData = nullptr);

            /// Create and post a shutdown lifecycle event
            void postShutdownEvent(Type type, QC::u32 reasonCode,
                                   void *context = nullptr,
                                   Priority priority = Priority::High);

            // ==================== Event Processing ====================

            /// Process all pending events in the queue
            /// @param maxEvents Maximum events to process (0 = all)
            /// @return Number of events processed
            QC::usize processEvents(QC::usize maxEvents = 0);

            /// Process events until the queue is empty or timeout
            /// @param timeoutMs Maximum time to spend processing
            /// @return Number of events processed
            QC::usize processEventsUntil(QC::u64 timeoutMs);

            /// Check if there are pending events
            bool hasPendingEvents() const;

            /// Get the number of pending events
            QC::usize pendingEventCount() const;

            /// Clear all pending events
            void clearEvents();

            /// Clear events of a specific type
            void clearEventsOfType(Type type);

            // ==================== Listener Management ====================

            /// Register an event listener
            /// @param listener The listener configuration
            /// @return Unique listener ID (0 if registration failed)
            ListenerId addListener(const EventListener &listener);

            /// Register a listener for a specific event type
            ListenerId addListener(Type type, EventHandler handler, void *userData = nullptr);

            /// Register a listener for an event category
            ListenerId addListener(Category category, EventHandler handler, void *userData = nullptr);

            /// Register a class-based event receiver
            ListenerId addReceiver(IEventReceiver *receiver);

            /// Remove a listener by ID
            void removeListener(ListenerId id);

            /// Remove all listeners for a specific handler
            void removeListenersForHandler(EventHandler handler);

            /// Enable/disable a listener
            void setListenerEnabled(ListenerId id, bool enabled);

            /// Check if a listener exists
            bool hasListener(ListenerId id) const;

            /// Get the number of registered listeners
            QC::usize listenerCount() const { return m_listenerCount; }

            // ==================== Statistics ====================

            /// Get total events dispatched since initialization
            QC::u64 totalEventsDispatched() const { return m_totalDispatched; }

            /// Get total events dropped (queue full)
            QC::u64 totalEventsDropped() const { return m_totalDropped; }

            /// Reset statistics
            void resetStats();

        private:
            EventManager() = default;
            ~EventManager() = default;

            /// Dispatch a single event to all matching listeners
            void dispatchEvent(const Event &event);

            /// Get the current timestamp
            QC::u64 getTimestamp() const;

            /// Generate next listener ID
            ListenerId nextListenerId();

            /// Find listener by ID
            EventListener *findListener(ListenerId id);
            const EventListener *findListener(ListenerId id) const;

            // Event queues
            EventQueue m_mainQueue;
            ImmediateQueue m_immediateQueue;

            // Listener storage
            EventListener m_listeners[MaxListeners];
            QC::usize m_listenerCount = 0;
            ListenerId m_nextListenerId = 1;

            // Receiver storage (for class-based listeners)
            IEventReceiver *m_receivers[MaxListeners] = {};
            QC::usize m_receiverCount = 0;

            // Statistics
            QC::u64 m_totalDispatched = 0;
            QC::u64 m_totalDropped = 0;

            bool m_initialized = false;
            bool m_dispatching = false; // Prevent re-entrancy issues
        };

// Convenience macros for common event posting
#define QK_POST_KEY_DOWN(scancode, keycode, ch, mods) \
    QK::Event::EventManager::instance().postKeyEvent( \
        QK::Event::Type::KeyDown, scancode, keycode, ch, mods)

#define QK_POST_KEY_UP(scancode, keycode, ch, mods)   \
    QK::Event::EventManager::instance().postKeyEvent( \
        QK::Event::Type::KeyUp, scancode, keycode, ch, mods)

#define QK_POST_MOUSE_MOVE(x, y, dx, dy) \
    QK::Event::EventManager::instance().postMouseMove(x, y, dx, dy)

#define QK_POST_MOUSE_BUTTON(type, button, x, y, mods) \
    QK::Event::EventManager::instance().postMouseButton(type, button, x, y, mods)

    } // namespace Event
} // namespace QK
