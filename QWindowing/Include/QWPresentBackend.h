#pragma once

// QWindowing Present Backend - abstracts how the compositor presents frames and optional hardware cursor
// Namespace: QW

#include "QCTypes.h"
#include "QCGeometry.h"
#include "QGfxBatch.h"
#include "QGfxSurface.h"

namespace QW
{
    class Framebuffer;

    struct PresentAccelerationStats
    {
        bool qgfxActive = false;
        bool qgfxScanoutUploadsActive = false;
        bool qgfxRectCopyActive = false;
        QC::u32 qgfxPresentCalls = 0;
        QC::u32 qgfxPresentSuccesses = 0;
        QC::u32 qgfxScanoutUploadCalls = 0;
        QC::u32 qgfxScanoutUploadRects = 0;
        QC::u32 qgfxScanoutUploadFallbacks = 0;
        QC::u32 qgfxRectCopyBatches = 0;
        QC::u32 qgfxRectCopyOps = 0;
    };

    struct WindowSurfaceBlit
    {
        const QGfx::Surface *surface = nullptr;
        const QC::u32 *pixels = nullptr;
        QC::u32 stridePixels = 0;
        QC::Rect dirtyRect{0, 0, 0, 0};
    };

    class PresentBackend
    {
    public:
        virtual ~PresentBackend() = default;

        virtual void initialize(Framebuffer *fb) = 0;
        virtual void present() = 0;

        // Dirty-rect present (optional). If not overridden, falls back to full present().
        // If dirtyCount == 0, callers should interpret that as "unknown" and present the full frame.
        virtual void present(const QC::Rect * /*dirtyRects*/, QC::usize /*dirtyCount*/) { present(); }

        // Optional acceleration hooks (future use)
        virtual bool supportsRectCopy() const { return false; }
        virtual void rectCopy(const QC::Rect & /*src*/, const QC::Rect & /*dst*/) {}
        virtual bool supportsWindowSurfaceBatches() const { return false; }
        virtual bool submitWindowSurfaceBatch(const QGfx::Batch & /*batch*/,
                                              const WindowSurfaceBlit * /*blits*/,
                                              QC::usize /*blitCount*/) { return false; }
        virtual const PresentAccelerationStats &accelerationStats() const
        {
            static PresentAccelerationStats s_empty;
            return s_empty;
        }
        virtual void resetAccelerationStats() {}

        // Optional cursor hooks
        virtual bool hasHardwareCursor() const { return false; }
        virtual void setCursorImage(const QC::u32 * /*pixels*/, QC::u16 /*width*/, QC::u16 /*height*/,
                                    QC::u16 /*hotspotX*/, QC::u16 /*hotspotY*/) {}
        virtual void setCursorVisible(bool /*visible*/) {}
        virtual void setCursorPosition(QC::u16 /*x*/, QC::u16 /*y*/) {}
    };
}
