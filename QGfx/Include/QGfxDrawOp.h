#pragma once

#include "QGfxTypes.h"

namespace QGfx
{
    struct DrawOp
    {
        SurfaceId srcSurface;
        QC::Rect srcRect;
        QC::Rect dstRect;
        QC::u8 opacity = 255;
        Transform transform = Transform::None;
        QC::i32 zOrder = 0;
    };
}
