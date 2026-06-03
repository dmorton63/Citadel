// QWindowing Framebuffer Present Backend
// Namespace: QW

#include "QWDisplayBootstrapUtil.h"
#include "QWFramebufferPresentBackend.h"

namespace QW
{
    void FramebufferPresentBackend::initialize(Framebuffer *fb)
    {
        m_framebuffer = fb;

        if (!m_framebuffer)
            return;

        const QDrv::Display::cvd_boot_framebuffer_desc_t desc = makeBootFramebufferDesc(m_framebuffer);
        QDrv::Display::cvd_sync_value_t readyValue = 0;
        (void)QDrv::Display::cvd_open_boot_swapchain(&desc,
                                                     &m_device,
                                                     &m_output,
                                                     &m_swapchain,
                                                     &m_presentSurface,
                                                     &readyValue);
    }

    void FramebufferPresentBackend::present()
    {
        if (!m_device || !m_swapchain || !m_presentSurface.isValid())
            return;

        (void)QDrv::Display::cvd_present_regions(m_device,
                                                 m_swapchain,
                                                 m_presentSurface,
                                                 0,
                                                 nullptr,
                                                 0,
                                                 QDrv::Display::CVD_PRESENT_VSYNC);
    }

    void FramebufferPresentBackend::present(const QC::Rect *dirtyRects, QC::usize dirtyCount)
    {
        if (!m_device || !m_swapchain || !m_presentSurface.isValid())
            return;

        (void)QDrv::Display::cvd_present_regions(m_device,
                                                 m_swapchain,
                                                 m_presentSurface,
                                                 0,
                                                 dirtyRects,
                                                 dirtyCount,
                                                 QDrv::Display::CVD_PRESENT_VSYNC);
    }
}
