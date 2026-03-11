#pragma once

// QDesktop Login Dialog - simple PIN unlock screen
// Namespace: QD

#include "QCTypes.h"

namespace QW
{
    class Window;
}

namespace QW::Controls
{
    class Panel;
    class Label;
    class Button;
    class TextBox;
}

namespace QD
{
    class Desktop;

    class LoginDialog
    {
    public:
        explicit LoginDialog(Desktop *desktop);
        ~LoginDialog();

        void open();
        void close();
        bool isOpen() const { return m_window != nullptr; }

    private:
        void createWindow();
        void setStatus(const char *text);

        static void onUnlockClick(QW::Controls::Button *button, void *userData);
        static void onCancelClick(QW::Controls::Button *button, void *userData);

        Desktop *m_desktop;
        QW::Window *m_window;

        QW::Controls::Panel *m_root;
        QW::Controls::Label *m_title;
        QW::Controls::Label *m_hint;
        QW::Controls::Label *m_pinLabel;
        QW::Controls::TextBox *m_pinBox;
        QW::Controls::Label *m_status;
        QW::Controls::Button *m_unlockButton;
        QW::Controls::Button *m_cancelButton;
    };

} // namespace QD
