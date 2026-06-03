#pragma once

// QWindowing Window Manager - Desktop window management
// Namespace: QW

#include "QCTypes.h"
#include "QCVector.h"
#include "QCGeometry.h"
#include "QCColor.h"
#include "QKEventTypes.h"
#include "QKEventListener.h"
#include "QWStyleSystem.h"

namespace QK
{
    namespace Event
    {
        class EventManager;
    }
}

namespace QW
{

    class Window;
    class Renderer;
    class Framebuffer;
    class Compositor;

    // Type aliases - use QC types in QW namespace for convenience
    using Point = QC::Point;
    using Size = QC::Size;
    using Rect = QC::Rect;
    using Color = QC::Color;
    using Margins = QC::Margins;

    class WindowManager : public QK::Event::IEventReceiver,
                          public StyleSystem::IStyleListener
    {
    public:
        static WindowManager &instance();

        void initialize(Framebuffer *fb);
        void shutdown();

        // IEventReceiver implementation
        bool onEvent(const QK::Event::Event &event) override;
        QK::Event::Category getEventMask() const override;

        // Window management
        Window *createWindow(const char *title, Rect bounds);
        void destroyWindow(Window *window);
        Window *windowById(QC::u32 id) const;

        // Focus
        void setFocus(Window *window);
        Window *focusedWindow() const { return m_focusedWindow; }

        // Z-order
        void bringToFront(Window *window);
        void sendToBack(Window *window);

        // Rendering
        void invalidate(const Rect &rect);
        void render();
        bool needsRender() const { return m_needsRender; }

        // Screen properties
        Size screenSize() const;

        // Compositor
        Compositor *compositor() { return m_compositor; }

        // Mouse state
        Point mousePosition() const { return m_mousePos; }

        // Input timing (updated on each routed mouse event)
        QC::u64 lastInputTimestamp() const { return m_lastInputTimestamp; }
        QC::u64 lastInputMs() const { return m_lastInputMs; }
        void noteMouseEventPosted(QC::u64 postedMs) { m_lastMouseEventPostedMs = postedMs; }
        QC::u64 lastMouseEventPostedMs() const { return m_lastMouseEventPostedMs; }
        QC::u64 lastMouseQueueDelayMs() const { return m_lastMouseQueueDelayMs; }
        QC::u64 maxMouseQueueDelayMs() const { return m_maxMouseQueueDelayMs; }
        void resetInputLatencyStats();

        // Window access for compositor
        QC::usize windowCount() const { return m_windows.size(); }
        Window *windowAtIndex(QC::usize index) { return m_windows[index]; }

        void onStyleChanged(const StyleSnapshot &snapshot) override;

    private:
        WindowManager();
        ~WindowManager();
        WindowManager(const WindowManager &) = delete;
        WindowManager &operator=(const WindowManager &) = delete;

        Window *windowAt(Point p);
        void routeMouseEvent(const QK::Event::MouseEventData &mouse);
        void routeKeyEvent(const QK::Event::KeyEventData &key);
        void postWindowEvent(QK::Event::Type type, Window *window);
        void applyStyleToWindow(Window *window, const StyleSnapshot &snapshot);
        void dumpDragReleaseBuffers(const char *phase,
                        QC::u32 sequence,
                        Window *window,
                        const Rect &windowBounds,
                        const Rect &decoratedBounds);

        struct PendingDestroy
        {
            Window *window;
            Rect bounds;
        };

        void processPendingDestroy();

        QC::u32 m_nextWindowId;
        QC::Vector<Window *> m_windows;
        Window *m_focusedWindow;
        Window *m_hoveredWindow;
        Framebuffer *m_framebuffer;
        Compositor *m_compositor;

        Point m_mousePos;
        QC::u64 m_lastInputTimestamp = 0;
        QC::u64 m_lastInputMs = 0;
        QC::u64 m_lastMouseEventPostedMs = 0;
        QC::u64 m_lastMouseQueueDelayMs = 0;
        QC::u64 m_maxMouseQueueDelayMs = 0;
        QK::Event::ListenerId m_listenerId;

        // Window drag/move state (title bar)
        Window *m_dragWindow;
        Point m_dragOffset;
        Rect m_dragStartBounds;

        // General mouse capture state (controls dragging inside a window)
        // When any mouse button is down on a window, keep routing subsequent
        // move/up events to that same window even if the cursor leaves.
        Window *m_captureWindow = nullptr;
        QC::u32 m_captureButtonsMask = 0;

        QC::u32 m_dispatchDepth = 0;
        QC::Vector<PendingDestroy> m_pendingDestroy;

        struct PendingDragDump
        {
            bool active = false;
            QC::u32 sequence = 0;
            QC::u32 windowId = 0;
            Rect windowBounds{0, 0, 0, 0};
            Rect decoratedBounds{0, 0, 0, 0};
        };

        QC::u32 m_nextDragDumpSequence = 0;
        PendingDragDump m_pendingDragDump;

        bool m_needsRender = true;
    };

} // namespace QW
