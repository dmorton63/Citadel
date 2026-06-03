#pragma once
#include "QCLinearAlgebra.h"
#include "QCColor.h"

namespace QC
{

struct Vertex
{
    Vec4f position;   // now 4D for clip-space + perspective
    Color color;
    Vec2f uv;
    Vec3f normal;

    Vertex()
        : position(0,0,0,1),
          color(),
          uv(0,0),
          normal(0,0,1)
    {
    }

    Vertex(const Vec4f& pos,
           const Color& col,
           const Vec2f& tex,
           const Vec3f& n)
        : position(pos),
          color(col),
          uv(tex),
          normal(n)
    {
    }

    // Convenience for Vec3f positions
    Vertex(const Vec3f& pos,
           const Color& col,
           const Vec2f& tex,
           const Vec3f& n)
        : position(pos.x, pos.y, pos.z, 1.0f),
          color(col),
          uv(tex),
          normal(n)
    {
    }
};

} // namespace QC
