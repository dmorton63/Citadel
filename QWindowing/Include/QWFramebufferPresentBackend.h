#pragma once

// QWindowing Framebuffer Present Backend - software presentation via Framebuffer::swap()
// Namespace: QW

#include "QDrvDisplaySurface.h"
#include "QWPresentBackend.h"

namespace QW
{
    class Framebuffer;

    class FramebufferPresentBackend final : public PresentBackend
    {
    public:
        void initialize(Framebuffer *fb) override;
        void present() override;
        void present(const QC::Rect *dirtyRects, QC::usize dirtyCount) override;

    private:
        Framebuffer *m_framebuffer = nullptr;
        QDrv::Display::cvd_device_t m_device = nullptr;
        QDrv::Display::cvd_output_t m_output = nullptr;
        QDrv::Display::cvd_swapchain_t m_swapchain = nullptr;
        QDrv::Display::cvd_surface_id_t m_presentSurface{};
    };
}
