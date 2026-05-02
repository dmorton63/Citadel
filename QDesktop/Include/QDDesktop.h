#pragma once

// QDesktop Desktop - Main desktop shell using Window and Controls
// Namespace: QD
//
// The desktop is built using the existing QW::Window and QW::Controls:
// - A fullscreen Window serves as the desktop background
// - Panel controls for TopBar, Sidebar, Taskbar
// - Button controls for sidebar icons and taskbar window buttons
// - Label controls for clock and titles
//
// Layout:
// ┌──────────────────────────────────────────────────────────────┐
// │  [Logo]  QAIOS+ Desktop                      10:32 AM   [⋯]  │  ← TopBar Panel
// ├───────┬──────────────────────────────────────────────────────┤
// │       │                                                      │
// │ Side  │                                                      │
// │ bar   │                Desktop Area                          │
// │ Panel │                (for windows)                         │
// │       │                                                      │
// ├───────┴──────────────────────────────────────────────────────┤
// │  [Win1]  [Win2]                              [Tray][Clock]   │  ← Taskbar Panel
// └──────────────────────────────────────────────────────────────┘

#include "QCTypes.h"
#include "QCGeometry.h"
#include "QCUIStyle.h"
#include "QWWindow.h"
#include "QWStyleTypes.h"
#include "QWControls/Containers/Panel.h"
#include "QWControls/Leaf/Button.h"
#include "QWControls/Leaf/ImageView.h"
#include "QWControls/Leaf/Label.h"
#include "QWInterfaces/IControl.h"
#include "QCVector.h"
#include "QG/Image.h"
#include "QKEventListener.h"
#include "QDAccent.h"
#include "QCQLEngine.h"
#include "QDThemeOverrides.h"
#include "QDThemeService.h"
#include "QDTerminal.h"

namespace QK
{
    namespace Shutdown
    {
        enum class Reason : QC::u8;
    }
}

namespace QC
{
    namespace JSON
    {
        class Value;
    }
}

namespace QD
{
    using QC::Rect;
    using QC::Size;

    class ShutdownDialog;
    class SetupWizard;
    class LoginDialog;
    class Browser;
    class CuiMLViewer;

    // Layout constants
    constexpr QC::u32 TOP_BAR_HEIGHT = 32;
    constexpr QC::u32 SIDEBAR_WIDTH = 64;
    constexpr QC::u32 TASKBAR_HEIGHT = 40;

    // Maximum windows in taskbar
    constexpr QC::u32 MAX_TASKBAR_WINDOWS = 12;

    /// Sidebar item identifiers
    enum class SidebarItem : QC::u8
    {
        Home = 0,
        Apps,
        Settings,
        Files,
        Terminal,
        Power,
        Count
    };

    /// Desktop - Main desktop shell
    /// Owns a fullscreen Window and control panels
    class Desktop
    {
    public:
        Desktop();
        ~Desktop();

        // ==================== Initialization ====================

        /// Initialize desktop with screen dimensions
        /// Creates the desktop window and all panels
        void initialize(QC::u32 screenWidth, QC::u32 screenHeight);

        /// Resize desktop (on resolution change)
        void resize(QC::u32 screenWidth, QC::u32 screenHeight);

        /// Clean up resources
        void shutdown();

        // ==================== Access ====================

        /// Get the desktop window
        QW::Window *window() { return m_desktopWindow; }
        const QW::Window *window() const { return m_desktopWindow; }

        /// Get screen dimensions
        QC::u32 screenWidth() const { return m_screenWidth; }
        QC::u32 screenHeight() const { return m_screenHeight; }

        /// Get the desktop work area (where app windows can appear)
        Rect workArea() const;

        // ==================== User Setup/Login (v1 UI) ====================

        /// Returns true when the owner enrollment marker exists
        bool isOwnerEnrolled() const;

        /// Show first-boot setup wizard (Owner enrollment)
        void showSetupWizard();

        /// Show simple PIN login/unlock dialog
        void showLoginDialog();

        /// Hide/close the login dialog if open
        void hideLoginDialog();

        // ==================== Browser (HTML Viewer) ====================

        /// Open (or focus) the HTML viewer and render an on-disk HTML file.
        void openBrowserFile(const char *path);

        /// Open (or focus) the HTML viewer and fetch+render a remote http:// URL (no TLS).
        void openBrowserUrl(const char *url);

        /// Open (or focus) the HTML viewer and render inline HTML content.
        void openBrowserHtmlText(const char *htmlText);

        // ==================== CUI-ML Viewer (MVP) ====================

        /// Open (or focus) the CUI-ML viewer and render an on-disk .cuiml file.
        void openCuiMLFile(const char *path);

        // ==================== Panels ====================

        QW::Controls::Panel *topBar() { return m_topBar; }
        QW::Controls::Panel *sidebar() { return m_sidebar; }
        QW::Controls::Panel *taskbar() { return m_taskbar; }

        // ==================== Time ====================

        /// Update displayed clock time
        void setTime(QC::u32 hours, QC::u32 minutes);

        // ==================== Window Management ====================

        /// Set the focused window title (shown in top bar)
        void setFocusedWindowTitle(const char *title);

        /// Add a window button to the taskbar
        void addTaskbarWindow(QC::u32 windowId, const char *title);

        /// Remove a window button from the taskbar
        void removeTaskbarWindow(QC::u32 windowId);

        /// Set which taskbar window is active
        void setActiveTaskbarWindow(QC::u32 windowId);

        // ==================== Rendering ====================

        /// Paint the entire desktop
        void paint();

        /// Paint just the background gradient
        void paintBackground();

        // ==================== State ====================

        bool isInitialized() const { return m_initialized; }

    private:
        bool tryInitializeFromCuiML();
        bool tryInitializeFromJson();

        bool tryLoadDesktopOverridesFromVfs(QC::JSON::Value &outRoot, const char *&outOpenedPath) const;
        void applyDesktopOverridesObject(const QC::JSON::Value &ovrRoot, bool &ioBackgroundApplied);

        void clearJsonDesktopState();
        void resetThemeOverrides();
        void parseThemeOverrides(const QC::JSON::Value *themeValue);
        void parseThemeOverridesMerge(const QC::JSON::Value *themeValue);
        void installDefaultMaterials();

        void createTopBar();
        void createSidebar();
        void createTaskbar();
        void updateLayout();
        void applyColors();
        void publishStyleSnapshot(const DesktopColors &colors);
        void applyThemeToDesktopColors(DesktopColors &colors) const;
        void updateSidebarButtonRoles();
        void resetBackgroundConfig();
        void parseBackground(const QC::JSON::Value *backgroundValue);
        struct ImageAsset;
        ImageAsset *findImageAsset(const char *path) const;
        ImageAsset *loadImageAsset(const char *path);
        void releaseImageAssets();
        bool readFileBytes(const char *path, QC::Vector<QC::u8> &outBuffer, bool logFailure = true) const;

        void openTerminal();
        void toggleTerminal();
        void openBrowser();
        bool ensureCmmsDatabaseReady();
        void openCMMS();
        void openHelpWindow();
        bool applyThemeStateOnce(ThemeID activeThemeId);
        void cycleThemeFromSettings();
        bool applyThemeByIdString(const char *themeIdText);
        void recomputeTaskbarWindowBase();
        void showShutdownPrompt(QK::Shutdown::Reason reason);

        // Sidebar button click handler
        static void onSidebarClick(QW::Controls::Button *button, void *userData);
        static bool onShutdownRequested(QK::Shutdown::Reason reason, void *userData);

        // JSON desktop action handlers
        static void onJsonTerminalClick(QW::Controls::Button *button, void *userData);
        static void onJsonShutdownClick(QW::Controls::Button *button, void *userData);
        static void onJsonSettingsClick(QW::Controls::Button *button, void *userData);
        static void onJsonCMMSClick(QW::Controls::Button *button, void *userData);

        // CUI-ML icon-style button action handlers
        static void onJsonTerminalButtonClick(QW::Controls::Button *button, void *userData);
        static void onJsonShutdownButtonClick(QW::Controls::Button *button, void *userData);
        static void onJsonSettingsButtonClick(QW::Controls::Button *button, void *userData);
        static void onJsonCMMSButtonClick(QW::Controls::Button *button, void *userData);

        // Taskbar button click handler
        static void onTaskbarClick(QW::Controls::Button *button, void *userData);
        static void onTaskbarIconButtonClick(QW::Controls::Button *button, void *userData);

        static bool onWindowEvent(const QK::Event::Event &event, void *userData);
        void ensureWindowEventListener();
        void layoutTaskbarWindows();

        // CUI-ML help window button handler
        static void onHelpClick(QW::Controls::Button *button, void *userData);

        bool m_initialized;
        QC::u32 m_screenWidth;
        QC::u32 m_screenHeight;

        // The desktop window (fullscreen, no chrome)
        QW::Window *m_desktopWindow;

        bool m_jsonDriven;
        QC::Vector<QW::Controls::IControl *> m_jsonControls;
        QC::Vector<QW::Controls::IControl *> m_jsonRootControls;

        ThemeOverrides m_themeOverrides;
        ThemeService m_themeService;
        ThemeLoadResult m_loadedTheme;

        // Font loading (theme-selected family -> VFS bytes -> QG::FontManager)
        bool m_lastAppliedFontFamilySet = false;
        char m_lastAppliedFontFamily[48] = {};

        bool loadThemeDefinition(const QC::JSON::Value *themeValue);
        void applyLoadedThemeToOverrides();

        static float clamp01(float value);
        static QC::u8 clampToByte(QC::u32 value);
        static bool parseColorOverride(const QC::JSON::Value *object, const char *key, ColorOverride &target);
        static bool parseUnsignedOverride(const QC::JSON::Value *object, const char *key, QC::u32 &outValue);
        static bool parseSignedOverride(const QC::JSON::Value *object, const char *key, QC::i32 &outValue);
        static bool parseBoolOverride(const QC::JSON::Value *object, const char *key, bool &outValue);
        static bool parseButtonStyleOverride(const QC::JSON::Value *buttons, const char *key, ButtonStyleOverrides &out);

        // Panels
        QW::Controls::Panel *m_topBar;
        QW::Controls::Panel *m_sidebar;
        QW::Controls::Panel *m_taskbar;

        // JSON-specific buttons we track for layout offsets
        QW::Controls::IControl *m_jsonStartButton;
        QW::Controls::IControl *m_jsonShutdownButton;

        // CUI-ML HelpWindow (optional)
        QW::Controls::Button *m_helpButton = nullptr;
        char *m_helpTitle = nullptr;
        char *m_helpSrcOrUrl = nullptr;
        char *m_helpInlineHtml = nullptr;

        // JSON-driven wallpaper (painted as a root control behind everything else)
        QW::Controls::ImageView *m_jsonWallpaperView;

        // Top bar controls
        QW::Controls::Button *m_logoButton;
        QW::Controls::Label *m_titleLabel;
        QW::Controls::Label *m_clockLabel;

        // Dynamic taskbar layout helpers
        QC::i32 m_taskbarWindowBaseX;

        // Sidebar buttons
        QW::Controls::Button *m_sidebarButtons[static_cast<QC::u8>(SidebarItem::Count)];
        SidebarItem m_selectedSidebarItem;

        // Taskbar window buttons
        struct TaskbarEntry
        {
            QC::u32 windowId;
            QW::Controls::Button *button;
            QC::u32 width;
            QC::u32 height;
            bool isActive;
        };
        TaskbarEntry m_taskbarEntries[MAX_TASKBAR_WINDOWS];
        QC::u32 m_taskbarWindowCount;

        QK::Event::ListenerId m_windowListenerId = QK::Event::InvalidListenerId;

        // Clock state
        QC::u32 m_hours;
        QC::u32 m_minutes;

        Terminal *m_terminal;
        Browser *m_browser;
        CuiMLViewer *m_cuimlViewer;
        class ShutdownDialog *m_shutdownDialog;

        SetupWizard *m_setupWizard;
        LoginDialog *m_loginDialog;

        QCQL::Database m_cmmsDatabase;
        bool m_cmmsDatabaseReady = false;

        struct ImageAsset
        {
            char path[128];
            QG::ImageSurface surface;
        };

        enum class BackgroundMode : QC::u8
        {
            Gradient,
            Image
        };

        struct BackgroundConfig
        {
            BackgroundMode mode = BackgroundMode::Gradient;
            QW::Color topColor;
            QW::Color bottomColor;
            bool topOverride = false;
            bool bottomOverride = false;
            ImageAsset *image = nullptr;
            QG::ImageScaleMode scaleMode = QG::ImageScaleMode::Stretch;
        };

        BackgroundConfig m_backgroundConfig;
        QC::Vector<ImageAsset *> m_imageAssets;
        QC::Vector<QC::u32> m_backgroundScratch;
    };

} // namespace QD
