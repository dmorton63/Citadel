#pragma once

// QDesktop Setup Wizard - first boot owner enrollment screen
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

    class SetupWizard
    {
    public:
        explicit SetupWizard(Desktop *desktop);
        ~SetupWizard();

        void open();
        void close();
        bool isOpen() const { return m_window != nullptr; }

    private:
        void createWindow();
        void setStatus(const char *text);
        bool tryWriteOwnerMarker(const char *username);

        static void onCreateClick(QW::Controls::Button *button, void *userData);
        static void onCancelClick(QW::Controls::Button *button, void *userData);

        Desktop *m_desktop;
        QW::Window *m_window;

        QW::Controls::Panel *m_root;
        QW::Controls::Label *m_title;
        QW::Controls::Label *m_hint;

        QW::Controls::Label *m_userLabel;
        QW::Controls::TextBox *m_userBox;

        QW::Controls::Label *m_pinLabel;
        QW::Controls::TextBox *m_pinBox;

        QW::Controls::Label *m_questionLabel;
        QW::Controls::TextBox *m_questionBox;

        QW::Controls::Label *m_answerLabel;
        QW::Controls::TextBox *m_answerBox;

        QW::Controls::Label *m_status;

        QW::Controls::Button *m_createButton;
        QW::Controls::Button *m_cancelButton;
    };

} // namespace QD
