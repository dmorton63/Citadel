// QDesktop Terminal - Simple command interpreter window
// Namespace: QD

#include "QDTerminal.h"

#include "QDDesktop.h"

#include "QCLogger.h"
#include "QCString.h"
#include "QCBuiltins.h"
#include "QKEventManager.h"
#include "QKEventTypes.h"
#include "QKShutdownController.h"
#include "QFSVFS.h"
#include "QFSFile.h"
#include "QFSDirectory.h"

#include "QWWindowManager.h"
#include "QWWindow.h"
#include "QWMessageBus.h"
#include "QWControls/Containers/Panel.h"
#include "QWControls/Leaf/Button.h"
#include "QWControls/Leaf/Label.h"
#include "QWControls/Leaf/TextBox.h"
#include "QWControls/Composite/ListView.h"
#include "QWControls/Leaf/ScrollBar.h"

#include "QKServiceRegistry.h"
#include "QKMsgBus.h"

#include "QDCommandMessages.h"

namespace
{
    static inline bool streq(const char *a, const char *b)
    {
        if (!a || !b)
            return false;
        while (*a && *b)
        {
            if (*a != *b)
                return false;
            ++a;
            ++b;
        }
        return *a == '\0' && *b == '\0';
    }

    static inline char lowerAscii(char c)
    {
        if (c >= 'A' && c <= 'Z')
            return static_cast<char>(c + 32);
        return c;
    }

    static inline bool streqIgnoreCase(const char *a, const char *b)
    {
        if (!a || !b)
            return a == b;
        while (*a && *b)
        {
            if (lowerAscii(*a) != lowerAscii(*b))
                return false;
            ++a;
            ++b;
        }
        return *a == '\0' && *b == '\0';
    }

    static inline const char *skipSpaces(const char *p)
    {
        while (p && (*p == ' ' || *p == '\t'))
            ++p;
        return p;
    }

    static inline bool hasSlash(const char *p)
    {
        if (!p)
            return false;
        for (; *p; ++p)
        {
            if (*p == '/')
                return true;
        }
        return false;
    }

    static inline bool startsWith(const char *s, const char *prefix)
    {
        if (!s || !prefix)
            return false;
        while (*prefix)
        {
            if (*s++ != *prefix++)
                return false;
        }
        return true;
    }

    static void destroyOwnedString(void *p)
    {
        char *s = static_cast<char *>(p);
        operator delete[](s);
    }

    static char *dupString(const char *s)
    {
        if (!s)
            s = "";
        const QC::usize len = QC::String::strlen(s);
        char *out = static_cast<char *>(operator new[](len + 1));
        for (QC::usize i = 0; i < len; ++i)
            out[i] = s[i];
        out[len] = '\0';
        return out;
    }
}

namespace QD
{

    namespace
    {
        static QC::u64 nextCorrelationId()
        {
            static QC::u64 g = 1;
            return g++;
        }
    }

    bool Terminal::onWindowMessage(QW::Window * /*window*/, const QW::Message &msg, void *userData)
    {
        auto *self = static_cast<Terminal *>(userData);
        if (!self)
            return false;

        if (msg.msgId == QD::CmdMsg::OutputLine || msg.msgId == QD::CmdMsg::ErrorLine)
        {
            const char *line = msg.payload ? static_cast<const char *>(msg.payload) : "";
            self->appendLine(line);
            return true;
        }

        if (msg.msgId == QD::CmdMsg::Done)
        {
            return true;
        }

        return false;
    }

    Terminal::Terminal(Desktop *desktop)
        : m_desktop(desktop),
          m_window(nullptr),
          m_windowId(0),
          m_root(nullptr),
          m_output(nullptr),
          m_outputScroll(nullptr),
          m_input(nullptr),
          m_windowListenerId(QK::Event::InvalidListenerId),
          m_followTail(true),
          m_syncingScroll(false)
    {
        auto &eventMgr = QK::Event::EventManager::instance();
        if (eventMgr.isInitialized())
        {
            m_windowListenerId = eventMgr.addListener(QK::Event::Category::Window, &Terminal::onEvent, this);
        }
    }

    Terminal::~Terminal()
    {
        if (m_windowListenerId != QK::Event::InvalidListenerId)
        {
            QK::Event::EventManager::instance().removeListener(m_windowListenerId);
            m_windowListenerId = QK::Event::InvalidListenerId;
        }
        close();
    }

    bool Terminal::isOpen() const
    {
        return m_window != nullptr && windowStillAlive();
    }

    bool Terminal::windowStillAlive() const
    {
        if (!m_window || m_windowId == 0)
            return false;

        // Compare pointer identity against the WindowManager's registry.
        // If the window was destroyed elsewhere, this will return nullptr.
        auto *alive = QW::WindowManager::instance().windowById(m_windowId);
        return alive == m_window;
    }

    bool Terminal::onEvent(const QK::Event::Event &event, void *userData)
    {
        auto *self = static_cast<Terminal *>(userData);
        if (!self)
            return false;

        if (event.type() == QK::Event::Type::WindowDestroy)
        {
            const auto &we = event.asWindow();
            if (self->m_windowId != 0 && we.windowId == self->m_windowId)
            {
                self->onWindowDestroyed(we.windowId);
            }
        }

        return false;
    }

    void Terminal::onWindowDestroyed(QC::u32 windowId)
    {
        // Do not dereference m_window here; the window may already be deleted.
        if (m_windowId == 0 || windowId == 0 || windowId != m_windowId)
            return;

        QC_LOG_WARN("QDTerminal", "Terminal window destroyed externally (id=%u)", windowId);

        if (m_desktop)
        {
            m_desktop->removeTaskbarWindow(windowId);
        }

        m_window = nullptr;
        m_root = nullptr;
        m_output = nullptr;
        m_outputScroll = nullptr;
        m_input = nullptr;
        m_windowId = 0;
    }

    void Terminal::onOutputScrollChanged(QW::Controls::ScrollBar *scrollBar, void *userData)
    {
        auto *self = static_cast<Terminal *>(userData);
        if (!self || !scrollBar || !self->m_output)
            return;

        if (self->m_syncingScroll)
            return;

        self->m_followTail = (scrollBar->value() >= scrollBar->maximum());
        self->m_output->setScrollOffset(static_cast<QC::usize>(scrollBar->value()));
    }

    void Terminal::onOutputViewScrollChanged(QW::Controls::ListView *listView, void *userData)
    {
        auto *self = static_cast<Terminal *>(userData);
        if (!self || !listView)
            return;

        self->syncScrollUi();
    }

    void Terminal::syncScrollUi()
    {
        if (!m_output || !m_outputScroll)
            return;

        const QC::usize items = m_output->itemCount();
        const QC::u32 itemHeight = m_output->itemHeight();
        const QC::u32 h = m_output->bounds().height;

        QC::usize visible = 0;
        if (itemHeight != 0)
            visible = static_cast<QC::usize>(h / itemHeight);

        QC::usize maxOffset = 0;
        if (items > visible)
            maxOffset = items - visible;

        m_syncingScroll = true;
        m_outputScroll->setMinimum(0);
        m_outputScroll->setMaximum(static_cast<QC::i32>(maxOffset));
        m_outputScroll->setPageSize(static_cast<QC::u32>(visible));
        m_outputScroll->setLargeStep(static_cast<QC::i32>((visible > 0) ? visible : 1));
        m_outputScroll->setSmallStep(1);
        m_outputScroll->setValue(static_cast<QC::i32>(m_output->scrollOffset()));
        m_syncingScroll = false;

        m_followTail = (m_output->scrollOffset() >= maxOffset);
    }

    void Terminal::scrollToTail()
    {
        if (!m_output || !m_outputScroll)
            return;

        const QC::usize items = m_output->itemCount();
        const QC::u32 itemHeight = m_output->itemHeight();
        const QC::u32 h = m_output->bounds().height;

        QC::usize visible = 0;
        if (itemHeight != 0)
            visible = static_cast<QC::usize>(h / itemHeight);

        QC::usize maxOffset = 0;
        if (items > visible)
            maxOffset = items - visible;

        m_output->setScrollOffset(maxOffset);
        // setScrollOffset will call syncScrollUi via handler.
        m_followTail = true;
    }

    void Terminal::open()
    {
        if (m_window)
        {
            // If the window was destroyed outside Terminal::close(), clear stale pointers.
            if (!windowStillAlive())
            {
                onWindowDestroyed(m_windowId);
            }
            else
            {
                focus();
                return;
            }
        }

        if (!m_desktop)
            return;

        // Create a movable window inside the work area.
        const QC::Rect wa = m_desktop->workArea();
        const QC::u32 w = 640;
        const QC::u32 h = 360;
        QC::i32 x = wa.x + static_cast<QC::i32>((wa.width > w) ? ((wa.width - w) / 2) : 0);
        QC::i32 y = wa.y + 24;

        m_window = QW::WindowManager::instance().createWindow("Terminal", {x, y, w, h});
        if (!m_window)
            return;

        m_windowId = m_window->windowId();
        QC_LOG_INFO("QDTerminal", "Terminal window created (id=%u)", m_windowId);

        // Ensure we have a window-destroy listener even if the Terminal was created
        // before the event system was initialized.
        if (m_windowListenerId == QK::Event::InvalidListenerId)
        {
            auto &eventMgr = QK::Event::EventManager::instance();
            if (eventMgr.isInitialized())
            {
                m_windowListenerId = eventMgr.addListener(QK::Event::Category::Window, &Terminal::onEvent, this);
            }
        }

        // Receive streaming output from CommandProcessor.
        m_window->setMessageHandler(&Terminal::onWindowMessage, this);

        // Disable close/min/max for now (keeps taskbar state simple).
        m_window->setFlags(QW::WindowFlags::Visible | QW::WindowFlags::Resizable | QW::WindowFlags::Movable | QW::WindowFlags::HasTitle | QW::WindowFlags::HasBorder);

        m_root = m_window->root();
        m_root->setBorderStyle(QW::Controls::BorderStyle::None);
        m_root->setPadding(8);

        // Output view + scrollbar
        const QC::u32 scrollW = 14;
        QC::Rect outBounds = {8, 8, static_cast<QC::u32>(w - 16 - scrollW - 4), static_cast<QC::u32>(h - 16 - 28)};
        QC::Rect scrollBounds = {static_cast<QC::i32>(outBounds.x + static_cast<QC::i32>(outBounds.width) + 4), outBounds.y, scrollW, outBounds.height};

        m_output = new QW::Controls::ListView(m_window, outBounds);
        m_output->setShowHeader(false);
        m_output->setSelectionMode(QW::Controls::SelectionMode::None);
        m_output->setItemHeight(16);
        m_output->setBackgroundColor(QW::Color(20, 20, 20, 255));
        m_output->setTextColor(QW::Color(230, 230, 230, 255));
        m_output->setSelectionColor(QW::Color(80, 120, 170, 255));
        m_output->setScrollOffsetChangeHandler(&Terminal::onOutputViewScrollChanged, this);
        m_root->addChild(m_output);

        m_outputScroll = new QW::Controls::ScrollBar(m_window, scrollBounds, QW::Controls::ScrollOrientation::Vertical);
        m_outputScroll->setBackgroundColor(QW::Color(20, 20, 20, 255));
        m_outputScroll->setTrackColor(QW::Color(20, 20, 20, 255));
        m_outputScroll->setThumbColor(QW::Color(110, 110, 110, 255));
        m_outputScroll->setArrowColor(QW::Color(230, 230, 230, 255));
        m_outputScroll->setScrollChangeHandler(&Terminal::onOutputScrollChanged, this);
        m_root->addChild(m_outputScroll);

        // Initial lines
        (void)m_output->addItem("QAIOS+ Terminal");
        (void)m_output->addItem("Type 'help'");
        scrollToTail();

        // Input textbox
        QC::Rect inBounds = {8, static_cast<QC::i32>(h - 8 - 20), static_cast<QC::u32>(w - 16), 20};
        m_input = new QW::Controls::TextBox(m_window, inBounds);
        m_input->setPlaceholder("command...");
        m_input->setBackgroundColor(QW::Color(20, 20, 20, 255));
        m_input->setTextColor(QW::Color(230, 230, 230, 255));
        m_input->setBorderColor(QW::Color(110, 110, 110, 255));
        m_input->setSelectionColor(QW::Color(80, 120, 170, 255));
        m_input->setTextSubmitHandler(&Terminal::onSubmit, this);
        m_root->addChild(m_input);

        // Close button in the upper-right corner
        QC::Rect closeBounds = {static_cast<QC::i32>(w - 28), 8, 20, 20};
        auto *closeButton = new QW::Controls::Button(m_window, "X", closeBounds);
        closeButton->setRole(QW::ButtonRole::Destructive);
        closeButton->setClickHandler(&Terminal::onCloseClick, this);
        m_root->addChild(closeButton);

        focus();
        m_window->invalidate();
        QW::WindowManager::instance().render();

        // Optional taskbar entry
        m_desktop->addTaskbarWindow(m_windowId, "Terminal");
        m_desktop->setActiveTaskbarWindow(m_windowId);
    }

    void Terminal::close()
    {
        if (!m_window)
            return;

        const QC::u32 closingId = m_windowId ? m_windowId : m_window->windowId();

        if (m_desktop)
        {
            m_desktop->removeTaskbarWindow(closingId);
        }

        // Prevent WindowDestroy event handler from doing any work for this window.
        m_windowId = 0;
        QW::WindowManager::instance().destroyWindow(m_window);
        m_window = nullptr;
        m_root = nullptr;
        m_output = nullptr;
        m_outputScroll = nullptr;
        m_input = nullptr;
    }

    void Terminal::focus()
    {
        if (!m_window)
            return;
        if (!windowStillAlive())
        {
            onWindowDestroyed(m_windowId);
            return;
        }
        QW::WindowManager::instance().bringToFront(m_window);
        QW::WindowManager::instance().setFocus(m_window);
    }

    void Terminal::onSubmit(QW::Controls::TextBox *textBox, void *userData)
    {
        auto *self = static_cast<Terminal *>(userData);
        if (!self || !textBox)
            return;

        const char *line = textBox->text();
        if (!line)
            line = "";

        self->appendLine("> ");
        self->appendLine(line);
        self->executeLine(line);

        textBox->setText("");
    }

    void Terminal::appendLine(const char *line)
    {
        if (!line)
            return;

        if (!m_output)
            return;

        // Normalize: split incoming text on \n into separate list items.
        // (Also ignore \r to keep Windows/serial style clean.)
        char tmp[384];
        QC::usize tlen = 0;

        auto flush = [&]() {
            tmp[tlen] = '\0';
            (void)m_output->addItem(tmp);
            tlen = 0;
        };

        for (const char *p = line; *p; ++p)
        {
            const char c = *p;
            if (c == '\r')
                continue;
            if (c == '\n')
            {
                flush();
                continue;
            }
            if (tlen + 1 < sizeof(tmp))
            {
                tmp[tlen++] = c;
            }
        }

        if (tlen > 0)
            flush();

        // Trim scrollback.
        while (m_output->itemCount() > OUTPUT_MAX_LINES)
        {
            m_output->removeItem(0);
            if (m_output->scrollOffset() > 0)
            {
                m_output->setScrollOffset(m_output->scrollOffset() - 1);
            }
        }

        // Keep scroll UI coherent.
        syncScrollUi();
        if (m_followTail)
        {
            scrollToTail();
        }
    }

    void Terminal::executeLine(const char *line)
    {
        if (!line)
            return;

        const char *p = skipSpaces(line);
        if (*p == '\0')
            return;

        // Extract command word
        char cmd[32];
        QC::usize ci = 0;
        while (*p && *p != ' ' && *p != '\t' && ci + 1 < sizeof(cmd))
        {
            cmd[ci++] = *p++;
        }
        cmd[ci] = '\0';

        p = skipSpaces(p);

        // Local-only commands (UI state): clear/saveterm.

        if (streqIgnoreCase(cmd, "clear"))
        {
            if (m_output)
            {
                m_output->clearItems();
            }
            m_followTail = true;
            syncScrollUi();
            return;
        }

        if (streqIgnoreCase(cmd, "saveterm"))
        {
            const char *arg = (p && *p) ? p : nullptr;

            char outPath[256];
            QC::String::memset(outPath, 0, sizeof(outPath));

            if (!arg || *arg == '\0')
            {
                QC::String::strncpy(outPath, "/shared/citadel.txt", sizeof(outPath) - 1);
            }
            else if (!hasSlash(arg))
            {
                QC::String::strncpy(outPath, "/shared/", sizeof(outPath) - 1);
                QC::usize used = QC::String::strlen(outPath);
                QC::String::strncpy(outPath + used, arg, sizeof(outPath) - 1 - used);
            }
            else
            {
                if (!startsWith(arg, "/shared"))
                {
                    appendLine("saveterm: path must be under /shared");
                    return;
                }
                QC::String::strncpy(outPath, arg, sizeof(outPath) - 1);
            }

            QFS::File *file = QFS::VFS::instance().open(
                outPath,
                QFS::OpenMode::Write | QFS::OpenMode::Create | QFS::OpenMode::Truncate);
            if (!file)
            {
                appendLine("saveterm: cannot open output file (is /shared mounted + writable?)");
                return;
            }

            if (m_output)
            {
                const QC::usize count = m_output->itemCount();
                for (QC::usize i = 0; i < count; ++i)
                {
                    const auto *it = m_output->item(i);
                    if (!it)
                        continue;
                    const QC::usize len = QC::String::strlen(it->text);
                    if (len)
                        (void)file->write(it->text, len);
                    (void)file->write("\r\n", 2);
                }
            }

            QFS::VFS::instance().close(file);
            appendLine("saveterm: wrote transcript to:");
            appendLine(outPath);
            return;
        }

        // All other commands go through CommandProcessor.
        if (!m_window)
        {
            appendLine("terminal: no window for command routing");
            return;
        }

        QK::Msg::Envelope *env = QK::Msg::makeEnvelope(QK::Msg::Topic::SvcMsg, nextCorrelationId());
        env->senderId = m_window->windowId();
        env->param1 = QD::CmdMsg::Request;
        env->payload = dupString(line);
        env->destroyPayload = &destroyOwnedString;

        const bool ok = QK::Svc::Registry::instance().sendTo(QD::CmdMsg::ServiceName, env);
        QK::Msg::release(env);

        if (!ok)
        {
            appendLine("command processor: send failed");
        }
    }

    void Terminal::listDirectory(const char *path)
    {
        const char *target = (path && *path) ? path : "/";
        QFS::Directory *dir = QFS::VFS::instance().openDir(target);
        if (!dir)
        {
            appendLine("ls: cannot open path");
            return;
        }

        char heading[320];
        QC::String::memset(heading, 0, sizeof(heading));
        const char prefix[] = "Listing ";
        QC::usize idx = 0;
        for (QC::usize i = 0; prefix[i] && idx + 1 < sizeof(heading); ++i)
        {
            heading[idx++] = prefix[i];
        }
        for (QC::usize i = 0; target[i] && idx + 1 < sizeof(heading); ++i)
        {
            heading[idx++] = target[i];
        }
        heading[idx] = '\0';
        appendLine(heading);

        QFS::DirEntry entry;
        while (dir->read(&entry))
        {
            char line[320];
            QC::String::memset(line, 0, sizeof(line));
            QC::usize pos = 0;

            char typeChar = '-';
            if (entry.type == QFS::FileType::Directory)
                typeChar = 'd';
            else if (entry.type == QFS::FileType::SymLink)
                typeChar = 'l';
            line[pos++] = typeChar;
            line[pos++] = ' ';

            // Size
            char sizeBuf[32];
            QC::String::memset(sizeBuf, 0, sizeof(sizeBuf));
            QC::u64 value = entry.size;
            int sizeIdx = 0;
            if (value == 0)
            {
                sizeBuf[sizeIdx++] = '0';
            }
            else
            {
                char temp[32];
                int tempIdx = 0;
                while (value > 0 && tempIdx < 31)
                {
                    temp[tempIdx++] = static_cast<char>('0' + (value % 10));
                    value /= 10;
                }
                for (int i = tempIdx - 1; i >= 0; --i)
                {
                    sizeBuf[sizeIdx++] = temp[i];
                }
            }
            for (int i = 0; i < sizeIdx && pos + 1 < sizeof(line); ++i)
            {
                line[pos++] = sizeBuf[i];
            }
            line[pos++] = ' ';

            for (int i = 0; entry.name[i] && pos + 1 < sizeof(line); ++i)
            {
                line[pos++] = entry.name[i];
            }
            line[pos] = '\0';
            appendLine(line);
        }

        QFS::VFS::instance().closeDir(dir);
    }

    void Terminal::onCloseClick(QW::Controls::Button *button, void *userData)
    {
        (void)button;
        auto *self = static_cast<Terminal *>(userData);
        if (!self)
            return;

        QC_LOG_INFO("QDTerminal", "Close button clicked");
        self->close();
    }

} // namespace QD
