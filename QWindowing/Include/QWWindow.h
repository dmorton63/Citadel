#pragma once

// QWindowing Window - Window hosting the UI control hierarchy
// Namespace: QW

#include "QCTypes.h"
#include "QKEventListener.h"
#include "QKEventTypes.h"
#include "QWControls/Containers/Panel.h"
#include "QWInterfaces/IControl.h"
#include "QG/PainterSurface.h"
#include "QGfxSurface.h"
#include "QWStyleRenderer.h"
#include "QWStyleTypes.h"
#include "QWSurfaceBackend.h"
#include "QCVector.h"

namespace QG
{
    class IPainter;
}

namespace QW
{
    class WindowManager;

    struct Message;
    using MessageHandler = bool (*)(Window *window, const Message &msg, void *userData);

    using Rect = QC::Rect;
    using Color = QC::Color;

    // Window flags
    namespace WindowFlags
    {
        constexpr QC::u32 Visible = 0x0001;
        constexpr QC::u32 Resizable = 0x0002;
        constexpr QC::u32 Movable = 0x0004;
        constexpr QC::u32 HasTitle = 0x0008;
        constexpr QC::u32 HasBorder = 0x0010;
        constexpr QC::u32 HasClose = 0x0020;
        constexpr QC::u32 HasMinimize = 0x0040;
        constexpr QC::u32 HasMaximize = 0x0080;

        // Special behavior flags
        // - NoFocus: window never becomes the focused window (desktop background, overlays).
        // - AlwaysBottom: window is pinned to the bottom of z-order and cannot be raised.
        constexpr QC::u32 NoFocus = 0x0100;
        constexpr QC::u32 AlwaysBottom = 0x0200;

        constexpr QC::u32 Default = Visible | Resizable | Movable |
                                    HasTitle | HasBorder | HasClose |
                                    HasMinimize | HasMaximize;
    }

    /// Window - wraps a painter surface and owns the root control tree
    class Window : public QK::Event::IEventReceiver
    {
    public:
        Window(const char *title, Rect bounds);
        ~Window();

        // identity
        uint32_t windowId() const;
        void setWindowId(uint32_t id);

        // properties
        const char *title() const;
        void setTitle(const char *title);
        Rect bounds() const;
        void setBounds(const Rect &bounds);
        bool isVisible() const;
        void setVisible(bool visible);
        uint32_t flags() const;
        void setFlags(uint32_t flags);

        // control tree
        QW::Controls::Panel *root() const;

        // style
        void setStyleSnapshot(const StyleSnapshot *snapshot);
        StyleRenderer *styleRenderer();
        QG::IPainter *painter() { return &m_painter; }
        const QG::IPainter *painter() const { return &m_painter; }

        // surface access (for compositor)
        const QC::u32 *buffer() const;
        QC::u32 bufferWidth() const;
        QC::u32 bufferHeight() const;
        QC::u32 bufferPitchBytes() const;
        const QGfx::Surface &graphicsSurface() const { return m_qgfxSurface; }
        QGfx::Surface &graphicsSurface() { return m_qgfxSurface; }

        // invalidation
        void invalidate();
        void invalidateRect(const Rect &rect);

        // Rendering (coalesced): invalidation marks the window dirty; actual painting happens
        // during WindowManager::render() to avoid doing full repaints inside input handlers.
        bool needsPaint() const { return m_needsPaint; }
        void paintIfNeeded();

        // event handling
        bool onEvent(const QK::Event::Event &e) override;

        // messaging (distinct from input/system events)
        void setMessageHandler(MessageHandler handler, void *userData);
        bool handleMessage(const Message &msg);

    protected:
        void onPaint();
        void onResize(uint32_t w, uint32_t h);

    private:
        void paint();
        bool ensureSurface(QC::u32 width, QC::u32 height);

        uint32_t m_windowId;
        char m_title[256];
        Rect m_bounds;
        uint32_t m_flags;

        QW::Controls::Panel *m_root;

        SurfaceBackend m_surfaceBackend;
        StyleRenderer m_styleRenderer;
        QG::PainterSurface m_painter;
        QC::Vector<QC::u32> m_surfacePixels;
        QGfx::Surface m_qgfxSurface;
        QC::u32 m_bufferWidth;
        QC::u32 m_bufferHeight;
        QC::u32 m_bufferPitchBytes;

        MessageHandler m_msgHandler = nullptr;
        void *m_msgUserData = nullptr;

        bool m_needsPaint = true;

        // Union of invalidated regions since last paint (window-local coordinates).
        Rect m_dirtyRect{0, 0, 0, 0};
        bool m_hasDirtyRect = false;
    };
} // namespace QW
