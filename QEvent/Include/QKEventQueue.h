#pragma once

// QEvent - Event Queue Implementation
// Namespace: QK::Event

#include "QCTypes.h"
#include "QKEventTypes.h"

namespace QK
{
    namespace Event
    {

        /// Circular buffer event queue with priority support
        class EventQueue
        {
        public:
            /// Maximum number of events in the queue
            static constexpr QC::usize MaxEvents = 256;

            EventQueue() = default;
            ~EventQueue() = default;

            // Non-copyable
            EventQueue(const EventQueue &) = delete;
            EventQueue &operator=(const EventQueue &) = delete;

            /// Initialize the queue
            void initialize();

            /// Push an event to the queue
            /// @param event The event to push
            /// @return true if the event was added, false if queue is full
            bool push(const Event &event);

            /// Pop the next event from the queue
            /// @param outEvent Output parameter for the event
            /// @return true if an event was retrieved, false if queue is empty
            bool pop(Event &outEvent);

            /// Peek at the next event without removing it
            /// @param outEvent Output parameter for the event
            /// @return true if an event exists, false if queue is empty
            bool peek(Event &outEvent) const;

            /// Check if the queue is empty
            bool isEmpty() const { return m_count == 0; }

            /// Check if the queue is full
            bool isFull() const { return m_count >= MaxEvents; }

            /// Get the number of events in the queue
            QC::usize count() const { return m_count; }

            /// Clear all events from the queue
            void clear();

            /// Clear all events of a specific type
            void clearType(Type type);

            /// Clear all events in a specific category
            void clearCategory(Category category);

        private:
            /// Find the insertion index for priority ordering
            QC::usize findInsertIndex(Priority priority) const;

            Event m_events[MaxEvents];
            QC::usize m_head = 0;  // Read position
            QC::usize m_tail = 0;  // Write position
            QC::usize m_count = 0; // Current count
            bool m_initialized = false;
        };

        /// High-priority immediate event queue
        /// Events here are processed before the main queue
        class ImmediateQueue
        {
        public:
            static constexpr QC::usize MaxEvents = 16;

            ImmediateQueue() = default;

            void initialize();
            bool push(const Event &event);
            bool pop(Event &outEvent);
            bool isEmpty() const { return m_count == 0; }
            void clear();

        private:
            Event m_events[MaxEvents];
            QC::usize m_head = 0;
            QC::usize m_tail = 0;
            QC::usize m_count = 0;
            bool m_initialized = false;
        };

    } // namespace Event
} // namespace QK
