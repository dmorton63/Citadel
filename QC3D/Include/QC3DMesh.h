#pragma once

// QC3DMesh - Basic mesh container for software 3D rendering
// Namespace: QC3D

#include "QCVertex.h"
#include "QCTypes.h"

namespace QC
{
    class Mesh
    {
    public:
        Mesh();
        Mesh(const QC::Vertex* vertices,
             QC::u32 vertexCount,
             const QC::u32* indices,
             QC::u32 indexCount);

        // Set data
        void setVertices(const QC::Vertex* vertices, QC::u32 count);
        void setIndices(const QC::u32* indices, QC::u32 count);

        // Accessors
        const QC::Vertex* vertices() const { return m_vertices; }
        QC::u32 vertexCount() const { return m_vertexCount; }

        const QC::u32* indices() const { return m_indices; }
        QC::u32 indexCount() const { return m_indexCount; }

        bool isValid() const { return m_vertices && m_indices && m_vertexCount > 0 && m_indexCount > 0; }

    private:
        const QC::Vertex* m_vertices;
        QC::u32 m_vertexCount;

        const QC::u32* m_indices;
        QC::u32 m_indexCount;
    };

} // namespace QC


