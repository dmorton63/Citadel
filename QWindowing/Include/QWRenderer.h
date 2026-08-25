#pragma once

// QWindowing Renderer - 2D rendering primitives
// Namespace: QW

#include "QCTypes.h"
#include "QWWindowManager.h"

namespace QW
{

    class Renderer
    {
    public:
        Renderer();
        ~Renderer();

        // Target
        void setTarget(QC::u32 *buffer, QC::u32 width, QC::u32 height, QC::u32 pitch);

        // Clipping
        void setClipRect(const Rect &rect);
        void clearClipRect();

        // Primitives
        void clear(Color color);
        void setPixel(QC::i32 x, QC::i32 y, Color color);
        Color getPixel(QC::i32 x, QC::i32 y) const;

        // Copy a region of pixels out of / into the render buffer (for cursor background save/restore).
        // dst/src must be width*height u32 pixels; unclipped regions are silently skipped.
        void copyRegionOut(QC::i32 x, QC::i32 y, QC::u32 width, QC::u32 height, QC::u32 *dst) const;
        void copyRegionIn(QC::i32 x, QC::i32 y, QC::u32 width, QC::u32 height, const QC::u32 *src);

        void drawLine(QC::i32 x1, QC::i32 y1, QC::i32 x2, QC::i32 y2, Color color);
        void drawHLine(QC::i32 x, QC::i32 y, QC::u32 length, Color color);
        void drawVLine(QC::i32 x, QC::i32 y, QC::u32 length, Color color);

        void drawRect(const Rect &rect, Color color);
        void fillRect(const Rect &rect, Color color);

        void drawCircle(QC::i32 cx, QC::i32 cy, QC::u32 radius, Color color);
        void fillCircle(QC::i32 cx, QC::i32 cy, QC::u32 radius, Color color);

        void drawTriangle(Point p1, Point p2, Point p3, Color color);
        void fillTriangle(Point p1, Point p2, Point p3, Color color);

        // Text
        void drawChar(QC::i32 x, QC::i32 y, char c, Color color);
        void drawString(QC::i32 x, QC::i32 y, const char *str, Color color);
        Size measureString(const char *str) const;

        // Blitting
        void blit(QC::i32 x, QC::i32 y, const QC::u32 *src,
                  QC::u32 srcWidth, QC::u32 srcHeight, QC::u32 srcPitch);
        void blitScaled(const Rect &dest, const QC::u32 *src,
                        QC::u32 srcWidth, QC::u32 srcHeight, QC::u32 srcPitch);
        void blitAlpha(QC::i32 x, QC::i32 y, const QC::u32 *src,
                       QC::u32 srcWidth, QC::u32 srcHeight, QC::u32 srcPitch);

    private:
        bool clipPoint(QC::i32 &x, QC::i32 &y) const;
        bool clipRect(Rect &rect) const;

        QC::u32 *m_buffer;
        QC::u32 m_width;
        QC::u32 m_height;
        QC::u32 m_pitch;

        Rect m_clipRect;
        bool m_hasClipRect;
    };

} // namespace QW
