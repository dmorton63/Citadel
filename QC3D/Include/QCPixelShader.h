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

        // Lighting parameters.
        void setLightDir(const QC::Vec3f &dir);    // should be normalised
        void setAmbient(float ambient);             // 0..1
        void setDiffuseStrength(float diffuse);     // 0..1
        void setLightingEnabled(bool enabled);

        // Shade an interpolated vertex; applies ambient + Lambert diffuse when
        // lighting is enabled, otherwise returns interpolated vertex color.
        QC::Color shade(const QC::Vertex& vInterp) const;

    private:
        QC::Vec3f m_lightDir;     // world-space light direction (normalised)
        float     m_ambient;
        float     m_diffuse;
        bool      m_lighting;    };

} // namespace QC
