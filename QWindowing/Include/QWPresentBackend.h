#pragma once

// QWindowing Present Backend - abstracts how the compositor presents frames and optional hardware cursor
// Namespace: QW

#include "QCTypes.h"
#include "QCGeometry.h"

namespace QW
{
    class Framebuffer;

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

        // Optional cursor hooks
        virtual bool hasHardwareCursor() const { return false; }
        virtual void setCursorImage(const QC::u32 * /*pixels*/, QC::u16 /*width*/, QC::u16 /*height*/,
                                    QC::u16 /*hotspotX*/, QC::u16 /*hotspotY*/) {}
        virtual void setCursorVisible(bool /*visible*/) {}
        virtual void setCursorPosition(QC::u16 /*x*/, QC::u16 /*y*/) {}
    };
}
