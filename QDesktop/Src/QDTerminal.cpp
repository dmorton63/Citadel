// QDesktop Terminal - Simple command interpreter window
// Namespace: QD

#include "QDTerminal.h"

#include "QDDesktop.h"

#include "QCLogger.h"
#include "QCString.h"
#include "QCBuiltins.h"
#include "QCCommandRegistry.h"
#include "QKSecurityCenter.h"
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

    static inline const char *readWord(const char *p, char *out, QC::usize outSize)
    {
        if (!out || outSize == 0)
            return p;
        out[0] = '\0';
        p = skipSpaces(p);
        if (!p || *p == '\0')
            return p;

        QC::usize i = 0;
        while (*p && *p != ' ' && *p != '\t' && i + 1 < outSize)
        {
            out[i++] = *p++;
        }
        out[i] = '\0';
        return p;
    }

    static bool parseU32Word(const char *p, QC::u32 &out)
    {
        if (!p)
            return false;
        p = skipSpaces(p);
        if (!p || *p == '\0')
            return false;

        QC::u64 value = 0;
        bool hasDigit = false;
        while (*p >= '0' && *p <= '9')
        {
            hasDigit = true;
            value = (value * 10u) + static_cast<QC::u64>(*p - '0');
            if (value > 0xFFFFFFFFull)
                return false;
            ++p;
        }
        if (!hasDigit)
            return false;

        p = skipSpaces(p);
        if (*p != '\0')
            return false;

        out = static_cast<QC::u32>(value);
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

    // TerminalFocusPanel: container that does NOT change keyboard focus
    // based on mouse clicks. This keeps the input TextBox focused while
    // still allowing mouse interactions (scrollbar drag, buttons, etc.).
    class TerminalFocusPanel final : public QW::Controls::Panel
    {
    public:
        TerminalFocusPanel(QW::Window *window, QC::Rect bounds)
            : QW::Controls::Panel(window, bounds)
        {
        }

        bool onMouseDown(QC::i32 x, QC::i32 y, QK::Event::MouseButton button) override
        {
            // Mirror Container behavior, but do NOT call setFocusedChild(child).
            // Keyboard focus stays on whatever the Terminal set (the input box).
            QW::Controls::IControl *child = childAtPoint(x, y);
            if (!child)
                return false;

            m_hoveredChild = child;

            const bool handled = child->onMouseDown(x, y, button);
            if (handled)
            {
                m_capturedChild = child;
            }
            return handled;
        }
    };
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
                    m_content(nullptr),
                    m_titleBar(nullptr),
                    m_titleLabel(nullptr),
          m_output(nullptr),
          m_outputScroll(nullptr),
          m_input(nullptr),
                    m_closeButton(nullptr),
                    m_minButton(nullptr),
                    m_maxButton(nullptr),
          m_windowListenerId(QK::Event::InvalidListenerId),
          m_followTail(true),
          m_syncingScroll(false),
                    m_isMaximized(false),
          m_restoreX(0),
          m_restoreY(0),
          m_restoreW(0),
          m_restoreH(0),
                    m_editorActive(false),
                    m_editorDirty(false),
                    m_editorLen(0),
                    m_callerAccess(static_cast<QC::u8>(QC::Cmd::AccessLevel::User)),
                    m_chmodeWindow(nullptr),
                    m_chmodeWindowId(0),
                    m_chmodeRoot(nullptr),
                    m_chmodeUser(nullptr),
                    m_chmodePass(nullptr),
                    m_chmodeOk(nullptr),
                    m_chmodeCancel(nullptr),
                    m_chmodePendingAccess(static_cast<QC::u8>(QC::Cmd::AccessLevel::User)),
                    m_shutdownPermWindow(nullptr),
                    m_shutdownPermWindowId(0),
                    m_shutdownPermRoot(nullptr),
                    m_shutdownPermOk(nullptr),
          m_savetermLastSavedCount(0),
          m_savetermHasBaseline(false)
    {
                QC::String::memset(m_editorPath, 0, sizeof(m_editorPath));
                QC::String::memset(m_editorBuffer, 0, sizeof(m_editorBuffer));
        QC::String::memset(m_savetermLastPath, 0, sizeof(m_savetermLastPath));
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

            if (self->m_chmodeWindowId != 0 && we.windowId == self->m_chmodeWindowId)
            {
                self->closeChmodeDialog();
            }

            if (self->m_shutdownPermWindowId != 0 && we.windowId == self->m_shutdownPermWindowId)
            {
                self->closeShutdownPermissionDialog();
            }
        }

        if (event.type() == QK::Event::Type::WindowFocus)
        {
            const auto &we = event.asWindow();
            if (self->m_windowId != 0 && we.windowId == self->m_windowId)
            {
                if (self->m_root && self->m_content)
                    self->m_root->setFocusedChild(self->m_content);
                if (self->m_content && self->m_input)
                    self->m_content->setFocusedChild(self->m_input);
            }
        }

        if (event.type() == QK::Event::Type::WindowResize)
        {
            const auto &we = event.asWindow();
            if (self->m_windowId != 0 && we.windowId == self->m_windowId)
            {
                self->layoutWindow();
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
        m_content = nullptr;
        m_titleBar = nullptr;
        m_titleLabel = nullptr;
        m_output = nullptr;
        m_outputScroll = nullptr;
        m_input = nullptr;
        m_closeButton = nullptr;
        m_minButton = nullptr;
        m_maxButton = nullptr;
        m_isMaximized = false;
        m_restoreX = 0;
        m_restoreY = 0;
        m_restoreW = 0;
        m_restoreH = 0;
        m_windowId = 0;
    }

    void Terminal::onOutputScrollChanged(QW::Controls::ScrollBar *scrollBar, void *userData)
    {
        auto *self = static_cast<Terminal *>(userData);
        if (!self || !scrollBar || !self->m_output)
            return;

        if (self->m_syncingScroll)
            return;

        static constexpr QC::i32 kTailSlack = 1;
        self->m_followTail = (scrollBar->value() + kTailSlack >= scrollBar->maximum());
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

        static constexpr QC::usize kTailSlack = 1;
        const QC::usize off = m_output->scrollOffset();
        m_followTail = (off + kTailSlack >= maxOffset);
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

        m_window->setFlags(QW::WindowFlags::Visible |
                   QW::WindowFlags::Resizable |
                   QW::WindowFlags::Movable |
                   QW::WindowFlags::HasTitle |
                   QW::WindowFlags::HasBorder |
                   QW::WindowFlags::HasClose |
                   QW::WindowFlags::HasMinimize |
                   QW::WindowFlags::HasMaximize);

        m_root = m_window->root();
        m_root->setBorderStyle(QW::Controls::BorderStyle::None);
        m_root->setPadding(0);
        m_isMaximized = false;
        {
            const QC::Rect b = m_window->bounds();
            m_restoreX = b.x;
            m_restoreY = b.y;
            m_restoreW = b.width;
            m_restoreH = b.height;
        }

        // Content container: keeps the input textbox focused even when
        // interacting with scrollbar/output via mouse.
        m_content = new TerminalFocusPanel(m_window, {0, 0, w, h});
        m_content->setBorderStyle(QW::Controls::BorderStyle::None);
        m_content->setPadding(0);
        m_root->addChild(m_content);

        static constexpr QC::i32 kTitleBarHeight = 24;
        static constexpr QC::i32 kPad = 8;
        static constexpr QC::u32 kInputHeight = 20;

        // Title bar (dedicated region for window dragging)
        {
            QC::Rect titleBounds = {0, 0, w, static_cast<QC::u32>(kTitleBarHeight)};
            m_titleBar = new QW::Controls::Panel(m_window, titleBounds);
            m_titleBar->setBorderStyle(QW::Controls::BorderStyle::None);
            m_titleBar->setBackgroundColor(QW::Color(20, 20, 20, 255));
            m_content->addChild(m_titleBar);

            QC::Rect titleLabelBounds = {kPad, 4, static_cast<QC::u32>(w > 64 ? (w - 64) : w), 16};
            m_titleLabel = new QW::Controls::Label(m_window, "Terminal", titleLabelBounds);
            m_titleLabel->setTextColor(QW::Color(230, 230, 230, 255));
            m_titleBar->addChild(m_titleLabel);
        }

        // Output view + scrollbar (below title bar)
        const QC::u32 scrollW = 14;
        const QC::i32 inputY = static_cast<QC::i32>(h) - kPad - static_cast<QC::i32>(kInputHeight);
        const QC::i32 outY = kTitleBarHeight + kPad;
        const QC::i32 outH = (inputY - kPad) - outY;
        QC::Rect outBounds = {kPad, outY,
                              static_cast<QC::u32>(w - static_cast<QC::u32>(kPad * 2) - scrollW - 4),
                              static_cast<QC::u32>(outH > 0 ? outH : 0)};
        QC::Rect scrollBounds = {static_cast<QC::i32>(outBounds.x + static_cast<QC::i32>(outBounds.width) + 4), outBounds.y, scrollW, outBounds.height};

        m_output = new QW::Controls::ListView(m_window, outBounds);
        m_output->setShowHeader(false);
        m_output->setSelectionMode(QW::Controls::SelectionMode::None);
        m_output->setItemHeight(16);
        m_output->setBackgroundColor(QW::Color(20, 20, 20, 255));
        m_output->setTextColor(QW::Color(230, 230, 230, 255));
        m_output->setSelectionColor(QW::Color(80, 120, 170, 255));
        m_output->setScrollOffsetChangeHandler(&Terminal::onOutputViewScrollChanged, this);
        m_content->addChild(m_output);

        m_outputScroll = new QW::Controls::ScrollBar(m_window, scrollBounds, QW::Controls::ScrollOrientation::Vertical);
        m_outputScroll->setBackgroundColor(QW::Color(20, 20, 20, 255));
        m_outputScroll->setTrackColor(QW::Color(20, 20, 20, 255));
        m_outputScroll->setThumbColor(QW::Color(110, 110, 110, 255));
        m_outputScroll->setArrowColor(QW::Color(230, 230, 230, 255));
        m_outputScroll->setScrollChangeHandler(&Terminal::onOutputScrollChanged, this);
        m_content->addChild(m_outputScroll);

        // Initial lines
        (void)m_output->addItem("CITADEL Terminal");
        (void)m_output->addItem("Type 'help'");
        scrollToTail();

        // Input textbox
        QC::Rect inBounds = {kPad, inputY, static_cast<QC::u32>(w - static_cast<QC::u32>(kPad * 2)), kInputHeight};
        m_input = new QW::Controls::TextBox(m_window, inBounds);
        m_input->setPlaceholder("command...");
        m_input->setBackgroundColor(QW::Color(20, 20, 20, 255));
        m_input->setTextColor(QW::Color(230, 230, 230, 255));
        m_input->setBorderColor(QW::Color(110, 110, 110, 255));
        m_input->setSelectionColor(QW::Color(80, 120, 170, 255));
        m_input->setTextSubmitHandler(&Terminal::onSubmit, this);
        m_content->addChild(m_input);

        // Window controls in title bar.
        m_minButton = new QW::Controls::Button(m_window, "_", {0, 0, 20, 20});
        m_minButton->setContentMode(QW::ButtonContentMode::Text);
        m_minButton->setVariant(QW::ButtonVariant::Compact);
        m_minButton->setRole(QW::ButtonRole::Default);
        m_minButton->setClickHandler(&Terminal::onMinClick, this);
        m_content->addChild(m_minButton);

        m_maxButton = new QW::Controls::Button(m_window, "[]", {0, 0, 20, 20});
        m_maxButton->setContentMode(QW::ButtonContentMode::Text);
        m_maxButton->setVariant(QW::ButtonVariant::Compact);
        m_maxButton->setRole(QW::ButtonRole::Default);
        m_maxButton->setClickHandler(&Terminal::onMaxClick, this);
        m_content->addChild(m_maxButton);

        m_closeButton = new QW::Controls::Button(m_window, "X", {0, 0, 20, 20});
        m_closeButton->setContentMode(QW::ButtonContentMode::Text);
        m_closeButton->setVariant(QW::ButtonVariant::Compact);
        m_closeButton->setRole(QW::ButtonRole::Destructive);
        m_closeButton->setClickHandler(&Terminal::onCloseClick, this);
        m_content->addChild(m_closeButton);

        layoutWindow();

        focus();
        if (m_root && m_content)
            m_root->setFocusedChild(m_content);
        if (m_content && m_input)
            m_content->setFocusedChild(m_input);
        m_window->invalidate();
        QW::WindowManager::instance().render();

        // Optional taskbar entry
        m_desktop->addTaskbarWindow(m_windowId, "Terminal");
        m_desktop->setActiveTaskbarWindow(m_windowId);
    }

    void Terminal::close()
    {
        closeChmodeDialog();
        closeShutdownPermissionDialog();

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
        m_content = nullptr;
        m_titleBar = nullptr;
        m_titleLabel = nullptr;
        m_output = nullptr;
        m_outputScroll = nullptr;
        m_input = nullptr;
        m_closeButton = nullptr;
        m_minButton = nullptr;
        m_maxButton = nullptr;
        m_isMaximized = false;
        m_restoreX = 0;
        m_restoreY = 0;
        m_restoreW = 0;
        m_restoreH = 0;
    }

    void Terminal::layoutWindow()
    {
        if (!m_window)
            return;

        const QC::Rect wb = m_window->bounds();
        const QC::u32 w = wb.width;
        const QC::u32 h = wb.height;

        static constexpr QC::i32 kTitleBarHeight = 24;
        static constexpr QC::i32 kPad = 8;
        static constexpr QC::u32 kInputHeight = 20;
        static constexpr QC::u32 kButtonWidth = 20;
        static constexpr QC::u32 kButtonGap = 2;
        static constexpr QC::u32 kScrollW = 14;

        if (m_content)
            m_content->setBounds({0, 0, w, h});

        if (m_titleBar)
            m_titleBar->setBounds({0, 0, w, static_cast<QC::u32>(kTitleBarHeight)});

        if (m_titleLabel)
        {
            const QC::u32 titleW = (w > 120) ? (w - 120) : 0;
            m_titleLabel->setBounds({kPad, 4, titleW, 16});
        }

        const QC::i32 inputY = static_cast<QC::i32>(h) - kPad - static_cast<QC::i32>(kInputHeight);
        const QC::i32 outY = kTitleBarHeight + kPad;
        const QC::i32 outH = (inputY - kPad) - outY;

        const QC::u32 outW =
            (w > static_cast<QC::u32>(kPad * 2) + kScrollW + 4)
                ? (w - static_cast<QC::u32>(kPad * 2) - kScrollW - 4)
                : 0;

        const QC::Rect outBounds = {kPad,
                                    outY,
                                    outW,
                                    static_cast<QC::u32>(outH > 0 ? outH : 0)};
        const QC::Rect scrollBounds = {
            static_cast<QC::i32>(outBounds.x + static_cast<QC::i32>(outBounds.width) + 4),
            outBounds.y,
            kScrollW,
            outBounds.height};

        if (m_output)
            m_output->setBounds(outBounds);
        if (m_outputScroll)
            m_outputScroll->setBounds(scrollBounds);

        const QC::u32 inputW = (w > static_cast<QC::u32>(kPad * 2)) ? (w - static_cast<QC::u32>(kPad * 2)) : 0;
        if (m_input)
            m_input->setBounds({kPad, inputY, inputW, kInputHeight});

        const QC::i32 closeX = static_cast<QC::i32>((w > kPad + static_cast<QC::i32>(kButtonWidth)) ? (w - kPad - kButtonWidth) : 0);
        const QC::i32 maxX = closeX - static_cast<QC::i32>(kButtonWidth + kButtonGap);
        const QC::i32 minX = maxX - static_cast<QC::i32>(kButtonWidth + kButtonGap);
        if (m_closeButton)
            m_closeButton->setBounds({closeX, 2, kButtonWidth, 20});
        if (m_maxButton)
            m_maxButton->setBounds({maxX, 2, kButtonWidth, 20});
        if (m_minButton)
            m_minButton->setBounds({minX, 2, kButtonWidth, 20});

        syncScrollUi();
    }

    void Terminal::minimizeWindow()
    {
        if (!m_window)
            return;
        m_window->setVisible(false);
        if (m_desktop)
            m_desktop->setActiveTaskbarWindow(0);
    }

    void Terminal::toggleMaximizeWindow()
    {
        if (!m_window || !m_desktop)
            return;

        if (!m_isMaximized)
        {
            const QC::Rect b = m_window->bounds();
            m_restoreX = b.x;
            m_restoreY = b.y;
            m_restoreW = b.width;
            m_restoreH = b.height;
            const QC::Rect wa = m_desktop->workArea();
            m_window->setBounds({wa.x, wa.y, wa.width, wa.height});
            m_isMaximized = true;
            if (m_maxButton)
                m_maxButton->setText("R");
        }
        else
        {
            if (m_restoreW == 0 || m_restoreH == 0)
            {
                const QC::Rect wa = m_desktop->workArea();
                m_restoreX = wa.x + 32;
                m_restoreY = wa.y + 32;
                m_restoreW = 640;
                m_restoreH = 360;
            }
            m_window->setBounds({m_restoreX, m_restoreY, m_restoreW, m_restoreH});
            m_isMaximized = false;
            if (m_maxButton)
                m_maxButton->setText("[]");
        }

        layoutWindow();
        m_window->invalidate();
        QW::WindowManager::instance().render();
    }

    bool Terminal::resolveInputPath(const char *rawPath, char *outPath, QC::usize outCap) const
    {
        if (!outPath || outCap == 0)
            return false;
        outPath[0] = '\0';

        const char *arg = skipSpaces(rawPath);
        if (!arg || *arg == '\0')
            return false;

        if (!hasSlash(arg))
        {
            QC::String::strncpy(outPath, "/shared/", outCap - 1);
            const QC::usize used = QC::String::strlen(outPath);
            QC::String::strncpy(outPath + used, arg, outCap - 1 - used);
        }
        else
        {
            QC::String::strncpy(outPath, arg, outCap - 1);
        }
        outPath[outCap - 1] = '\0';
        return outPath[0] != '\0';
    }

    void Terminal::viewFile(const char *pathArg)
    {
        char path[256];
        QC::String::memset(path, 0, sizeof(path));
        if (!resolveInputPath(pathArg, path, sizeof(path)))
        {
            appendLine("usage: view <path>");
            appendLine("  (paths default to /shared if no slash)");
            return;
        }

        QFS::File *file = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
        if (!file)
        {
            appendLine("view: cannot open file");
            return;
        }

        appendLine("view: begin");
        appendLine(path);

        static constexpr QC::usize kChunkSize = 256;
        static constexpr QC::usize kLineCap = 384;
        static constexpr QC::u32 kMaxLines = 500;

        char chunk[kChunkSize];
        char line[kLineCap];
        QC::usize lineLen = 0;
        QC::u32 linesShown = 0;
        bool truncated = false;

        auto flushLine = [&]() {
            line[lineLen] = '\0';
            appendLine(line);
            lineLen = 0;
            ++linesShown;
            if (linesShown >= kMaxLines)
                truncated = true;
        };

        while (!truncated)
        {
            const QC::isize n = file->read(chunk, sizeof(chunk));
            if (n <= 0)
                break;

            for (QC::isize i = 0; i < n; ++i)
            {
                const char c = chunk[i];
                if (c == '\r')
                    continue;
                if (c == '\n')
                {
                    flushLine();
                    if (truncated)
                        break;
                    continue;
                }

                if (lineLen + 1 < sizeof(line))
                {
                    line[lineLen++] = c;
                }
                else
                {
                    flushLine();
                    if (truncated)
                        break;
                    line[lineLen++] = c;
                }
            }
        }

        if (!truncated && lineLen > 0)
            flushLine();

        QFS::VFS::instance().close(file);

        if (truncated)
            appendLine("view: output truncated (first 500 lines)");
        appendLine("view: end");
    }

    void Terminal::openMiniEditor(const char *pathArg)
    {
        char path[256];
        QC::String::memset(path, 0, sizeof(path));

        const char *target = pathArg;
        if (!target || *skipSpaces(target) == '\0')
            target = "note.txt";

        if (!resolveInputPath(target, path, sizeof(path)))
        {
            appendLine("edit: invalid path");
            return;
        }

        m_editorActive = true;
        m_editorDirty = false;
        m_editorLen = 0;
        QC::String::memset(m_editorBuffer, 0, sizeof(m_editorBuffer));
        QC::String::strncpy(m_editorPath, path, sizeof(m_editorPath) - 1);

        QFS::File *file = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
        if (file)
        {
            while (m_editorLen + 1 < EDITOR_BUFFER_CAP)
            {
                const QC::usize remain = EDITOR_BUFFER_CAP - 1 - m_editorLen;
                const QC::usize chunk = (remain > 256) ? 256 : remain;
                const QC::isize n = file->read(m_editorBuffer + m_editorLen, chunk);
                if (n <= 0)
                    break;
                m_editorLen += static_cast<QC::usize>(n);
            }
            m_editorBuffer[m_editorLen] = '\0';
            QFS::VFS::instance().close(file);
        }

        appendLine("mini-editor: active");
        appendLine(m_editorPath);
        appendLine(":h help, :p print, :w write, :q quit, :q! force quit, :wq write+quit");
        appendLine("Type text lines to append.");
    }

    void Terminal::showMiniEditorBuffer()
    {
        if (!m_editorActive)
            return;

        appendLine("----- mini-editor buffer -----");
        if (m_editorLen == 0)
        {
            appendLine("(empty)");
            appendLine("------------------------------");
            return;
        }

        char out[460];
        char line[360];
        QC::usize lineLen = 0;
        QC::u32 lineNo = 1;

        auto emit = [&]() {
            line[lineLen] = '\0';
            QC::String::memset(out, 0, sizeof(out));
            QC::usize pos = 0;

            char digits[16];
            QC::usize di = 0;
            QC::u32 v = lineNo;
            do
            {
                digits[di++] = static_cast<char>('0' + (v % 10));
                v /= 10;
            } while (v && di < sizeof(digits));

            for (QC::isize i = static_cast<QC::isize>(di) - 1; i >= 0 && pos + 1 < sizeof(out); --i)
                out[pos++] = digits[static_cast<QC::usize>(i)];

            if (pos + 3 < sizeof(out))
            {
                out[pos++] = '|';
                out[pos++] = ' ';
            }

            for (QC::usize i = 0; i < lineLen && pos + 1 < sizeof(out); ++i)
                out[pos++] = line[i];
            out[pos] = '\0';
            appendLine(out);
            ++lineNo;
            lineLen = 0;
        };

        for (QC::usize i = 0; i < m_editorLen; ++i)
        {
            const char c = m_editorBuffer[i];
            if (c == '\r')
                continue;
            if (c == '\n')
            {
                emit();
                continue;
            }
            if (lineLen + 1 < sizeof(line))
                line[lineLen++] = c;
        }
        if (lineLen > 0)
            emit();

        appendLine("------------------------------");
    }

    bool Terminal::saveMiniEditorBuffer()
    {
        if (!m_editorActive)
            return false;

        QFS::File *file = QFS::VFS::instance().open(
            m_editorPath,
            QFS::OpenMode::Write | QFS::OpenMode::Create | QFS::OpenMode::Truncate);
        if (!file)
            return false;

        QC::usize written = 0;
        while (written < m_editorLen)
        {
            const QC::isize n = file->write(m_editorBuffer + written, m_editorLen - written);
            if (n <= 0)
                break;
            written += static_cast<QC::usize>(n);
        }

        QFS::VFS::instance().close(file);
        if (written != m_editorLen)
            return false;

        m_editorDirty = false;
        return true;
    }

    void Terminal::handleMiniEditorLine(const char *line)
    {
        if (!m_editorActive)
            return;

        const char *p = line ? line : "";
        p = skipSpaces(p);

        if (*p == ':')
        {
            ++p;
            p = skipSpaces(p);
            if (streqIgnoreCase(p, "h") || streqIgnoreCase(p, "help"))
            {
                appendLine(":h help, :p print, :w write, :q quit, :q! force quit, :wq write+quit");
                return;
            }
            if (streqIgnoreCase(p, "p") || streqIgnoreCase(p, "print"))
            {
                showMiniEditorBuffer();
                return;
            }
            if (streqIgnoreCase(p, "w"))
            {
                if (saveMiniEditorBuffer())
                    appendLine("mini-editor: saved");
                else
                    appendLine("mini-editor: save failed");
                return;
            }
            if (streqIgnoreCase(p, "q"))
            {
                if (m_editorDirty)
                {
                    appendLine("mini-editor: unsaved changes; use :w or :q!");
                    return;
                }
                m_editorActive = false;
                appendLine("mini-editor: closed");
                return;
            }
            if (streqIgnoreCase(p, "q!"))
            {
                m_editorActive = false;
                m_editorDirty = false;
                appendLine("mini-editor: closed (discarded)");
                return;
            }
            if (streqIgnoreCase(p, "wq"))
            {
                if (saveMiniEditorBuffer())
                {
                    m_editorActive = false;
                    appendLine("mini-editor: saved + closed");
                }
                else
                {
                    appendLine("mini-editor: save failed");
                }
                return;
            }

            appendLine("mini-editor: unknown command (try :h)");
            return;
        }

        const QC::usize inLen = line ? QC::String::strlen(line) : 0;
        const QC::usize need = inLen + 1; // keep newline
        if (m_editorLen + need + 1 >= EDITOR_BUFFER_CAP)
        {
            appendLine("mini-editor: buffer full");
            return;
        }

        if (inLen > 0)
            QC::String::memcpy(m_editorBuffer + m_editorLen, line, inLen);
        m_editorLen += inLen;
        m_editorBuffer[m_editorLen++] = '\n';
        m_editorBuffer[m_editorLen] = '\0';
        m_editorDirty = true;
    }

    void Terminal::setCallerAccess(QC::u8 access)
    {
        const QC::u8 maxAccess = static_cast<QC::u8>(QC::Cmd::AccessLevel::System);
        if (access > maxAccess)
            access = maxAccess;
        m_callerAccess = access;

        const QC::u8 userLvl = static_cast<QC::u8>(QC::Cmd::AccessLevel::User);
        if (m_callerAccess <= userLvl)
        {
            // Dropping back to User/guest should lock the Owner session so future elevations
            // require credentials again.
            QK::SecurityCenter::instance().ownerLock();
        }
    }

    const char *Terminal::callerAccessName() const
    {
        const auto lvl = static_cast<QC::Cmd::AccessLevel>(m_callerAccess);
        switch (lvl)
        {
        case QC::Cmd::AccessLevel::Everyone:
            return "guest";
        case QC::Cmd::AccessLevel::User:
            return "User";
        case QC::Cmd::AccessLevel::Admin:
            return "Admin";
        case QC::Cmd::AccessLevel::SysAdmin:
            return "SysAdmin";
        case QC::Cmd::AccessLevel::System:
            return "System";
        default:
            return "unknown";
        }
    }

    void Terminal::openChmodeDialog(QC::u8 targetAccess)
    {
        closeChmodeDialog();

        if (!m_desktop)
            return;

        static constexpr QC::i32 DIALOG_WIDTH = 460;
        static constexpr QC::i32 DIALOG_HEIGHT = 250;

        const QC::Rect work = m_desktop->workArea();
        QC::i32 x = work.x + static_cast<QC::i32>((work.width - DIALOG_WIDTH) / 2);
        QC::i32 y = work.y + static_cast<QC::i32>((work.height - DIALOG_HEIGHT) / 2);
        QW::Rect bounds = {x, y, static_cast<QC::u32>(DIALOG_WIDTH), static_cast<QC::u32>(DIALOG_HEIGHT)};

        const auto lvl = static_cast<QC::Cmd::AccessLevel>(targetAccess);
        const char *title = "Credentials";
        if (lvl == QC::Cmd::AccessLevel::Admin)
            title = "Admin Credentials";
        else if (lvl == QC::Cmd::AccessLevel::System)
            title = "System Credentials";

        m_chmodeWindow = QW::WindowManager::instance().createWindow(title, bounds);
        if (!m_chmodeWindow)
            return;

        m_chmodeWindowId = m_chmodeWindow->windowId();
        m_chmodeWindow->setFlags(QW::WindowFlags::Visible | QW::WindowFlags::Movable | QW::WindowFlags::HasTitle | QW::WindowFlags::HasBorder);

        m_chmodeRoot = m_chmodeWindow->root();
        if (!m_chmodeRoot)
        {
            closeChmodeDialog();
            return;
        }

        m_chmodePendingAccess = targetAccess;

        m_chmodeRoot->setPadding(14);
        m_chmodeRoot->setBorderStyle(QW::Controls::BorderStyle::None);

        const char *modeName = (lvl == QC::Cmd::AccessLevel::System) ? "System" : "Admin";

        QW::Rect hintBounds = {18, 20, static_cast<QC::u32>(DIALOG_WIDTH - 36), 40};
        auto *hint = new QW::Controls::Label(m_chmodeWindow, "Enter credentials (fake) to elevate:", hintBounds);
        hint->setWordWrap(true);
        m_chmodeRoot->addChild(hint);

        QW::Rect modeBounds = {18, 52, static_cast<QC::u32>(DIALOG_WIDTH - 36), 20};
        auto *mode = new QW::Controls::Label(m_chmodeWindow, modeName, modeBounds);
        m_chmodeRoot->addChild(mode);

        QW::Rect userLabelBounds = {24, 98, 110, 24};
        auto *userLabel = new QW::Controls::Label(m_chmodeWindow, "User:", userLabelBounds);
        m_chmodeRoot->addChild(userLabel);

        QW::Rect userBoxBounds = {24 + 110, 98, static_cast<QC::u32>(DIALOG_WIDTH - 24 - 110 - 24), 24};
        m_chmodeUser = new QW::Controls::TextBox(m_chmodeWindow, userBoxBounds);
        m_chmodeUser->setPlaceholder("user");
        m_chmodeUser->setMaxLength(32);
        m_chmodeRoot->addChild(m_chmodeUser);

        QW::Rect passLabelBounds = {24, 130, 110, 24};
        auto *passLabel = new QW::Controls::Label(m_chmodeWindow, "Password:", passLabelBounds);
        m_chmodeRoot->addChild(passLabel);

        QW::Rect passBoxBounds = {24 + 110, 130, static_cast<QC::u32>(DIALOG_WIDTH - 24 - 110 - 24), 24};
        m_chmodePass = new QW::Controls::TextBox(m_chmodeWindow, passBoxBounds);
        m_chmodePass->setPlaceholder("password");
        m_chmodePass->setPassword(true);
        m_chmodePass->setMaxLength(64);
        m_chmodeRoot->addChild(m_chmodePass);

        const QC::i32 buttonWidth = 140;
        const QC::i32 buttonHeight = 32;
        const QC::i32 spacing = 14;
        const QC::i32 baseY = DIALOG_HEIGHT - buttonHeight - 22;
        const QC::i32 startX = (DIALOG_WIDTH - (buttonWidth * 2 + spacing)) / 2;

        QW::Rect okBounds = {startX, baseY, static_cast<QC::u32>(buttonWidth), static_cast<QC::u32>(buttonHeight)};
        m_chmodeOk = new QW::Controls::Button(m_chmodeWindow, "OK", okBounds);
        m_chmodeOk->setContentMode(QW::ButtonContentMode::Text);
        m_chmodeOk->setRole(QW::ButtonRole::Accent);
        m_chmodeOk->setClickHandler(&Terminal::onChmodeOkClick, this);
        m_chmodeRoot->addChild(m_chmodeOk);

        QW::Rect cancelBounds = {startX + buttonWidth + spacing, baseY, static_cast<QC::u32>(buttonWidth), static_cast<QC::u32>(buttonHeight)};
        m_chmodeCancel = new QW::Controls::Button(m_chmodeWindow, "Cancel", cancelBounds);
        m_chmodeCancel->setContentMode(QW::ButtonContentMode::Text);
        m_chmodeCancel->setRole(QW::ButtonRole::Default);
        m_chmodeCancel->setClickHandler(&Terminal::onChmodeCancelClick, this);
        m_chmodeRoot->addChild(m_chmodeCancel);

        QW::WindowManager::instance().bringToFront(m_chmodeWindow);
        QW::WindowManager::instance().setFocus(m_chmodeWindow);
        m_chmodeWindow->setVisible(true);
        QW::WindowManager::instance().render();
    }

    void Terminal::closeChmodeDialog()
    {
        if (!m_chmodeWindow)
            return;

        QW::WindowManager::instance().destroyWindow(m_chmodeWindow);
        m_chmodeWindow = nullptr;

        m_chmodeWindowId = 0;
        m_chmodeRoot = nullptr;
        m_chmodeUser = nullptr;
        m_chmodePass = nullptr;
        m_chmodeOk = nullptr;
        m_chmodeCancel = nullptr;
    }

    void Terminal::onChmodeOkClick(QW::Controls::Button *button, void *userData)
    {
        (void)button;
        auto *self = static_cast<Terminal *>(userData);
        if (!self)
            return;

        const bool bypass = QK::SecurityCenter::instance().bypassEnabled();
        if (!bypass)
        {
            const char *user = self->m_chmodeUser ? self->m_chmodeUser->text() : nullptr;
            const char *pass = self->m_chmodePass ? self->m_chmodePass->text() : nullptr;
            const QC::Status st = QK::SecurityCenter::instance().ownerUnlock(user, pass);
            if (st != QC::Status::Success)
            {
                char msg[160];
                QC::String::memset(msg, 0, sizeof(msg));
                QC::String::strncpy(msg, "chmode: denied", sizeof(msg) - 1);

                if (!QK::SecurityCenter::instance().ownerIsEnrolled())
                {
                    const QC::usize used = QC::String::strlen(msg);
                    QC::String::strncpy(msg + used, " (no Owner enrolled; use sys_user_enroll)", sizeof(msg) - 1 - used);
                }
                else if (QK::SecurityCenter::instance().ownerLockedOut())
                {
                    const QC::usize used = QC::String::strlen(msg);
                    QC::String::strncpy(msg + used, " (Owner unlock backoff active)", sizeof(msg) - 1 - used);
                }
                else if (!QK::SecurityCenter::instance().ownerUnlocked())
                {
                    const QC::usize used = QC::String::strlen(msg);
                    QC::String::strncpy(msg + used, " (Owner locked; use sys_user_unlock or valid Owner creds)", sizeof(msg) - 1 - used);
                }

                const QC::u32 backoff = QK::SecurityCenter::instance().ownerUnlockBackoffMs();
                if (backoff)
                {
                    const QC::usize used0 = QC::String::strlen(msg);
                    QC::String::strncpy(msg + used0, " (backoff ", sizeof(msg) - 1 - used0);

                    // Append backoff integer (ms)
                    char num[16];
                    QC::String::memset(num, 0, sizeof(num));
                    QC::u32 v = backoff;
                    char tmp[16];
                    QC::String::memset(tmp, 0, sizeof(tmp));
                    QC::usize n = 0;
                    do
                    {
                        tmp[n++] = static_cast<char>('0' + (v % 10));
                        v /= 10;
                    } while (v && n < sizeof(tmp));
                    for (QC::usize i = 0; i < n && i < sizeof(num) - 1; ++i)
                        num[i] = tmp[n - 1 - i];

                    const QC::usize used1 = QC::String::strlen(msg);
                    QC::String::strncpy(msg + used1, num, sizeof(msg) - 1 - used1);
                    const QC::usize used2 = QC::String::strlen(msg);
                    QC::String::strncpy(msg + used2, "ms)", sizeof(msg) - 1 - used2);
                }

                self->appendLine(msg);
                self->focus();
                return;
            }
        }

        self->setCallerAccess(self->m_chmodePendingAccess);
        self->closeChmodeDialog();

        char msg[96];
        QC::String::memset(msg, 0, sizeof(msg));
        QC::String::strncpy(msg, "chmode: now ", sizeof(msg) - 1);
        const QC::usize used = QC::String::strlen(msg);
        QC::String::strncpy(msg + used, self->callerAccessName(), sizeof(msg) - 1 - used);
        self->appendLine(msg);
        self->focus();
    }

    void Terminal::onChmodeCancelClick(QW::Controls::Button *button, void *userData)
    {
        (void)button;
        auto *self = static_cast<Terminal *>(userData);
        if (!self)
            return;

        self->closeChmodeDialog();
        self->appendLine("chmode: cancelled");
        self->focus();
    }

    void Terminal::openShutdownPermissionDialog()
    {
        if (m_shutdownPermWindow)
        {
            QW::WindowManager::instance().bringToFront(m_shutdownPermWindow);
            QW::WindowManager::instance().setFocus(m_shutdownPermWindow);
            m_shutdownPermWindow->setVisible(true);
            QW::WindowManager::instance().render();
            return;
        }

        if (!m_desktop)
            return;

        static constexpr QC::i32 DIALOG_WIDTH = 560;
        static constexpr QC::i32 DIALOG_HEIGHT = 180;

        const QC::Rect work = m_desktop->workArea();
        QC::i32 x = work.x + static_cast<QC::i32>((work.width - DIALOG_WIDTH) / 2);
        QC::i32 y = work.y + static_cast<QC::i32>((work.height - DIALOG_HEIGHT) / 2);
        QW::Rect bounds = {x, y, static_cast<QC::u32>(DIALOG_WIDTH), static_cast<QC::u32>(DIALOG_HEIGHT)};

        m_shutdownPermWindow = QW::WindowManager::instance().createWindow("Permission Required", bounds);
        if (!m_shutdownPermWindow)
            return;

        m_shutdownPermWindowId = m_shutdownPermWindow->windowId();
        m_shutdownPermWindow->setFlags(QW::WindowFlags::Visible | QW::WindowFlags::Movable | QW::WindowFlags::HasTitle | QW::WindowFlags::HasBorder);

        m_shutdownPermRoot = m_shutdownPermWindow->root();
        if (!m_shutdownPermRoot)
        {
            closeShutdownPermissionDialog();
            return;
        }

        m_shutdownPermRoot->setPadding(14);
        m_shutdownPermRoot->setBorderStyle(QW::Controls::BorderStyle::None);

        QW::Rect msgBounds = {18, 24, static_cast<QC::u32>(DIALOG_WIDTH - 36), 70};
        auto *msg = new QW::Controls::Label(
            m_shutdownPermWindow,
            "You must have Administrative permissions to execute shutdown from the command line.",
            msgBounds);
        msg->setWordWrap(true);
        m_shutdownPermRoot->addChild(msg);

        const QC::i32 buttonWidth = 160;
        const QC::i32 buttonHeight = 32;
        const QC::i32 baseY = DIALOG_HEIGHT - buttonHeight - 20;
        const QC::i32 startX = (DIALOG_WIDTH - buttonWidth) / 2;
        QW::Rect okBounds = {startX, baseY, static_cast<QC::u32>(buttonWidth), static_cast<QC::u32>(buttonHeight)};
        m_shutdownPermOk = new QW::Controls::Button(m_shutdownPermWindow, "OK", okBounds);
        m_shutdownPermOk->setContentMode(QW::ButtonContentMode::Text);
        m_shutdownPermOk->setRole(QW::ButtonRole::Default);
        m_shutdownPermOk->setClickHandler(&Terminal::onShutdownPermissionOkClick, this);
        m_shutdownPermRoot->addChild(m_shutdownPermOk);

        QW::WindowManager::instance().bringToFront(m_shutdownPermWindow);
        QW::WindowManager::instance().setFocus(m_shutdownPermWindow);
        m_shutdownPermWindow->setVisible(true);
        QW::WindowManager::instance().render();
    }

    void Terminal::closeShutdownPermissionDialog()
    {
        if (!m_shutdownPermWindow)
            return;

        QW::WindowManager::instance().destroyWindow(m_shutdownPermWindow);
        m_shutdownPermWindow = nullptr;

        m_shutdownPermWindowId = 0;
        m_shutdownPermRoot = nullptr;
        m_shutdownPermOk = nullptr;
    }

    void Terminal::onShutdownPermissionOkClick(QW::Controls::Button *button, void *userData)
    {
        (void)button;
        auto *self = static_cast<Terminal *>(userData);
        if (!self)
            return;

        self->closeShutdownPermissionDialog();
        self->focus();
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

        // Ensure the Terminal's input remains the keyboard focus target
        // while this window is focused.
        if (m_root && m_content)
            m_root->setFocusedChild(m_content);
        if (m_content && m_input)
            m_content->setFocusedChild(m_input);
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

        // Preserve follow-tail state across appends; syncScrollUi() recomputes
        // maxOffset and would otherwise disable tail-follow on new output.
        const bool shouldFollowTail = m_followTail;

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
        if (shouldFollowTail)
        {
            scrollToTail();
        }
    }

    void Terminal::executeLine(const char *line)
    {
        if (!line)
            return;

        if (m_editorActive)
        {
            handleMiniEditorLine(line);
            return;
        }

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

        // Role aliases for convenience.
        if (streqIgnoreCase(cmd, "whoami"))
        {
            char msg[96];
            QC::String::memset(msg, 0, sizeof(msg));
            QC::String::strncpy(msg, "role=", sizeof(msg) - 1);
            const QC::usize used = QC::String::strlen(msg);
            QC::String::strncpy(msg + used, callerAccessName(), sizeof(msg) - 1 - used);
            appendLine(msg);
            return;
        }

        if (streqIgnoreCase(cmd, "user"))
        {
            setCallerAccess(static_cast<QC::u8>(QC::Cmd::AccessLevel::User));
            appendLine("chmode: now user");
            return;
        }

        if (streqIgnoreCase(cmd, "admin"))
        {
            // Use the same UX behavior as chmode admin (dialog).
            openChmodeDialog(static_cast<QC::u8>(QC::Cmd::AccessLevel::Admin));
            return;
        }

        if (streqIgnoreCase(cmd, "su"))
        {
            // Map su -> system for now.
            openChmodeDialog(static_cast<QC::u8>(QC::Cmd::AccessLevel::System));
            return;
        }

        if (streqIgnoreCase(cmd, "chmode"))
        {
            char mode[32];
            QC::String::memset(mode, 0, sizeof(mode));
            (void)readWord(p, mode, sizeof(mode));

            if (mode[0] == '\0')
            {
                char msg[128];
                QC::String::memset(msg, 0, sizeof(msg));
                QC::String::strncpy(msg, "chmode: current=", sizeof(msg) - 1);
                const QC::usize used = QC::String::strlen(msg);
                QC::String::strncpy(msg + used, callerAccessName(), sizeof(msg) - 1 - used);
                appendLine(msg);
                appendLine("usage: chmode [Admin|User|System|guest]");
                return;
            }

            QC::u8 target = m_callerAccess;
            bool needsDialog = false;

            if (streqIgnoreCase(mode, "guest") || streqIgnoreCase(mode, "everyone"))
            {
                target = static_cast<QC::u8>(QC::Cmd::AccessLevel::Everyone);
            }
            else if (streqIgnoreCase(mode, "user"))
            {
                target = static_cast<QC::u8>(QC::Cmd::AccessLevel::User);
            }
            else if (streqIgnoreCase(mode, "admin"))
            {
                target = static_cast<QC::u8>(QC::Cmd::AccessLevel::Admin);
                needsDialog = true;
            }
            else if (streqIgnoreCase(mode, "system"))
            {
                target = static_cast<QC::u8>(QC::Cmd::AccessLevel::System);
                needsDialog = true;
            }
            else
            {
                appendLine("chmode: unknown mode");
                appendLine("usage: chmode [Admin|User|System|guest]");
                return;
            }

            // Downgrade happens immediately; elevation prompts with a fake credentials dialog.
            if (!needsDialog || target <= m_callerAccess)
            {
                setCallerAccess(target);
                char msg[96];
                QC::String::memset(msg, 0, sizeof(msg));
                QC::String::strncpy(msg, "chmode: now ", sizeof(msg) - 1);
                const QC::usize used = QC::String::strlen(msg);
                QC::String::strncpy(msg + used, callerAccessName(), sizeof(msg) - 1 - used);
                appendLine(msg);
            }
            else
            {
                openChmodeDialog(target);
            }

            return;
        }

        if (streqIgnoreCase(cmd, "shutdown"))
        {
            const QC::u8 admin = static_cast<QC::u8>(QC::Cmd::AccessLevel::Admin);
            if (m_callerAccess < admin)
            {
                openShutdownPermissionDialog();
                return;
            }
            // Admin+ falls through to the shared CommandProcessor.
        }

        if (streqIgnoreCase(cmd, "min"))
        {
            minimizeWindow();
            return;
        }

        if (streqIgnoreCase(cmd, "max") || streqIgnoreCase(cmd, "maximize"))
        {
            if (!m_isMaximized)
                toggleMaximizeWindow();
            return;
        }

        if (streqIgnoreCase(cmd, "restore"))
        {
            if (m_isMaximized)
                toggleMaximizeWindow();
            return;
        }

        if (streqIgnoreCase(cmd, "resize"))
        {
            char wWord[24];
            char hWord[24];
            QC::String::memset(wWord, 0, sizeof(wWord));
            QC::String::memset(hWord, 0, sizeof(hWord));
            const char *rest = readWord(p, wWord, sizeof(wWord));
            (void)readWord(rest, hWord, sizeof(hWord));

            QC::u32 reqW = 0;
            QC::u32 reqH = 0;
            if (!parseU32Word(wWord, reqW) || !parseU32Word(hWord, reqH))
            {
                appendLine("usage: resize <width> <height>");
                return;
            }

            const QC::Rect wa = m_desktop ? m_desktop->workArea() : QC::Rect{0, 0, 0, 0};
            if (reqW < 420)
                reqW = 420;
            if (reqH < 220)
                reqH = 220;
            if (wa.width && reqW > wa.width)
                reqW = wa.width;
            if (wa.height && reqH > wa.height)
                reqH = wa.height;

            if (m_window)
            {
                QC::Rect b = m_window->bounds();
                b.width = reqW;
                b.height = reqH;
                m_window->setBounds(b);
                m_isMaximized = false;
                if (m_maxButton)
                    m_maxButton->setText("[]");
                layoutWindow();
                m_window->invalidate();
                QW::WindowManager::instance().render();
            }

            appendLine("window: resized");
            return;
        }

        if (streqIgnoreCase(cmd, "clear"))
        {
            if (m_output)
            {
                m_output->clearItems();
            }
            m_savetermLastSavedCount = 0;
            m_followTail = true;
            syncScrollUi();
            return;
        }

        if (streqIgnoreCase(cmd, "saveterm"))
        {
            bool appendMode = false;
            const char *pathArg = nullptr;

            if (p && *p)
            {
                char w1[32];
                QC::String::memset(w1, 0, sizeof(w1));
                const char *rest = readWord(p, w1, sizeof(w1));
                rest = skipSpaces(rest);
                if (streqIgnoreCase(w1, "append"))
                {
                    appendMode = true;
                    pathArg = (rest && *rest) ? rest : nullptr;
                }
                else
                {
                    pathArg = p;
                }
            }

            const char *arg = pathArg;

            char outPath[256];
            QC::String::memset(outPath, 0, sizeof(outPath));

            if ((!arg || *arg == '\0') && appendMode && m_savetermHasBaseline && m_savetermLastPath[0])
            {
                QC::String::strncpy(outPath, m_savetermLastPath, sizeof(outPath) - 1);
            }
            else if (!arg || *arg == '\0')
            {
                QC::String::strncpy(outPath, "/dump/citadel.txt", sizeof(outPath) - 1);
            }
            else if (!hasSlash(arg))
            {
                QC::String::strncpy(outPath, "/dump/", sizeof(outPath) - 1);
                QC::usize used = QC::String::strlen(outPath);
                QC::String::strncpy(outPath + used, arg, sizeof(outPath) - 1 - used);
            }
            else
            {
                QC::String::strncpy(outPath, arg, sizeof(outPath) - 1);
            }

            QC::usize start = 0;
            QC::usize count = 0;
            if (m_output)
            {
                count = m_output->itemCount();
                if (appendMode && m_savetermHasBaseline && streq(m_savetermLastPath, outPath))
                {
                    start = m_savetermLastSavedCount;
                    if (start > count)
                        start = 0;
                }
            }

            if (appendMode && (!m_output || start >= count))
            {
                appendLine("saveterm append: nothing new to append");
                return;
            }

            QFS::OpenMode mode = QFS::OpenMode::Write | QFS::OpenMode::Create;
            if (!appendMode)
                mode = mode | QFS::OpenMode::Truncate;

            QFS::File *file = QFS::VFS::instance().open(outPath, mode);
            if (!file)
            {
                appendLine("saveterm: cannot open output file (is the target path mounted + writable?)");
                return;
            }

            if (appendMode)
            {
                (void)file->seek(0, QFS::SeekOrigin::End);
            }

            if (m_output)
            {
                char outBuf[2048];
                QC::usize outLen = 0;

                auto flushBuf = [&]() {
                    if (outLen == 0)
                        return;
                    (void)file->write(outBuf, outLen);
                    outLen = 0;
                };

                auto appendBytes = [&](const char *s, QC::usize len) {
                    if (!s || len == 0)
                        return;
                    while (len > 0)
                    {
                        const QC::usize avail = sizeof(outBuf) - outLen;
                        if (avail == 0)
                        {
                            flushBuf();
                            continue;
                        }
                        const QC::usize chunk = (len < avail) ? len : avail;
                        QC::String::memcpy(outBuf + outLen, s, chunk);
                        outLen += chunk;
                        s += chunk;
                        len -= chunk;
                    }
                };

                for (QC::usize i = start; i < count; ++i)
                {
                    const auto *it = m_output->item(i);
                    if (!it)
                        continue;
                    const QC::usize len = QC::String::strlen(it->text);
                    if (len)
                        appendBytes(it->text, len);
                    appendBytes("\r\n", 2);
                }

                flushBuf();
            }

            QFS::VFS::instance().close(file);

            m_savetermHasBaseline = true;
            QC::String::memset(m_savetermLastPath, 0, sizeof(m_savetermLastPath));
            QC::String::strncpy(m_savetermLastPath, outPath, sizeof(m_savetermLastPath) - 1);

            if (appendMode)
                appendLine("saveterm: appended transcript to:");
            else
                appendLine("saveterm: wrote transcript to:");
            appendLine(outPath);

            // Skip the status lines we just printed.
            m_savetermLastSavedCount = m_output ? m_output->itemCount() : 0;
            return;
        }

        if (streqIgnoreCase(cmd, "view"))
        {
            viewFile(p);
            return;
        }

        if (streqIgnoreCase(cmd, "edit") || streqIgnoreCase(cmd, "nano") || streqIgnoreCase(cmd, "vim"))
        {
            openMiniEditor(p);
            return;
        }

        if (streqIgnoreCase(cmd, "browsefile"))
        {
            char pathBuf[256];
            QC::String::memset(pathBuf, 0, sizeof(pathBuf));
            (void)readWord(p, pathBuf, sizeof(pathBuf));

            if (pathBuf[0] == '\0')
            {
                appendLine("usage: browsefile <path | http://url>");
                appendLine("  (paths default to /shared if no slash; URLs are plain HTTP only)");
                return;
            }

            if (!m_desktop)
            {
                appendLine("browsefile: no desktop");
                return;
            }

            if (startsWith(pathBuf, "http://") || startsWith(pathBuf, "https://"))
            {
                m_desktop->openBrowserUrl(pathBuf);
            }
            else
            {
                m_desktop->openBrowserFile(pathBuf);
            }
            appendLine("browsefile: opened");
            return;
        }

        if (streqIgnoreCase(cmd, "cuiml"))
        {
            char pathBuf[256];
            QC::String::memset(pathBuf, 0, sizeof(pathBuf));
            (void)readWord(p, pathBuf, sizeof(pathBuf));

            if (pathBuf[0] == '\0')
            {
                appendLine("usage: cuiml <path>");
                appendLine("  (paths default to /shared if no slash)");
                return;
            }

            if (!m_desktop)
            {
                appendLine("cuiml: no desktop");
                return;
            }

            m_desktop->openCuiMLFile(pathBuf);
            appendLine("cuiml: opened");
            return;
        }

        if (streqIgnoreCase(cmd, "enroll"))
        {
            if (!m_desktop)
            {
                appendLine("enroll: no desktop");
                return;
            }

            m_desktop->showSetupWizard();
            appendLine("enroll: opened owner setup");
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
        env->param2 = m_callerAccess;
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

    void Terminal::onMinClick(QW::Controls::Button *button, void *userData)
    {
        (void)button;
        auto *self = static_cast<Terminal *>(userData);
        if (!self)
            return;
        self->minimizeWindow();
    }

    void Terminal::onMaxClick(QW::Controls::Button *button, void *userData)
    {
        (void)button;
        auto *self = static_cast<Terminal *>(userData);
        if (!self)
            return;
        self->toggleMaximizeWindow();
    }

} // namespace QD
