#pragma once

// QWindowing VMware SVGA Present Backend - hooks for SVGA2D present/blit (cursor-only for now)
// Namespace: QW

#include "QWPresentBackend.h"

namespace QW
{
    class Framebuffer;

    class VmwareSVGAPresentBackend final : public PresentBackend
    {
    public:
        void initialize(Framebuffer *fb) override;
        void present() override;
        void present(const QC::Rect *dirtyRects, QC::usize dirtyCount) override;

        bool hasHardwareCursor() const override;
        void setCursorImage(const QC::u32 *pixels, QC::u16 width, QC::u16 height,
                            QC::u16 hotspotX, QC::u16 hotspotY) override;
        void setCursorVisible(bool visible) override;
        void setCursorPosition(QC::u16 x, QC::u16 y) override;

    private:
        Framebuffer *m_framebuffer = nullptr;
    };
}
