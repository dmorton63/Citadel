#pragma once

#include "QDrvDisplayTypes.h"
#include "QGfxSurface.h"
#include "QGfxTypes.h"

namespace QGfx
{
    struct DisplaySurfaceBinding
    {
        QDrv::Display::cvd_device_t device = nullptr;
        QDrv::Display::cvd_swapchain_t swapchain = nullptr;
        QDrv::Display::cvd_surface_id_t surface_id{};
        QDrv::Display::cvd_sync_value_t ready_value = 0;
    };

    bool toDisplayFormat(PixelFormat source,
                         QDrv::Display::cvd_format_t &out_format);

    bool fromDisplayFormat(QDrv::Display::cvd_format_t source,
                           PixelFormat &out_format);

    void bindDisplaySurface(Surface &surface,
                            const DisplaySurfaceBinding &binding);

    DisplaySurfaceBinding displaySurfaceBinding(const Surface &surface);
}