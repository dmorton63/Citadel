#pragma once

// QGraphics Camera (Right-handed)
// Namespace: QG
//
// Minimal helper for advanced graphics work.
// Uses QC column-major, RH conventions from QCLinearAlgebra.

#include "QCTypes.h"
#include "QCLinearAlgebra.h"

namespace QG
{

    struct CameraRH
    {
        QC::Vec3f eye{0.0f, 0.0f, 0.0f};
        QC::Vec3f center{0.0f, 0.0f, -1.0f};
        QC::Vec3f up{0.0f, 1.0f, 0.0f};

        // Projection
        QC::f32 fovYDegrees = 60.0f;
        QC::f32 aspect = 1.0f;
        QC::f32 nearZ = 0.1f;
        QC::f32 farZ = 1000.0f;

        QC::Mat4f view() const
        {
            return QC::lookAtRH(eye, center, up);
        }

        QC::Mat4f proj() const
        {
            return QC::perspectiveRHFromFovYDegrees(fovYDegrees, aspect, nearZ, farZ);
        }

        QC::Mat4f viewProj() const
        {
            // Column-major: viewProj = proj * view
            return QC::mul(proj(), view());
        }
    };

    // Orthographic camera for UI/2D rendering.
    // Pixel space: (0,0) is top-left, +x right, +y down.
    // Right-handed: +z points out of the screen; near/far typically -1..1.
    struct UICameraOrthoRH
    {
        QC::u32 width = 1;
        QC::u32 height = 1;
        QC::f32 nearZ = -1.0f;
        QC::f32 farZ = 1.0f;

        // Optional view offset in pixels (e.g., camera pan).
        QC::Vec3f viewOffset{0.0f, 0.0f, 0.0f};

        QC::Mat4f view() const
        {
            // Camera motion is opposite of world motion.
            return QC::Mat4f::translation(QC::Vec3f{-viewOffset.x, -viewOffset.y, -viewOffset.z});
        }

        QC::Mat4f proj() const
        {
            const QC::f32 w = static_cast<QC::f32>(width);
            const QC::f32 h = static_cast<QC::f32>(height);

            // y-down: bottom=height, top=0
            return QC::orthoRH(0.0f, w, h, 0.0f, nearZ, farZ);
        }

        QC::Mat4f viewProj() const
        {
            return QC::mul(proj(), view());
        }
    };

} // namespace QG
