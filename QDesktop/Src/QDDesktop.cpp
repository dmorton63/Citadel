// QDesktop Desktop - the static Citadel desktop shell
// Namespace: QD

#include "QDDesktop.h"
#include "QDCitadelTheme.h"
#include "QDTextFormat.h"
#include "QDCommandProcessor.h"
#include "QDShutdownDialog.h"
#include "QDSetupWizard.h"
#include "QDLoginDialog.h"
#include "QDBrowser.h"
#include "QDCuiMLViewer.h"
#include "QDExplorer.h"
#include "QDTasks.h"
#include "QWWindowManager.h"
#include "QWStyleSystem.h"
#include "QWStyleTypes.h"
#include "QWPaintContext.h"
#include "QCLogger.h"
#include "QCString.h"
#include "QCBuiltins.h"
#include "QFSVolumeManager.h"
#include "QKEventManager.h"
#include "QKShutdownController.h"
#include "QKSecurityCenter.h"
#include "QG/FontManager.h"
#include "QDrvTimer.h"

namespace QK::Boot::Config
{
    enum class StartupMode : QC::u8
    {
        Desktop,
        Terminal,
        Safe,
        Recovery,
        Installer,
        Network
    };

    StartupMode GetStartupMode();
}

namespace QD
{
    namespace
    {
        constexpr const char *LOG_MODULE = "QDesktop";

        constexpr QC::i32 kTaskbarPad = 4;
        constexpr QC::u32 kTaskbarButtonH = TASKBAR_HEIGHT - 2 * kTaskbarPad;
        constexpr QC::u32 kAppButtonW = 96;
        constexpr QC::u32 kClockButtonW = 72;
        constexpr QC::u32 kSystemButtonW = 96;

        constexpr QC::u32 kMenuWidth = 168;
        constexpr QC::u32 kMenuItemH = 28;
        constexpr QC::i32 kMenuPad = 4;
        constexpr QC::u32 kClockPopupW = 220;
        constexpr QC::u32 kClockPopupH = 104;

        constexpr QC::u64 kClockRefreshMs = 1000;

        static const char *const kWeekdayNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                                    "Thursday", "Friday", "Saturday"};
        static const char *const kMonthNames[] = {"January", "February", "March", "April", "May", "June", "July",
                                                  "August", "September", "October", "November", "December"};

        struct RtcDateTime
        {
            QC::u8 second = 0;
            QC::u8 minute = 0;
            QC::u8 hour = 0;
            QC::u8 day = 0;
            QC::u8 month = 0;
            QC::u8 year = 0;
            QC::u8 statusB = 0;
        };

        static inline void ioWait()
        {
            // Classic ISA "wait" port.
            QC::outb(0x80, 0);
        }

        static QC::u8 cmosRead(QC::u8 reg)
        {
            // Disable NMI (bit 7) during access.
            QC::outb(0x70, static_cast<QC::u8>(0x80u | reg));
            ioWait();
            return QC::inb(0x71);
        }

        static inline bool rtcUpdateInProgress()
        {
            return (cmosRead(0x0A) & 0x80u) != 0;
        }

        static inline QC::u8 bcdToBin(QC::u8 v)
        {
            return static_cast<QC::u8>((v & 0x0Fu) + ((v >> 4) * 10u));
        }

        static bool tryReadRtcStable(RtcDateTime &out)
        {
            for (int attempt = 0; attempt < 5; ++attempt)
            {
                while (rtcUpdateInProgress())
                {
                }

                RtcDateTime a{};
                a.second = cmosRead(0x00);
                a.minute = cmosRead(0x02);
                a.hour = cmosRead(0x04);
                a.day = cmosRead(0x07);
                a.month = cmosRead(0x08);
                a.year = cmosRead(0x09);
                a.statusB = cmosRead(0x0B);

                while (rtcUpdateInProgress())
                {
                }

                RtcDateTime b{};
                b.second = cmosRead(0x00);
                b.minute = cmosRead(0x02);
                b.hour = cmosRead(0x04);
                b.day = cmosRead(0x07);
                b.month = cmosRead(0x08);
                b.year = cmosRead(0x09);
                b.statusB = cmosRead(0x0B);

                const bool same = (a.second == b.second) && (a.minute == b.minute) && (a.hour == b.hour) &&
                                  (a.day == b.day) && (a.month == b.month) && (a.year == b.year) &&
                                  (a.statusB == b.statusB);
                if (same)
                {
                    out = a;
                    return true;
                }
            }
            return false;
        }

        static bool normalizeRtcToBinaryAnd24h(RtcDateTime &dt)
        {
            // Status B:
            // - bit 1 (0x02): 24-hour mode when set
            // - bit 2 (0x04): binary mode when set; BCD when clear
            const bool is24h = (dt.statusB & 0x02u) != 0;
            const bool isBinary = (dt.statusB & 0x04u) != 0;

            QC::u8 hour = dt.hour;
            const bool pm = (!is24h && (hour & 0x80u) != 0);
            if (!is24h)
            {
                hour &= 0x7Fu;
            }

            if (!isBinary)
            {
                dt.second = bcdToBin(dt.second);
                dt.minute = bcdToBin(dt.minute);
                hour = bcdToBin(hour);
                dt.day = bcdToBin(dt.day);
                dt.month = bcdToBin(dt.month);
                dt.year = bcdToBin(dt.year);
            }

            if (!is24h)
            {
                // Convert 12-hour -> 24-hour.
                // 12:xx AM => 00:xx; 12:xx PM stays 12:xx.
                if (hour == 12)
                    hour = 0;
                if (pm)
                    hour = static_cast<QC::u8>((hour + 12) % 24);
            }

            dt.hour = hour;

            if (dt.month < 1 || dt.month > 12)
                return false;
            if (dt.day < 1 || dt.day > 31)
                return false;
            if (dt.hour > 23 || dt.minute > 59 || dt.second > 59)
                return false;

            return true;
        }

        static bool readRtc(RtcDateTime &dt)
        {
            return tryReadRtcStable(dt) && normalizeRtcToBinaryAnd24h(dt);
        }

        // Sakamoto's algorithm; 0 = Sunday.
        static QC::u32 dayOfWeek(QC::u32 year, QC::u32 month, QC::u32 day)
        {
            static const QC::u32 offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
            if (month < 3)
                year -= 1;
            return (year + year / 4 - year / 100 + year / 400 + offsets[month - 1] + day) % 7;
        }

        static bool pointInWindow(const QW::Window *window, QC::i32 x, QC::i32 y)
        {
            return window && window->isVisible() && window->bounds().contains(x, y);
        }

        static bool pointInControl(const QW::Controls::Button *control, QC::i32 x, QC::i32 y)
        {
            return control && control->absoluteBounds().contains(x, y);
        }

        /// Taskbar button that draws a small down-pointing caret on its right edge.
        class CaretButton : public QW::Controls::Button
        {
        public:
            CaretButton(QW::Window *window, const char *text, QW::Rect bounds)
                : Button(window, text, bounds)
            {
            }

            void paint(const QW::PaintContext &ctx) override
            {
                Button::paint(ctx);
                if (!isVisible() || !ctx.painter)
                    return;

                const QW::Rect abs = absoluteBounds();
                const QC::i32 caretW = 7;
                const QC::i32 x = abs.x + static_cast<QC::i32>(abs.width) - 10 - caretW;
                const QC::i32 y = abs.y + (static_cast<QC::i32>(abs.height) - 4) / 2;
                const QC::Color color = isEnabled() ? CitadelTheme::text() : CitadelTheme::textDim();
                for (QC::i32 row = 0; row < 4; ++row)
                {
                    ctx.painter->fillRect(QC::Rect{x + row, y + row, static_cast<QC::u32>(caretW - 2 * row), 1}, color);
                }
            }
        };
    }

    Desktop::Desktop()
        : m_initialized(false),
          m_screenWidth(0),
          m_screenHeight(0),
          m_desktopWindow(nullptr),
          m_taskbar(nullptr),
          m_terminalButton(nullptr),
          m_explorerButton(nullptr),
          m_tasksButton(nullptr),
          m_clockButton(nullptr),
          m_systemButton(nullptr),
          m_systemMenu(nullptr),
          m_lockItem(nullptr),
          m_clockPopup(nullptr),
          m_clockDateLabel(nullptr),
          m_clockTimeLabel(nullptr),
          m_clockZoneLabel(nullptr),
          m_clockUptimeLabel(nullptr),
          m_lastClockRefreshMs(0),
          m_clockText{},
          m_running{},
          m_runningCount(0),
          m_activeWindowId(0),
          m_locked(false),
          m_lockedWindowIds{},
          m_lockedWindowCount(0),
          m_terminal(nullptr),
          m_browser(nullptr),
          m_cuimlViewer(nullptr),
          m_explorer(nullptr),
          m_tasks(nullptr),
          m_shutdownDialog(nullptr),
          m_setupWizard(nullptr),
          m_loginDialog(nullptr)
    {
    }

    Desktop::~Desktop()
    {
        shutdown();
    }

    // ==================== Initialization ====================

    void Desktop::initialize(QC::u32 screenWidth, QC::u32 screenHeight)
    {
        if (m_initialized)
            return;

        m_screenWidth = screenWidth;
        m_screenHeight = screenHeight;

        publishStyle();

        // Fullscreen background surface: never takes focus and is pinned to the bottom.
        // Keep it hidden until initialization completes to avoid early synchronous paints.
        QW::Rect desktopBounds = {0, 0, screenWidth, screenHeight};
        m_desktopWindow = QW::WindowManager::instance().createWindow("Desktop", desktopBounds);
        if (!m_desktopWindow)
        {
            QC_LOG_ERROR(LOG_MODULE, "Failed to create desktop window\n");
            return;
        }
        m_desktopWindow->setFlags(QW::WindowFlags::AlwaysBottom | QW::WindowFlags::NoFocus);

        if (auto *root = m_desktopWindow->root())
        {
            root->setBorderStyle(QW::Controls::BorderStyle::None);
            root->setPadding(0);
            root->setBackgroundColor(CitadelTheme::background());
        }

        createTaskbar();
        refreshClock(true);

        QK::Shutdown::Controller::instance().registerUIHandler(&Desktop::onShutdownRequested, this);

        ensureWindowEventListener();
        ensureInputEventListener();

        // Ensure command processor is available for terminals.
        QD::CommandProcessor::instance().initialize();

        // In BYPASS mode we skip owner setup prompts during startup.
        // In ENFORCE mode, pre-desktop gates usually handle enroll/unlock already;
        // only prompt here if the owner session is still locked.
        const bool bypass = QK::SecurityCenter::instance().bypassEnabled();
        const bool ownerUnlocked = QK::SecurityCenter::instance().ownerUnlocked();
        const bool ownerEnrolled = isOwnerEnrolled();
        const bool hasSystemVolume = QFS::VolumeManager::instance().isMounted("QFS_SYSTEM");
        const auto startupMode = QK::Boot::Config::GetStartupMode();

        QC_LOG_INFO(LOG_MODULE,
                    "Owner gate state: bypass=%d unlocked=%d enrolled=%d startupMode=%u hasSystemVolume=%d\n",
                    bypass ? 1 : 0,
                    ownerUnlocked ? 1 : 0,
                    ownerEnrolled ? 1 : 0,
                    static_cast<unsigned>(startupMode),
                    hasSystemVolume ? 1 : 0);

        if (!bypass && !ownerUnlocked)
        {
            if (startupMode == QK::Boot::Config::StartupMode::Recovery)
            {
                // Recovery mode must not dead-end at a graphical owner gate.
                QC_LOG_WARN(LOG_MODULE, "Recovery mode: skipping graphical owner login gate\n");
            }
            else if (!hasSystemVolume)
            {
                // With no /system volume, owner credential records are usually unavailable.
                QC_LOG_WARN(LOG_MODULE, "No /system volume: skipping graphical owner login gate\n");
            }
            else if (!ownerEnrolled)
            {
                showSetupWizard();
            }
            else
            {
                showLoginDialog();
            }
        }

        m_desktopWindow->setFlags(QW::WindowFlags::Visible | QW::WindowFlags::AlwaysBottom | QW::WindowFlags::NoFocus);
        QW::WindowManager::instance().sendToBack(m_desktopWindow);

        QC_LOG_INFO(LOG_MODULE, "Desktop mode: static Citadel shell\n");
        m_initialized = true;
    }

    void Desktop::publishStyle()
    {
        QW::StyleSnapshot::VistaThemeConfig cfg;
        cfg.windowBackground = CitadelTheme::surface();
        cfg.windowBorder = CitadelTheme::border();
        cfg.sidebarBackground = CitadelTheme::surface();
        cfg.sidebarHover = CitadelTheme::taskbarHover();
        cfg.sidebarSelected = CitadelTheme::accent();
        cfg.sidebarText = CitadelTheme::text();
        cfg.topBarDivider = CitadelTheme::border();
        cfg.taskbarBackground = CitadelTheme::taskbar();
        cfg.taskbarHover = CitadelTheme::taskbarHover();
        cfg.taskbarText = CitadelTheme::text();
        cfg.taskbarActiveWindow = CitadelTheme::accent();
        cfg.desktopBackgroundTop = CitadelTheme::background();
        cfg.desktopBackgroundBottom = CitadelTheme::background();
        cfg.windowShadow = QC::Color::transparent();
        cfg.accent = CitadelTheme::accent();

        QG::FontManager::instance().clearDefaultFont();
        QW::StyleSystem::instance().setStyle(QW::StyleSnapshot::makeVista(cfg));
    }

    QW::Controls::Button *Desktop::makeTaskbarButton(const char *text, QW::ButtonRole role,
                                                      QW::Controls::ButtonClickHandler handler)
    {
        auto *button = new QW::Controls::Button(m_desktopWindow, text, {0, kTaskbarPad, kAppButtonW, kTaskbarButtonH});
        button->setContentMode(QW::ButtonContentMode::Text);
        button->setRole(role);
        button->setClickHandler(handler, this);
        m_taskbar->addChild(button);
        return button;
    }

    void Desktop::createTaskbar()
    {
        m_taskbar = new QW::Controls::Panel(m_desktopWindow, {0, 0, m_screenWidth, TASKBAR_HEIGHT});
        m_taskbar->setBorderStyle(QW::Controls::BorderStyle::None);
        m_taskbar->setFrameVisible(false);
        m_taskbar->setPadding(0);
        m_taskbar->setBackgroundColor(CitadelTheme::taskbar());
        m_desktopWindow->root()->addChild(m_taskbar);

        m_terminalButton = makeTaskbarButton("Terminal", QW::ButtonRole::Taskbar, &Desktop::onTerminalClick);
        m_explorerButton = makeTaskbarButton("Explorer", QW::ButtonRole::Taskbar, &Desktop::onExplorerClick);
        m_tasksButton = makeTaskbarButton("Tasks", QW::ButtonRole::Taskbar, &Desktop::onTasksClick);
        m_clockButton = makeTaskbarButton("--:--", QW::ButtonRole::Taskbar, &Desktop::onClockClick);

        auto *systemButton = new CaretButton(m_desktopWindow, "System    ", {0, kTaskbarPad, kSystemButtonW, kTaskbarButtonH});
        systemButton->setContentMode(QW::ButtonContentMode::Text);
        systemButton->setRole(QW::ButtonRole::Taskbar);
        systemButton->setClickHandler(&Desktop::onSystemClick, this);
        m_taskbar->addChild(systemButton);
        m_systemButton = systemButton;

        layoutTaskbar();
    }

    void Desktop::layoutTaskbar()
    {
        if (!m_taskbar)
            return;

        m_taskbar->setBounds({0, static_cast<QC::i32>(m_screenHeight - TASKBAR_HEIGHT), m_screenWidth, TASKBAR_HEIGHT});

        QC::i32 x = kTaskbarPad;
        QW::Controls::Button *left[] = {m_terminalButton, m_explorerButton, m_tasksButton};
        for (auto *button : left)
        {
            button->setBounds({x, kTaskbarPad, kAppButtonW, kTaskbarButtonH});
            x += static_cast<QC::i32>(kAppButtonW) + kTaskbarPad;
        }

        QC::i32 right = static_cast<QC::i32>(m_screenWidth) - kTaskbarPad;
        right -= static_cast<QC::i32>(kSystemButtonW);
        m_systemButton->setBounds({right, kTaskbarPad, kSystemButtonW, kTaskbarButtonH});
        right -= kTaskbarPad + static_cast<QC::i32>(kClockButtonW);
        m_clockButton->setBounds({right, kTaskbarPad, kClockButtonW, kTaskbarButtonH});

        m_taskbar->invalidate();
    }

    void Desktop::resize(QC::u32 screenWidth, QC::u32 screenHeight)
    {
        m_screenWidth = screenWidth;
        m_screenHeight = screenHeight;

        if (m_desktopWindow)
        {
            m_desktopWindow->setBounds({0, 0, screenWidth, screenHeight});
        }

        layoutTaskbar();
        hidePopups();
    }

    void Desktop::shutdown()
    {
        if (!m_initialized)
            return;

        QK::Shutdown::Controller::instance().registerUIHandler(nullptr, nullptr);

        if (m_windowListenerId != QK::Event::InvalidListenerId)
        {
            QK::Event::EventManager::instance().removeListener(m_windowListenerId);
            m_windowListenerId = QK::Event::InvalidListenerId;
        }

        if (m_inputListenerId != QK::Event::InvalidListenerId)
        {
            QK::Event::EventManager::instance().removeListener(m_inputListenerId);
            m_inputListenerId = QK::Event::InvalidListenerId;
        }

        delete m_shutdownDialog;
        m_shutdownDialog = nullptr;
        delete m_setupWizard;
        m_setupWizard = nullptr;
        delete m_loginDialog;
        m_loginDialog = nullptr;
        delete m_browser;
        m_browser = nullptr;
        delete m_cuimlViewer;
        m_cuimlViewer = nullptr;
        delete m_explorer;
        m_explorer = nullptr;
        delete m_tasks;
        m_tasks = nullptr;
        delete m_terminal;
        m_terminal = nullptr;

        auto destroyPopup = [](QW::Window *&popup)
        {
            if (!popup)
                return;
            if (auto *root = popup->root())
            {
                while (root->childCount() > 0)
                {
                    QW::Controls::IControl *child = root->childAt(0);
                    root->removeChildAt(0);
                    delete child;
                }
            }
            QW::WindowManager::instance().destroyWindow(popup);
            popup = nullptr;
        };
        destroyPopup(m_systemMenu);
        destroyPopup(m_clockPopup);
        m_lockItem = nullptr;
        m_clockDateLabel = nullptr;
        m_clockTimeLabel = nullptr;
        m_clockZoneLabel = nullptr;
        m_clockUptimeLabel = nullptr;

        if (m_taskbar)
        {
            while (m_taskbar->childCount() > 0)
            {
                QW::Controls::IControl *child = m_taskbar->childAt(0);
                m_taskbar->removeChildAt(0);
                delete child;
            }
        }
        m_terminalButton = nullptr;
        m_explorerButton = nullptr;
        m_tasksButton = nullptr;
        m_clockButton = nullptr;
        m_systemButton = nullptr;

        if (m_desktopWindow && m_desktopWindow->root())
        {
            m_desktopWindow->root()->clearChildren();
        }
        delete m_taskbar;
        m_taskbar = nullptr;

        if (m_desktopWindow)
        {
            QW::WindowManager::instance().destroyWindow(m_desktopWindow);
            m_desktopWindow = nullptr;
        }

        m_runningCount = 0;
        m_activeWindowId = 0;
        m_locked = false;
        m_lockedWindowCount = 0;
        m_initialized = false;
    }

    QC::Rect Desktop::workArea() const
    {
        const QC::u32 height = (m_screenHeight > TASKBAR_HEIGHT) ? (m_screenHeight - TASKBAR_HEIGHT) : 0;
        return {0, 0, m_screenWidth, height};
    }

    // ==================== User Setup/Login ====================

    bool Desktop::isOwnerEnrolled() const
    {
        return QK::SecurityCenter::instance().ownerIsEnrolled();
    }

    void Desktop::showSetupWizard()
    {
        QC_LOG_INFO(LOG_MODULE, "Opening setup wizard\n");
        if (!m_setupWizard)
            m_setupWizard = new SetupWizard(this);
        if (m_setupWizard)
            m_setupWizard->open();
    }

    void Desktop::showLoginDialog()
    {
        QC_LOG_INFO(LOG_MODULE, "Opening login dialog\n");
        if (!m_loginDialog)
            m_loginDialog = new LoginDialog(this);
        if (m_loginDialog)
            m_loginDialog->open();
    }

    void Desktop::hideLoginDialog()
    {
        if (m_loginDialog)
            m_loginDialog->close();
    }

    void Desktop::lock()
    {
        if (m_locked)
            return;

        auto &security = QK::SecurityCenter::instance();
        if (!security.bypassEnabled() && !isOwnerEnrolled())
        {
            QC_LOG_WARN(LOG_MODULE, "Lock ignored: no owner is enrolled\n");
            return;
        }

        hidePopups();

        m_lockedWindowCount = 0;
        for (QC::u32 i = 0; i < m_runningCount; ++i)
        {
            QW::Window *window = QW::WindowManager::instance().windowById(m_running[i].windowId);
            if (window && window->isVisible())
            {
                hideWindow(window);
                m_lockedWindowIds[m_lockedWindowCount++] = m_running[i].windowId;
            }
        }

        m_locked = true;
        security.ownerLock();
        QC_LOG_INFO(LOG_MODULE, "Session locked (%u windows hidden)\n", m_lockedWindowCount);
        showLoginDialog();
    }

    void Desktop::finishUnlockIfReady()
    {
        if (!m_locked)
            return;
        if (m_loginDialog && m_loginDialog->isOpen())
            return;

        auto &security = QK::SecurityCenter::instance();
        if (!security.ownerUnlocked() && !security.bypassEnabled())
        {
            // The dialog closed without unlocking; keep the session gated.
            showLoginDialog();
            return;
        }

        m_locked = false;
        for (QC::u32 i = 0; i < m_lockedWindowCount; ++i)
        {
            if (QW::Window *window = QW::WindowManager::instance().windowById(m_lockedWindowIds[i]))
                window->setVisible(true);
        }
        m_lockedWindowCount = 0;
        QW::WindowManager::instance().render();
        QC_LOG_INFO(LOG_MODULE, "Session unlocked\n");
    }

    // ==================== Applications ====================

    void Desktop::openTerminal()
    {
        if (!m_terminal)
            m_terminal = new Terminal(this);
        if (m_terminal)
            m_terminal->open();
    }

    void Desktop::openExplorer()
    {
        if (!m_explorer)
            m_explorer = new Explorer(this);
        if (m_explorer)
            m_explorer->open();
    }

    void Desktop::openTasks()
    {
        if (!m_tasks)
            m_tasks = new Tasks(this);
        if (m_tasks)
            m_tasks->open();
    }

    void Desktop::openBrowserFile(const char *path)
    {
        if (!m_browser)
            m_browser = new Browser(this);
        if (m_browser)
            m_browser->openFile(path);
    }

    void Desktop::openBrowserUrl(const char *url)
    {
        if (!m_browser)
            m_browser = new Browser(this);
        if (m_browser)
            m_browser->openUrl(url);
    }

    void Desktop::openBrowserHtmlText(const char *htmlText)
    {
        if (!m_browser)
            m_browser = new Browser(this);
        if (m_browser)
            m_browser->openHtmlText(htmlText);
    }

    void Desktop::openCuiMLFile(const char *path)
    {
        if (!m_cuimlViewer)
            m_cuimlViewer = new CuiMLViewer(this);
        if (m_cuimlViewer)
            m_cuimlViewer->openFile(path);
    }

    void Desktop::showShutdownPrompt(QK::Shutdown::Reason reason)
    {
        hidePopups();
        if (!m_shutdownDialog)
            m_shutdownDialog = new ShutdownDialog(this);
        if (m_shutdownDialog)
            m_shutdownDialog->open(reason);
    }

    // ==================== Window Tracking ====================

    void Desktop::addTaskbarWindow(QC::u32 windowId, const char *title)
    {
        if (windowId == 0)
            return;

        for (QC::u32 i = 0; i < m_runningCount; ++i)
        {
            if (m_running[i].windowId == windowId)
            {
                QC::String::strncpy(m_running[i].title, title ? title : "Window", sizeof(m_running[i].title) - 1);
                m_running[i].title[sizeof(m_running[i].title) - 1] = '\0';
                return;
            }
        }

        if (m_runningCount >= MAX_TASKBAR_WINDOWS)
        {
            QC_LOG_WARN(LOG_MODULE, "Window tracking full; window %u not listed\n", windowId);
            return;
        }

        RunningWindow &entry = m_running[m_runningCount++];
        entry.windowId = windowId;
        QC::String::strncpy(entry.title, title ? title : "Window", sizeof(entry.title) - 1);
        entry.title[sizeof(entry.title) - 1] = '\0';
    }

    void Desktop::removeTaskbarWindow(QC::u32 windowId)
    {
        for (QC::u32 i = 0; i < m_runningCount; ++i)
        {
            if (m_running[i].windowId != windowId)
                continue;

            for (QC::u32 j = i + 1; j < m_runningCount; ++j)
                m_running[j - 1] = m_running[j];
            --m_runningCount;
            break;
        }

        for (QC::u32 i = 0; i < m_lockedWindowCount; ++i)
        {
            if (m_lockedWindowIds[i] != windowId)
                continue;

            for (QC::u32 j = i + 1; j < m_lockedWindowCount; ++j)
                m_lockedWindowIds[j - 1] = m_lockedWindowIds[j];
            --m_lockedWindowCount;
            break;
        }

        if (m_activeWindowId == windowId)
            m_activeWindowId = 0;
    }

    void Desktop::setActiveTaskbarWindow(QC::u32 windowId)
    {
        m_activeWindowId = windowId;
    }

    bool Desktop::runningWindowAt(QC::u32 index, QC::u32 &outWindowId, const char *&outTitle) const
    {
        if (index >= m_runningCount)
            return false;
        outWindowId = m_running[index].windowId;
        outTitle = m_running[index].title;
        return true;
    }

    void Desktop::activateWindow(QC::u32 windowId)
    {
        if (m_locked)
            return;

        auto &wm = QW::WindowManager::instance();
        QW::Window *window = wm.windowById(windowId);
        if (!window)
            return;

        window->setVisible(true);
        wm.bringToFront(window);
        wm.setFocus(window);
        m_activeWindowId = windowId;
        wm.render();
    }

    void Desktop::hideWindow(QW::Window *window)
    {
        if (!window || !window->isVisible())
            return;

        auto &wm = QW::WindowManager::instance();
        const QC::Rect bounds = window->bounds();
        window->setVisible(false);
        if (wm.focusedWindow() == window)
            wm.setFocus(nullptr);

        // Include the decoration/shadow margin so no residual pixels remain.
        wm.invalidate({bounds.x - 1, bounds.y - 1, bounds.width + 8, bounds.height + 8});
        wm.render();
    }

    // ==================== Clock ====================

    void Desktop::refreshClock(bool force)
    {
        const QC::u64 now = QDrv::Timer::instance().milliseconds();
        if (!force && (now - m_lastClockRefreshMs) < kClockRefreshMs)
            return;
        m_lastClockRefreshMs = now;

        char text[sizeof(m_clockText)] = "--:--";
        RtcDateTime dt{};
        if (readRtc(dt))
        {
            QC::usize len = Text::appendUnsigned(text, sizeof(text), 0, dt.hour, 2);
            len = Text::append(text, sizeof(text), len, ":");
            Text::appendUnsigned(text, sizeof(text), len, dt.minute, 2);
        }

        if (QC::String::strcmp(text, m_clockText) != 0)
        {
            QC::String::strncpy(m_clockText, text, sizeof(m_clockText) - 1);
            m_clockText[sizeof(m_clockText) - 1] = '\0';
            if (m_clockButton)
            {
                m_clockButton->setText(m_clockText);
                m_clockButton->invalidate();
            }
        }

        if (m_clockPopup && m_clockPopup->isVisible())
            updateClockPopup();
    }

    void Desktop::createClockPopup()
    {
        if (m_clockPopup)
            return;

        m_clockPopup = QW::WindowManager::instance().createWindow("Clock", {0, 0, kClockPopupW, kClockPopupH});
        if (!m_clockPopup)
            return;
        m_clockPopup->setFlags(0);

        auto *root = m_clockPopup->root();
        root->setBorderStyle(QW::Controls::BorderStyle::Flat);
        root->setBorderColor(CitadelTheme::border());
        root->setPadding(0);
        root->setBackgroundColor(CitadelTheme::surface());

        auto makeLabel = [&](QC::i32 y, QC::Color color) -> QW::Controls::Label *
        {
            auto *label = new QW::Controls::Label(m_clockPopup, "", {10, y, kClockPopupW - 20, 18});
            label->setTextColor(color);
            label->setBackgroundColor(QC::Color(0, 0, 0, 0));
            root->addChild(label);
            return label;
        };

        m_clockTimeLabel = makeLabel(10, CitadelTheme::text());
        m_clockDateLabel = makeLabel(32, CitadelTheme::text());
        m_clockZoneLabel = makeLabel(54, CitadelTheme::textDim());
        m_clockUptimeLabel = makeLabel(76, CitadelTheme::textDim());
    }

    void Desktop::updateClockPopup()
    {
        if (!m_clockPopup)
            return;

        char line[64];
        RtcDateTime dt{};
        const bool haveTime = readRtc(dt);

        if (haveTime)
        {
            QC::usize len = Text::append(line, sizeof(line), 0, "Time: ");
            len = Text::appendUnsigned(line, sizeof(line), len, dt.hour, 2);
            len = Text::append(line, sizeof(line), len, ":");
            len = Text::appendUnsigned(line, sizeof(line), len, dt.minute, 2);
            len = Text::append(line, sizeof(line), len, ":");
            Text::appendUnsigned(line, sizeof(line), len, dt.second, 2);
            m_clockTimeLabel->setText(line);

            const QC::u32 year = 2000u + dt.year;
            len = Text::append(line, sizeof(line), 0, kWeekdayNames[dayOfWeek(year, dt.month, dt.day)]);
            len = Text::append(line, sizeof(line), len, ", ");
            len = Text::append(line, sizeof(line), len, kMonthNames[dt.month - 1]);
            len = Text::append(line, sizeof(line), len, " ");
            len = Text::appendUnsigned(line, sizeof(line), len, dt.day);
            len = Text::append(line, sizeof(line), len, ", ");
            Text::appendUnsigned(line, sizeof(line), len, year);
            m_clockDateLabel->setText(line);
        }
        else
        {
            m_clockTimeLabel->setText("Time: unavailable");
            m_clockDateLabel->setText("Date: unavailable");
        }

        m_clockZoneLabel->setText("Timezone: RTC local time");

        const QC::u64 uptimeSeconds = QDrv::Timer::instance().milliseconds() / 1000ull;
        QC::usize len = Text::append(line, sizeof(line), 0, "Uptime: ");
        len = Text::appendUnsigned(line, sizeof(line), len, uptimeSeconds / 3600ull);
        len = Text::append(line, sizeof(line), len, "h ");
        len = Text::appendUnsigned(line, sizeof(line), len, (uptimeSeconds / 60ull) % 60ull, 2);
        len = Text::append(line, sizeof(line), len, "m ");
        len = Text::appendUnsigned(line, sizeof(line), len, uptimeSeconds % 60ull, 2);
        Text::append(line, sizeof(line), len, "s");
        m_clockUptimeLabel->setText(line);

        m_clockPopup->invalidate();
    }

    // ==================== Popups ====================

    void Desktop::positionPopup(QW::Window *popup, QW::Controls::Button *anchor)
    {
        if (!popup || !anchor)
            return;

        const QC::Rect anchorBounds = anchor->absoluteBounds();
        const QC::Rect current = popup->bounds();
        QC::i32 x = anchorBounds.x + static_cast<QC::i32>(anchorBounds.width) - static_cast<QC::i32>(current.width);
        if (x < 0)
            x = 0;
        const QC::i32 y = static_cast<QC::i32>(m_screenHeight - TASKBAR_HEIGHT) - static_cast<QC::i32>(current.height) - 2;
        popup->setBounds({x, y, current.width, current.height});
    }

    void Desktop::createSystemMenu()
    {
        if (m_systemMenu)
            return;

        // Update, Lock | Restart, Hibernate, Shutdown
        const QC::u32 separatorH = 9;
        const QC::u32 height = 5 * kMenuItemH + separatorH + 2 * kMenuPad;
        m_systemMenu = QW::WindowManager::instance().createWindow("System", {0, 0, kMenuWidth, height});
        if (!m_systemMenu)
            return;
        m_systemMenu->setFlags(0);

        auto *root = m_systemMenu->root();
        root->setBorderStyle(QW::Controls::BorderStyle::Flat);
        root->setBorderColor(CitadelTheme::border());
        root->setPadding(0);
        root->setBackgroundColor(CitadelTheme::surface());

        QC::i32 y = kMenuPad;
        auto addItem = [&](const char *text, QW::ButtonRole role, QW::Controls::ButtonClickHandler handler) -> QW::Controls::Button *
        {
            auto *item = new QW::Controls::Button(m_systemMenu, text, {kMenuPad, y, kMenuWidth - 2 * kMenuPad, kMenuItemH});
            item->setContentMode(QW::ButtonContentMode::Text);
            item->setRole(role);
            if (handler)
                item->setClickHandler(handler, this);
            else
                item->setEnabled(false);
            root->addChild(item);
            y += static_cast<QC::i32>(kMenuItemH);
            return item;
        };

        addItem("Update", QW::ButtonRole::Sidebar, nullptr);
        m_lockItem = addItem("Lock", QW::ButtonRole::Sidebar, &Desktop::onLockClick);

        auto *separator = new QW::Controls::Panel(m_systemMenu, {kMenuPad + 4, y + static_cast<QC::i32>(separatorH / 2), kMenuWidth - 2 * kMenuPad - 8, 1});
        separator->setBorderStyle(QW::Controls::BorderStyle::None);
        separator->setFrameVisible(false);
        separator->setBackgroundColor(CitadelTheme::border());
        root->addChild(separator);
        y += static_cast<QC::i32>(separatorH);

        addItem("Restart", QW::ButtonRole::Sidebar, &Desktop::onRestartClick);
        addItem("Hibernate", QW::ButtonRole::Sidebar, nullptr);
        addItem("Shutdown", QW::ButtonRole::Destructive, &Desktop::onShutdownClick);
    }

    void Desktop::toggleSystemMenu()
    {
        if (m_systemMenu && m_systemMenu->isVisible())
        {
            hideWindow(m_systemMenu);
            return;
        }

        hidePopups();
        createSystemMenu();
        if (!m_systemMenu)
            return;

        const bool canLock = QK::SecurityCenter::instance().bypassEnabled() || isOwnerEnrolled();
        if (m_lockItem)
            m_lockItem->setEnabled(canLock);

        positionPopup(m_systemMenu, m_systemButton);
        m_systemMenu->setVisible(true);
        QW::WindowManager::instance().bringToFront(m_systemMenu);
        QW::WindowManager::instance().render();
    }

    void Desktop::toggleClockPopup()
    {
        if (m_clockPopup && m_clockPopup->isVisible())
        {
            hideWindow(m_clockPopup);
            return;
        }

        hidePopups();
        createClockPopup();
        if (!m_clockPopup)
            return;

        positionPopup(m_clockPopup, m_clockButton);
        updateClockPopup();
        m_clockPopup->setVisible(true);
        QW::WindowManager::instance().bringToFront(m_clockPopup);
        QW::WindowManager::instance().render();
    }

    void Desktop::hidePopups()
    {
        hideWindow(m_systemMenu);
        hideWindow(m_clockPopup);
    }

    void Desktop::dismissPopupsOutside(QC::i32 x, QC::i32 y)
    {
        // Clicks on a popup's own taskbar button are handled by its toggle.
        if (m_systemMenu && m_systemMenu->isVisible() &&
            !pointInWindow(m_systemMenu, x, y) && !pointInControl(m_systemButton, x, y))
        {
            hideWindow(m_systemMenu);
        }

        if (m_clockPopup && m_clockPopup->isVisible() &&
            !pointInWindow(m_clockPopup, x, y) && !pointInControl(m_clockButton, x, y))
        {
            hideWindow(m_clockPopup);
        }
    }

    // ==================== Runtime ====================

    void Desktop::tickAnimations()
    {
        if (!m_initialized)
            return;

        refreshClock(false);
        finishUnlockIfReady();
        if (m_tasks)
            m_tasks->tick();
    }

    // ==================== Callbacks ====================

    void Desktop::onTerminalClick(QW::Controls::Button *, void *userData)
    {
        auto *desktop = static_cast<Desktop *>(userData);
        if (!desktop || desktop->m_locked)
            return;
        desktop->hidePopups();
        desktop->openTerminal();
    }

    void Desktop::onExplorerClick(QW::Controls::Button *, void *userData)
    {
        auto *desktop = static_cast<Desktop *>(userData);
        if (!desktop || desktop->m_locked)
            return;
        desktop->hidePopups();
        desktop->openExplorer();
    }

    void Desktop::onTasksClick(QW::Controls::Button *, void *userData)
    {
        auto *desktop = static_cast<Desktop *>(userData);
        if (!desktop || desktop->m_locked)
            return;
        desktop->hidePopups();
        desktop->openTasks();
    }

    void Desktop::onClockClick(QW::Controls::Button *, void *userData)
    {
        auto *desktop = static_cast<Desktop *>(userData);
        if (desktop && !desktop->m_locked)
            desktop->toggleClockPopup();
    }

    void Desktop::onSystemClick(QW::Controls::Button *, void *userData)
    {
        auto *desktop = static_cast<Desktop *>(userData);
        if (desktop && !desktop->m_locked)
            desktop->toggleSystemMenu();
    }

    void Desktop::onLockClick(QW::Controls::Button *, void *userData)
    {
        auto *desktop = static_cast<Desktop *>(userData);
        if (desktop)
            desktop->lock();
    }

    void Desktop::onRestartClick(QW::Controls::Button *, void *userData)
    {
        auto *desktop = static_cast<Desktop *>(userData);
        if (!desktop)
            return;
        desktop->hidePopups();
        QK::Shutdown::Controller::instance().requestRestart(QK::Shutdown::Reason::UserRequest);
    }

    void Desktop::onShutdownClick(QW::Controls::Button *, void *userData)
    {
        auto *desktop = static_cast<Desktop *>(userData);
        if (!desktop)
            return;
        desktop->hidePopups();
        QK::Event::EventManager::instance().postShutdownEvent(
            QK::Event::Type::ShutdownRequest,
            static_cast<QC::u32>(QK::Shutdown::Reason::UserRequest));
    }

    bool Desktop::onShutdownRequested(QK::Shutdown::Reason reason, void *userData)
    {
        auto *desktop = static_cast<Desktop *>(userData);
        if (!desktop)
            return false;

        desktop->showShutdownPrompt(reason);
        return true;
    }

    bool Desktop::onWindowEvent(const QK::Event::Event &event, void *userData)
    {
        auto *desktop = static_cast<Desktop *>(userData);
        if (!desktop)
            return false;

        if (event.type() == QK::Event::Type::WindowDestroy)
        {
            desktop->removeTaskbarWindow(event.asWindow().windowId);
        }

        return false;
    }

    void Desktop::ensureWindowEventListener()
    {
        if (m_windowListenerId != QK::Event::InvalidListenerId)
            return;

        auto &eventMgr = QK::Event::EventManager::instance();
        if (!eventMgr.isInitialized())
            return;

        m_windowListenerId = eventMgr.addListener(QK::Event::Category::Window, &Desktop::onWindowEvent, this);
    }

    bool Desktop::onInputEvent(const QK::Event::Event &event, void *userData)
    {
        auto *desktop = static_cast<Desktop *>(userData);
        if (!desktop)
            return false;

        if (event.type() == QK::Event::Type::MouseButtonDown)
        {
            const QC::Point pos = QW::WindowManager::instance().mousePosition();
            desktop->dismissPopupsOutside(pos.x, pos.y);
            return false;
        }

        if (event.type() != QK::Event::Type::KeyDown || desktop->m_locked)
            return false;

        // Only treat T as a shortcut when no application window has keyboard focus.
        QW::Window *focused = QW::WindowManager::instance().focusedWindow();
        if (focused && focused != desktop->m_desktopWindow)
            return false;

        const auto &key = event.asKey();
        if (QK::Event::hasModifier(key.modifiers, QK::Event::Modifiers::Ctrl) ||
            QK::Event::hasModifier(key.modifiers, QK::Event::Modifiers::Alt))
        {
            return false;
        }

        if (key.character == 't' || key.character == 'T')
        {
            desktop->hidePopups();
            desktop->openTerminal();
            return true;
        }

        return false;
    }

    void Desktop::ensureInputEventListener()
    {
        if (m_inputListenerId != QK::Event::InvalidListenerId)
            return;

        auto &eventMgr = QK::Event::EventManager::instance();
        if (!eventMgr.isInitialized())
            return;

        m_inputListenerId = eventMgr.addListener(QK::Event::Category::Input, &Desktop::onInputEvent, this);
    }

} // namespace QD
