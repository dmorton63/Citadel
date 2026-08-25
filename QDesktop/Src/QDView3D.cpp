#include "QDView3D.h"
#include "QWPaintContext.h"
#include "QGPainter.h"
#include "QCLinearAlgebra.h"
#include "QCTrig.h"
#include "QCLogger.h"
#include "QFSVFS.h"
#include "QFSFile.h"

namespace QD
{

namespace
{
static bool writeAll(QFS::File *file, const void *data, QC::usize size)
{
    if (!file || !data)
        return false;

    const QC::u8 *bytes = static_cast<const QC::u8 *>(data);
    QC::usize written = 0;
    while (written < size)
    {
        const QC::usize n = file->write(bytes + written, size - written);
        if (n == 0)
            return false;
        written += n;
    }
    return true;
}

static void copyPath(char *dst, QC::usize cap, const char *src)
{
    if (!dst || cap == 0)
        return;

    if (!src)
    {
        dst[0] = 0;
        return;
    }

    QC::usize i = 0;
    for (; i + 1 < cap && src[i] != 0; ++i)
        dst[i] = src[i];
    dst[i] = 0;
}

static bool appendChar(char *dst, QC::usize cap, QC::usize &pos, char ch)
{
    if (!dst || pos + 1 >= cap)
        return false;
    dst[pos++] = ch;
    dst[pos] = 0;
    return true;
}

static bool appendText(char *dst, QC::usize cap, QC::usize &pos, const char *text)
{
    if (!dst || !text)
        return false;

    for (QC::usize i = 0; text[i] != 0; ++i)
    {
        if (!appendChar(dst, cap, pos, text[i]))
            return false;
    }
    return true;
}

static bool appendUnsigned(char *dst, QC::usize cap, QC::usize &pos, QC::u32 value)
{
    char rev[16] = {};
    QC::usize count = 0;
    do
    {
        rev[count++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof(rev));

    while (count > 0)
    {
        if (!appendChar(dst, cap, pos, rev[--count]))
            return false;
    }
    return true;
}
}

// ---------------------------------------------------------------------------
// Unit cube: 24 verts (4 per face × 6 faces), 12 tris, per-face normals/colours
// ---------------------------------------------------------------------------

static void buildUnitCube(QC::Mesh &mesh)
{
    // Real 3D unit cube centered at origin (-0.5 to 0.5 on each axis)
    // Vertices defined in 3D space (model coordinates), will be transformed by matrices
    // Colors per face for visibility
    
    static const QC::Vertex verts[24] = {
        // Front face (z = 0.5) - Red
        {{-0.5f, -0.5f,  0.5f, 1}, {255,0,0,255}, {0,0}, {0,0,1}},
        {{ 0.5f, -0.5f,  0.5f, 1}, {255,0,0,255}, {1,0}, {0,0,1}},
        {{ 0.5f,  0.5f,  0.5f, 1}, {255,0,0,255}, {1,1}, {0,0,1}},
        {{-0.5f,  0.5f,  0.5f, 1}, {255,0,0,255}, {0,1}, {0,0,1}},
        
        // Back face (z = -0.5) - Green
        {{ 0.5f, -0.5f, -0.5f, 1}, {0,255,0,255}, {0,0}, {0,0,-1}},
        {{-0.5f, -0.5f, -0.5f, 1}, {0,255,0,255}, {1,0}, {0,0,-1}},
        {{-0.5f,  0.5f, -0.5f, 1}, {0,255,0,255}, {1,1}, {0,0,-1}},
        {{ 0.5f,  0.5f, -0.5f, 1}, {0,255,0,255}, {0,1}, {0,0,-1}},
        
        // Top face (y = 0.5) - Blue
        {{-0.5f,  0.5f, -0.5f, 1}, {0,0,255,255}, {0,0}, {0,1,0}},
        {{-0.5f,  0.5f,  0.5f, 1}, {0,0,255,255}, {1,0}, {0,1,0}},
        {{ 0.5f,  0.5f,  0.5f, 1}, {0,0,255,255}, {1,1}, {0,1,0}},
        {{ 0.5f,  0.5f, -0.5f, 1}, {0,0,255,255}, {0,1}, {0,1,0}},
        
        // Bottom face (y = -0.5) - Yellow
        {{-0.5f, -0.5f,  0.5f, 1}, {255,255,0,255}, {0,0}, {0,-1,0}},
        {{-0.5f, -0.5f, -0.5f, 1}, {255,255,0,255}, {1,0}, {0,-1,0}},
        {{ 0.5f, -0.5f, -0.5f, 1}, {255,255,0,255}, {1,1}, {0,-1,0}},
        {{ 0.5f, -0.5f,  0.5f, 1}, {255,255,0,255}, {0,1}, {0,-1,0}},
        
        // Right face (x = 0.5) - Magenta
        {{ 0.5f, -0.5f, -0.5f, 1}, {255,0,255,255}, {0,0}, {1,0,0}},
        {{ 0.5f, -0.5f,  0.5f, 1}, {255,0,255,255}, {1,0}, {1,0,0}},
        {{ 0.5f,  0.5f,  0.5f, 1}, {255,0,255,255}, {1,1}, {1,0,0}},
        {{ 0.5f,  0.5f, -0.5f, 1}, {255,0,255,255}, {0,1}, {1,0,0}},
        
        // Left face (x = -0.5) - Cyan
        {{-0.5f, -0.5f,  0.5f, 1}, {0,255,255,255}, {0,0}, {-1,0,0}},
        {{-0.5f, -0.5f, -0.5f, 1}, {0,255,255,255}, {1,0}, {-1,0,0}},
        {{-0.5f,  0.5f, -0.5f, 1}, {0,255,255,255}, {1,1}, {-1,0,0}},
        {{-0.5f,  0.5f,  0.5f, 1}, {0,255,255,255}, {0,1}, {-1,0,0}},
    };

    // Index buffer: 12 triangles (2 per face × 6 faces)
    static const QC::u32 idx[36] = {
        // Front
        0, 1, 2,   0, 2, 3,
        // Back (note: reversed winding for back face)
        4, 5, 6,   4, 6, 7,
        // Top
        8, 9, 10,  8, 10, 11,
        // Bottom  
        12, 13, 14, 12, 14, 15,
        // Right
        16, 17, 18, 16, 18, 19,
        // Left
        20, 21, 22, 20, 22, 23,
    };

    mesh.setVertices(verts, 24);
    mesh.setIndices(idx, 36);
}

// ---------------------------------------------------------------------------

View3D::View3D(QW::Window *window, const QC::Rect &bounds)
    : QW::Controls::ControlBase(window, bounds),
    m_modelMat(QC::Mat4f::identity())
{
    buildUnitCube(m_defaultMesh);

    m_ctx.setLightDir({0.6f, -0.8f, -0.5f});
    m_ctx.setAmbient(0.20f);
    m_ctx.setDiffuseStrength(0.80f);
    m_ctx.setLightingEnabled(true);
    // Keep only front-facing triangles to avoid backface overdraw speckling.
    m_ctx.enableBackfaceCulling(true);
    m_ctx.enableDepth(true);

    // Use a centered default camera; per-frame view setup may override this.
    m_ctx.setViewMatrix(QC::lookAtRH(
        {0.0f, 0.0f, 2.5f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}));
}

View3D::~View3D()
{
    delete[] m_buffer;
}

void View3D::setMesh(const QC::Mesh *mesh)
{
    m_mesh = mesh;
    invalidate();
}

void View3D::setAutoRotate(float ax, float ay, float az, float degreesPerFrame)
{
    m_axisX = ax;
    m_axisY = ay;
    m_axisZ = az;
    m_degreesPerFrame = degreesPerFrame;
    m_angleX = 0.0f;
    m_angleY = 0.0f;
    m_angleZ = 0.0f;
    m_hasAutoRotate = (ax != 0.0f || ay != 0.0f || az != 0.0f);
}

void View3D::requestFrameDump(const char *path)
{
    copyPath(m_dumpPath, sizeof(m_dumpPath), path);
    m_dumpNextFrame = (m_dumpPath[0] != 0);
}

void View3D::tick()
{
    if (m_hasAutoRotate)
    {
        m_angleX += m_axisX * m_degreesPerFrame;
        m_angleY += m_axisY * m_degreesPerFrame;
        m_angleZ += m_axisZ * m_degreesPerFrame;

        // Keep angles bounded so trig range reduction stays well-behaved.
        if (m_angleX >= 360.0f) m_angleX -= 360.0f;
        if (m_angleX <= -360.0f) m_angleX += 360.0f;
        if (m_angleY >= 360.0f) m_angleY -= 360.0f;
        if (m_angleY <= -360.0f) m_angleY += 360.0f;
        if (m_angleZ >= 360.0f) m_angleZ -= 360.0f;
        if (m_angleZ <= -360.0f) m_angleZ += 360.0f;

        float sx = 0.0f, cx = 1.0f;
        float sy = 0.0f, cy = 1.0f;
        float sz = 0.0f, cz = 1.0f;

        QC::sincosf_approx(QC::degToRad(m_angleX), &sx, &cx);
        QC::sincosf_approx(QC::degToRad(m_angleY), &sy, &cy);
        QC::sincosf_approx(QC::degToRad(m_angleZ), &sz, &cz);

        const QC::Mat4f rx = QC::Mat4f::rotationX(cx, sx);
        const QC::Mat4f ry = QC::Mat4f::rotationY(cy, sy);
        const QC::Mat4f rz = QC::Mat4f::rotationZ(cz, sz);
        m_modelMat = QC::mul(QC::mul(rx, ry), rz);
    }
    invalidate();
}

void View3D::paint(const QW::PaintContext &pc)
{
    if (!m_visible || !pc.painter)
        return;

    const QC::Rect b = bounds();
    if (b.width == 0 || b.height == 0)
        return;

    // Resize target buffer if the control was resized.
    if (m_bufW != b.width || m_bufH != b.height)
    {
        delete[] m_buffer;
        m_bufW = b.width;
        m_bufH = b.height;
        m_buffer = new QC::u32[m_bufW * m_bufH];
        m_ctx.setTarget(m_buffer, m_bufW, m_bufH, m_bufW * sizeof(QC::u32));
        QC_LOG_INFO("QDView3D", "Buffer resized to %u x %u", m_bufW, m_bufH);
    }

    // Recompute projection for current aspect ratio.
    const float aspect = static_cast<float>(b.width) / static_cast<float>(b.height);
    m_ctx.setProjectionMatrix(QC::perspectiveRH(aspect, 0.4142f, 0.1f, 100.0f));
    
    // Camera looking at cube from slightly above and forward
    const QC::Vec3f eye(0.0f, 0.0f, 2.5f);
    const QC::Vec3f center(0.0f, 0.0f, 0.0f);
    const QC::Vec3f up(0.0f, 1.0f, 0.0f);
    m_ctx.setViewMatrix(QC::lookAtRH(eye, center, up));
    
    // Model matrix with incremental rotation
    m_ctx.setModelMatrix(m_modelMat);

    m_ctx.clearColor(QC::Color(18, 18, 24, 255));
    m_ctx.clearDepth();

    if (m_mesh && m_mesh->isValid())
    {
        m_ctx.drawMesh(*m_mesh);
    }
    else
    {
        m_ctx.drawMesh(m_defaultMesh);
    }

    m_ctx.blitTo(pc.painter, b.x, b.y);

    if (m_dumpNextFrame && m_buffer && m_bufW > 0 && m_bufH > 0)
    {
        auto &vfs = QFS::VFS::instance();
        QFS::File *file = vfs.open(
            m_dumpPath,
            QFS::OpenMode::Write |
                QFS::OpenMode::Create |
                QFS::OpenMode::Truncate |
                QFS::OpenMode::Binary);
        const char *usedPath = m_dumpPath;

        if (!file)
        {
            usedPath = "/dump/open3d_latest.ppm";
            file = vfs.open(
                usedPath,
                QFS::OpenMode::Write |
                    QFS::OpenMode::Create |
                    QFS::OpenMode::Truncate |
                    QFS::OpenMode::Binary);
        }

        bool ok = (file != nullptr);
        if (ok)
        {
            char header[64] = {};
            QC::usize headerPos = 0;
            ok = appendText(header, sizeof(header), headerPos, "P6\n") &&
                 appendUnsigned(header, sizeof(header), headerPos, m_bufW) &&
                 appendChar(header, sizeof(header), headerPos, ' ') &&
                 appendUnsigned(header, sizeof(header), headerPos, m_bufH) &&
                 appendText(header, sizeof(header), headerPos, "\n255\n") &&
                 writeAll(file, header, headerPos);

            QC::Vector<QC::u8> row;
            if (ok)
                row.resize(static_cast<QC::usize>(m_bufW) * 3u);

            for (QC::u32 y = 0; ok && y < m_bufH; ++y)
            {
                const QC::u32 *src = m_buffer + static_cast<QC::usize>(y) * m_bufW;
                for (QC::u32 x = 0; x < m_bufW; ++x)
                {
                    const QC::u32 pixel = src[x]; // ARGB8888
                    row[static_cast<QC::usize>(x) * 3u + 0u] = static_cast<QC::u8>((pixel >> 16) & 0xFFu);
                    row[static_cast<QC::usize>(x) * 3u + 1u] = static_cast<QC::u8>((pixel >> 8) & 0xFFu);
                    row[static_cast<QC::usize>(x) * 3u + 2u] = static_cast<QC::u8>(pixel & 0xFFu);
                }
                ok = writeAll(file, row.data(), row.size());
            }

            vfs.close(file);
        }

        QC_LOG_INFO("QDView3D", "%s frame dump: requested=%s used=%s",
                    ok ? "Wrote" : "Failed",
                    m_dumpPath,
                    usedPath);
        m_dumpNextFrame = false;
    }
}

} // namespace QD
