#pragma once

// QCMSNavBar - Left sidebar navigation for CMMS
// Namespace: QCMS

#include "QCMSTypes.h"
#include "QWControls/Leaf/Button.h"
#include "QWControls/Containers/Panel.h"

namespace QW { class Window; }

namespace QCMS
{

    using NavSelectHandler = void (*)(PanelId panel, void *userData);

    class NavBar
    {
    public:
        NavBar() = default;

        void build(QW::Window *window, QW::Controls::Panel *parent, QC::u32 panelHeight);
        void destroy();

        void setActive(PanelId panel);
        void setSelectHandler(NavSelectHandler handler, void *userData);

    private:
        static void onButtonClicked(QW::Controls::Button *btn, void *userData);

        struct ButtonSlot
        {
            QW::Controls::Button *button = nullptr;
            PanelId               panel  = PanelId::DbBrowser;
        };

        QW::Controls::Panel *m_panel         = nullptr;
        ButtonSlot            m_slots[static_cast<QC::u32>(PanelId::Count)] = {};
        NavSelectHandler      m_handler       = nullptr;
        void                 *m_handlerData   = nullptr;
        PanelId               m_active        = PanelId::DbBrowser;
    };

} // namespace QCMS
