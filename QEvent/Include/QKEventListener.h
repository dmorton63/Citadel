#pragma once

// QEvent - Event Listener Interface
// Namespace: QK::Event

#include "QCTypes.h"
#include "QKEventTypes.h"

namespace QK
{
    namespace Event
    {

        /// Forward declaration
        class EventManager;

        /// Unique listener ID type
        using ListenerId = QC::u32;
        constexpr ListenerId InvalidListenerId = 0;

        /// Event handler function signature
        /// Returns true if the event was handled and should stop propagating
        using EventHandler = bool (*)(const Event &event, void *userData);

        /// Event listener registration structure
        struct EventListener
        {
            ListenerId id = InvalidListenerId;
            Type eventType = Type::None;           // Specific type, or None for all
            Category categoryMask = Category::All; // Filter by category
            Priority minPriority = Priority::Low;  // Minimum priority to receive
            EventHandler handler = nullptr;
            void *userData = nullptr;
            bool enabled = true;

            EventListener() = default;

            EventListener(EventHandler h, void *ud = nullptr)
                : handler(h), userData(ud) {}

            EventListener(Type t, EventHandler h, void *ud = nullptr)
                : eventType(t), handler(h), userData(ud) {}

            EventListener(Category c, EventHandler h, void *ud = nullptr)
                : categoryMask(c), handler(h), userData(ud) {}
        };

        /// Interface for classes that want to receive events
        class IEventReceiver
        {
        public:
            virtual ~IEventReceiver() = default;

            /// Called when an event is dispatched
            /// @param event The event data
            /// @return true if the event was handled and should stop propagating
            virtual bool onEvent(const Event &event) = 0;

            /// Get the category mask for events this receiver wants
            virtual Category getEventMask() const { return Category::All; }

            /// Check if this receiver is currently enabled
            virtual bool isEnabled() const { return true; }
        };

        /// RAII wrapper for listener registration
        class ScopedListener
        {
        public:
            ScopedListener() = default;

            ScopedListener(EventManager *manager, ListenerId id)
                : m_manager(manager), m_id(id) {}

            ~ScopedListener();

            // Move-only
            ScopedListener(ScopedListener &&other) noexcept
                : m_manager(other.m_manager), m_id(other.m_id)
            {
                other.m_manager = nullptr;
                other.m_id = InvalidListenerId;
            }

            ScopedListener &operator=(ScopedListener &&other) noexcept
            {
                if (this != &other)
                {
                    release();
                    m_manager = other.m_manager;
                    m_id = other.m_id;
                    other.m_manager = nullptr;
                    other.m_id = InvalidListenerId;
                }
                return *this;
            }

            // No copy
            ScopedListener(const ScopedListener &) = delete;
            ScopedListener &operator=(const ScopedListener &) = delete;

            ListenerId id() const { return m_id; }
            bool isValid() const { return m_id != InvalidListenerId; }
            void release();

        private:
            EventManager *m_manager = nullptr;
            ListenerId m_id = InvalidListenerId;
        };

    } // namespace Event
} // namespace QK
