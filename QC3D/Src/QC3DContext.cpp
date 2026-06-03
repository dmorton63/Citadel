#include "QC3DContext.h"
#include "QCVertexShader.h"
#include "QCClipping.h"
#include "QCRasterizer.h"
#include "QCDepthBuffer.h"
#include "QC3DMesh.h"

namespace QC
{

Context::Context()
    : m_rasterizer(new Rasterizer()),
      m_depth(new DepthBuffer()),
      m_pixels(nullptr),
      m_width(0),
      m_height(0),
      m_pitch(0),
      m_depthEnabled(true)
{
    m_model = Mat4f::identity();
    m_view  = Mat4f::identity();
    m_proj  = Mat4f::identity();
}

Context::~Context()
{
    delete m_rasterizer;
    delete m_depth;
}

void Context::setTarget(u32* pixels, u32 width, u32 height, u32 pitchBytes)
{
    m_pixels = pixels;
    m_width  = width;
    m_height = height;
    m_pitch  = pitchBytes;

    m_rasterizer->setRenderTarget(reinterpret_cast<Color*>(pixels), width, height);
    m_depth->resize(width, height);
}

void Context::enableDepth(bool enabled)
{
    m_depthEnabled = enabled;
}

void Context::clearDepth(f32 depth)
{
    if (m_depthEnabled)
        m_depth->clear(depth);
}

void Context::clearColor(Color color)
{
    if (!m_pixels) return;

    u32 pitchPixels = m_pitch / sizeof(u32);

    for (u32 y = 0; y < m_height; ++y)
    {
        u32* row = m_pixels + y * pitchPixels;
        for (u32 x = 0; x < m_width; ++x)
            row[x] = color.value;
    }
}

void Context::setViewMatrix(const Mat4f& view)
{
    m_view = view;
}

void Context::setProjectionMatrix(const Mat4f& proj)
{
    m_proj = proj;
}

void Context::setModelMatrix(const Mat4f& model)
{
    m_model = model;
}

void Context::drawMesh(const Mesh& mesh)
{
    if (!mesh.isValid() || !m_pixels)
        return;

    VertexShader vs;
    vs.setModelMatrix(m_model);
    vs.setViewMatrix(m_view);
    vs.setProjectionMatrix(m_proj);

    m_rasterizer->setDepthBuffer(m_depthEnabled ? m_depth : nullptr);

    const Vertex* verts = mesh.vertices();
    const u32* indices  = mesh.indices();

    for (u32 i = 0; i < mesh.indexCount(); i += 3)
    {
        Vertex v0 = vs.transform(verts[indices[i + 0]], m_width, m_height);
        Vertex v1 = vs.transform(verts[indices[i + 1]], m_width, m_height);
        Vertex v2 = vs.transform(verts[indices[i + 2]], m_width, m_height);

        Vertex out0, out1, out2, out3;

        u32 triCount = Clipping::clipAgainstNearPlane(
            v0, v1, v2,
            out0, out1, out2, out3
        );

        if (triCount == 1)
        {
            m_rasterizer->drawTriangle(out0, out1, out2);
        }
        else if (triCount == 2)
        {
            m_rasterizer->drawTriangle(out0, out1, out2);
            m_rasterizer->drawTriangle(out0, out2, out3);
        }
    }
}

} // namespace QC
