#pragma once

// QWindowing Framebuffer Present Backend - software presentation via Framebuffer::swap()
// Namespace: QW

#include "QWPresentBackend.h"

namespace QW
{
    class Framebuffer;

    class FramebufferPresentBackend final : public PresentBackend
    {
    public:
        void initialize(Framebuffer *fb) override;
        void present() override;
        void present(const QC::Rect *dirtyRects, QC::usize dirtyCount) override
        {
            (void)dirtyRects;
            (void)dirtyCount;
            present();
        }

    private:
        Framebuffer *m_framebuffer = nullptr;
    };
}
