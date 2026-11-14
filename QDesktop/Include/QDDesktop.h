#pragma once

// QDesktop Desktop - the static Citadel desktop shell
// Namespace: QD
//
// Layout (fixed, see CitadelDesktopRedesign.md):
// ┌──────────────────────────────────────────────────────────────┐
// │                                                              │
// │                       Desktop Surface                        │
// │                                                              │
// ├──────────────────────────────────────────────────────────────┤
// │ Terminal | Explorer | Tasks                 Clock | System ▼ │  ← Taskbar
// └──────────────────────────────────────────────────────────────┘

#include "QCTypes.h"
#include "QCGeometry.h"
#include "QCColor.h"
#include "QCUIStyle.h"
#include "QCVector.h"
#include "QWWindow.h"
#include "QWStyleTypes.h"
#include "QWControls/Containers/Panel.h"
#include "QWControls/Leaf/Button.h"
#include "QWControls/Leaf/Label.h"
#include "QKEventListener.h"
#include "QDTerminal.h"

namespace QK
{
    namespace Shutdown
    {
        enum class Reason : QC::u8;
    }
}

namespace QD
{
    using QC::Color;
    using QC::Rect;
    using QC::Size;

    class ShutdownDialog;
    class SetupWizard;
    class LoginDialog;
    class Browser;
    class CuiMLViewer;
    class Explorer;
    class Tasks;

    constexpr QC::u32 TASKBAR_HEIGHT = 40;

    // Maximum application windows tracked for the Tasks view.
    constexpr QC::u32 MAX_TASKBAR_WINDOWS = 12;

    /// Desktop - Main desktop shell
    /// Owns a fullscreen background window with the fixed taskbar.
    class Desktop
    {
    public:
        Desktop();
        ~Desktop();

        // ==================== Initialization ====================

        /// Initialize desktop with screen dimensions
        void initialize(QC::u32 screenWidth, QC::u32 screenHeight);

        /// Resize desktop (on resolution change)
        void resize(QC::u32 screenWidth, QC::u32 screenHeight);

        /// Clean up resources
        void shutdown();

        // ==================== Access ====================

        QW::Window *window() { return m_desktopWindow; }
        const QW::Window *window() const { return m_desktopWindow; }

        QC::u32 screenWidth() const { return m_screenWidth; }
        QC::u32 screenHeight() const { return m_screenHeight; }

        /// The area where application windows can appear (screen minus taskbar)
        Rect workArea() const;

        // ==================== User Setup/Login ====================

        bool isOwnerEnrolled() const;
        void showSetupWizard();
        void showLoginDialog();
        void hideLoginDialog();

        /// Lock the owner session: hide application windows until the owner unlocks again.
        void lock();
        bool isLocked() const { return m_locked; }

        // ==================== Applications ====================

        void openTerminal();
        void openExplorer();
        void openTasks();

        void openBrowserFile(const char *path);
        void openBrowserUrl(const char *url);
        void openBrowserHtmlText(const char *htmlText);
        void openCuiMLFile(const char *path);

        // ==================== Window Tracking ====================

        /// Register an application window (shown in Tasks)
        void addTaskbarWindow(QC::u32 windowId, const char *title);
        void removeTaskbarWindow(QC::u32 windowId);
        void setActiveTaskbarWindow(QC::u32 windowId);

        QC::u32 runningWindowCount() const { return m_runningCount; }
        bool runningWindowAt(QC::u32 index, QC::u32 &outWindowId, const char *&outTitle) const;

        /// Show and focus a tracked application window
        void activateWindow(QC::u32 windowId);

        /// Hide a window and repaint the region it covered
        static void hideWindow(QW::Window *window);

        // ==================== Runtime ====================

        /// Periodic work driven by the desktop run loop (clock, Tasks refresh, unlock detection)
        void tickAnimations();

        bool isInitialized() const { return m_initialized; }

    private:
        void publishStyle();
        void createTaskbar();
        void layoutTaskbar();
        QW::Controls::Button *makeTaskbarButton(const char *text, QW::ButtonRole role,
                                                QW::Controls::ButtonClickHandler handler);

        void refreshClock(bool force);
        void toggleSystemMenu();
        void toggleClockPopup();
        void createSystemMenu();
        void createClockPopup();
        void updateClockPopup();
        void hidePopups();
        void dismissPopupsOutside(QC::i32 x, QC::i32 y);
        void positionPopup(QW::Window *popup, QW::Controls::Button *anchor);
        void finishUnlockIfReady();
        void showShutdownPrompt(QK::Shutdown::Reason reason);

        static void onTerminalClick(QW::Controls::Button *button, void *userData);
        static void onExplorerClick(QW::Controls::Button *button, void *userData);
        static void onTasksClick(QW::Controls::Button *button, void *userData);
        static void onClockClick(QW::Controls::Button *button, void *userData);
        static void onSystemClick(QW::Controls::Button *button, void *userData);
        static void onLockClick(QW::Controls::Button *button, void *userData);
        static void onRestartClick(QW::Controls::Button *button, void *userData);
        static void onShutdownClick(QW::Controls::Button *button, void *userData);

        static bool onShutdownRequested(QK::Shutdown::Reason reason, void *userData);
        static bool onWindowEvent(const QK::Event::Event &event, void *userData);
        static bool onInputEvent(const QK::Event::Event &event, void *userData);
        void ensureWindowEventListener();
        void ensureInputEventListener();

        bool m_initialized;
        QC::u32 m_screenWidth;
        QC::u32 m_screenHeight;

        QW::Window *m_desktopWindow;
        QW::Controls::Panel *m_taskbar;
        QW::Controls::Button *m_terminalButton;
        QW::Controls::Button *m_explorerButton;
        QW::Controls::Button *m_tasksButton;
        QW::Controls::Button *m_clockButton;
        QW::Controls::Button *m_systemButton;

        // System ▼ popup
        QW::Window *m_systemMenu;
        QW::Controls::Button *m_lockItem;

        // Clock popup
        QW::Window *m_clockPopup;
        QW::Controls::Label *m_clockDateLabel;
        QW::Controls::Label *m_clockTimeLabel;
        QW::Controls::Label *m_clockZoneLabel;
        QW::Controls::Label *m_clockUptimeLabel;

        QC::u64 m_lastClockRefreshMs;
        char m_clockText[8];

        struct RunningWindow
        {
            QC::u32 windowId;
            char title[48];
        };
        RunningWindow m_running[MAX_TASKBAR_WINDOWS];
        QC::u32 m_runningCount;
        QC::u32 m_activeWindowId;

        bool m_locked;
        QC::u32 m_lockedWindowIds[MAX_TASKBAR_WINDOWS];
        QC::u32 m_lockedWindowCount;

        QK::Event::ListenerId m_windowListenerId = QK::Event::InvalidListenerId;
        QK::Event::ListenerId m_inputListenerId = QK::Event::InvalidListenerId;

        Terminal *m_terminal;
        Browser *m_browser;
        CuiMLViewer *m_cuimlViewer;
        Explorer *m_explorer;
        Tasks *m_tasks;
        ShutdownDialog *m_shutdownDialog;
        SetupWizard *m_setupWizard;
        LoginDialog *m_loginDialog;
    };

} // namespace QD
