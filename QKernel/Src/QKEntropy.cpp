#include "QKEntropy.h"

#include "QCString.h"

namespace
{

    static inline QC::u64 rdtsc()
    {
        QC::u32 lo = 0, hi = 0;
        asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
        return (static_cast<QC::u64>(hi) << 32) | lo;
    }

    static inline QC::u32 rotl32(QC::u32 v, int n)
    {
        return (v << n) | (v >> (32 - n));
    }

    static inline QC::u32 load_le32(const QC::u8 *p)
    {
        return static_cast<QC::u32>(p[0]) |
               (static_cast<QC::u32>(p[1]) << 8) |
               (static_cast<QC::u32>(p[2]) << 16) |
               (static_cast<QC::u32>(p[3]) << 24);
    }

    static inline void store_le32(QC::u8 *p, QC::u32 v)
    {
        p[0] = static_cast<QC::u8>(v & 0xFF);
        p[1] = static_cast<QC::u8>((v >> 8) & 0xFF);
        p[2] = static_cast<QC::u8>((v >> 16) & 0xFF);
        p[3] = static_cast<QC::u8>((v >> 24) & 0xFF);
    }

    struct ChaCha20State
    {
        QC::u32 s[16];
    };

    static inline void quarterRound(QC::u32 &a, QC::u32 &b, QC::u32 &c, QC::u32 &d)
    {
        a += b;
        d ^= a;
        d = rotl32(d, 16);

        c += d;
        b ^= c;
        b = rotl32(b, 12);

        a += b;
        d ^= a;
        d = rotl32(d, 8);

        c += d;
        b ^= c;
        b = rotl32(b, 7);
    }

    static void chacha20Block(const QC::u8 key[32], QC::u32 counter, const QC::u8 nonce[12], QC::u8 out[64])
    {
        // RFC 8439 constants: "expand 32-byte k"
        ChaCha20State st;
        st.s[0] = 0x61707865u;
        st.s[1] = 0x3320646eu;
        st.s[2] = 0x79622d32u;
        st.s[3] = 0x6b206574u;

        for (int i = 0; i < 8; ++i)
            st.s[4 + i] = load_le32(key + i * 4);

        st.s[12] = counter;
        st.s[13] = load_le32(nonce + 0);
        st.s[14] = load_le32(nonce + 4);
        st.s[15] = load_le32(nonce + 8);

        ChaCha20State w = st;

        for (int i = 0; i < 10; ++i)
        {
            // Column rounds
            quarterRound(w.s[0], w.s[4], w.s[8], w.s[12]);
            quarterRound(w.s[1], w.s[5], w.s[9], w.s[13]);
            quarterRound(w.s[2], w.s[6], w.s[10], w.s[14]);
            quarterRound(w.s[3], w.s[7], w.s[11], w.s[15]);

            // Diagonal rounds
            quarterRound(w.s[0], w.s[5], w.s[10], w.s[15]);
            quarterRound(w.s[1], w.s[6], w.s[11], w.s[12]);
            quarterRound(w.s[2], w.s[7], w.s[8], w.s[13]);
            quarterRound(w.s[3], w.s[4], w.s[9], w.s[14]);
        }

        for (int i = 0; i < 16; ++i)
        {
            const QC::u32 v = w.s[i] + st.s[i];
            store_le32(out + i * 4, v);
        }
    }

    static void chacha20Xor(const QC::u8 key[32], QC::u32 counter, const QC::u8 nonce[12], QC::u8 *data, QC::usize size)
    {
        QC::u8 block[64];
        QC::usize offset = 0;
        QC::u32 ctr = counter;
        while (offset < size)
        {
            chacha20Block(key, ctr, nonce, block);
            ++ctr;

            const QC::usize n = (size - offset) < 64 ? (size - offset) : 64;
            for (QC::usize i = 0; i < n; ++i)
            {
                data[offset + i] ^= block[i];
            }
            offset += n;
        }

        // Wipe keystream block.
        QC::String::memset(block, 0, sizeof(block));
    }

    struct EntropyState
    {
        bool seeded;
        QC::u8 key[32];
        QC::u8 nonce[12];
        QC::u32 counter;
    };

    static EntropyState g_state = {
        .seeded = false,
        .key = {0},
        .nonce = {0},
        .counter = 1,
    };

    static void rekey()
    {
        // Generate a fresh key by encrypting 64 zero bytes.
        QC::u8 tmp[64];
        QC::String::memset(tmp, 0, sizeof(tmp));
        chacha20Xor(g_state.key, g_state.counter++, g_state.nonce, tmp, sizeof(tmp));
        for (int i = 0; i < 32; ++i)
            g_state.key[i] = tmp[i];
        for (int i = 0; i < 12; ++i)
            g_state.nonce[i] ^= tmp[32 + i];
        QC::String::memset(tmp, 0, sizeof(tmp));
    }

    static void stirBootJitter()
    {
        // Very small early-boot jitter: mix a few timestamp reads.
        QC::u64 t[8];
        for (int i = 0; i < 8; ++i)
            t[i] = rdtsc();

        // XOR into key.
        for (int i = 0; i < 8; ++i)
        {
            const QC::u8 *p = reinterpret_cast<const QC::u8 *>(&t[i]);
            for (int j = 0; j < 8; ++j)
                g_state.key[(i * 8 + j) % 32] ^= p[j];
        }

        rekey();
    }

} // namespace

namespace QK
{

    namespace Entropy
    {

        void addEntropy(const void *data, QC::usize size)
        {
            if (!data || size == 0)
                return;

            const QC::u8 *p = static_cast<const QC::u8 *>(data);
            for (QC::usize i = 0; i < size; ++i)
            {
                g_state.key[i % 32] ^= p[i];
                g_state.nonce[i % 12] ^= static_cast<QC::u8>(p[i] + static_cast<QC::u8>(i));
            }

            g_state.seeded = true;
            rekey();
        }

        QC::Status fillRandom(void *out, QC::usize size)
        {
            if (!out && size != 0)
                return QC::Status::InvalidParam;

            if (!g_state.seeded)
            {
                // Still provide output, but stir some boot-time jitter first.
                stirBootJitter();
            }

            QC::u8 *dst = static_cast<QC::u8 *>(out);
            QC::usize offset = 0;
            while (offset < size)
            {
                QC::u8 block[64];
                chacha20Block(g_state.key, g_state.counter++, g_state.nonce, block);

                const QC::usize n = (size - offset) < 64 ? (size - offset) : 64;
                for (QC::usize i = 0; i < n; ++i)
                    dst[offset + i] = block[i];

                QC::String::memset(block, 0, sizeof(block));
                offset += n;
            }

            // Rekey after generating output to limit backtracking.
            rekey();

            return g_state.seeded ? QC::Status::Success : QC::Status::Busy;
        }

        bool isSeeded()
        {
            return g_state.seeded;
        }

    } // namespace Entropy

} // namespace QK
