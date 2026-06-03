#include "QCPixelShader.h"

namespace QC
{

PixelShader::PixelShader()
{
}

QC::Color PixelShader::shade(const Vertex& vInterp) const
{
    // For now: just return the interpolated vertex color
    return vInterp.color;
}

} // namespace QC
