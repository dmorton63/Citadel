#pragma once

// QGraphics Painter - Abstract painting interface
// Namespace: QG
//
// This is the core abstraction for all painting operations.
// Controls and UI elements paint through this interface,
// allowing the rendering backend to be swapped without
// changing UI code.
//
// Implementations:
// - QWRenderer (software rasterizer for Window)
// - Future: GPU-accelerated renderer

#include "QCTypes.h"
#include "QCGeometry.h"
#include "QCColor.h"
#include "QGPen.h"
#include "QGBrush.h"

namespace QG
{

    /// Text alignment options
    enum class TextAlign : QC::u8
    {
        Left = 0x01,
        Center = 0x02,
        Right = 0x04,
        Top = 0x10,
        VCenter = 0x20,
        Bottom = 0x40,

        // Common combinations
        TopLeft = Left | Top,
        TopCenter = Center | Top,
        TopRight = Right | Top,
        CenterLeft = Left | VCenter,
        Centered = Center | VCenter,
        CenterRight = Right | VCenter,
        BottomLeft = Left | Bottom,
        BottomCenter = Center | Bottom,
        BottomRight = Right | Bottom
    };

    /// Text formatting options
    struct TextFormat
    {
        TextAlign align;
        bool wordWrap;
        bool ellipsis; // Add ... if text doesn't fit

        TextFormat()
            : align(TextAlign::TopLeft),
              wordWrap(false),
              ellipsis(false) {}
    };

    /// Abstract Painter interface
    /// All drawing operations go through this interface
    class IPainter
    {
    public:
        virtual ~IPainter() = default;

        // ==================== State ====================

        /// Get the current size of the paint surface
        virtual QC::Size size() const = 0;

        /// Get the bounding rect of the paint surface
        virtual QC::Rect bounds() const = 0;

        // ==================== Clipping ====================

        /// Set clip rectangle (all drawing clipped to this area)
        virtual void setClipRect(const QC::Rect &rect) = 0;

        /// Clear clip rectangle (no clipping)
        virtual void clearClipRect() = 0;

        /// Get current clip rectangle
        virtual QC::Rect clipRect() const = 0;

        // ==================== Coordinate Transform ====================

        /// Set origin offset (all coordinates are relative to this)
        virtual void setOrigin(QC::i32 x, QC::i32 y) = 0;
        virtual void setOrigin(QC::Point origin) { setOrigin(origin.x, origin.y); }

        /// Get current origin
        virtual QC::Point origin() const = 0;

        /// Translate origin by delta
        virtual void translate(QC::i32 dx, QC::i32 dy) = 0;

        /// Set scalar applied to the fixed-width font renderer
        virtual void setTextScale(float scale) = 0;

        /// Get the current text scale
        virtual float textScale() const = 0;

        // ==================== Primitive Drawing ====================

        /// Set a single pixel
        virtual void setPixel(QC::i32 x, QC::i32 y, QC::Color color) = 0;

        /// Get a single pixel
        virtual QC::Color pixel(QC::i32 x, QC::i32 y) const = 0;

        /// Draw a line
        virtual void drawLine(QC::i32 x1, QC::i32 y1, QC::i32 x2, QC::i32 y2, const Pen &pen) = 0;

        /// Draw a horizontal line (optimized)
        virtual void drawHLine(QC::i32 x, QC::i32 y, QC::u32 length, QC::Color color) = 0;

        /// Draw a vertical line (optimized)
        virtual void drawVLine(QC::i32 x, QC::i32 y, QC::u32 length, QC::Color color) = 0;

        // ==================== Rectangle Drawing ====================

        /// Fill a rectangle with a brush
        virtual void fillRect(const QC::Rect &rect, const Brush &brush) = 0;

        /// Fill a rectangle with solid color (convenience)
        virtual void fillRect(const QC::Rect &rect, QC::Color color)
        {
            fillRect(rect, Brush::solid(color));
        }

        /// Draw rectangle outline with a pen
        virtual void drawRect(const QC::Rect &rect, const Pen &pen) = 0;

        /// Draw rectangle outline with solid color (convenience)
        virtual void drawRect(const QC::Rect &rect, QC::Color color)
        {
            drawRect(rect, Pen(color));
        }

        /// Fill and stroke a rectangle
        virtual void fillAndDrawRect(const QC::Rect &rect, const Brush &brush, const Pen &pen)
        {
            fillRect(rect, brush);
            drawRect(rect, pen);
        }

        // ==================== 3D Border Drawing ====================

        /// Draw a 3D raised border
        virtual void drawRaisedBorder(const QC::Rect &rect,
                                      QC::Color light, QC::Color dark,
                                      QC::u32 width = 1) = 0;

        /// Draw a 3D sunken border
        virtual void drawSunkenBorder(const QC::Rect &rect,
                                      QC::Color light, QC::Color dark,
                                      QC::u32 width = 1) = 0;

        /// Draw an etched border (sunken outer, raised inner)
        virtual void drawEtchedBorder(const QC::Rect &rect,
                                      QC::Color light, QC::Color dark) = 0;

        // ==================== Gradient Fill ====================

        /// Fill with vertical gradient
        virtual void fillGradientV(const QC::Rect &rect, QC::Color top, QC::Color bottom) = 0;

        /// Fill with horizontal gradient
        virtual void fillGradientH(const QC::Rect &rect, QC::Color left, QC::Color right) = 0;

        // ==================== Text Drawing ====================

        /// Draw text at position
        virtual void drawText(QC::i32 x, QC::i32 y, const char *text, QC::Color color) = 0;

        /// Draw text within a rectangle (with alignment)
        virtual void drawText(const QC::Rect &rect, const char *text,
                              QC::Color color, const TextFormat &format) = 0;

        /// Measure text dimensions
        virtual QC::Size measureText(const char *text) const = 0;

        // ==================== Blitting ====================

        /// Copy pixels from a buffer
        virtual void blit(QC::i32 x, QC::i32 y,
                          const QC::u32 *pixels, QC::u32 width, QC::u32 height,
                          QC::u32 stride = 0) = 0;

        /// Copy pixels with alpha blending
        virtual void blitAlpha(QC::i32 x, QC::i32 y,
                               const QC::u32 *pixels, QC::u32 width, QC::u32 height,
                               QC::u32 stride = 0) = 0;

        // ==================== Clear ====================

        /// Clear entire surface with color
        virtual void clear(QC::Color color) = 0;
    };

} // namespace QG
