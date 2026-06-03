#pragma once
#include "QCLinearAlgebra.h"
#include "QCTypes.h"

namespace QC
{
    class DepthBuffer
    {
    public:
        DepthBuffer();
        ~DepthBuffer();

        // Allocate or resize the buffer
        void resize(QC::u32 width, QC::u32 height);

        // Clear to a depth value (default = 1.0f = far plane)
        void clear(float depth = 1.0f);

        float get(u32 x, u32 y) const;

        // Depth test: returns true if the new depth passes
        bool testAndSet(QC::u32 x, QC::u32 y, float depth);

        // Accessors
        float* data() { return m_data; }
        const float* data() const { return m_data; }

        QC::u32 width() const { return m_width; }
        QC::u32 height() const { return m_height; }

    private:
        float* m_data;
        QC::u32 m_width;
        QC::u32 m_height;
    
    };
} // namespace QC