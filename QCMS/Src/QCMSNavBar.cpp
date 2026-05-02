#include "QCMSNavBar.h"

#include "QWWindow.h"
#include "QWWindowManager.h"
#include "QWControls/Containers/Panel.h"
#include "QWControls/Leaf/Button.h"

namespace QCMS
{
    namespace
    {
        struct ButtonLabel
        {
            PanelId     panel;
            const char *label;
        };

        static const ButtonLabel kLabels[] = {
            {PanelId::DbBrowser,   "DB Browser"},
            {PanelId::ThemeEditor, "Themes"},
            {PanelId::SysConfig,   "System Config"},
            {PanelId::Security,    "Security"},
            {PanelId::ServiceMgr,  "Services"},
        };
    } // namespace

    void NavBar::build(QW::Window *window, QW::Controls::Panel *parent, QC::u32 panelHeight)
    {
        if (!window || !parent)
            return;

        const QW::Rect panelBounds = {
            0, 0,
            kNavBarWidth,
            panelHeight
        };
        m_panel = new QW::Controls::Panel(window, panelBounds);
        m_panel->setBorderStyle(QW::Controls::BorderStyle::Flat);
        m_panel->setPadding(kNavPadding);
        parent->addChild(m_panel);

        constexpr QC::usize kCount = sizeof(kLabels) / sizeof(kLabels[0]);
        for (QC::usize i = 0; i < kCount; ++i)
        {
            const QC::u32 y = kNavPadding + static_cast<QC::u32>(i) * (kNavButtonH + 4);
            const QW::Rect btnBounds = {
                static_cast<QC::i32>(kNavPadding),
                static_cast<QC::i32>(y),
                kNavBarWidth - kNavPadding * 2,
                kNavButtonH
            };

            auto *btn = new QW::Controls::Button(window, kLabels[i].label, btnBounds);
            btn->setContentMode(QW::ButtonContentMode::Text);
            btn->setVariant(QW::ButtonVariant::Borderless);

            ButtonSlot &slot = m_slots[static_cast<QC::u32>(kLabels[i].panel)];
            slot.button = btn;
            slot.panel  = kLabels[i].panel;

            btn->setClickHandler(&NavBar::onButtonClicked, this);
            m_panel->addChild(btn);
        }

        setActive(m_active);
    }

    void NavBar::destroy()
    {
        // Controls are owned by the window; just null our pointers.
        constexpr QC::usize kCount = static_cast<QC::u32>(PanelId::Count);
        for (QC::usize i = 0; i < kCount; ++i)
            m_slots[i].button = nullptr;
        m_panel = nullptr;
    }

    void NavBar::setActive(PanelId panel)
    {
        m_active = panel;
        constexpr QC::usize kCount = static_cast<QC::u32>(PanelId::Count);
        for (QC::usize i = 0; i < kCount; ++i)
        {
            if (!m_slots[i].button)
                continue;
            // Accent role = active, Default role = inactive
            const QW::ButtonRole role = (m_slots[i].panel == panel)
                ? QW::ButtonRole::Accent
                : QW::ButtonRole::Default;
            m_slots[i].button->setRole(role);
        }
    }

    void NavBar::setSelectHandler(NavSelectHandler handler, void *userData)
    {
        m_handler     = handler;
        m_handlerData = userData;
    }

    // static
    void NavBar::onButtonClicked(QW::Controls::Button *btn, void *userData)
    {
        auto *self = static_cast<NavBar *>(userData);
        if (!self || !btn)
            return;

        constexpr QC::usize kCount = static_cast<QC::u32>(PanelId::Count);
        for (QC::usize i = 0; i < kCount; ++i)
        {
            if (self->m_slots[i].button == btn)
            {
                self->setActive(self->m_slots[i].panel);
                if (self->m_handler)
                    self->m_handler(self->m_slots[i].panel, self->m_handlerData);
                return;
            }
        }
    }

} // namespace QCMS
