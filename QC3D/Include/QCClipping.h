#pragma once

// QCClipping - Triangle clipping against the view frustum (near plane only for now)
// Namespace: QC

#include "QCVertex.h"
#include "QCTypes.h"

namespace QC
{    
    class Clipping
    {
    public:
        // Clip a triangle against the near plane (z >= 0 in clip space)
        // Returns number of output triangles (0, 1, or 2)
        // out0/out1 are valid only if return value >= 1 or 2 respectively
        static QC::u32 clipAgainstNearPlane(
            const QC::Vertex& v0,
            const QC::Vertex& v1,
            const QC::Vertex& v2,
            QC::Vertex& out0,
            QC::Vertex& out1,
            QC::Vertex& out2,
            QC::Vertex& out3);
    };

} // namespace QC


