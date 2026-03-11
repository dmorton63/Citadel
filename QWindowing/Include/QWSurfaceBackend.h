#pragma once

// QWindowing SurfaceBackend - GraphicsBackend for painter-backed window surfaces
// Namespace: QW

#include "QGGraphicsBackend.h"
#include "QG/PainterSurface.h"
#include "QGBrush.h"
#include "QGPen.h"

namespace QW
{

    class SurfaceBackend : public QG::GraphicsBackend
    {
    public:
        SurfaceBackend();

        void setSurface(QG::PainterSurface *surface,
                        QC::u32 *pixels,
                        QC::u32 width,
                        QC::u32 height,
                        QC::u32 pitchBytes = 0);

        const TargetDesc &target() const override { return m_target; }
        const Capabilities &capabilities() const override { return m_caps; }

        bool beginFrame() override;
        void endFrame() override;

        void clear(QC::Color color) override;
        void drawRect(const QC::Rect &rect,
                      QC::Color fill,
                      QC::Color stroke,
                      QC::u32 strokeWidth) override;
        void drawGradient(const QC::Rect &rect,
                          QC::Color from,
                          QC::Color to,
                          QG::GradientDirection direction) override;
        void drawRoundedRect(const QC::Rect &rect,
                             QC::u32 radius,
                             QC::Color fill,
                             QC::Color stroke,
                             QC::u32 strokeWidth) override;
        void drawShadow(const QC::Rect &rect,
                        QC::Point offset,
                        QC::i32 blurRadius,
                        QC::Color color,
                        QC::u8 opacity) override;
        void blit(const QC::Rect &rect,
                  const QC::u32 *pixels,
                  QC::u32 stride,
                  bool useAlpha) override;

    private:
        bool ensureSurface() const { return m_surface != nullptr && m_target.pixels != nullptr; }
        bool clipRect(const QC::Rect &rect, QC::Rect &clipped) const;
        void fillRectAlpha(const QC::Rect &rect, QC::Color color);

        QG::PainterSurface *m_surface;
        TargetDesc m_target;
        Capabilities m_caps;
    };

} // namespace QW
