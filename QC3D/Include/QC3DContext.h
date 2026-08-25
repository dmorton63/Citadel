#pragma once

// QC3DContext - High-level 3D rendering context
// Namespace: QC3D

#include "QCLinearAlgebra.h"
#include "QCVertex.h"
#include "QCColor.h"

namespace QG { class IPainter; }

namespace QC
{
    class DepthBuffer;
    class Rasterizer;
    class Mesh;
    class PixelShader;

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

        // Lighting
        void setLightDir(const QC::Vec3f& dir);
        void setAmbient(float ambient);
        void setDiffuseStrength(float diffuse);
        void setLightingEnabled(bool enabled);

        // Backface culling (enabled by default)
        void enableBackfaceCulling(bool enabled);

        // Draw a mesh (triangles)
        void drawMesh(const Mesh& mesh);

        // Blit the rendered color buffer into a painter surface at (x, y).
        // Call after drawMesh to composite the 3D render into a UI window.
        void blitTo(QG::IPainter* painter, QC::i32 x, QC::i32 y) const;

    private:
        Rasterizer*  m_rasterizer;
        DepthBuffer* m_depth;
        PixelShader* m_pixelShader;

        QC::u32* m_pixels;
        QC::u32 m_width;
        QC::u32 m_height;
        QC::u32 m_pitch;

        QC::Mat4f m_model;
        QC::Mat4f m_view;
        QC::Mat4f m_proj;

        bool m_depthEnabled;
        bool m_backfaceCulling;
    };

} // namespace QC

