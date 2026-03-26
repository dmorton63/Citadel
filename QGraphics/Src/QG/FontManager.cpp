// QGraphics FontManager implementation

#include "QG/FontManager.h"

#include "QCMemUtil.h"

namespace
{
    static inline float qg_absf(float v) { return (v < 0.0f) ? -v : v; }

    static inline int qg_ifloor(float x)
    {
        const int i = static_cast<int>(x);
        if (x >= 0.0f)
            return i;
        return (static_cast<float>(i) == x) ? i : (i - 1);
    }

    static inline int qg_iceil(float x)
    {
        const int i = static_cast<int>(x);
        if (x <= 0.0f)
            return i;
        return (static_cast<float>(i) == x) ? i : (i + 1);
    }

    static inline float qg_sqrt(float x)
    {
        if (x <= 0.0f)
            return 0.0f;

        // Newton-Raphson; good enough for stb rasterization.
        float g = (x >= 1.0f) ? x : 1.0f;
        for (int i = 0; i < 8; ++i)
            g = 0.5f * (g + x / g);
        return g;
    }

    static inline float qg_fmod(float x, float y)
    {
        if (y == 0.0f)
            return 0.0f;
        const float q = x / y;
        const int k = qg_ifloor(q);
        return x - static_cast<float>(k) * y;
    }

    static inline float qg_wrap_pi(float x)
    {
        static constexpr float kPi = 3.14159265358979323846f;
        static constexpr float kTwoPi = 6.28318530717958647692f;
        const float t = (x + kPi) / kTwoPi;
        const int k = qg_ifloor(t);
        return x - static_cast<float>(k) * kTwoPi;
    }

    static inline float qg_cos(float x)
    {
        // Range reduce to [-pi, pi] then use a low-order cosine polynomial.
        x = qg_wrap_pi(x);
        const float x2 = x * x;
        const float x4 = x2 * x2;
        const float x6 = x4 * x2;
        return 1.0f - (x2 * 0.5f) + (x4 * (1.0f / 24.0f)) - (x6 * (1.0f / 720.0f));
    }

    static inline float qg_acos(float x)
    {
        // Fast approximation for acos(x) on [-1, 1].
        // Based on a common minimax-style polynomial approximation.
        static constexpr float kPi = 3.14159265358979323846f;

        if (x <= -1.0f)
            return kPi;
        if (x >= 1.0f)
            return 0.0f;

        const float negate = (x < 0.0f) ? 1.0f : 0.0f;
        x = qg_absf(x);

        float ret = -0.0187293f;
        ret = ret * x + 0.0742610f;
        ret = ret * x - 0.2121144f;
        ret = ret * x + 1.5707288f;
        ret = ret * qg_sqrt(1.0f - x);
        ret = ret - 2.0f * negate * ret;
        return negate * kPi + ret;
    }

    static inline float qg_cbrt(float x)
    {
        if (x <= 0.0f)
            return 0.0f;
        float g = (x >= 1.0f) ? x : 1.0f;
        for (int i = 0; i < 10; ++i)
            g = (2.0f * g + x / (g * g)) * (1.0f / 3.0f);
        return g;
    }

    static inline float qg_pow(float x, float y)
    {
        // stb_truetype uses STBTT_pow for cube roots in its cubic solver.
        // Implement the common cases; fall back to integer exponents.
        const float one_third = 1.0f / 3.0f;
        if (y == 0.5f)
            return qg_sqrt(x);
        if (y == one_third)
            return qg_cbrt(x);

        const int yi = static_cast<int>(y);
        if (static_cast<float>(yi) == y)
        {
            float r = 1.0f;
            float base = x;
            int exp = yi;
            if (exp < 0)
                return 0.0f;
            while (exp)
            {
                if (exp & 1)
                    r *= base;
                base *= base;
                exp >>= 1;
            }
            return r;
        }
        return 0.0f;
    }

    static inline void *qg_stb_malloc(QC::usize size)
    {
        return ::operator new(size);
    }

    static inline void qg_stb_free(void *ptr)
    {
        ::operator delete(ptr);
    }
} // namespace

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC

#define STBTT_ifloor(x) qg_ifloor((float)(x))
#define STBTT_iceil(x) qg_iceil((float)(x))
#define STBTT_sqrt(x) qg_sqrt((float)(x))
#define STBTT_pow(x, y) qg_pow((float)(x), (float)(y))
#define STBTT_fmod(x, y) qg_fmod((float)(x), (float)(y))
#define STBTT_cos(x) qg_cos((float)(x))
#define STBTT_acos(x) qg_acos((float)(x))
#define STBTT_fabs(x) qg_absf((float)(x))

#define STBTT_malloc(x, u) ((void)(u), qg_stb_malloc((QC::usize)(x)))
#define STBTT_free(x, u) ((void)(u), qg_stb_free((x)))

#define STBTT_assert(x) ((void)0)

#define STBTT_strlen(x) ::strlen((x))
#define STBTT_memcpy ::memcpy
#define STBTT_memset ::memset

#include "stb/stb_truetype.h"

namespace QG
{

    struct FontManager::StbFont
    {
        stbtt_fontinfo info;
    };

    FontManager &FontManager::instance()
    {
        static FontManager inst;
        return inst;
    }

    FontManager::FontManager()
    {
        m_cacheGlyphs.resize(128);
    }

    void FontManager::invalidateCache()
    {
        m_cacheValid = false;
        m_cachePixelHeight = 0;
        m_cacheMetrics = {};
        for (QC::usize i = 0; i < m_cacheGlyphs.size(); ++i)
        {
            m_cacheGlyphs[i].ready = false;
            m_cacheGlyphs[i].width = 0;
            m_cacheGlyphs[i].height = 0;
            m_cacheGlyphs[i].xOff = 0;
            m_cacheGlyphs[i].yOff = 0;
            m_cacheGlyphs[i].alpha.clear();
        }
    }

    void FontManager::clearDefaultFont()
    {
        m_hasFont = false;
        m_fontBytes.clear();
        if (m_font)
        {
            delete m_font;
            m_font = nullptr;
        }
        invalidateCache();
    }

    bool FontManager::setDefaultFontFromBytes(const QC::u8 *data, QC::usize size)
    {
        if (!data || size == 0)
        {
            clearDefaultFont();
            return false;
        }

        m_fontBytes.resize(size);
        ::memcpy(m_fontBytes.data(), data, size);

        if (!m_font)
            m_font = new StbFont();

        const int ok = stbtt_InitFont(&m_font->info, m_fontBytes.data(), 0);
        if (!ok)
        {
            clearDefaultFont();
            return false;
        }

        m_hasFont = true;
        invalidateCache();
        return true;
    }

    bool FontManager::setDefaultFontFromBytes(const QC::Vector<QC::u8> &bytes)
    {
        return setDefaultFontFromBytes(bytes.data(), bytes.size());
    }

    bool FontManager::ensureCache(QC::i32 pixelHeight)
    {
        if (!m_hasFont || !m_font)
            return false;

        if (pixelHeight < 6)
            pixelHeight = 6;
        if (pixelHeight > 96)
            pixelHeight = 96;

        if (m_cacheValid && m_cachePixelHeight == pixelHeight)
            return true;

        invalidateCache();

        const float scale = stbtt_ScaleForPixelHeight(&m_font->info, static_cast<float>(pixelHeight));

        int ascent = 0, descent = 0, lineGap = 0;
        stbtt_GetFontVMetrics(&m_font->info, &ascent, &descent, &lineGap);

        m_cacheMetrics.ascentPx = static_cast<QC::i32>(ascent * scale + 0.5f);
        const float lineAdvance = (ascent - descent + lineGap) * scale;
        m_cacheMetrics.lineAdvancePx = static_cast<QC::i32>(lineAdvance + 0.5f);
        if (m_cacheMetrics.lineAdvancePx < 1)
            m_cacheMetrics.lineAdvancePx = 1;

        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&m_font->info, 'M', &adv, &lsb);
        (void)lsb;
        m_cacheMetrics.fixedAdvancePx = static_cast<QC::i32>(adv * scale + 0.5f);
        if (m_cacheMetrics.fixedAdvancePx < 1)
            m_cacheMetrics.fixedAdvancePx = 1;

        for (QC::u32 cp = 0; cp < 128; ++cp)
        {
            CachedGlyph &g = m_cacheGlyphs[cp];
            g.ready = true;

            if (cp < 32 || cp > 126)
                continue;

            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            stbtt_GetCodepointBitmapBox(&m_font->info, static_cast<int>(cp), scale, scale, &x0, &y0, &x1, &y1);
            const int w = x1 - x0;
            const int h = y1 - y0;
            if (w <= 0 || h <= 0)
                continue;

            g.width = static_cast<QC::i32>(w);
            g.height = static_cast<QC::i32>(h);
            g.xOff = static_cast<QC::i32>(x0);
            g.yOff = static_cast<QC::i32>(y0);
            g.alpha.resize(static_cast<QC::usize>(w * h));
            stbtt_MakeCodepointBitmap(&m_font->info,
                                      g.alpha.data(),
                                      w,
                                      h,
                                      w,
                                      scale,
                                      scale,
                                      static_cast<int>(cp));
        }

        m_cachePixelHeight = pixelHeight;
        m_cacheValid = true;
        return true;
    }

    bool FontManager::getMetrics(QC::i32 pixelHeight, Metrics *outMetrics)
    {
        if (!outMetrics)
            return false;
        if (!ensureCache(pixelHeight))
            return false;
        *outMetrics = m_cacheMetrics;
        return true;
    }

    bool FontManager::getGlyph(QC::i32 pixelHeight, QC::u32 codepoint, GlyphBitmap *outGlyph)
    {
        if (!outGlyph)
            return false;
        if (!ensureCache(pixelHeight))
            return false;

        if (codepoint >= 128)
            codepoint = static_cast<QC::u32>('?');

        const CachedGlyph &g = m_cacheGlyphs[codepoint];
        if (!g.ready)
            return false;

        outGlyph->width = g.width;
        outGlyph->height = g.height;
        outGlyph->xOff = g.xOff;
        outGlyph->yOff = g.yOff;
        outGlyph->alpha = g.alpha.empty() ? nullptr : g.alpha.data();
        return true;
    }

} // namespace QG
