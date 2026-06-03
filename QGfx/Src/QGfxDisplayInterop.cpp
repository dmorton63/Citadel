#include "QGfxDisplayInterop.h"

namespace QGfx
{
    bool toDisplayFormat(PixelFormat source,
                         QDrv::Display::cvd_format_t &out_format)
    {
        switch (source)
        {
        case PixelFormat::ARGB8888:
            out_format = QDrv::Display::CVD_FORMAT_ARGB8888;
            return true;
        case PixelFormat::XRGB8888:
            out_format = QDrv::Display::CVD_FORMAT_XRGB8888;
            return true;
        default:
            break;
        }

        return false;
    }

    bool fromDisplayFormat(QDrv::Display::cvd_format_t source,
                           PixelFormat &out_format)
    {
        switch (source)
        {
        case QDrv::Display::CVD_FORMAT_ARGB8888:
            out_format = PixelFormat::ARGB8888;
            return true;
        case QDrv::Display::CVD_FORMAT_XRGB8888:
            out_format = PixelFormat::XRGB8888;
            return true;
        default:
            break;
        }

        return false;
    }

    void bindDisplaySurface(Surface &surface,
                            const DisplaySurfaceBinding &binding)
    {
        surface.displayDevice = binding.device;
        surface.displaySwapchain = binding.swapchain;
        surface.displaySurfaceId = binding.surface_id;
        surface.displayReadyValue = binding.ready_value;
    }

    DisplaySurfaceBinding displaySurfaceBinding(const Surface &surface)
    {
        DisplaySurfaceBinding binding{};
        binding.device = surface.displayDevice;
        binding.swapchain = surface.displaySwapchain;
        binding.surface_id = surface.displaySurfaceId;
        binding.ready_value = surface.displayReadyValue;
        return binding;
    }
}