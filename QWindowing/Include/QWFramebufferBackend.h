#pragma once

// QWindowing FramebufferBackend - GraphicsBackend implementation bound to the system framebuffer
// Namespace: QW

#include "QGGraphicsBackend.h"
#include "QWFramebuffer.h"
#include "QWRenderer.h"

namespace QW
{

    class FramebufferBackend : public QG::GraphicsBackend
    {
    public:
        explicit FramebufferBackend(Framebuffer *framebuffer = nullptr);

        void setFramebuffer(Framebuffer *framebuffer);

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
        bool updateTarget();
        bool clipRect(const QC::Rect &rect, QC::Rect &clipped) const;
        void fillRectAlpha(const QC::Rect &rect, QC::Color color);
        static QG::PixelFormat convertFormat(PixelFormat format);

        Framebuffer *m_framebuffer;
        Renderer m_renderer;
        TargetDesc m_target;
        Capabilities m_caps;
    };

} // namespace QW
