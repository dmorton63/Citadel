#pragma once

// QGraphics FontManager - TrueType/OpenType font loading + raster caching
// Namespace: QG

#include "QCTypes.h"
#include "QCVector.h"

namespace QG
{

    class FontManager
    {
    public:
        struct Metrics
        {
            QC::i32 ascentPx = 0;      // baseline = top + ascentPx
            QC::i32 lineAdvancePx = 0; // advance to next line
            QC::i32 fixedAdvancePx = 0;
        };

        struct GlyphBitmap
        {
            QC::i32 width = 0;
            QC::i32 height = 0;
            QC::i32 xOff = 0; // offset from pen position
            QC::i32 yOff = 0; // offset from baseline
            const QC::u8 *alpha = nullptr;
        };

        static FontManager &instance();

        bool setDefaultFontFromBytes(const QC::u8 *data, QC::usize size);
        bool setDefaultFontFromBytes(const QC::Vector<QC::u8> &bytes);
        void clearDefaultFont();

        bool hasDefaultFont() const { return m_hasFont; }

        bool getMetrics(QC::i32 pixelHeight, Metrics *outMetrics);
        bool getGlyph(QC::i32 pixelHeight, QC::u32 codepoint, GlyphBitmap *outGlyph);

    private:
        FontManager();
        FontManager(const FontManager &) = delete;
        FontManager &operator=(const FontManager &) = delete;

        struct CachedGlyph
        {
            bool ready = false;
            QC::i32 width = 0;
            QC::i32 height = 0;
            QC::i32 xOff = 0;
            QC::i32 yOff = 0;
            QC::Vector<QC::u8> alpha;
        };

        bool ensureCache(QC::i32 pixelHeight);
        void invalidateCache();

        bool m_hasFont = false;
        QC::Vector<QC::u8> m_fontBytes;

        // Cache for the currently requested pixel height.
        bool m_cacheValid = false;
        QC::i32 m_cachePixelHeight = 0;
        Metrics m_cacheMetrics;
        QC::Vector<CachedGlyph> m_cacheGlyphs;

        // Opaque in header; defined in .cpp to keep stb headers out of public API.
        struct StbFont;
        StbFont *m_font = nullptr;
    };

} // namespace QG
