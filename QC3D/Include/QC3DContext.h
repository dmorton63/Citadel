#pragma once

// QC3DContext - High-level 3D rendering context
// Namespace: QC3D

#include "QCLinearAlgebra.h"
#include "QCVertex.h"
#include "QCColor.h"

namespace QC
{
    class DepthBuffer;
    class Rasterizer;
    class Mesh;

    class Context
    {
    public:
        Context();
        ~Context();

        // Target surface (RGBA32)
        void setTarget(QC::u32* pixels,
                       QC::u32 width,
                       QC::u32 height,
                       QC::u32 pitchBytes);

        // Depth buffer
        void enableDepth(bool enabled);
        void clearDepth(f32 depth = 1.0f);

        // Clear color buffer
        void clearColor(QC::Color color);

        // Camera transforms
        void setViewMatrix(const QC::Mat4f& view);
        void setProjectionMatrix(const QC::Mat4f& proj);

        // World transform for next draw
        void setModelMatrix(const QC::Mat4f& model);

        // Draw a mesh (triangles)
        void drawMesh(const Mesh& mesh);

    private:
        // Internal pipeline pieces
        Rasterizer* m_rasterizer;
        DepthBuffer* m_depth;

        QC::u32* m_pixels;
        QC::u32 m_width;
        QC::u32 m_height;
        QC::u32 m_pitch;

        QC::Mat4f m_model;
        QC::Mat4f m_view;
        QC::Mat4f m_proj;

        bool m_depthEnabled;
    };

} // namespace QC

