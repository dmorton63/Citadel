// QWindowing StyleSystem implementation

#include "QWStyleSystem.h"

#include <cstring>

namespace QW
{

    StyleSystem &StyleSystem::instance()
    {
        static StyleSystem s_instance;
        return s_instance;
    }

    StyleSystem::StyleSystem()
        : m_current(StyleSnapshot::fallback()),
          m_initialized(false),
          m_generation(0)
    {
    }

    void StyleSystem::initialize(const StyleSnapshot *initialSnapshot)
    {
        if (m_initialized)
            return;

        if (initialSnapshot)
        {
            m_current = *initialSnapshot;
        }
        else
        {
            m_current = StyleSnapshot::fallback();
        }

        m_initialized = true;
        m_generation = 1;
        notifyListeners();
    }

    void StyleSystem::shutdown()
    {
        m_listeners.clear();
        m_initialized = false;
    }

    void StyleSystem::addListener(IStyleListener *listener)
    {
        if (!listener)
            return;

        for (QC::usize i = 0; i < m_listeners.size(); ++i)
        {
            if (m_listeners[i] == listener)
                return;
        }

        m_listeners.push_back(listener);

        if (m_initialized)
        {
            listener->onStyleChanged(m_current);
        }
    }

    void StyleSystem::removeListener(IStyleListener *listener)
    {
        if (!listener)
            return;

        for (QC::usize i = 0; i < m_listeners.size(); ++i)
        {
            if (m_listeners[i] == listener)
            {
                for (QC::usize j = i; j + 1 < m_listeners.size(); ++j)
                {
                    m_listeners[j] = m_listeners[j + 1];
                }
                m_listeners.pop_back();
                break;
            }
        }
    }

    void StyleSystem::setStyle(const StyleSnapshot &snapshot)
    {
        if (m_initialized)
        {
            if (std::memcmp(&m_current, &snapshot, sizeof(StyleSnapshot)) == 0)
            {
                return;
            }
        }

        m_current = snapshot;
        if (!m_initialized)
        {
            m_initialized = true;
            m_generation = 1;
        }
        else
        {
            ++m_generation;
        }
        notifyListeners();
    }

    void StyleSystem::notifyListeners()
    {
        if (!m_initialized)
            return;

        for (QC::usize i = 0; i < m_listeners.size(); ++i)
        {
            if (m_listeners[i])
            {
                m_listeners[i]->onStyleChanged(m_current);
            }
        }
    }

} // namespace QW
