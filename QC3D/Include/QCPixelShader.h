#pragma once

// QCPixelShader - Pixel shading stage for software 3D rendering
// Namespace: QC

#include "QCVertex.h"
#include "QCColor.h"

namespace QC
{
    class PixelShader
    {
    public:
        PixelShader();

        // Simple shading: just return interpolated vertex color
        QC::Color shade(const QC::Vertex& vInterp) const;

        // Future expansion:
        // - texture sampling
        // - normal mapping
        // - lighting models
        // - material parameters
        // - specular highlights
        // - shadows
        // - fog
        // - gamma correction
    };

} // namespace QC
