// QEvent - Event Listener Implementation
// QKEventListener.cpp

#include "QKEventListener.h"
#include "QKEventManager.h"

namespace QK
{
    namespace Event
    {

        // ==================== ScopedListener Implementation ====================

        ScopedListener::~ScopedListener()
        {
            release();
        }

        void ScopedListener::release()
        {
            if (m_manager && m_id != InvalidListenerId)
            {
                m_manager->removeListener(m_id);
                m_manager = nullptr;
                m_id = InvalidListenerId;
            }
        }

    } // namespace Event
} // namespace QK
