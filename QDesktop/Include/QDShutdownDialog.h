#pragma once

// QDesktop Shutdown Dialog - user prompt for graceful shutdown
// Namespace: QD

#include "QCTypes.h"
#include "QKShutdownController.h"

namespace QW
{
    class Window;
}

namespace QW::Controls
{
    class Panel;
    class Label;
    class Button;
}

namespace QD
{
    class Desktop;

    class ShutdownDialog
    {
    public:
        explicit ShutdownDialog(Desktop *desktop);
        ~ShutdownDialog();

        void open(QK::Shutdown::Reason reason);
        void close();
        bool isOpen() const { return m_window != nullptr; }

    private:
        void createWindow();
        void updateMessage();

        static void onConfirm(QW::Controls::Button *button, void *userData);
        static void onCancel(QW::Controls::Button *button, void *userData);

        Desktop *m_desktop;
        QK::Shutdown::Reason m_reason;

        QW::Window *m_window;
        QW::Controls::Panel *m_root;
        QW::Controls::Label *m_message;
        QW::Controls::Button *m_confirmButton;
        QW::Controls::Button *m_cancelButton;
    };

} // namespace QD
