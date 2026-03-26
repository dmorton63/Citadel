#pragma once

// QDesktop Browser - Minimal HTML viewer window
// Namespace: QD

#include "QCTypes.h"
#include "QDHtml.h"

namespace QW
{
    class Window;
}

namespace QW::Controls
{
    class Panel;
    class ScrollPanel;
    class Button;
    class ScrollBar;
}

namespace QD
{
    class Desktop;

    class Browser
    {
    public:
        explicit Browser(Desktop *desktop);
        ~Browser();

        void openFile(const char *path);
        void openUrl(const char *url);
        void openHtmlText(const char *htmlText);
        void close();
        bool isOpen() const;

    private:
        void ensureWindow();
        bool windowStillAlive() const;

        static void onLinkClick(const char *href, void *userData);

        Desktop *m_desktop;

        QW::Window *m_window = nullptr;
        QC::u32 m_windowId = 0;

        QW::Controls::Panel *m_root = nullptr;
        QW::Controls::ScrollPanel *m_viewport = nullptr;
        QW::Controls::ScrollBar *m_vScroll = nullptr;

        Html::Document m_doc;
    };

} // namespace QD
