#include "QCDepthBuffer.h"
#include <cstring> // for memset

namespace QC
{

DepthBuffer::DepthBuffer()
    : m_width(0),
      m_height(0),
      m_data(nullptr)
{
}

DepthBuffer::~DepthBuffer()
{
    delete[] m_data;
}

void DepthBuffer::resize(u32 width, u32 height)
{
    if (width == m_width && height == m_height)
        return;

    m_width  = width;
    m_height = height;

    delete[] m_data;
    m_data = new float[width * height];
}

void DepthBuffer::clear(float depth)
{
    if (!m_data)
        return;

    for (u32 i = 0; i < m_width * m_height; ++i)
        m_data[i] = depth;
}

bool DepthBuffer::testAndSet(u32 x, u32 y, float depth)
{
    if (!m_data)
        return true; // no depth buffer → always pass

    if (x >= m_width || y >= m_height)
        return false;

    u32 index = y * m_width + x;

    if (depth < m_data[index])
    {
        m_data[index] = depth;
        return true;
    }

    return false;
}

float DepthBuffer::get(u32 x, u32 y) const
{
    if (!m_data)
        return 1.0f;

    if (x >= m_width || y >= m_height)
        return 1.0f;

    return m_data[y * m_width + x];
}

} // namespace QC
