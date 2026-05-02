#pragma once

#include "QGfxDrawOp.h"
#include "QCVector.h"

namespace QGfx
{
    class Batch
    {
    public:
        void clear();
        void setTarget(SurfaceId target) { m_target = target; }
        SurfaceId target() const { return m_target; }

        void setDirtyRegion(const QC::Rect &rect) { m_dirtyRegion = rect; }
        const QC::Rect &dirtyRegion() const { return m_dirtyRegion; }

        void addOp(const DrawOp &op);
        void optimize();

        const QC::Vector<DrawOp> &ops() const { return m_ops; }
        QC::Vector<DrawOp> &ops() { return m_ops; }

    private:
        SurfaceId m_target;
        QC::Rect m_dirtyRegion{0, 0, 0, 0};
        QC::Vector<DrawOp> m_ops;
    };
}
