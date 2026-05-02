#pragma once

// QCMSApp - Citadel Management Studio main window controller
// Namespace: QCMS
//
// Usage (called from desktop/launcher):
//   QCMS::App::instance().open(database, workArea);
//   QCMS::App::instance().close();

#include "QCMSNavBar.h"
#include "QCMSDbBrowser.h"
#include "QCMSQueryStudio.h"
#include "QCQLEngine.h"

namespace QW  { class Window;  }
namespace QW::Controls { class Panel; class Label; class Button; }

namespace QCMS
{

    class App
    {
    public:
        static App &instance();

        // Opens (or raises) the CMMS window against the given live database.
        void open(QCQL::Database *database, const QC::Rect &workArea);
        void close();
        bool isOpen() const;

        App(const App &) = delete;
        App &operator=(const App &) = delete;
        App() = default;

    private:
        void buildWindow(const QC::Rect &workArea);
        void switchPanel(PanelId panel);
        void buildPlaceholderPanel(QW::Window *window, PanelId panel);

        static void onNavSelect(PanelId panel, void *userData);
        static void onWindowClose(QW::Window *window, void *userData);
        static void onCloseButton(QW::Controls::Button *button, void *userData);

        QW::Window            *m_window   = nullptr;
        QCQL::Database        *m_database = nullptr;
        QW::Controls::Panel   *m_chromePanel = nullptr;
        QW::Controls::Panel   *m_clientPanel = nullptr;
        QW::Controls::Button  *m_closeButton = nullptr;

        NavBar                 m_navBar;
        DbBrowser              m_dbBrowser;
        QueryStudio            m_queryStudio;

        // Content panels — one per section, shown/hidden on nav select.
        QW::Controls::Panel   *m_contentPanels[static_cast<QC::u32>(PanelId::Count)] = {};

        PanelId                m_active   = PanelId::DbBrowser;
    };

} // namespace QCMS
