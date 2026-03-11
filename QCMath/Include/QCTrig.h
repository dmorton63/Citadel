#pragma once

// QCommon Trig - Freestanding-friendly trig approximations
// Namespace: QC
//
// Notes:
// - No <cmath> dependencies
// - Intended for graphics/projection math, not scientific computing
// - Accuracy is reasonable for typical angles; if you need higher precision,
//   we can add higher-order polynomials or CORDIC later.

#include "QCTypes.h"

namespace QC
{
    static constexpr f32 kPi = 3.14159265358979323846f;
    static constexpr f32 kTwoPi = 6.28318530717958647692f;
    static constexpr f32 kHalfPi = 1.57079632679489661923f;

    inline f32 absf_local(f32 v) { return v < 0.0f ? -v : v; }

    inline i32 floorToI32(f32 x)
    {
        // Truncates toward zero; adjust for negatives.
        const i32 i = static_cast<i32>(x);
        if (x >= 0.0f)
            return i;
        // If x is already an integer, keep it; otherwise subtract 1.
        return (static_cast<f32>(i) == x) ? i : (i - 1);
    }

    inline f32 wrapPi(f32 x)
    {
        // Wrap to [-pi, pi] using a cheap range reduction.
        // k = floor((x + pi) / 2pi)
        const f32 t = (x + kPi) / kTwoPi;
        const i32 k = floorToI32(t);
        return x - static_cast<f32>(k) * kTwoPi;
    }

    inline f32 sinf_approx(f32 x)
    {
        // Range reduce to [-pi, pi].
        x = wrapPi(x);

        // Use a common fast sine approximation:
        // y = Bx + Cx|x|, then refine with a quadratic term.
        // This is fast and decent for real-time graphics.
        // Reference form often used in demos.
        static constexpr f32 B = 4.0f / kPi;
        static constexpr f32 C = -4.0f / (kPi * kPi);
        static constexpr f32 P = 0.225f;

        const f32 y = B * x + C * x * absf_local(x);
        return P * (y * absf_local(y) - y) + y;
    }

    inline f32 cosf_approx(f32 x)
    {
        // cos(x) = sin(x + pi/2)
        return sinf_approx(x + kHalfPi);
    }

    inline void sincosf_approx(f32 x, f32 *outSin, f32 *outCos)
    {
        if (outSin)
            *outSin = sinf_approx(x);
        if (outCos)
            *outCos = cosf_approx(x);
    }

    inline f32 tanf_approx(f32 x)
    {
        // tan(x) = sin/cos; guard against division by ~0.
        const f32 s = sinf_approx(x);
        const f32 c = cosf_approx(x);
        const f32 ac = absf_local(c);
        if (ac < 1e-6f)
        {
            // Saturate to a large magnitude with correct sign.
            return (s >= 0.0f) ? 1e6f : -1e6f;
        }
        return s / c;
    }

} // namespace QC
