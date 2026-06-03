#pragma once

#include "QDrvDisplayBootstrap.h"
#include "QWFramebuffer.h"

namespace QW
{
    inline QDrv::Display::cvd_boot_framebuffer_desc_t makeBootFramebufferDesc(Framebuffer *fb)
    {
        QDrv::Display::cvd_boot_framebuffer_desc_t desc{};
        if (!fb)
            return desc;

        desc.front_buffer = fb->buffer();
        desc.back_buffer = fb->backBuffer();
        desc.width = fb->width();
        desc.height = fb->height();
        desc.pitch = fb->pitch();
        desc.bpp = fb->bpp();
        desc.frontbuffer_is_mmio = fb->frontbufferIsMMIO();

        switch (fb->format())
        {
        case PixelFormat::ARGB8888:
            desc.format = QDrv::Display::CVD_FORMAT_ARGB8888;
            break;
        case PixelFormat::RGB565:
        case PixelFormat::BGR565:
            desc.format = QDrv::Display::CVD_FORMAT_RGB565;
            break;
        default:
            desc.format = QDrv::Display::CVD_FORMAT_XRGB8888;
            break;
        }

        return desc;
    }

    inline QDrv::Display::cvd_format_t toDisplayFormat(Framebuffer *fb)
    {
        return makeBootFramebufferDesc(fb).format;
    }
}