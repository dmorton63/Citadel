#pragma once

#include "QDrvDisplayTypes.h"
#include "QGfxTypes.h"

namespace QGfx
{
    struct Surface
    {
        SurfaceId id;
        QC::u32 width = 0;
        QC::u32 height = 0;
        PixelFormat format = PixelFormat::Unknown;
        SurfaceUsage usage = SurfaceUsage::Dynamic;
        QDrv::Display::cvd_device_t displayDevice = nullptr;
        QDrv::Display::cvd_swapchain_t displaySwapchain = nullptr;
        QDrv::Display::cvd_surface_id_t displaySurfaceId{};
        QDrv::Display::cvd_sync_value_t displayReadyValue = 0;
        ResourceHandle gpuHandle;
        QC::Rect dirtyRegion{0, 0, 0, 0};

        bool isValid() const
        {
            return id.isValid() && width > 0 && height > 0 && format != PixelFormat::Unknown;
        }

        void clearDirtyRegion()
        {
            dirtyRegion = QC::Rect{0, 0, 0, 0};
        }
    };
}
