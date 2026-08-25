#pragma once

// QDView3D - Desktop control that owns a software 3D rendering context.
// Drop it into any QW::Window; override update() to animate the scene.

#include "QWInterfaces/IControl.h"
#include "QWControls/Base/ControlBase.h"
#include "QC3DContext.h"
#include "QC3DMesh.h"

namespace QD
{

class View3D : public QW::Controls::ControlBase
{
public:
    View3D(QW::Window *window, const QC::Rect &bounds);
    ~View3D() override;

    // Access the 3D context to set matrices, lighting, etc.
    QC::Context &context() { return m_ctx; }

    // Set the mesh to render.  View3D does NOT take ownership.
    // If never called, a built-in unit cube is rendered.
    void setMesh(const QC::Mesh *mesh);

    // Rotate the model matrix by angleDeg degrees around (ax,ay,az) each frame.
    // Uses incremental matrix multiplication — no trig required per frame.
    void setAutoRotate(float ax, float ay, float az, float degreesPerFrame);

    // Advance animation state (call once per frame before invalidate()).
    void tick();

    // Request a one-shot framebuffer dump after the next paint.
    // Path should be a writable VFS path, e.g. /shared/view3d/frame.ppm.
    void requestFrameDump(const char *path);

    // IControl / ControlBase
    void paint(const QW::PaintContext &ctx) override;

private:
    void ensureBuffer(QC::u32 w, QC::u32 h);
    void buildDefaultCamera();

    QC::Context   m_ctx;
    const QC::Mesh *m_mesh = nullptr;
    QC::Mesh       m_defaultMesh;
    QC::Mat4f      m_modelMat;    // accumulated rotation matrix
    float          m_axisX = 0.0f;
    float          m_axisY = 0.0f;
    float          m_axisZ = 0.0f;
    float          m_degreesPerFrame = 0.0f;
    float          m_angleX = 0.0f;
    float          m_angleY = 0.0f;
    float          m_angleZ = 0.0f;
    bool           m_hasAutoRotate = false;

    char           m_dumpPath[128] = {};
    bool           m_dumpNextFrame = false;

    QC::u32  *m_buffer = nullptr;
    QC::u32   m_bufW   = 0;
    QC::u32   m_bufH   = 0;
};

} // namespace QD
