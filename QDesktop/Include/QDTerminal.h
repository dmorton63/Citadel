#pragma once

// QDesktop Terminal - Simple command interpreter window
// Namespace: QD

#include "QCTypes.h"
#include "QKEventListener.h"

namespace QW
{
    class Window;
    struct Message;
}

namespace QW::Controls
{
    class Label;
    class TextBox;
    class Panel;
    class Button;
    class ListView;
    class ScrollBar;
}

namespace QD
{

    class Desktop;

    class Terminal
    {
    public:
        explicit Terminal(Desktop *desktop);
        ~Terminal();

        void open();
        bool isOpen() const;
        void close();

        void focus();

    private:
        static void onSubmit(QW::Controls::TextBox *textBox, void *userData);
        static void onCloseClick(QW::Controls::Button *button, void *userData);
        static void onMinClick(QW::Controls::Button *button, void *userData);
        static void onMaxClick(QW::Controls::Button *button, void *userData);
        static bool onWindowMessage(QW::Window *window, const QW::Message &msg, void *userData);
        static bool onEvent(const QK::Event::Event &event, void *userData);

        void onWindowDestroyed(QC::u32 windowId);
        bool windowStillAlive() const;

        void appendLine(const char *line);
        void executeLine(const char *line);
        void listDirectory(const char *path);
        void layoutWindow();
        void minimizeWindow();
        void toggleMaximizeWindow();
        bool resolveInputPath(const char *rawPath, char *outPath, QC::usize outCap) const;
        void viewFile(const char *pathArg);
        void openMiniEditor(const char *pathArg);
        void handleMiniEditorLine(const char *line);
        void showMiniEditorBuffer();
        bool saveMiniEditorBuffer();

        // Access/mode testing (fake auth)
        void setCallerAccess(QC::u8 access);
        const char *callerAccessName() const;
        void openChmodeDialog(QC::u8 targetAccess);
        void closeChmodeDialog();
        static void onChmodeOkClick(QW::Controls::Button *button, void *userData);
        static void onChmodeCancelClick(QW::Controls::Button *button, void *userData);

        // Terminal-only permission notice (used by shutdown)
        void openShutdownPermissionDialog();
        void closeShutdownPermissionDialog();
        static void onShutdownPermissionOkClick(QW::Controls::Button *button, void *userData);

        void syncScrollUi();
        void scrollToTail();

        static void onOutputScrollChanged(QW::Controls::ScrollBar *scrollBar, void *userData);
        static void onOutputViewScrollChanged(QW::Controls::ListView *listView, void *userData);

        Desktop *m_desktop;

        QW::Window *m_window;
        QC::u32 m_windowId;
        QW::Controls::Panel *m_root;
        QW::Controls::Panel *m_content;
        QW::Controls::Panel *m_titleBar;
        QW::Controls::Label *m_titleLabel;
        QW::Controls::ListView *m_output;
        QW::Controls::ScrollBar *m_outputScroll;
        QW::Controls::TextBox *m_input;
        QW::Controls::Button *m_closeButton;
        QW::Controls::Button *m_minButton;
        QW::Controls::Button *m_maxButton;

        QK::Event::ListenerId m_windowListenerId;

        bool m_followTail;
        bool m_syncingScroll;
        bool m_isMaximized;
        QC::i32 m_restoreX;
        QC::i32 m_restoreY;
        QC::u32 m_restoreW;
        QC::u32 m_restoreH;

        bool m_editorActive;
        bool m_editorDirty;
        char m_editorPath[256];
        QC::usize m_editorLen;
        static constexpr QC::usize EDITOR_BUFFER_CAP = 32768;
        char m_editorBuffer[EDITOR_BUFFER_CAP];

        // Encodes QC::Cmd::AccessLevel values (0..4). Defaults to User for desktop terminal.
        QC::u8 m_callerAccess;

        // Fake credentials dialog for elevation.
        QW::Window *m_chmodeWindow;
        QC::u32 m_chmodeWindowId;
        QW::Controls::Panel *m_chmodeRoot;
        QW::Controls::TextBox *m_chmodeUser;
        QW::Controls::TextBox *m_chmodePass;
        QW::Controls::Button *m_chmodeOk;
        QW::Controls::Button *m_chmodeCancel;
        QC::u8 m_chmodePendingAccess;

        // Terminal-only shutdown permission dialog.
        QW::Window *m_shutdownPermWindow;
        QC::u32 m_shutdownPermWindowId;
        QW::Controls::Panel *m_shutdownPermRoot;
        QW::Controls::Button *m_shutdownPermOk;

        QC::usize m_savetermLastSavedCount;
        bool m_savetermHasBaseline;
        char m_savetermLastPath[256];

        static constexpr QC::usize OUTPUT_MAX_LINES = 2048;
    };

} // namespace QD
