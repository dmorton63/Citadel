#include "QCMSApp.h"

#include "QWWindow.h"
#include "QWWindowManager.h"
#include "QWControls/Containers/Panel.h"
#include "QWControls/Leaf/Label.h"
#include "QWControls/Leaf/Button.h"

namespace QCMS
{
    namespace
    {
        static App *s_app = nullptr;
        static constexpr QC::u32 kChromeHeight = 24;

        struct PanelMeta
        {
            PanelId     id;
            const char *title;
        };

        static const PanelMeta kPanelMeta[] = {
            {PanelId::DbBrowser,   "DB Browser"},
            {PanelId::ThemeEditor, "Query Studio"},
            {PanelId::SysConfig,   "System Configuration"},
            {PanelId::Security,    "Security & Capabilities"},
            {PanelId::ServiceMgr,  "Service Manager"},
        };
    } // namespace

    // -------------------------------------------------------------------------

    App &App::instance()
    {
        if (!s_app)
            s_app = new App();
        return *s_app;
    }

    bool App::isOpen() const
    {
        return m_window != nullptr &&
               QW::WindowManager::instance().windowById(m_window->windowId()) != nullptr;
    }

    void App::open(QCQL::Database *database, const QC::Rect &workArea)
    {
        m_database = database;

        if (isOpen())
        {
            QW::WindowManager::instance().bringToFront(m_window);
            QW::WindowManager::instance().setFocus(m_window);
            QW::WindowManager::instance().render();
            return;
        }

        buildWindow(workArea);
    }

    void App::close()
    {
        if (!m_window)
            return;

        QW::WindowManager::instance().destroyWindow(m_window);
        m_window = nullptr;
        m_chromePanel = nullptr;
        m_clientPanel = nullptr;
        m_closeButton = nullptr;
        m_navBar.destroy();
        m_dbBrowser.destroy();
        m_queryStudio.destroy();

        constexpr QC::usize kCount = static_cast<QC::u32>(PanelId::Count);
        for (QC::usize i = 0; i < kCount; ++i)
            m_contentPanels[i] = nullptr;
    }

    // -------------------------------------------------------------------------
    // Private
    // -------------------------------------------------------------------------

    void App::buildWindow(const QC::Rect &workArea)
    {
        // Center on caller-provided work area.
        const QC::i32 x = workArea.x + static_cast<QC::i32>((workArea.width  - kWindowW) / 2);
        const QC::i32 y = workArea.y + static_cast<QC::i32>((workArea.height - kWindowH) / 2);

        const QW::Rect bounds = {x, y, kWindowW, kWindowH};
        m_window = QW::WindowManager::instance().createWindow(
            "Citadel Management Studio", bounds);
        if (!m_window)
            return;

        m_window->setFlags(
            QW::WindowFlags::Visible  |
            QW::WindowFlags::Resizable|
            QW::WindowFlags::Movable  |
            QW::WindowFlags::HasTitle |
            QW::WindowFlags::HasBorder|
            QW::WindowFlags::HasClose |
            QW::WindowFlags::HasMinimize);

        m_window->root()->setBorderStyle(QW::Controls::BorderStyle::None);
        m_window->root()->setPadding(0);

        const QW::Rect clientBounds = {
            0,
            static_cast<QC::i32>(kChromeHeight),
            kWindowW,
            kWindowH - kChromeHeight};
        m_clientPanel = new QW::Controls::Panel(m_window, clientBounds);
        m_clientPanel->setBorderStyle(QW::Controls::BorderStyle::None);
        m_clientPanel->setPadding(0);
        m_window->root()->addChild(m_clientPanel);

        const QW::Rect chromeBounds = {0, 0, kWindowW, kChromeHeight};
        m_chromePanel = new QW::Controls::Panel(m_window, chromeBounds);
        m_chromePanel->setBorderStyle(QW::Controls::BorderStyle::None);
        m_chromePanel->setPadding(0);
        m_window->root()->addChild(m_chromePanel);

        // Explicit close control for environments where title-bar close handling
        // may be inconsistent.
        const QW::Rect closeBounds = {
            static_cast<QC::i32>(kWindowW - 90),
            0,
            80,
            kChromeHeight};
        m_closeButton = new QW::Controls::Button(m_window, "Close", closeBounds);
        m_closeButton->setContentMode(QW::ButtonContentMode::Text);
        m_closeButton->setVariant(QW::ButtonVariant::Compact);
        m_closeButton->setRole(QW::ButtonRole::Destructive);
        m_closeButton->setClickHandler(&App::onCloseButton, this);
        m_chromePanel->addChild(m_closeButton);

        // Build navigation sidebar.
        m_navBar.setSelectHandler(&App::onNavSelect, this);
        m_navBar.build(m_window, m_clientPanel, kWindowH - kChromeHeight);

        // Build one real panel (DB Browser) + placeholders for remaining sections.
        constexpr QC::usize kCount = static_cast<QC::u32>(PanelId::Count);
        for (QC::usize i = 0; i < kCount; ++i)
        {
            const PanelId id = static_cast<PanelId>(i);
            if (id == PanelId::DbBrowser)
            {
                const QW::Rect contentBounds = {
                    static_cast<QC::i32>(kNavBarWidth),
                    0,
                    kWindowW - kNavBarWidth,
                    kWindowH - kChromeHeight
                };
                m_dbBrowser.build(m_window, m_clientPanel, m_database, contentBounds);
                m_contentPanels[i] = m_dbBrowser.panel();
            }
            else if (id == PanelId::ThemeEditor)
            {
                const QW::Rect contentBounds = {
                    static_cast<QC::i32>(kNavBarWidth),
                    0,
                    kWindowW - kNavBarWidth,
                    kWindowH - kChromeHeight
                };
                m_queryStudio.build(m_window, m_clientPanel, m_database, contentBounds);
                m_contentPanels[i] = m_queryStudio.panel();
            }
            else
            {
                buildPlaceholderPanel(m_window, id);
            }
        }

        // Show the first panel.
        switchPanel(PanelId::DbBrowser);

        QW::WindowManager::instance().bringToFront(m_window);
        QW::WindowManager::instance().setFocus(m_window);
        QW::WindowManager::instance().render();
    }

    void App::buildPlaceholderPanel(QW::Window *window, PanelId panel)
    {
        const QC::u32 idx = static_cast<QC::u32>(panel);

        const QW::Rect bounds = {
            static_cast<QC::i32>(kNavBarWidth),
            0,
            kWindowW - kNavBarWidth,
            kWindowH
        };

        auto *p = new QW::Controls::Panel(window, bounds);
        p->setBorderStyle(QW::Controls::BorderStyle::None);
        p->setPadding(16);
        p->setVisible(false);
        if (m_clientPanel)
            m_clientPanel->addChild(p);

        // Title label — will be replaced by real content panels in later steps.
        const char *title = kPanelMeta[idx].title;
        const QW::Rect labelBounds = {0, 0, kWindowW - kNavBarWidth - 32, 28};
        auto *lbl = new QW::Controls::Label(window, title, labelBounds);
        p->addChild(lbl);

        m_contentPanels[idx] = p;
    }

    void App::switchPanel(PanelId panel)
    {
        constexpr QC::usize kCount = static_cast<QC::u32>(PanelId::Count);
        for (QC::usize i = 0; i < kCount; ++i)
        {
            if (static_cast<PanelId>(i) == PanelId::DbBrowser ||
                static_cast<PanelId>(i) == PanelId::ThemeEditor)
                continue;
            if (m_contentPanels[i])
                m_contentPanels[i]->setVisible(
                    static_cast<PanelId>(i) == panel);
        }

        m_dbBrowser.setVisible(panel == PanelId::DbBrowser);
        m_queryStudio.setVisible(panel == PanelId::ThemeEditor);
        m_navBar.setActive(panel);
        m_active = panel;
        if (m_window)
            m_window->invalidate();
    }

    // static
    void App::onNavSelect(PanelId panel, void *userData)
    {
        auto *self = static_cast<App *>(userData);
        if (self)
            self->switchPanel(panel);
    }

    void App::onCloseButton(QW::Controls::Button *, void *userData)
    {
        auto *self = static_cast<App *>(userData);
        if (self)
            self->close();
    }

} // namespace QCMS
