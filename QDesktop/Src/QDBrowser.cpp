// QDesktop Browser - Minimal HTML viewer window
// Namespace: QD

#include "QDBrowser.h"

#include "QDDesktop.h"

#include "QCLogger.h"
#include "QCString.h"
#include "QCVector.h"

#include "QFSVFS.h"
#include "QFSFile.h"

#include "QWWindowManager.h"
#include "QWWindow.h"
#include "QWControls/Containers/Panel.h"
#include "QWControls/Containers/ScrollPanel.h"
#include "QWControls/Leaf/Button.h"
#include "QWControls/Leaf/Label.h"
#include "QWControls/Leaf/ScrollBar.h"

namespace QD
{
    namespace
    {
        static char *readFileToOwnedCString(const char *path)
        {
            if (!path || !*path)
                return nullptr;

            QFS::File *file = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
            if (!file)
                return nullptr;

            const QC::u64 size64 = file->size();
            if (size64 == 0 || size64 > 1024 * 1024)
            {
                QFS::VFS::instance().close(file);
                return nullptr;
            }

            QC::Vector<char> buf;
            buf.resize(static_cast<QC::usize>(size64) + 1);

            const QC::isize n = file->read(buf.data(), static_cast<QC::usize>(size64));
            QFS::VFS::instance().close(file);

            if (n <= 0)
                return nullptr;

            buf[static_cast<QC::usize>(n)] = '\0';

            char *out = static_cast<char *>(operator new[](static_cast<QC::usize>(n) + 1));
            for (QC::usize i = 0; i < static_cast<QC::usize>(n) + 1; ++i)
                out[i] = buf[i];
            return out;
        }

        static bool hasSlash(const char *s)
        {
            if (!s)
                return false;
            for (; *s; ++s)
            {
                if (*s == '/')
                    return true;
            }
            return false;
        }

    } // namespace

    Browser::Browser(Desktop *desktop)
        : m_desktop(desktop)
    {
        m_doc.setLinkClickHandler(&Browser::onLinkClick, this);
    }

    Browser::~Browser()
    {
        close();
    }

    bool Browser::windowStillAlive() const
    {
        if (!m_windowId)
            return false;
        return QW::WindowManager::instance().windowById(m_windowId) != nullptr;
    }

    bool Browser::isOpen() const
    {
        if (!m_window)
            return false;
        return windowStillAlive();
    }

    void Browser::ensureWindow()
    {
        auto syncScrollUi = [&]()
        {
            if (!m_viewport || !m_vScroll)
                return;

            const QC::Rect cr = m_viewport->clientRect();
            const QC::i32 viewportH = static_cast<QC::i32>(cr.height);
            const QC::i32 contentH = static_cast<QC::i32>(m_viewport->contentHeight());

            QC::i32 maxY = 0;
            if (contentH > viewportH)
                maxY = contentH - viewportH;

            m_vScroll->setMinimum(0);
            m_vScroll->setMaximum(maxY);
            m_vScroll->setPageSize((viewportH > 0) ? static_cast<QC::u32>(viewportH) : 0);
            m_vScroll->setSmallStep(24);
            m_vScroll->setLargeStep((viewportH > 0) ? viewportH : 24);
            m_vScroll->setValue(m_viewport->scrollOffsetY());
        };

        if (m_window)
        {
            if (!windowStillAlive())
            {
                m_window = nullptr;
                m_windowId = 0;
                m_root = nullptr;
                m_viewport = nullptr;
                m_vScroll = nullptr;
            }
            else
            {
                QW::WindowManager::instance().bringToFront(m_window);
                QW::WindowManager::instance().setFocus(m_window);
                syncScrollUi();
                return;
            }
        }

        if (!m_desktop)
            return;

        const QC::Rect wa = m_desktop->workArea();
        const QC::u32 w = 720;
        const QC::u32 h = 520;
        const QC::i32 x = wa.x + static_cast<QC::i32>((wa.width > w) ? ((wa.width - w) / 2) : 0);
        const QC::i32 y = wa.y + 24;

        m_window = QW::WindowManager::instance().createWindow("HTML Viewer", {x, y, w, h});
        if (!m_window)
            return;

        m_windowId = m_window->windowId();
        QC_LOG_INFO("QDBrowser", "Browser window created (id=%u)", m_windowId);

        m_root = m_window->root();
        m_root->setBorderStyle(QW::Controls::BorderStyle::None);
        m_root->setPadding(0);
        m_root->setBackgroundColor(Color(255, 255, 255, 255));

        // Disable default chrome buttons; use the top-N pixels as a drag region.
        m_window->setFlags(QW::WindowFlags::Visible | QW::WindowFlags::Resizable | QW::WindowFlags::Movable | QW::WindowFlags::HasTitle | QW::WindowFlags::HasBorder);

        static constexpr QC::i32 kTitleBarHeight = 24;
        static constexpr QC::i32 kPad = 8;
        static constexpr QC::u32 kScrollBarW = 18;
        static constexpr QC::i32 kScrollBarGap = 4;

        // Title bar controls (within the draggable region).
        {
            auto *titleBar = new QW::Controls::Panel(m_window, {0, 0, w, static_cast<QC::u32>(kTitleBarHeight)});
            titleBar->setBorderStyle(QW::Controls::BorderStyle::None);
            titleBar->setPadding(0);
            titleBar->setBackgroundColor(Color(255, 255, 255, 255));
            m_root->addChild(titleBar);

            auto *title = new QW::Controls::Label(m_window, "HTML Viewer", {kPad, 4, w - 64, 16});
            titleBar->addChild(title);

            auto *closeButton = new QW::Controls::Button(m_window, "X", {static_cast<QC::i32>(w - kPad - 20), 2, 20, 20});
            closeButton->setRole(QW::ButtonRole::Destructive);
            closeButton->setClickHandler([](QW::Controls::Button *, void *ud) {
                auto *self = static_cast<Browser *>(ud);
                if (self)
                    self->close();
            }, this);
            titleBar->addChild(closeButton);

        }

        // Viewport (scrollable HTML content)
        const QC::i32 vpY = kTitleBarHeight + kPad;
        const QC::i32 vpH = static_cast<QC::i32>(h) - vpY - kPad;

        const QC::u32 vpW = (w > static_cast<QC::u32>(kPad * 2) + kScrollBarW + static_cast<QC::u32>(kScrollBarGap))
                                 ? (w - static_cast<QC::u32>(kPad * 2) - kScrollBarW - static_cast<QC::u32>(kScrollBarGap))
                                 : 0;

        m_viewport = new QW::Controls::ScrollPanel(m_window,
                                                   {kPad,
                                                    vpY,
                                                    vpW,
                                                    static_cast<QC::u32>(vpH > 0 ? vpH : 0)});
        m_viewport->setBorderStyle(QW::Controls::BorderStyle::None);
        m_viewport->setFrameVisible(false);
        m_viewport->setPadding(8);
        m_viewport->setBackgroundColor(Color(255, 255, 255, 255));
        m_root->addChild(m_viewport);

        // Visible scrollbar (synced to viewport scroll offset).
        const QC::i32 sbX = kPad + static_cast<QC::i32>(vpW) + kScrollBarGap;
        m_vScroll = new QW::Controls::ScrollBar(m_window,
                                               {sbX,
                                                vpY,
                                                kScrollBarW,
                                                static_cast<QC::u32>(vpH > 0 ? vpH : 0)},
                                               QW::Controls::ScrollOrientation::Vertical);
        m_vScroll->setScrollChangeHandler([](QW::Controls::ScrollBar *sb, void *ud) {
            auto *self = static_cast<Browser *>(ud);
            if (!self || !sb || !self->m_viewport)
                return;
            self->m_viewport->setScrollOffsetY(sb->value());
        }, this);
        m_root->addChild(m_vScroll);

        m_viewport->setScrollChangeHandler([](QW::Controls::ScrollPanel *panel, void *ud) {
            auto *self = static_cast<Browser *>(ud);
            if (!self || !panel || !self->m_vScroll)
                return;
            self->m_vScroll->setValue(panel->scrollOffsetY());
        }, this);

        syncScrollUi();

        m_window->invalidate();
        QW::WindowManager::instance().render();

        m_desktop->addTaskbarWindow(m_windowId, "Browser");
        m_desktop->setActiveTaskbarWindow(m_windowId);
    }

    void Browser::openFile(const char *path)
    {
        auto syncScrollUi = [&]()
        {
            if (!m_viewport || !m_vScroll)
                return;
            const QC::Rect cr = m_viewport->clientRect();
            const QC::i32 viewportH = static_cast<QC::i32>(cr.height);
            const QC::i32 contentH = static_cast<QC::i32>(m_viewport->contentHeight());
            QC::i32 maxY = 0;
            if (contentH > viewportH)
                maxY = contentH - viewportH;
            m_vScroll->setMinimum(0);
            m_vScroll->setMaximum(maxY);
            m_vScroll->setPageSize((viewportH > 0) ? static_cast<QC::u32>(viewportH) : 0);
            m_vScroll->setLargeStep((viewportH > 0) ? viewportH : 24);
            m_vScroll->setSmallStep(24);
            m_vScroll->setValue(m_viewport->scrollOffsetY());
        };

        ensureWindow();
        if (!m_window || !m_viewport)
            return;

        m_viewport->setScrollOffsetY(0);

        char absPath[256];
        QC::String::memset(absPath, 0, sizeof(absPath));

        if (!path || !*path)
        {
            m_doc.renderTo(m_window, m_viewport, "<p>browsefile: missing path</p>");
            m_viewport->setContentHeight(m_doc.contentHeight());
            syncScrollUi();
            m_window->invalidate();
            QW::WindowManager::instance().render();
            return;
        }

        if (!hasSlash(path))
        {
            // Default to /shared for convenience.
            QC::String::strncpy(absPath, "/shared/", sizeof(absPath) - 1);
            const QC::usize used = QC::String::strlen(absPath);
            QC::String::strncpy(absPath + used, path, sizeof(absPath) - 1 - used);
        }
        else
        {
            QC::String::strncpy(absPath, path, sizeof(absPath) - 1);
        }

        char *html = readFileToOwnedCString(absPath);
        if (!html)
        {
            char msg[384];
            QC::String::memset(msg, 0, sizeof(msg));
            QC::String::strncpy(msg, "<p>browsefile: failed to read file: ", sizeof(msg) - 1);
            const QC::usize used = QC::String::strlen(msg);
            QC::String::strncpy(msg + used, absPath, sizeof(msg) - 1 - used);
            const QC::usize used2 = QC::String::strlen(msg);
            QC::String::strncpy(msg + used2, "</p>", sizeof(msg) - 1 - used2);

            m_doc.renderTo(m_window, m_viewport, msg);
            m_viewport->setContentHeight(m_doc.contentHeight());
            syncScrollUi();
            m_window->invalidate();
            QW::WindowManager::instance().render();
            return;
        }

        m_doc.renderTo(m_window, m_viewport, html);
        m_viewport->setContentHeight(m_doc.contentHeight());
        syncScrollUi();

        operator delete[](html);

        QW::WindowManager::instance().bringToFront(m_window);
        QW::WindowManager::instance().setFocus(m_window);
        m_window->invalidate();
        QW::WindowManager::instance().render();
    }

    void Browser::openUrl(const char *url)
    {
        auto syncScrollUi = [&]()
        {
            if (!m_viewport || !m_vScroll)
                return;
            const QC::Rect cr = m_viewport->clientRect();
            const QC::i32 viewportH = static_cast<QC::i32>(cr.height);
            const QC::i32 contentH = static_cast<QC::i32>(m_viewport->contentHeight());
            QC::i32 maxY = 0;
            if (contentH > viewportH)
                maxY = contentH - viewportH;
            m_vScroll->setMinimum(0);
            m_vScroll->setMaximum(maxY);
            m_vScroll->setPageSize((viewportH > 0) ? static_cast<QC::u32>(viewportH) : 0);
            m_vScroll->setLargeStep((viewportH > 0) ? viewportH : 24);
            m_vScroll->setSmallStep(24);
            m_vScroll->setValue(m_viewport->scrollOffsetY());
        };

        ensureWindow();
        if (!m_window || !m_viewport)
            return;

        m_viewport->setScrollOffsetY(0);

        if (!url || !*url)
        {
            m_doc.renderTo(m_window, m_viewport, "<p>browsefile: missing URL</p>");
            m_viewport->setContentHeight(m_doc.contentHeight());
            syncScrollUi();
            m_window->invalidate();
            QW::WindowManager::instance().render();
            return;
        }

        m_doc.renderUrlTo(m_window, m_viewport, url);
        m_viewport->setContentHeight(m_doc.contentHeight());
        syncScrollUi();

        QW::WindowManager::instance().bringToFront(m_window);
        QW::WindowManager::instance().setFocus(m_window);
        m_window->invalidate();
        QW::WindowManager::instance().render();
    }

    void Browser::openHtmlText(const char *htmlText)
    {
        auto syncScrollUi = [&]()
        {
            if (!m_viewport || !m_vScroll)
                return;
            const QC::Rect cr = m_viewport->clientRect();
            const QC::i32 viewportH = static_cast<QC::i32>(cr.height);
            const QC::i32 contentH = static_cast<QC::i32>(m_viewport->contentHeight());
            QC::i32 maxY = 0;
            if (contentH > viewportH)
                maxY = contentH - viewportH;
            m_vScroll->setMinimum(0);
            m_vScroll->setMaximum(maxY);
            m_vScroll->setPageSize((viewportH > 0) ? static_cast<QC::u32>(viewportH) : 0);
            m_vScroll->setLargeStep((viewportH > 0) ? viewportH : 24);
            m_vScroll->setSmallStep(24);
            m_vScroll->setValue(m_viewport->scrollOffsetY());
        };

        ensureWindow();
        if (!m_window || !m_viewport)
            return;

        if (!htmlText)
            htmlText = "";

        m_viewport->setScrollOffsetY(0);

        m_doc.renderTo(m_window, m_viewport, htmlText);
        m_viewport->setContentHeight(m_doc.contentHeight());
        syncScrollUi();

        QW::WindowManager::instance().bringToFront(m_window);
        QW::WindowManager::instance().setFocus(m_window);
        m_window->invalidate();
        QW::WindowManager::instance().render();
    }

    void Browser::close()
    {
        if (!m_window)
            return;

        const QC::u32 closingId = m_windowId ? m_windowId : m_window->windowId();
        if (m_desktop)
        {
            m_desktop->removeTaskbarWindow(closingId);
        }

        m_doc.clear();

        // Delete all controls we created under the root panel.
        if (m_root)
        {
            while (m_root->childCount() > 0)
            {
                QW::Controls::IControl *c = m_root->childAt(0);
                m_root->removeChildAt(0);
                delete c;
            }
        }

        m_windowId = 0;
        QW::WindowManager::instance().destroyWindow(m_window);
        m_window = nullptr;
        m_root = nullptr;
        m_viewport = nullptr;
    }

    void Browser::onLinkClick(const char *href, void *userData)
    {
        auto *self = static_cast<Browser *>(userData);
        if (!self)
            return;

        QC_LOG_INFO("QDBrowser", "link: %s", href ? href : "");

        // MVP: just log the href. Future: route to DNS/HTTP fetch or file open.
    }

} // namespace QD
