#include "QCVertexShader.h"

namespace QC
{

VertexShader::VertexShader()
{
    m_model = Mat4f::identity();
    m_view  = Mat4f::identity();
    m_proj  = Mat4f::identity();
}

void VertexShader::setModelMatrix(const Mat4f& m)
{
    m_model = m;
}

void VertexShader::setViewMatrix(const Mat4f& v)
{
    m_view = v;
}

void VertexShader::setProjectionMatrix(const Mat4f& p)
{
    m_proj = p;
}

Vertex VertexShader::transform(const Vertex& in,
                               u32 screenWidth,
                               u32 screenHeight) const
{
    Vertex out = in;

    // 1. Model → World
    Vec4f p = m_model * Vec4f(in.position.x, in.position.y, in.position.z, 1.0f);

    // 2. World → View
    p = m_view * p;

    // 3. View → Clip
    p = m_proj * p;

    // Preserve clip-space W for perspective-correct interpolation in rasterizer.
    const float clipW = p.w;

    // 4. Perspective divide (Clip → NDC)
    float invW = 1.0f / p.w;
    float ndcX = p.x * invW;
    float ndcY = p.y * invW;
    float ndcZ = p.z * invW;

    // 5. NDC → Screen
    // Convert from normalized device coordinates [-1,1] to screen [0,width] x [0,height]
    float sx = (ndcX * 0.5f + 0.5f) * screenWidth;
    float sy = (1.0f - (ndcY * 0.5f + 0.5f)) * screenHeight;  // Flip Y for screen space

    out.position.x = sx;
    out.position.y = sy;
    out.position.z = ndcZ;   // depth in NDC space
    out.position.w = clipW;  // keep clip-space W for perspective-correct interpolation

    return out;
}

} // namespace QC
