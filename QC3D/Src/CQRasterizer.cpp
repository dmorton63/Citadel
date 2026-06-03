#include "QCRasterizer.h"

namespace QC
{

namespace
{
static inline float min3(float a, float b, float c)
{
    float result = (a < b) ? a : b;
    return (c < result) ? c : result;
}

static inline float max3(float a, float b, float c)
{
    float result = (a > b) ? a : b;
    return (c > result) ? c : result;
}

static inline int floorToInt(float value)
{
    const int truncated = static_cast<int>(value);
    return (static_cast<float>(truncated) > value) ? (truncated - 1) : truncated;
}

static inline int ceilToInt(float value)
{
    const int truncated = static_cast<int>(value);
    return (static_cast<float>(truncated) < value) ? (truncated + 1) : truncated;
}

static inline int imin(int a, int b)
{
    return (a < b) ? a : b;
}

static inline int imax(int a, int b)
{
    return (a > b) ? a : b;
}

static inline float edgeFunction(const Vec4f& a, const Vec4f& b, const Vec4f& c)
{
    return (c.x - a.x) * (b.y - a.y) -
           (c.y - a.y) * (b.x - a.x);
}

static Vertex interpolate(const Vertex& v0,
                          const Vertex& v1,
                          const Vertex& v2,
                          float w0,
                          float w1,
                          float w2)
{
    Vertex result;

    const float invW0 = 1.0f / v0.position.w;
    const float invW1 = 1.0f / v1.position.w;
    const float invW2 = 1.0f / v2.position.w;

    const float denom = w0 * invW0 + w1 * invW1 + w2 * invW2;
    const float invDenom = 1.0f / denom;

    const float a = (w0 * invW0) * invDenom;
    const float b = (w1 * invW1) * invDenom;
    const float c = (w2 * invW2) * invDenom;

    result.position.x = v0.position.x * a + v1.position.x * b + v2.position.x * c;
    result.position.y = v0.position.y * a + v1.position.y * b + v2.position.y * c;
    result.position.z = v0.position.z * a + v1.position.z * b + v2.position.z * c;
    result.position.w = 1.0f;

    result.uv.x = v0.uv.x * a + v1.uv.x * b + v2.uv.x * c;
    result.uv.y = v0.uv.y * a + v1.uv.y * b + v2.uv.y * c;

    result.normal.x = v0.normal.x * a + v1.normal.x * b + v2.normal.x * c;
    result.normal.y = v0.normal.y * a + v1.normal.y * b + v2.normal.y * c;
    result.normal.z = v0.normal.z * a + v1.normal.z * b + v2.normal.z * c;

    result.color.r = static_cast<u8>(v0.color.r * a + v1.color.r * b + v2.color.r * c);
    result.color.g = static_cast<u8>(v0.color.g * a + v1.color.g * b + v2.color.g * c);
    result.color.b = static_cast<u8>(v0.color.b * a + v1.color.b * b + v2.color.b * c);
    result.color.a = static_cast<u8>(v0.color.a * a + v1.color.a * b + v2.color.a * c);

    return result;
}
}

Rasterizer::Rasterizer()
    : m_colorBuffer(nullptr),
      m_width(0),
      m_height(0),
      m_depthBuffer(nullptr),
      m_pixelShader(nullptr)
{
}

void Rasterizer::setRenderTarget(Color* colorBuffer, u32 width, u32 height)
{
    m_colorBuffer = colorBuffer;
    m_width = width;
    m_height = height;
}

void Rasterizer::setDepthBuffer(DepthBuffer* depth)
{
    m_depthBuffer = depth;
}

void Rasterizer::setPixelShader(PixelShader* ps)
{
    m_pixelShader = ps;
}

void Rasterizer::drawTriangle(const Vertex& v0,
                              const Vertex& v1,
                              const Vertex& v2)
{
    if (!m_colorBuffer || !m_pixelShader || !m_depthBuffer || m_width == 0 || m_height == 0)
        return;

    const float minX = min3(v0.position.x, v1.position.x, v2.position.x);
    const float minY = min3(v0.position.y, v1.position.y, v2.position.y);
    const float maxX = max3(v0.position.x, v1.position.x, v2.position.x);
    const float maxY = max3(v0.position.y, v1.position.y, v2.position.y);

    const int x0 = imax(0, floorToInt(minX));
    const int y0 = imax(0, floorToInt(minY));
    const int x1 = imin(static_cast<int>(m_width) - 1, ceilToInt(maxX));
    const int y1 = imin(static_cast<int>(m_height) - 1, ceilToInt(maxY));

    const float area = edgeFunction(v0.position, v1.position, v2.position);
    if (area == 0.0f)
        return;

    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            const Vec4f samplePoint(static_cast<float>(x) + 0.5f,
                                    static_cast<float>(y) + 0.5f,
                                    0.0f,
                                    1.0f);

            float w0 = edgeFunction(v1.position, v2.position, samplePoint);
            float w1 = edgeFunction(v2.position, v0.position, samplePoint);
            float w2 = edgeFunction(v0.position, v1.position, samplePoint);

            if ((area > 0.0f && (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)) ||
                (area < 0.0f && (w0 > 0.0f || w1 > 0.0f || w2 > 0.0f)))
            {
                continue;
            }

            w0 /= area;
            w1 /= area;
            w2 /= area;

            const float depth =
                v0.position.z * w0 +
                v1.position.z * w1 +
                v2.position.z * w2;

            if (!m_depthBuffer->testAndSet(static_cast<u32>(x), static_cast<u32>(y), depth))
                continue;

            const Vertex interpolated = interpolate(v0, v1, v2, w0, w1, w2);
            const Color shaded = m_pixelShader->shade(interpolated);
            m_colorBuffer[y * static_cast<int>(m_width) + x] = shaded;
        }
    }
}

} // namespace QC
