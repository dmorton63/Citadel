#pragma once

// QWindowing StyleSystem - Global style snapshot distribution
// Namespace: QW

#include "QCVector.h"
#include "QWStyleTypes.h"

namespace QW
{

    class StyleSystem
    {
    public:
        class IStyleListener
        {
        public:
            virtual ~IStyleListener() = default;
            virtual void onStyleChanged(const StyleSnapshot &snapshot) = 0;
        };

        static StyleSystem &instance();

        void initialize(const StyleSnapshot *initialSnapshot = nullptr);
        void shutdown();

        void addListener(IStyleListener *listener);
        void removeListener(IStyleListener *listener);

        void setStyle(const StyleSnapshot &snapshot);
        const StyleSnapshot &currentStyle() const { return m_current; }
        QC::u64 generation() const { return m_generation; }

    private:
        StyleSystem();
        StyleSystem(const StyleSystem &) = delete;
        StyleSystem &operator=(const StyleSystem &) = delete;

        void notifyListeners();

        StyleSnapshot m_current;
        QC::Vector<IStyleListener *> m_listeners;
        bool m_initialized;
        QC::u64 m_generation;
    };

} // namespace QW
