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
}
