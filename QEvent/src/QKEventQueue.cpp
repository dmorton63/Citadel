// QEvent - Event Queue Implementation
// QKEventQueue.cpp

#include "QKEventQueue.h"
#include "QCBuiltins.h"

namespace QK
{
    namespace Event
    {

        // ==================== EventQueue Implementation ====================

        void EventQueue::initialize()
        {
            m_head = 0;
            m_tail = 0;
            m_count = 0;
            m_initialized = true;
        }

        bool EventQueue::push(const Event &event)
        {
            if (!m_initialized || isFull())
            {
                return false;
            }

            // Coalesce consecutive mouse-move events: keep only the most recent.
            // This prevents input floods from building large backlogs of stale
            // cursor positions (which makes hover highlighting appear "behind").
            if (event.type() == Type::MouseMove && m_count > 0)
            {
                const QC::usize lastIdx = (m_tail == 0) ? (MaxEvents - 1) : (m_tail - 1);
                if (m_events[lastIdx].type() == Type::MouseMove)
                {
                    m_events[lastIdx] = event;
                    return true;
                }
            }

            // For priority queue, we insert in order
            // Higher priority events are processed first
            if (m_count == 0 || event.priority() <= Priority::Normal)
            {
                // Simple case: add to tail
                m_events[m_tail] = event;
                m_tail = (m_tail + 1) % MaxEvents;
                m_count++;
            }
            else
            {
                // Priority insertion: find correct position
                QC::usize insertIdx = findInsertIndex(event.priority());

                // Shift elements to make room
                if (insertIdx != m_tail)
                {
                    // Shift elements from insertIdx to tail
                    QC::usize current = m_tail;
                    while (current != insertIdx)
                    {
                        QC::usize prev = (current == 0) ? MaxEvents - 1 : current - 1;
                        m_events[current] = m_events[prev];
                        current = prev;
                    }
                }

                m_events[insertIdx] = event;
                m_tail = (m_tail + 1) % MaxEvents;
                m_count++;
            }

            return true;
        }

        bool EventQueue::pop(Event &outEvent)
        {
            if (!m_initialized || isEmpty())
            {
                return false;
            }

            outEvent = m_events[m_head];
            m_head = (m_head + 1) % MaxEvents;
            m_count--;

            return true;
        }

        bool EventQueue::peek(Event &outEvent) const
        {
            if (!m_initialized || isEmpty())
            {
                return false;
            }

            outEvent = m_events[m_head];
            return true;
        }

        void EventQueue::clear()
        {
            m_head = 0;
            m_tail = 0;
            m_count = 0;
        }

        void EventQueue::clearType(Type type)
        {
            if (!m_initialized || isEmpty())
            {
                return;
            }

            // Create a new queue without the specified type
            Event tempEvents[MaxEvents];
            QC::usize tempCount = 0;

            QC::usize current = m_head;
            for (QC::usize i = 0; i < m_count; i++)
            {
                if (m_events[current].type() != type)
                {
                    tempEvents[tempCount++] = m_events[current];
                }
                current = (current + 1) % MaxEvents;
            }

            // Copy back
            m_head = 0;
            m_tail = tempCount;
            m_count = tempCount;
            for (QC::usize i = 0; i < tempCount; i++)
            {
                m_events[i] = tempEvents[i];
            }
        }

        void EventQueue::clearCategory(Category category)
        {
            if (!m_initialized || isEmpty())
            {
                return;
            }

            Event tempEvents[MaxEvents];
            QC::usize tempCount = 0;

            QC::usize current = m_head;
            for (QC::usize i = 0; i < m_count; i++)
            {
                if (!hasCategory(m_events[current].category(), category))
                {
                    tempEvents[tempCount++] = m_events[current];
                }
                current = (current + 1) % MaxEvents;
            }

            m_head = 0;
            m_tail = tempCount;
            m_count = tempCount;
            for (QC::usize i = 0; i < tempCount; i++)
            {
                m_events[i] = tempEvents[i];
            }
        }

        QC::usize EventQueue::findInsertIndex(Priority priority) const
        {
            // Find first event with lower priority than the new one
            QC::usize current = m_head;
            for (QC::usize i = 0; i < m_count; i++)
            {
                if (m_events[current].priority() < priority)
                {
                    return current;
                }
                current = (current + 1) % MaxEvents;
            }
            return m_tail;
        }

        // ==================== ImmediateQueue Implementation ====================

        void ImmediateQueue::initialize()
        {
            m_head = 0;
            m_tail = 0;
            m_count = 0;
            m_initialized = true;
        }

        bool ImmediateQueue::push(const Event &event)
        {
            if (!m_initialized || m_count >= MaxEvents)
            {
                return false;
            }

            m_events[m_tail] = event;
            m_tail = (m_tail + 1) % MaxEvents;
            m_count++;
            return true;
        }

        bool ImmediateQueue::pop(Event &outEvent)
        {
            if (!m_initialized || m_count == 0)
            {
                return false;
            }

            outEvent = m_events[m_head];
            m_head = (m_head + 1) % MaxEvents;
            m_count--;
            return true;
        }

        void ImmediateQueue::clear()
        {
            m_head = 0;
            m_tail = 0;
            m_count = 0;
        }

    } // namespace Event
} // namespace QK
