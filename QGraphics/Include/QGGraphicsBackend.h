#pragma once

// QGraphics GraphicsBackend - Rendering backend abstraction
// Namespace: QG

#include "QCTypes.h"
#include "QCGeometry.h"
#include "QCColor.h"

namespace QG
{

    enum class GradientDirection : QC::u8
    {
        Horizontal,
        Vertical
    };

    enum class PixelFormat : QC::u8
    {
        RGB565,
        BGR565,
        RGB888,
        BGR888,
        ARGB8888,
        ABGR8888
    };

    class GraphicsBackend
    {
    public:
        struct TargetDesc
        {
            QC::u32 width = 0;
            QC::u32 height = 0;
            QC::u32 pitch = 0; // bytes per row
            PixelFormat format = PixelFormat::ARGB8888;
            QC::u8 *pixels = nullptr;
        };

        struct Capabilities
        {
            bool supportsRoundedRect = false;
            bool supportsShadows = false;
            bool supportsAlpha = true;
        };

        virtual ~GraphicsBackend() = default;

        virtual const TargetDesc &target() const = 0;
        virtual const Capabilities &capabilities() const = 0;

        virtual bool beginFrame() = 0;
        virtual void endFrame() = 0;

        virtual void clear(QC::Color color) = 0;
        virtual void drawRect(const QC::Rect &rect,
                              QC::Color fill,
                              QC::Color stroke,
                              QC::u32 strokeWidth = 1) = 0;
        virtual void drawGradient(const QC::Rect &rect,
                                  QC::Color from,
                                  QC::Color to,
                                  GradientDirection direction) = 0;
        virtual void drawRoundedRect(const QC::Rect &rect,
                                     QC::u32 radius,
                                     QC::Color fill,
                                     QC::Color stroke,
                                     QC::u32 strokeWidth = 1) = 0;
        virtual void drawShadow(const QC::Rect &rect,
                                QC::Point offset,
                                QC::i32 blurRadius,
                                QC::Color color,
                                QC::u8 opacity) = 0;
        virtual void blit(const QC::Rect &rect,
                          const QC::u32 *pixels,
                          QC::u32 stride,
                          bool useAlpha) = 0;
    };

} // namespace QG
