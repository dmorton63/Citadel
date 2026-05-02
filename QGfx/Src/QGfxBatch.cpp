#include "QGfxBatch.h"

namespace QGfx
{
    namespace
    {
        inline bool intersects(const QC::Rect &a, const QC::Rect &b)
        {
            if (a.width == 0 || a.height == 0 || b.width == 0 || b.height == 0)
                return false;

            return !(a.right() <= b.x ||
                     b.right() <= a.x ||
                     a.bottom() <= b.y ||
                     b.bottom() <= a.y);
        }

        inline void swapOps(DrawOp &a, DrawOp &b)
        {
            DrawOp temp = a;
            a = b;
            b = temp;
        }
    }

    void Batch::clear()
    {
        m_target = SurfaceId{};
        m_dirtyRegion = QC::Rect{0, 0, 0, 0};
        m_ops.clear();
    }

    void Batch::addOp(const DrawOp &op)
    {
        m_ops.push_back(op);
    }

    void Batch::optimize()
    {
        if (m_ops.size() < 2)
            return;

        QC::usize writeIndex = 0;
        for (QC::usize i = 0; i < m_ops.size(); ++i)
        {
            if (m_dirtyRegion.width > 0 && m_dirtyRegion.height > 0 && !intersects(m_ops[i].dstRect, m_dirtyRegion))
                continue;

            if (writeIndex != i)
                m_ops[writeIndex] = m_ops[i];
            ++writeIndex;
        }

        while (m_ops.size() > writeIndex)
            m_ops.pop_back();

        for (QC::usize i = 0; i < m_ops.size(); ++i)
        {
            for (QC::usize j = i + 1; j < m_ops.size(); ++j)
            {
                const bool shouldSwap = (m_ops[j].zOrder < m_ops[i].zOrder) ||
                                        ((m_ops[j].zOrder == m_ops[i].zOrder) &&
                                         (m_ops[j].srcSurface.value < m_ops[i].srcSurface.value));
                if (shouldSwap)
                    swapOps(m_ops[i], m_ops[j]);
            }
        }
    }
}
