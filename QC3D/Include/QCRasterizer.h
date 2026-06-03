#pragma once
#include "QCVertex.h"
#include "QCDepthBuffer.h"
#include "QCPixelShader.h"
#include "QCColor.h"

namespace QC
{

class Rasterizer
{
public:
    Rasterizer();

    void setRenderTarget(Color* colorBuffer, u32 width, u32 height);
    void setDepthBuffer(DepthBuffer* depth);
    void setPixelShader(PixelShader* ps);

    void drawTriangle(const Vertex& v0,
                      const Vertex& v1,
                      const Vertex& v2);

private:
    Color*       m_colorBuffer;
    u32          m_width;
    u32          m_height;

    DepthBuffer* m_depthBuffer;
    PixelShader* m_pixelShader;
};

} // namespace QC
