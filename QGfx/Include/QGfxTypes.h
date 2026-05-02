#pragma once

#include "QCGeometry.h"
#include "QCTypes.h"

namespace QGfx
{
    enum class PixelFormat : QC::u8
    {
        Unknown = 0,
        ARGB8888,
        XRGB8888
    };

    enum class SurfaceUsage : QC::u8
    {
        Static = 0,
        Dynamic,
        RenderTarget,
        Scanout
    };

    enum class Transform : QC::u8
    {
        None = 0,
        Scale
    };

    struct SurfaceId
    {
        QC::u32 value = 0;

        bool isValid() const { return value != 0; }
    };

    struct ResourceHandle
    {
        QC::u32 value = 0;

        bool isValid() const { return value != 0; }
    };

    struct DriverCapabilities
    {
        bool supportsSurfaceUploads = false;
        bool supportsScanoutUploads = false;
        bool supportsSurfaceBlits = false;
        bool supportsScreenRectCopy = false;
        bool supportsAlphaBlend = false;
        bool supportsScaling = false;
        bool supportsPresent = false;
    };
}
