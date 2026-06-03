#include "QCClipping.h"

namespace QC
{
    // Clip plane: z >= 0 (in whatever space you're using here)
    static inline bool isInside(const Vertex& v)
    {
        return v.position.z >= 0.0f;
    }

    // Linear interpolation between two vertices
    static Vertex lerpVertex(const Vertex& a, const Vertex& b, float t)
    {
        Vertex r = a;

        // Position
        r.position.x = a.position.x + (b.position.x - a.position.x) * t;
        r.position.y = a.position.y + (b.position.y - a.position.y) * t;
        r.position.z = a.position.z + (b.position.z - a.position.z) * t;
        r.position.w = a.position.w + (b.position.w - a.position.w) * t;

        // UV
        r.uv.x = a.uv.x + (b.uv.x - a.uv.x) * t;
        r.uv.y = a.uv.y + (b.uv.y - a.uv.y) * t;

        // Normal
        r.normal.x = a.normal.x + (b.normal.x - a.normal.x) * t;
        r.normal.y = a.normal.y + (b.normal.y - a.normal.y) * t;
        r.normal.z = a.normal.z + (b.normal.z - a.normal.z) * t;

        // Color – simplest: just take a's color (you can upgrade to interpolated later)
        r.color = a.color;

        return r;
    }

    // Compute intersection t for segment a→b with plane z = 0
    static float intersectT(const Vertex& a, const Vertex& b)
    {
        float da = a.position.z;
        float db = b.position.z;
        return da / (da - db); // plane at z = 0
    }

    u32 Clipping::clipAgainstNearPlane(
        const Vertex& v0,
        const Vertex& v1,
        const Vertex& v2,
        Vertex& out0,
        Vertex& out1,
        Vertex& out2,
        Vertex& out3)
    {
        bool in0 = isInside(v0);
        bool in1 = isInside(v1);
        bool in2 = isInside(v2);

        int insideCount = (in0 ? 1 : 0) + (in1 ? 1 : 0) + (in2 ? 1 : 0);

        // 0 inside → fully clipped
        if (insideCount == 0)
            return 0;

        // 3 inside → no clipping
        if (insideCount == 3)
        {
            out0 = v0;
            out1 = v1;
            out2 = v2;
            return 1;
        }

        // 1 inside → 1 triangle
        if (insideCount == 1)
        {
            const Vertex *vi, *vo0, *vo1;

            if (in0)
            {
                vi  = &v0;
                vo0 = &v1;
                vo1 = &v2;
            }
            else if (in1)
            {
                vi  = &v1;
                vo0 = &v2;
                vo1 = &v0;
            }
            else
            {
                vi  = &v2;
                vo0 = &v0;
                vo1 = &v1;
            }

            float t0 = intersectT(*vi, *vo0);
            float t1 = intersectT(*vi, *vo1);

            Vertex i0 = lerpVertex(*vi, *vo0, t0);
            Vertex i1 = lerpVertex(*vi, *vo1, t1);

            out0 = *vi;
            out1 = i0;
            out2 = i1;

            return 1;
        }

        // 2 inside → 2 triangles
        const Vertex *vi0, *vi1, *vo;

        if (!in0)
        {
            vo  = &v0;
            vi0 = &v1;
            vi1 = &v2;
        }
        else if (!in1)
        {
            vo  = &v1;
            vi0 = &v2;
            vi1 = &v0;
        }
        else
        {
            vo  = &v2;
            vi0 = &v0;
            vi1 = &v1;
        }

        float t0 = intersectT(*vi0, *vo);
        float t1 = intersectT(*vi1, *vo);

        Vertex i0 = lerpVertex(*vi0, *vo, t0);
        Vertex i1 = lerpVertex(*vi1, *vo, t1);

        // Triangle 1: vi0, vi1, i1
        out0 = *vi0;
        out1 = *vi1;
        out2 = i1;

        // Triangle 2: vi0, i1, i0
        out3 = i0;

        return 2;
    }

} // namespace QC
