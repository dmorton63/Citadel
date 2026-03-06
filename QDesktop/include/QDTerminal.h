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
        static bool onWindowMessage(QW::Window *window, const QW::Message &msg, void *userData);
        static bool onEvent(const QK::Event::Event &event, void *userData);

        void onWindowDestroyed(QC::u32 windowId);
        bool windowStillAlive() const;

        void appendLine(const char *line);
        void executeLine(const char *line);
        void listDirectory(const char *path);

        void syncScrollUi();
        void scrollToTail();

        static void onOutputScrollChanged(QW::Controls::ScrollBar *scrollBar, void *userData);
        static void onOutputViewScrollChanged(QW::Controls::ListView *listView, void *userData);

        Desktop *m_desktop;

        QW::Window *m_window;
        QC::u32 m_windowId;
        QW::Controls::Panel *m_root;
        QW::Controls::ListView *m_output;
        QW::Controls::ScrollBar *m_outputScroll;
        QW::Controls::TextBox *m_input;

        QK::Event::ListenerId m_windowListenerId;

        bool m_followTail;
        bool m_syncingScroll;

        static constexpr QC::usize OUTPUT_MAX_LINES = 2048;
    };

} // namespace QD
