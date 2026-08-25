#include "QC3DContext.h"
#include "QCVertexShader.h"
#include "QCClipping.h"
#include "QCRasterizer.h"
#include "QCDepthBuffer.h"
#include "QCPixelShader.h"
#include "QC3DMesh.h"
#include "QCLogger.h"
// IPainter is forward-declared in QC3DContext.h; include the full header here
// so blitAlpha() can be called through the vtable.
#include "QGPainter.h"

namespace QC
{

Context::Context()
    : m_rasterizer(new Rasterizer()),
      m_depth(new DepthBuffer()),
      m_pixelShader(new PixelShader()),
      m_pixels(nullptr),
      m_width(0),
      m_height(0),
      m_pitch(0),
      m_depthEnabled(true),
      m_backfaceCulling(true)
{
    m_model = Mat4f::identity();
    m_view  = Mat4f::identity();
    m_proj  = Mat4f::identity();
    m_rasterizer->setPixelShader(m_pixelShader);
}

Context::~Context()
{
    delete m_rasterizer;
    delete m_depth;
    delete m_pixelShader;
}

void Context::setTarget(u32* pixels, u32 width, u32 height, u32 pitchBytes)
{
    m_pixels = pixels;
    m_width  = width;
    m_height = height;
    m_pitch  = pitchBytes;
    m_rasterizer->setRenderTarget(reinterpret_cast<Color*>(pixels), width, height);
    m_depth->resize(width, height);
    m_depth->clear(1.0f);  // Initialize to far plane
}

void Context::enableDepth(bool enabled)           { m_depthEnabled = enabled; }
void Context::enableBackfaceCulling(bool enabled) { m_backfaceCulling = enabled; }

void Context::clearDepth(f32 depth)
{
    if (m_depthEnabled) m_depth->clear(depth);
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

void Context::setViewMatrix(const Mat4f& view)       { m_view  = view; }
void Context::setProjectionMatrix(const Mat4f& proj) { m_proj  = proj; }
void Context::setModelMatrix(const Mat4f& model)     { m_model = model; }

void Context::setLightDir(const Vec3f& dir)      { m_pixelShader->setLightDir(dir); }
void Context::setAmbient(float ambient)          { m_pixelShader->setAmbient(ambient); }
void Context::setDiffuseStrength(float diffuse)  { m_pixelShader->setDiffuseStrength(diffuse); }
void Context::setLightingEnabled(bool enabled)   { m_pixelShader->setLightingEnabled(enabled); }

void Context::drawMesh(const Mesh& mesh)
{
    if (!mesh.isValid() || !m_pixels)
        return;

    VertexShader vs;
    vs.setModelMatrix(m_model);
    vs.setViewMatrix(m_view);
    vs.setProjectionMatrix(m_proj);

    m_rasterizer->setDepthBuffer(m_depthEnabled ? m_depth : nullptr);

    const Vertex* verts   = mesh.vertices();
    const u32*    indices = mesh.indices();

    // Log matrices for first render
    static bool loggedMatrices = false;
    if (!loggedMatrices)
    {
        loggedMatrices = true;
        QC_LOG_INFO("QC3D", "drawMesh: viewport=%ux%u", m_width, m_height);
        int m00 = (int)(m_model.m[0][0] * 1000.0f);
        int m11 = (int)(m_model.m[1][1] * 1000.0f);
        int m22 = (int)(m_model.m[2][2] * 1000.0f);
        int aspect10 = (int)(static_cast<float>(m_width) / m_height * 10.0f);
        QC_LOG_INFO("QC3D", "  Model: [0,0]=%d, [1,1]=%d, [2,2]=%d *0.001", m00, m11, m22);
        QC_LOG_INFO("QC3D", "  View eye=(0,0.5,2.5)");
        QC_LOG_INFO("QC3D", "  Proj: aspect=%d*0.1, fov~45deg, near=0.1, far=100", aspect10);
    }

    u32 triIdx = 0;
    static bool loggedFirstVert = false;
    
    for (u32 i = 0; i < mesh.indexCount(); i += 3)
    {
        u32 idx0 = indices[i + 0];
        const Vertex& inVert0 = verts[idx0];
        Vertex v0 = vs.transform(inVert0, m_width, m_height);
        Vertex v1 = vs.transform(verts[indices[i + 1]], m_width, m_height);
        Vertex v2 = vs.transform(verts[indices[i + 2]], m_width, m_height);
        
        if (!loggedFirstVert && triIdx == 0)
        {
            loggedFirstVert = true;
            // Log input and output vertices - using hex bit patterns to avoid format issues
            u32 inX = *reinterpret_cast<const u32*>(&inVert0.position.x);
            u32 inY = *reinterpret_cast<const u32*>(&inVert0.position.y);
            u32 inZ = *reinterpret_cast<const u32*>(&inVert0.position.z);
            u32 outX = *reinterpret_cast<const u32*>(&v0.position.x);
            u32 outY = *reinterpret_cast<const u32*>(&v0.position.y);
            // Note: divide hex by 2^24 ≈ 16.7M to get approximate float value
            QC_LOG_INFO("QC3D", "Vert in(hex):  x=%08x y=%08x z=%08x", inX, inY, inZ);
            QC_LOG_INFO("QC3D", "Vert out(hex): x=%08x y=%08x", outX, outY);
        }

        // Backface culling: screen-space signed area; skip back-facing triangles.
        if (m_backfaceCulling)
        {
            const float ax = v1.position.x - v0.position.x;
            const float ay = v1.position.y - v0.position.y;
            const float bx = v2.position.x - v0.position.x;
            const float by = v2.position.y - v0.position.y;
            if ((ax * by - ay * bx) <= 0.0f)
            {
                triIdx++;
                continue;
            }
        }

        Vertex out0, out1, out2, out3;
        u32 triCount = Clipping::clipAgainstNearPlane(v0, v1, v2,
                                                      out0, out1, out2, out3);
        if (triCount >= 1)
            m_rasterizer->drawTriangle(out0, out1, out2);
        if (triCount >= 2)
            m_rasterizer->drawTriangle(out0, out2, out3);

        triIdx++;
    }
}

void Context::blitTo(QG::IPainter* painter, QC::i32 x, QC::i32 y) const
{
    if (!painter || !m_pixels || m_width == 0 || m_height == 0)
        return;
    painter->blitAlpha(x, y, m_pixels, m_width, m_height,
                       m_pitch > 0 ? m_pitch : m_width * sizeof(u32));
}

} // namespace QC
