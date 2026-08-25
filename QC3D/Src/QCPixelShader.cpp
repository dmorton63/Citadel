#include "QCPixelShader.h"
#include "QCLinearAlgebra.h"  // for rsqrtf_approx

namespace QC
{

PixelShader::PixelShader()
    : m_lightDir(0.0f, -1.0f, -1.0f),
      m_ambient(0.15f),
      m_diffuse(0.85f),
      m_lighting(true)
{
    // Normalise default light direction using the SSE-backed approx rsqrt.
    const float lsq = m_lightDir.x * m_lightDir.x +
                      m_lightDir.y * m_lightDir.y +
                      m_lightDir.z * m_lightDir.z;
    if (lsq > 0.0001f)
    {
        const float inv = rsqrtf_approx(lsq);
        m_lightDir.x *= inv;
        m_lightDir.y *= inv;
        m_lightDir.z *= inv;
    }
}

void PixelShader::setLightDir(const Vec3f &dir)
{
    const float lsq = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
    if (lsq > 0.0001f)
    {
        const float inv = rsqrtf_approx(lsq);
        m_lightDir.x = dir.x * inv;
        m_lightDir.y = dir.y * inv;
        m_lightDir.z = dir.z * inv;
    }
}

void PixelShader::setAmbient(float ambient)
{
    m_ambient = ambient < 0.0f ? 0.0f : (ambient > 1.0f ? 1.0f : ambient);
}

void PixelShader::setDiffuseStrength(float diffuse)
{
    m_diffuse = diffuse < 0.0f ? 0.0f : (diffuse > 1.0f ? 1.0f : diffuse);
}

void PixelShader::setLightingEnabled(bool enabled)
{
    m_lighting = enabled;
}

QC::Color PixelShader::shade(const Vertex& vInterp) const
{
    if (!m_lighting)
        return vInterp.color;

    // Lambert diffuse: dot(surface normal, -lightDir) clamped to [0,1].
    const float dot = -(vInterp.normal.x * m_lightDir.x +
                        vInterp.normal.y * m_lightDir.y +
                        vInterp.normal.z * m_lightDir.z);
    const float d = dot > 0.0f ? dot : 0.0f;
    const float intensity = m_ambient + m_diffuse * d;
    const float scale = intensity > 1.0f ? 1.0f : intensity;

    Color out;
    out.r = static_cast<u8>(vInterp.color.r * scale);
    out.g = static_cast<u8>(vInterp.color.g * scale);
    out.b = static_cast<u8>(vInterp.color.b * scale);
    out.a = vInterp.color.a;
    return out;
}

} // namespace QC
