#pragma once

// QCommon Linear Algebra - Lightweight vector/matrix types
// Namespace: QC
//
// Conventions (explicit):
// - Column-major storage: m[col][row]
// - Vectors are column vectors
// - Transform: v' = M * v
// - Right-handed coordinate system (graphics-friendly; OpenGL-style view/proj defaults)
//
// Goals:
// - Freestanding-friendly (no <cmath>, no STL)
// - Fast enough for graphics/compositing/GPU work
// - Header-only for easy reuse across modules

#include "QCTypes.h"
#include "QCTrig.h"

namespace QC
{

    inline f32 absf(f32 v) { return v < 0.0f ? -v : v; }

    inline f32 minf(f32 a, f32 b) { return (a < b) ? a : b; }
    inline f32 maxf(f32 a, f32 b) { return (a > b) ? a : b; }

    inline f32 clampf(f32 v, f32 lo, f32 hi)
    {
        return (v < lo) ? lo : ((v > hi) ? hi : v);
    }

    inline f32 degToRad(f32 deg)
    {
        return deg * (3.14159265358979323846f / 180.0f);
    }

    inline f32 radToDeg(f32 rad)
    {
        return rad * (180.0f / 3.14159265358979323846f);
    }

    // sqrtf using SSE (requires SSE enabled; QArchCPU enables FPU/SSE early).
    inline f32 sqrtf_sse(f32 v)
    {
        f32 out;
        asm volatile(
            "sqrtss %1, %0"
            : "=x"(out)
            : "x"(v));
        return out;
    }

    inline f32 rsqrtf_approx(f32 v)
    {
        // Approx reciprocal sqrt; good for normalization with one refinement if needed.
        f32 out;
        asm volatile(
            "rsqrtss %1, %0"
            : "=x"(out)
            : "x"(v));
        return out;
    }

    struct Vec2f
    {
        f32 x;
        f32 y;

        constexpr Vec2f() : x(0), y(0) {}
        constexpr Vec2f(f32 px, f32 py) : x(px), y(py) {}

        Vec2f operator+(const Vec2f &o) const { return Vec2f{x + o.x, y + o.y}; }
        Vec2f operator-(const Vec2f &o) const { return Vec2f{x - o.x, y - o.y}; }
        Vec2f operator*(f32 s) const { return Vec2f{x * s, y * s}; }

        Vec2f &operator+=(const Vec2f &o)
        {
            x += o.x;
            y += o.y;
            return *this;
        }

        Vec2f &operator-=(const Vec2f &o)
        {
            x -= o.x;
            y -= o.y;
            return *this;
        }
    };

    struct Vec3f
    {
        f32 x;
        f32 y;
        f32 z;

        constexpr Vec3f() : x(0), y(0), z(0) {}
        constexpr Vec3f(f32 px, f32 py, f32 pz) : x(px), y(py), z(pz) {}

        Vec3f operator+(const Vec3f &o) const { return Vec3f{x + o.x, y + o.y, z + o.z}; }
        Vec3f operator-(const Vec3f &o) const { return Vec3f{x - o.x, y - o.y, z - o.z}; }
        Vec3f operator*(f32 s) const { return Vec3f{x * s, y * s, z * s}; }

        Vec3f &operator+=(const Vec3f &o)
        {
            x += o.x;
            y += o.y;
            z += o.z;
            return *this;
        }

        Vec3f &operator-=(const Vec3f &o)
        {
            x -= o.x;
            y -= o.y;
            z -= o.z;
            return *this;
        }
    };

    struct Vec4f
    {
        f32 x;
        f32 y;
        f32 z;
        f32 w;

        constexpr Vec4f() : x(0), y(0), z(0), w(0) {}
        constexpr Vec4f(f32 px, f32 py, f32 pz, f32 pw) : x(px), y(py), z(pz), w(pw) {}
    };

    inline f32 dot(const Vec3f &a, const Vec3f &b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    inline Vec3f cross(const Vec3f &a, const Vec3f &b)
    {
        return Vec3f{
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
    }

    inline f32 lengthSq(const Vec3f &v)
    {
        return dot(v, v);
    }

    inline f32 length(const Vec3f &v)
    {
        return sqrtf_sse(lengthSq(v));
    }

    inline Vec3f normalize(const Vec3f &v)
    {
        const f32 lsq = lengthSq(v);
        if (lsq <= 0.0f)
            return Vec3f{0, 0, 0};

        // Use reciprocal sqrt approximation, refine once (Newton-Raphson).
        f32 inv = rsqrtf_approx(lsq);
        // inv = inv * (1.5 - 0.5*x*inv^2)
        inv = inv * (1.5f - 0.5f * lsq * inv * inv);
        return v * inv;
    }

    // Column-major 4x4 matrix (OpenGL-style), convenient for GPU math.
    // m[col][row]
    struct Mat4f
    {
        f32 m[4][4];

        static Mat4f identity()
        {
            Mat4f r{};
            r.m[0][0] = 1.0f;
            r.m[1][1] = 1.0f;
            r.m[2][2] = 1.0f;
            r.m[3][3] = 1.0f;
            return r;
        }

        static Mat4f translation(const Vec3f &t)
        {
            Mat4f r = identity();
            r.m[3][0] = t.x;
            r.m[3][1] = t.y;
            r.m[3][2] = t.z;
            return r;
        }

        static Mat4f scale(const Vec3f &s)
        {
            Mat4f r{};
            r.m[0][0] = s.x;
            r.m[1][1] = s.y;
            r.m[2][2] = s.z;
            r.m[3][3] = 1.0f;
            return r;
        }

        // Rotation around X/Y/Z given sin/cos (no trig dependency).
        static Mat4f rotationX(f32 c, f32 s)
        {
            Mat4f r = identity();
            r.m[1][1] = c;
            r.m[2][1] = -s;
            r.m[1][2] = s;
            r.m[2][2] = c;
            return r;
        }

        static Mat4f rotationY(f32 c, f32 s)
        {
            Mat4f r = identity();
            r.m[0][0] = c;
            r.m[2][0] = s;
            r.m[0][2] = -s;
            r.m[2][2] = c;
            return r;
        }

        static Mat4f rotationZ(f32 c, f32 s)
        {
            Mat4f r = identity();
            r.m[0][0] = c;
            r.m[1][0] = -s;
            r.m[0][1] = s;
            r.m[1][1] = c;
            return r;
        }
    };

    inline Mat4f mul(const Mat4f &a, const Mat4f &b)
    {
        Mat4f r{};
        // Column-major multiply: r = a * b
        for (int c = 0; c < 4; ++c)
        {
            for (int rrow = 0; rrow < 4; ++rrow)
            {
                r.m[c][rrow] =
                    a.m[0][rrow] * b.m[c][0] +
                    a.m[1][rrow] * b.m[c][1] +
                    a.m[2][rrow] * b.m[c][2] +
                    a.m[3][rrow] * b.m[c][3];
            }
        }
        return r;
    }

    inline Vec4f mul(const Mat4f &m, const Vec4f &v)
    {
        // Column-major: result = m * v
        return Vec4f{
            m.m[0][0] * v.x + m.m[1][0] * v.y + m.m[2][0] * v.z + m.m[3][0] * v.w,
            m.m[0][1] * v.x + m.m[1][1] * v.y + m.m[2][1] * v.z + m.m[3][1] * v.w,
            m.m[0][2] * v.x + m.m[1][2] * v.y + m.m[2][2] * v.z + m.m[3][2] * v.w,
            m.m[0][3] * v.x + m.m[1][3] * v.y + m.m[2][3] * v.z + m.m[3][3] * v.w};
    }

    inline Vec3f transformPoint(const Mat4f &m, const Vec3f &p)
    {
        const Vec4f r = mul(m, Vec4f{p.x, p.y, p.z, 1.0f});
        if (r.w == 0.0f)
            return Vec3f{r.x, r.y, r.z};
        const f32 invW = 1.0f / r.w;
        return Vec3f{r.x * invW, r.y * invW, r.z * invW};
    }

    inline Vec3f transformVector(const Mat4f &m, const Vec3f &v)
    {
        const Vec4f r = mul(m, Vec4f{v.x, v.y, v.z, 0.0f});
        return Vec3f{r.x, r.y, r.z};
    }

    // Right-handed lookAt matrix.
    // Assumes camera looks towards -Z in view space (OpenGL-style).
    inline Mat4f lookAtRH(const Vec3f &eye, const Vec3f &center, const Vec3f &up)
    {
        const Vec3f f = normalize(Vec3f{center.x - eye.x, center.y - eye.y, center.z - eye.z});
        const Vec3f s = normalize(cross(f, up));
        const Vec3f u = cross(s, f);

        Mat4f r = Mat4f::identity();

        // Column 0: s
        r.m[0][0] = s.x;
        r.m[0][1] = s.y;
        r.m[0][2] = s.z;

        // Column 1: u
        r.m[1][0] = u.x;
        r.m[1][1] = u.y;
        r.m[1][2] = u.z;

        // Column 2: -f
        r.m[2][0] = -f.x;
        r.m[2][1] = -f.y;
        r.m[2][2] = -f.z;

        // Column 3: translation
        r.m[3][0] = -dot(s, eye);
        r.m[3][1] = -dot(u, eye);
        r.m[3][2] = dot(f, eye);

        return r;
    }

    // Right-handed perspective projection using explicit frustum.
    // nearZ/farZ are positive distances from the eye.
    inline Mat4f perspectiveRHFrustum(f32 left, f32 right, f32 bottom, f32 top, f32 nearZ, f32 farZ)
    {
        Mat4f r{};

        const f32 rl = right - left;
        const f32 tb = top - bottom;
        const f32 fn = farZ - nearZ;

        // Row-major entries (OpenGL-style), then assigned into m[col][row].
        const f32 m00 = (2.0f * nearZ) / rl;
        const f32 m11 = (2.0f * nearZ) / tb;
        const f32 m02 = (right + left) / rl;
        const f32 m12 = (top + bottom) / tb;
        const f32 m22 = -(farZ + nearZ) / fn;
        const f32 m32 = -(2.0f * farZ * nearZ) / fn;

        r.m[0][0] = m00;
        r.m[1][1] = m11;
        r.m[2][0] = m02;
        r.m[2][1] = m12;
        r.m[2][2] = m22;
        r.m[3][2] = -1.0f; // m23
        r.m[2][3] = m32;   // m32

        return r;
    }

    // Convenience: perspective from vertical fov expressed as tan(fovY/2).
    // This avoids requiring tanf() in freestanding builds.
    inline Mat4f perspectiveRH(f32 aspect, f32 tanHalfFovy, f32 nearZ, f32 farZ)
    {
        const f32 top = nearZ * tanHalfFovy;
        const f32 bottom = -top;
        const f32 right = top * aspect;
        const f32 left = -right;
        return perspectiveRHFrustum(left, right, bottom, top, nearZ, farZ);
    }

    // Convenience: perspective from vertical FOV in radians.
    inline Mat4f perspectiveRHFromFovYRadians(f32 fovYRadians, f32 aspect, f32 nearZ, f32 farZ)
    {
        const f32 tanHalfFovy = QC::tanf_approx(0.5f * fovYRadians);
        return perspectiveRH(aspect, tanHalfFovy, nearZ, farZ);
    }

    inline Mat4f perspectiveRHFromFovYDegrees(f32 fovYDegrees, f32 aspect, f32 nearZ, f32 farZ)
    {
        return perspectiveRHFromFovYRadians(degToRad(fovYDegrees), aspect, nearZ, farZ);
    }

    // Right-handed orthographic projection.
    inline Mat4f orthoRH(f32 left, f32 right, f32 bottom, f32 top, f32 nearZ, f32 farZ)
    {
        Mat4f r = Mat4f::identity();

        const f32 rl = right - left;
        const f32 tb = top - bottom;
        const f32 fn = farZ - nearZ;

        // Row-major OpenGL-style ortho (RH):
        // [ 2/rl   0      0     -(r+l)/rl ]
        // [ 0      2/tb   0     -(t+b)/tb ]
        // [ 0      0     -2/fn  -(f+n)/fn ]
        // [ 0      0      0      1 ]

        r.m[0][0] = 2.0f / rl;
        r.m[1][1] = 2.0f / tb;
        r.m[2][2] = -2.0f / fn;

        r.m[3][0] = -(right + left) / rl;
        r.m[3][1] = -(top + bottom) / tb;
        r.m[3][2] = -(farZ + nearZ) / fn;

        return r;
    }

    struct Quatf
    {
        // (x,y,z,w) with w as scalar part.
        f32 x;
        f32 y;
        f32 z;
        f32 w;

        constexpr Quatf() : x(0), y(0), z(0), w(1) {}
        constexpr Quatf(f32 px, f32 py, f32 pz, f32 pw) : x(px), y(py), z(pz), w(pw) {}

        static Quatf identity() { return Quatf{0, 0, 0, 1}; }

        // Axis-angle using provided sin/cos(halfAngle).
        static Quatf fromAxisAngle(const Vec3f &axisUnit, f32 sinHalf, f32 cosHalf)
        {
            return Quatf{axisUnit.x * sinHalf, axisUnit.y * sinHalf, axisUnit.z * sinHalf, cosHalf};
        }
    };

    inline Quatf mul(const Quatf &a, const Quatf &b)
    {
        // Hamilton product.
        return Quatf{
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
    }

    inline Quatf normalize(const Quatf &q)
    {
        const f32 lsq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
        if (lsq <= 0.0f)
            return Quatf::identity();
        f32 inv = rsqrtf_approx(lsq);
        inv = inv * (1.5f - 0.5f * lsq * inv * inv);
        return Quatf{q.x * inv, q.y * inv, q.z * inv, q.w * inv};
    }

    inline Mat4f toMat4(const Quatf &qIn)
    {
        const Quatf q = normalize(qIn);
        const f32 x2 = q.x + q.x;
        const f32 y2 = q.y + q.y;
        const f32 z2 = q.z + q.z;

        const f32 xx = q.x * x2;
        const f32 yy = q.y * y2;
        const f32 zz = q.z * z2;
        const f32 xy = q.x * y2;
        const f32 xz = q.x * z2;
        const f32 yz = q.y * z2;
        const f32 wx = q.w * x2;
        const f32 wy = q.w * y2;
        const f32 wz = q.w * z2;

        Mat4f m = Mat4f::identity();

        // Row-major rotation, assigned to column-major storage.
        m.m[0][0] = 1.0f - (yy + zz);
        m.m[0][1] = (xy + wz);
        m.m[0][2] = (xz - wy);

        m.m[1][0] = (xy - wz);
        m.m[1][1] = 1.0f - (xx + zz);
        m.m[1][2] = (yz + wx);

        m.m[2][0] = (xz + wy);
        m.m[2][1] = (yz - wx);
        m.m[2][2] = 1.0f - (xx + yy);

        return m;
    }

} // namespace QC
