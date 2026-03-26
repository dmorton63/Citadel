#pragma once

// QDesktop CUI-ML Viewer (MVP)
// Namespace: QD
//
// Loads a local .cuiml file and renders a minimal subset into its own window.
// Supported elements (MVP): Desktop/Layout/Panel/Label/Button + HelpWindow.

#include "QCTypes.h"
#include "QCVector.h"

namespace QW
{
    class Window;
}

namespace QW::Controls
{
    class Panel;
    class IControl;
    class Button;
}

namespace QG
{
    struct ImageSurface;
}

namespace QD
{
    class Desktop;

    class CuiMLViewer
    {
    public:
        explicit CuiMLViewer(Desktop *desktop);
        ~CuiMLViewer();

        void openFile(const char *path);
        void close();
        bool isOpen() const;

        // Click handler used by the parser to deny forbidden actions.
        static void onShutdownDeniedRequested(QW::Controls::Button *button, void *userData);

        // Parser hook: store decoded image surfaces for <Image> controls.
        // Surfaces are owned by the viewer and freed on close.
        void registerImageSurface(QG::ImageSurface *surface);

    private:
        void ensureWindow();
        bool windowStillAlive() const;
        void clearControls();

        // Policy: preview/child windows must not be able to request real system shutdown.
        void openShutdownDeniedDialog();
        void closeShutdownDeniedDialog();
        static void onShutdownDeniedOkClick(QW::Controls::Button *button, void *userData);

        Desktop *m_desktop = nullptr;

        QW::Window *m_window = nullptr;
        QC::u32 m_windowId = 0;
        QW::Controls::Panel *m_root = nullptr;

        // Local denial dialog for forbidden actions (currently: shutdown).
        QW::Window *m_shutdownDeniedWindow = nullptr;
        QC::u32 m_shutdownDeniedWindowId = 0;
        QW::Controls::Panel *m_shutdownDeniedRoot = nullptr;
        QW::Controls::Button *m_shutdownDeniedOk = nullptr;

        // Controls created by this viewer (for cleanup).
        QC::Vector<QW::Controls::IControl *> m_controls;

        // Decoded images used by ImageView controls (owned by viewer).
        QC::Vector<QG::ImageSurface *> m_imageSurfaces;
    };

} // namespace QD
