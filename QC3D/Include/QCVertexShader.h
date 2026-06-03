#pragma once

// QCVertexShader - Vertex transformation stage for software 3D rendering
// Namespace: QC

#include "QCVertex.h"
#include "QCLinearAlgebra.h"

namespace QC
{
    class VertexShader
    {
    public:
        VertexShader();

        // Set transform matrices
        void setModelMatrix(const QC::Mat4f& m);
        void setViewMatrix(const QC::Mat4f& v);
        void setProjectionMatrix(const QC::Mat4f& p);

        // Transform a vertex from model → world → view → clip → NDC → screen
        QC::Vertex transform(const QC::Vertex& in,
                             QC::u32 screenWidth,
                             QC::u32 screenHeight) const;

    private:
        QC::Mat4f m_model;
        QC::Mat4f m_view;
        QC::Mat4f m_proj;
    };

} // namespace QC


