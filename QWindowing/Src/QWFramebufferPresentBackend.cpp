// QWindowing Framebuffer Present Backend
// Namespace: QW

#include "QWFramebufferPresentBackend.h"
#include "QWFramebuffer.h"

namespace QW
{
    void FramebufferPresentBackend::initialize(Framebuffer *fb)
    {
        m_framebuffer = fb;
    }

    void FramebufferPresentBackend::present()
    {
        if (m_framebuffer)
        {
            m_framebuffer->swap();
        }
    }

    void FramebufferPresentBackend::present(const QC::Rect *dirtyRects, QC::usize dirtyCount)
    {
        if (!m_framebuffer)
            return;

        if (!dirtyRects || dirtyCount == 0)
        {
            m_framebuffer->swap();
            return;
        }

        for (QC::usize i = 0; i < dirtyCount; ++i)
        {
            m_framebuffer->swapRect(dirtyRects[i]);
        }
    }
}
