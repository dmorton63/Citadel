#include "QKBootSha256.h"

#include "QCString.h"

namespace QK::Boot::Security
{
    namespace
    {
        static inline QC::u32 Ror(QC::u32 x, QC::u32 n)
        {
            return (x >> n) | (x << (32 - n));
        }

        static inline QC::u32 Ch(QC::u32 x, QC::u32 y, QC::u32 z)
        {
            return (x & y) ^ (~x & z);
        }

        static inline QC::u32 Maj(QC::u32 x, QC::u32 y, QC::u32 z)
        {
            return (x & y) ^ (x & z) ^ (y & z);
        }

        static inline QC::u32 BigSigma0(QC::u32 x)
        {
            return Ror(x, 2) ^ Ror(x, 13) ^ Ror(x, 22);
        }

        static inline QC::u32 BigSigma1(QC::u32 x)
        {
            return Ror(x, 6) ^ Ror(x, 11) ^ Ror(x, 25);
        }

        static inline QC::u32 SmallSigma0(QC::u32 x)
        {
            return Ror(x, 7) ^ Ror(x, 18) ^ (x >> 3);
        }

        static inline QC::u32 SmallSigma1(QC::u32 x)
        {
            return Ror(x, 17) ^ Ror(x, 19) ^ (x >> 10);
        }

        static QC::u32 ReadBe32(const QC::u8 *p)
        {
            return (static_cast<QC::u32>(p[0]) << 24) |
                   (static_cast<QC::u32>(p[1]) << 16) |
                   (static_cast<QC::u32>(p[2]) << 8) |
                   static_cast<QC::u32>(p[3]);
        }

        static void WriteBe32(QC::u8 *p, QC::u32 v)
        {
            p[0] = static_cast<QC::u8>((v >> 24) & 0xFF);
            p[1] = static_cast<QC::u8>((v >> 16) & 0xFF);
            p[2] = static_cast<QC::u8>((v >> 8) & 0xFF);
            p[3] = static_cast<QC::u8>(v & 0xFF);
        }

        static void WriteBe64(QC::u8 *p, QC::u64 v)
        {
            p[0] = static_cast<QC::u8>((v >> 56) & 0xFF);
            p[1] = static_cast<QC::u8>((v >> 48) & 0xFF);
            p[2] = static_cast<QC::u8>((v >> 40) & 0xFF);
            p[3] = static_cast<QC::u8>((v >> 32) & 0xFF);
            p[4] = static_cast<QC::u8>((v >> 24) & 0xFF);
            p[5] = static_cast<QC::u8>((v >> 16) & 0xFF);
            p[6] = static_cast<QC::u8>((v >> 8) & 0xFF);
            p[7] = static_cast<QC::u8>(v & 0xFF);
        }

        static void Transform(QC::u32 state[8], const QC::u8 block[64])
        {
            static constexpr QC::u32 K[64] = {
                0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
                0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
                0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
                0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
                0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
                0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
                0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
                0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

            QC::u32 w[64];
            for (int i = 0; i < 16; ++i)
                w[i] = ReadBe32(block + i * 4);
            for (int i = 16; i < 64; ++i)
                w[i] = SmallSigma1(w[i - 2]) + w[i - 7] + SmallSigma0(w[i - 15]) + w[i - 16];

            QC::u32 a = state[0];
            QC::u32 b = state[1];
            QC::u32 c = state[2];
            QC::u32 d = state[3];
            QC::u32 e = state[4];
            QC::u32 f = state[5];
            QC::u32 g = state[6];
            QC::u32 h = state[7];

            for (int i = 0; i < 64; ++i)
            {
                const QC::u32 t1 = h + BigSigma1(e) + Ch(e, f, g) + K[i] + w[i];
                const QC::u32 t2 = BigSigma0(a) + Maj(a, b, c);
                h = g;
                g = f;
                f = e;
                e = d + t1;
                d = c;
                c = b;
                b = a;
                a = t1 + t2;
            }

            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
            state[5] += f;
            state[6] += g;
            state[7] += h;
        }
    }

    void Sha256(const QC::u8 *Data, QC::usize Len, QC::u8 OutDigest[32])
    {
        if (!OutDigest)
            return;

        QC::u32 state[8] = {
            0x6a09e667u,
            0xbb67ae85u,
            0x3c6ef372u,
            0xa54ff53au,
            0x510e527fu,
            0x9b05688cu,
            0x1f83d9abu,
            0x5be0cd19u};

        QC::u8 block[64];
        QC::usize filled = 0;
        QC::u64 totalBits = 0;

        const QC::u8 *p = Data;
        QC::usize remaining = Len;

        while (remaining > 0)
        {
            const QC::usize take = (remaining < (64 - filled)) ? remaining : (64 - filled);
            if (take)
            {
                QC::String::memcpy(block + filled, p, take);
                filled += take;
                p += take;
                remaining -= take;
                totalBits += static_cast<QC::u64>(take) * 8ull;
            }

            if (filled == 64)
            {
                Transform(state, block);
                filled = 0;
            }
        }

        // Padding
        block[filled++] = 0x80;
        if (filled > 56)
        {
            while (filled < 64)
                block[filled++] = 0;
            Transform(state, block);
            filled = 0;
        }

        while (filled < 56)
            block[filled++] = 0;

        WriteBe64(block + 56, totalBits);
        Transform(state, block);

        for (int i = 0; i < 8; ++i)
            WriteBe32(OutDigest + i * 4, state[i]);

        // best-effort scrub
        QC::String::memset(block, 0, sizeof(block));
        QC::String::memset(state, 0, sizeof(state));
    }
}
