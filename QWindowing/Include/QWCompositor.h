#pragma once

// QWindowing Compositor - Window compositing
// Namespace: QW

#include "QCTypes.h"
#include "QCVector.h"
#include "QWWindowManager.h"

namespace QW
{

    class Window;
    class Framebuffer;
    class Renderer;
    class PresentBackend;

    // Composition effect
    enum class CompositionEffect : QC::u8
    {
        None,
        Shadow,
        Blur,
        Transparency
    };

    // Dirty region
    struct DirtyRegion
    {
        Rect rect;
        bool merged;
    };

    class Compositor
    {
    public:
        Compositor(Framebuffer *fb);
        ~Compositor();

        void initialize();

        // Composition
        void compose();
        void composeWindow(Window *window);

        // Dirty regions
        void invalidate(const Rect &rect);
        void invalidateAll();
        void clearDirtyRegions();

        // Effects
        void setEffect(CompositionEffect effect, bool enabled);
        bool hasEffect(CompositionEffect effect) const;

        // Window decorations
        void drawWindowDecorations(Window *window);
        void drawTitleBar(Window *window);
        void drawBorder(Window *window);
        void drawShadow(Window *window);

        // Desktop
        void setWallpaper(const QC::u32 *pixels, QC::u32 width, QC::u32 height);
        void drawDesktop();

        // Cursor
        void setCursor(const QC::u32 *pixels, QC::u32 width, QC::u32 height,
                       QC::i32 hotspotX, QC::i32 hotspotY);
        void syncHardwareCursorPosition();
        void drawCursor(QC::i32 x, QC::i32 y);
        void saveCursorBackground(QC::i32 x, QC::i32 y);
        void restoreCursorBackground();

        // Performance
        QC::u64 lastComposeTime() const { return m_lastComposeTime; }
        QC::u32 frameCount() const { return m_frameCount; }

    private:
        void mergeDirtyRegions();

        Framebuffer *m_framebuffer;
        Renderer *m_renderer;
        PresentBackend *m_presentBackend;

        QC::Vector<DirtyRegion> m_dirtyRegions;
        QC::u32 m_effects;

        // Wallpaper
        QC::u32 *m_wallpaper;
        QC::u32 m_wallpaperWidth;
        QC::u32 m_wallpaperHeight;

        // Cursor
        QC::u32 *m_cursorPixels;
        QC::u32 m_cursorWidth;
        QC::u32 m_cursorHeight;
        QC::i32 m_cursorHotspotX;
        QC::i32 m_cursorHotspotY;
        QC::u32 *m_cursorBackground;
        QC::i32 m_cursorBackX;
        QC::i32 m_cursorBackY;

        // Stats
        QC::u64 m_lastComposeTime;
        QC::u32 m_frameCount;
    };

} // namespace QW
