#pragma once

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
