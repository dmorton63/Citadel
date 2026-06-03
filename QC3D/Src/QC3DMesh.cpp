#include "QC3DMesh.h"

namespace QC
{

Mesh::Mesh()
    : m_vertices(nullptr),
      m_vertexCount(0),
      m_indices(nullptr),
      m_indexCount(0)
{
}

Mesh::Mesh(const Vertex* vertices,
           u32 vertexCount,
           const u32* indices,
           u32 indexCount)
    : m_vertices(vertices),
      m_vertexCount(vertexCount),
      m_indices(indices),
      m_indexCount(indexCount)
{
}

void Mesh::setVertices(const Vertex* vertices, u32 count)
{
    m_vertices = vertices;
    m_vertexCount = count;
}

void Mesh::setIndices(const u32* indices, u32 count)
{
    m_indices = indices;
    m_indexCount = count;
}

} // namespace QC
