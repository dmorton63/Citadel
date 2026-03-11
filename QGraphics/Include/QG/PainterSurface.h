#pragma once

// QGraphics PainterSurface - Software IPainter implementation over a raw buffer
// Namespace: QG

#include "QGPainter.h"

namespace QG
{

    /// PainterSurface - concrete software painter targeting a raw RGBA buffer
    class PainterSurface : public IPainter
    {
    public:
        PainterSurface(QC::u32 *pixels = nullptr,
                       QC::u32 width = 0,
                       QC::u32 height = 0,
                       QC::u32 pitch = 0);

        /// Attach a new surface (optionally overriding pitch)
        void setSurface(QC::u32 *pixels,
                        QC::u32 width,
                        QC::u32 height,
                        QC::u32 pitch = 0);

        // ==================== IPainter Implementation ====================
        QC::Size size() const override;
        QC::Rect bounds() const override;

        void setClipRect(const QC::Rect &rect) override;
        void clearClipRect() override;
        QC::Rect clipRect() const override;

        void setOrigin(QC::i32 x, QC::i32 y) override;
        QC::Point origin() const override;
        void translate(QC::i32 dx, QC::i32 dy) override;

        void setTextScale(float scale) override;
        float textScale() const override;

        void setPixel(QC::i32 x, QC::i32 y, QC::Color color) override;
        QC::Color pixel(QC::i32 x, QC::i32 y) const override;

        void drawLine(QC::i32 x1, QC::i32 y1, QC::i32 x2, QC::i32 y2, const Pen &pen) override;
        void drawHLine(QC::i32 x, QC::i32 y, QC::u32 length, QC::Color color) override;
        void drawVLine(QC::i32 x, QC::i32 y, QC::u32 length, QC::Color color) override;

        void fillRect(const QC::Rect &rect, const Brush &brush) override;
        void drawRect(const QC::Rect &rect, const Pen &pen) override;

        void drawRaisedBorder(const QC::Rect &rect, QC::Color light, QC::Color dark, QC::u32 width = 1) override;
        void drawSunkenBorder(const QC::Rect &rect, QC::Color light, QC::Color dark, QC::u32 width = 1) override;
        void drawEtchedBorder(const QC::Rect &rect, QC::Color light, QC::Color dark) override;

        void fillGradientV(const QC::Rect &rect, QC::Color top, QC::Color bottom) override;
        void fillGradientH(const QC::Rect &rect, QC::Color left, QC::Color right) override;

        void drawText(QC::i32 x, QC::i32 y, const char *text, QC::Color color) override;
        void drawText(const QC::Rect &rect, const char *text, QC::Color color, const TextFormat &format) override;
        QC::Size measureText(const char *text) const override;

        void blit(QC::i32 x, QC::i32 y,
                  const QC::u32 *pixels, QC::u32 width, QC::u32 height,
                  QC::u32 stride = 0) override;

        void blitAlpha(QC::i32 x, QC::i32 y,
                       const QC::u32 *pixels, QC::u32 width, QC::u32 height,
                       QC::u32 stride = 0) override;

        void clear(QC::Color color) override;

    private:
        bool inClip(QC::i32 x, QC::i32 y) const;
        QC::i32 textPixelScale() const;
        void stampGlyphPixel(QC::i32 baseX, QC::i32 baseY, QC::i32 scale, QC::Color color);

        QC::u32 *m_pixels;
        QC::u32 m_width;
        QC::u32 m_height;
        QC::u32 m_pitch;

        QC::Rect m_clip;
        bool m_hasClip;

        QC::Point m_origin;
        float m_textScale;
    };

} // namespace QG
