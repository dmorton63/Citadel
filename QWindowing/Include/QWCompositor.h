#pragma once

// QWindowing Compositor - Window compositing
// Namespace: QW

#include "QCTypes.h"
#include "QCVector.h"
#include "QWPresentBackend.h"
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

    struct CompositorStats
    {
        QC::u64 lastComposeTimeMs = 0;
        QC::u64 lastPresentTimeMs = 0;
        QC::u64 maxPresentTimeMs = 0;
        QC::u64 lastInputToPresentMs = 0;
        QC::u64 maxInputToPresentMs = 0;
        QC::u32 frameCount = 0;
        QC::usize dirtyRegionCount = 0;
        QC::usize lastMergedDirtyRegionCount = 0;
        QC::usize lastPresentedDirtyRectCount = 0;
        QC::u64 lastDirtyArea = 0;
        QC::u32 lastDirtyCoveragePercent = 0;
        QC::u32 dirtyCollapseCount = 0;
        bool lastPresentWasFullFrame = false;
        bool lastPresentUsedBatching = false;
        bool hardwareCursorActive = false;
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
        bool supportsRectCopy() const;
        bool copyRectInBackBuffer(const Rect &src, const Rect &dst);
        bool rectCopy(const Rect &src, const Rect &dst);

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
        bool hasHardwareCursor() const;
        Rect cursorBoundsAt(QC::i32 x, QC::i32 y) const;
        void syncHardwareCursorPosition();
        void drawCursor(QC::i32 x, QC::i32 y);
        void saveCursorBackground(QC::i32 x, QC::i32 y);
        void restoreCursorBackground();

        // Performance
        QC::u64 lastComposeTime() const { return m_lastComposeTime; }
        QC::u32 frameCount() const { return m_frameCount; }
        const CompositorStats &stats() const { return m_stats; }
        const PresentAccelerationStats &accelerationStats() const;
        void resetStats();

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
        QC::u32 m_dirtyCollapseCount = 0;
        CompositorStats m_stats;
    };

} // namespace QW
