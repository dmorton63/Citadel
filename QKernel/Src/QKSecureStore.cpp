#include "QKSecureStore.h"

#include "QFSVFS.h"
#include "QFSFile.h"
#include "QKEntropy.h"
#include "QCString.h"
#include "QCSha256.h"

namespace
{

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

    static inline QC::u32 rotl32(QC::u32 v, int n)
    {
        return (v << n) | (v >> (32 - n));
    }

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
        QC::u32 st[16];
        st[0] = 0x61707865u;
        st[1] = 0x3320646eu;
        st[2] = 0x79622d32u;
        st[3] = 0x6b206574u;
        for (int i = 0; i < 8; ++i)
            st[4 + i] = load_le32(key + i * 4);
        st[12] = counter;
        st[13] = load_le32(nonce + 0);
        st[14] = load_le32(nonce + 4);
        st[15] = load_le32(nonce + 8);

        QC::u32 w[16];
        for (int i = 0; i < 16; ++i)
            w[i] = st[i];

        for (int i = 0; i < 10; ++i)
        {
            quarterRound(w[0], w[4], w[8], w[12]);
            quarterRound(w[1], w[5], w[9], w[13]);
            quarterRound(w[2], w[6], w[10], w[14]);
            quarterRound(w[3], w[7], w[11], w[15]);

            quarterRound(w[0], w[5], w[10], w[15]);
            quarterRound(w[1], w[6], w[11], w[12]);
            quarterRound(w[2], w[7], w[8], w[13]);
            quarterRound(w[3], w[4], w[9], w[14]);
        }

        for (int i = 0; i < 16; ++i)
            store_le32(out + i * 4, w[i] + st[i]);
    }

    static void chacha20Xor(const QC::u8 key[32], QC::u32 counter, const QC::u8 nonce[12], QC::u8 *data, QC::usize size)
    {
        QC::u8 block[64];
        QC::usize off = 0;
        QC::u32 ctr = counter;
        while (off < size)
        {
            chacha20Block(key, ctr++, nonce, block);
            const QC::usize n = (size - off) < 64 ? (size - off) : 64;
            for (QC::usize i = 0; i < n; ++i)
                data[off + i] ^= block[i];
            off += n;
        }
        QC::String::memset(block, 0, sizeof(block));
    }

    // Poly1305 (RFC 8439) - 16-byte tag
    static inline QC::u32 load_le32_u8(const QC::u8 *p)
    {
        return load_le32(p);
    }

    static inline QC::u64 load_le64(const QC::u8 *p)
    {
        return static_cast<QC::u64>(p[0]) |
               (static_cast<QC::u64>(p[1]) << 8) |
               (static_cast<QC::u64>(p[2]) << 16) |
               (static_cast<QC::u64>(p[3]) << 24) |
               (static_cast<QC::u64>(p[4]) << 32) |
               (static_cast<QC::u64>(p[5]) << 40) |
               (static_cast<QC::u64>(p[6]) << 48) |
               (static_cast<QC::u64>(p[7]) << 56);
    }

    static inline void store_le64(QC::u8 *p, QC::u64 v)
    {
        for (int i = 0; i < 8; ++i)
            p[i] = static_cast<QC::u8>((v >> (i * 8)) & 0xFF);
    }

    static void poly1305Mac(const QC::u8 key[32],
                            const QC::u8 *m,
                            QC::usize mlen,
                            QC::u8 tag[16])
    {
        // Poly1305 with 26-bit limbs (RFC 8439). r is clamped.
        const QC::u64 t0 = load_le32_u8(key + 0);
        const QC::u64 t1 = load_le32_u8(key + 4);
        const QC::u64 t2 = load_le32_u8(key + 8);
        const QC::u64 t3 = load_le32_u8(key + 12);

        QC::u64 r0 = (t0) & 0x3ffffff;
        QC::u64 r1 = ((t0 >> 26) | (t1 << 6)) & 0x3ffff03;
        QC::u64 r2 = ((t1 >> 20) | (t2 << 12)) & 0x3ffc0ff;
        QC::u64 r3 = ((t2 >> 14) | (t3 << 18)) & 0x3f03fff;
        QC::u64 r4 = ((t3 >> 8)) & 0x00fffff;

        QC::u64 s1 = r1 * 5;
        QC::u64 s2 = r2 * 5;
        QC::u64 s3 = r3 * 5;
        QC::u64 s4 = r4 * 5;

        QC::u64 h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;

        while (mlen > 0)
        {
            QC::u8 block[16];
            const QC::usize n = mlen < 16 ? mlen : 16;
            QC::String::memset(block, 0, sizeof(block));
            for (QC::usize i = 0; i < n; ++i)
                block[i] = m[i];

            // Append 1 bit (as a byte) for partial blocks; full blocks use hibit.
            const QC::u64 hibit = (n == 16) ? (1ULL << 24) : 0;
            if (n < 16)
                block[n] = 1;

            const QC::u64 t0 = load_le32_u8(block + 0);
            const QC::u64 t1 = load_le32_u8(block + 4);
            const QC::u64 t2 = load_le32_u8(block + 8);
            const QC::u64 t3 = load_le32_u8(block + 12);

            h0 += t0 & 0x3ffffff;
            h1 += ((t0 >> 26) | (t1 << 6)) & 0x3ffffff;
            h2 += ((t1 >> 20) | (t2 << 12)) & 0x3ffffff;
            h3 += ((t2 >> 14) | (t3 << 18)) & 0x3ffffff;
            h4 += ((t3 >> 8) & 0x3ffffff) | hibit;

            QC::u64 d0 = h0 * r0 + h1 * s4 + h2 * s3 + h3 * s2 + h4 * s1;
            QC::u64 d1 = h0 * r1 + h1 * r0 + h2 * s4 + h3 * s3 + h4 * s2;
            QC::u64 d2 = h0 * r2 + h1 * r1 + h2 * r0 + h3 * s4 + h4 * s3;
            QC::u64 d3 = h0 * r3 + h1 * r2 + h2 * r1 + h3 * r0 + h4 * s4;
            QC::u64 d4 = h0 * r4 + h1 * r3 + h2 * r2 + h3 * r1 + h4 * r0;

            QC::u64 c;
            c = (d0 >> 26);
            h0 = d0 & 0x3ffffff;
            d1 += c;
            c = (d1 >> 26);
            h1 = d1 & 0x3ffffff;
            d2 += c;
            c = (d2 >> 26);
            h2 = d2 & 0x3ffffff;
            d3 += c;
            c = (d3 >> 26);
            h3 = d3 & 0x3ffffff;
            d4 += c;
            c = (d4 >> 26);
            h4 = d4 & 0x3ffffff;
            h0 += c * 5;
            c = h0 >> 26;
            h0 &= 0x3ffffff;
            h1 += c;

            QC::String::memset(block, 0, sizeof(block));

            m += n;
            mlen -= n;
        }

        // Final reduction
        QC::u64 c;
        c = h1 >> 26;
        h1 &= 0x3ffffff;
        h2 += c;
        c = h2 >> 26;
        h2 &= 0x3ffffff;
        h3 += c;
        c = h3 >> 26;
        h3 &= 0x3ffffff;
        h4 += c;
        c = h4 >> 26;
        h4 &= 0x3ffffff;
        h0 += c * 5;
        c = h0 >> 26;
        h0 &= 0x3ffffff;
        h1 += c;

        QC::u64 g0 = h0 + 5;
        c = g0 >> 26;
        g0 &= 0x3ffffff;
        QC::u64 g1 = h1 + c;
        c = g1 >> 26;
        g1 &= 0x3ffffff;
        QC::u64 g2 = h2 + c;
        c = g2 >> 26;
        g2 &= 0x3ffffff;
        QC::u64 g3 = h3 + c;
        c = g3 >> 26;
        g3 &= 0x3ffffff;
        QC::u64 g4 = h4 + c - (1ULL << 26);

        const QC::u64 mask = (g4 >> 63) - 1;
        g0 &= mask;
        g1 &= mask;
        g2 &= mask;
        g3 &= mask;
        g4 &= mask;
        const QC::u64 nmask = ~mask;
        h0 = (h0 & nmask) | g0;
        h1 = (h1 & nmask) | g1;
        h2 = (h2 & nmask) | g2;
        h3 = (h3 & nmask) | g3;
        h4 = (h4 & nmask) | g4;

        // Serialize into 32-bit words and add pad (second half of key).
        QC::u64 f0 = (h0) | (h1 << 26);
        QC::u64 f1 = (h1 >> 6) | (h2 << 20);
        QC::u64 f2 = (h2 >> 12) | (h3 << 14);
        QC::u64 f3 = (h3 >> 18) | (h4 << 8);

        const QC::u64 p0 = load_le32_u8(key + 16);
        const QC::u64 p1 = load_le32_u8(key + 20);
        const QC::u64 p2 = load_le32_u8(key + 24);
        const QC::u64 p3 = load_le32_u8(key + 28);

        QC::u64 g;
        g = (f0 & 0xffffffffULL) + p0;
        const QC::u32 o0 = static_cast<QC::u32>(g & 0xffffffffULL);
        g >>= 32;
        g += (f1 & 0xffffffffULL) + p1;
        const QC::u32 o1 = static_cast<QC::u32>(g & 0xffffffffULL);
        g >>= 32;
        g += (f2 & 0xffffffffULL) + p2;
        const QC::u32 o2 = static_cast<QC::u32>(g & 0xffffffffULL);
        g >>= 32;
        g += (f3 & 0xffffffffULL) + p3;
        const QC::u32 o3 = static_cast<QC::u32>(g & 0xffffffffULL);

        store_le32(tag + 0, o0);
        store_le32(tag + 4, o1);
        store_le32(tag + 8, o2);
        store_le32(tag + 12, o3);
    }

    static void poly1305TagForAead(const QC::u8 aeadKey[32],
                                   const QC::u8 *aad,
                                   QC::usize aadLen,
                                   const QC::u8 *cipher,
                                   QC::usize cipherLen,
                                   QC::u8 tag[16])
    {
        // Build MAC input: aad || pad16 || cipher || pad16 || aadLen(64) || cipherLen(64)
        QC::Vector<QC::u8> buf;
        const QC::usize padA = (16 - (aadLen % 16)) % 16;
        const QC::usize padC = (16 - (cipherLen % 16)) % 16;

        const QC::usize total = aadLen + padA + cipherLen + padC + 16;
        buf.resize(total);
        QC::usize o = 0;
        for (QC::usize i = 0; i < aadLen; ++i)
            buf[o++] = aad[i];
        for (QC::usize i = 0; i < padA; ++i)
            buf[o++] = 0;
        for (QC::usize i = 0; i < cipherLen; ++i)
            buf[o++] = cipher[i];
        for (QC::usize i = 0; i < padC; ++i)
            buf[o++] = 0;
        // lengths (little-endian 64-bit each)
        QC::u8 lenBlock[16];
        store_le64(lenBlock + 0, static_cast<QC::u64>(aadLen));
        store_le64(lenBlock + 8, static_cast<QC::u64>(cipherLen));
        for (int i = 0; i < 16; ++i)
            buf[o++] = lenBlock[i];

        poly1305Mac(aeadKey, buf.data(), buf.size(), tag);
        // best-effort wipe
        QC::String::memset(buf.data(), 0, buf.size());
        buf.clear();
    }

    static bool constantTimeEq16(const QC::u8 a[16], const QC::u8 b[16])
    {
        QC::u8 diff = 0;
        for (int i = 0; i < 16; ++i)
            diff |= static_cast<QC::u8>(a[i] ^ b[i]);
        return diff == 0;
    }

    static void secureZero(void *ptr, QC::usize len)
    {
        volatile QC::u8 *p = reinterpret_cast<volatile QC::u8 *>(ptr);
        while (len--)
            *p++ = 0;
    }

    constexpr const char *kWrapKeyPlainName = "WRAPKEY.BIN";
    constexpr const char *kWrapKeyKdfName = "WRAPKEY.KDF";
    constexpr const char *kDefaultHiddenBaseDir = "/system/.sc";
    constexpr const char *kLegacyBaseDir = "/system/sc";
    constexpr const char *kCompatBaseDir = "/system";

    constexpr QC::u8 kWrapKeyKdfMagic[4] = {'W', 'K', 'D', '1'};
    constexpr QC::u32 kWrapKeyKdfVersion = 1;
    constexpr QC::u32 kWrapKeyKdfDefaultIterations = 50000;

    static bool g_nonTpmDerivedKeySet = false;
    static QC::u8 g_nonTpmDerivedKey[32];

    static bool isDefaultHiddenBaseDir(const char *baseDir)
    {
        return baseDir && QC::String::strcmp(baseDir, kDefaultHiddenBaseDir) == 0;
    }

    static const char *resolveBaseDirForPath(const QK::SecureStore::Config &cfg)
    {
        const char *baseDir = cfg.baseDir ? cfg.baseDir : kDefaultHiddenBaseDir;
        if (!isDefaultHiddenBaseDir(baseDir))
            return baseDir;

        QFS::FileInfo info;
        QFS::VFS &vfs = QFS::VFS::instance();
        if (vfs.stat(baseDir, &info) == QC::Status::Success && info.type == QFS::FileType::Directory)
            return baseDir;

        if (vfs.stat(kLegacyBaseDir, &info) == QC::Status::Success && info.type == QFS::FileType::Directory)
            return kLegacyBaseDir;

        if (vfs.stat(kCompatBaseDir, &info) == QC::Status::Success && info.type == QFS::FileType::Directory)
            return kCompatBaseDir;

        return baseDir;
    }

    static void hmacSha256(const QC::u8 *key, QC::usize keyLen,
                           const QC::u8 *data, QC::usize dataLen,
                           QC::u8 outDigest[32])
    {
        constexpr QC::usize kBlockSize = 64;
        constexpr QC::usize kMaxDataLen = 128;

        if (!outDigest)
            return;

        if (dataLen > kMaxDataLen)
        {
            QC::String::memset(outDigest, 0, 32);
            return;
        }

        QC::u8 keyBlock[kBlockSize];
        QC::String::memset(keyBlock, 0, sizeof(keyBlock));

        if (keyLen > kBlockSize)
        {
            QC::u8 keyHash[32];
            QC::Sha256(key, keyLen, keyHash);
            QC::String::memcpy(keyBlock, keyHash, sizeof(keyHash));
            secureZero(keyHash, sizeof(keyHash));
        }
        else
        {
            QC::String::memcpy(keyBlock, key, keyLen);
        }

        QC::u8 ipad[kBlockSize];
        QC::u8 opad[kBlockSize];
        for (QC::usize i = 0; i < kBlockSize; ++i)
        {
            ipad[i] = static_cast<QC::u8>(keyBlock[i] ^ 0x36);
            opad[i] = static_cast<QC::u8>(keyBlock[i] ^ 0x5c);
        }

        QC::u8 innerDigest[32];
        QC::u8 inner[kBlockSize + kMaxDataLen];
        QC::String::memcpy(inner, ipad, kBlockSize);
        if (dataLen)
            QC::String::memcpy(inner + kBlockSize, data, dataLen);
        QC::Sha256(inner, kBlockSize + dataLen, innerDigest);

        QC::u8 outer[kBlockSize + sizeof(innerDigest)];
        QC::String::memcpy(outer, opad, kBlockSize);
        QC::String::memcpy(outer + kBlockSize, innerDigest, sizeof(innerDigest));
        QC::Sha256(outer, sizeof(outer), outDigest);

        secureZero(keyBlock, sizeof(keyBlock));
        secureZero(ipad, sizeof(ipad));
        secureZero(opad, sizeof(opad));
        secureZero(inner, sizeof(inner));
        secureZero(innerDigest, sizeof(innerDigest));
        secureZero(outer, sizeof(outer));
    }

    static void pbkdf2HmacSha256_32(const QC::u8 *password, QC::usize passwordLen,
                                   const QC::u8 *salt, QC::usize saltLen,
                                   QC::u32 iterations,
                                   QC::u8 outKey[32])
    {
        if (!outKey)
            return;

        if (iterations == 0)
            iterations = 1;

        // One-block PBKDF2 for 32-byte output.
        // U1 = HMAC(P, S || INT_32_BE(1))
        QC::u8 msg[64];
        QC::usize mlen = 0;
        if (salt && saltLen)
        {
            const QC::usize copyLen = (saltLen > (sizeof(msg) - 4)) ? (sizeof(msg) - 4) : saltLen;
            QC::String::memcpy(msg, salt, copyLen);
            mlen = copyLen;
        }
        msg[mlen + 0] = 0;
        msg[mlen + 1] = 0;
        msg[mlen + 2] = 0;
        msg[mlen + 3] = 1;
        mlen += 4;

        QC::u8 u[32];
        hmacSha256(password, passwordLen, msg, mlen, u);
        QC::u8 t[32];
        QC::String::memcpy(t, u, sizeof(t));

        for (QC::u32 i = 2; i <= iterations; ++i)
        {
            QC::u8 uNext[32];
            hmacSha256(password, passwordLen, u, sizeof(u), uNext);
            for (int j = 0; j < 32; ++j)
                t[j] = static_cast<QC::u8>(t[j] ^ uNext[j]);
            QC::String::memcpy(u, uNext, sizeof(u));
            secureZero(uNext, sizeof(uNext));
        }

        QC::String::memcpy(outKey, t, 32);
        secureZero(msg, sizeof(msg));
        secureZero(u, sizeof(u));
        secureZero(t, sizeof(t));
    }

    static QC::usize normalizeRecoveryCode(const char *in, char *out, QC::usize outCap)
    {
        if (!out || outCap == 0)
            return 0;
        out[0] = '\0';
        if (!in)
            return 0;

        QC::usize o = 0;
        for (const char *p = in; *p; ++p)
        {
            const char c = *p;
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '-')
                continue;
            if (o + 1 >= outCap)
                break;
            out[o++] = c;
        }
        out[o] = '\0';
        return o;
    }

    static QC::Status wrapWrapKeyKdfBlob(const QC::u8 derivedKey[32],
                                        QC::u32 iterations,
                                        const QC::u8 salt[16],
                                        const QC::u8 nonce[12],
                                        const QC::u8 wrapKey[32],
                                        QC::u8 outBlob[88])
    {
        if (!derivedKey || !salt || !nonce || !wrapKey || !outBlob)
            return QC::Status::InvalidParam;

        // Header (AAD): magic(4) + version(4) + iterations(4) + salt(16) + nonce(12) = 40 bytes
        QC::u8 aad[40];
        aad[0] = kWrapKeyKdfMagic[0];
        aad[1] = kWrapKeyKdfMagic[1];
        aad[2] = kWrapKeyKdfMagic[2];
        aad[3] = kWrapKeyKdfMagic[3];
        store_le32(aad + 4, kWrapKeyKdfVersion);
        store_le32(aad + 8, iterations);
        for (int i = 0; i < 16; ++i)
            aad[12 + i] = salt[i];
        for (int i = 0; i < 12; ++i)
            aad[28 + i] = nonce[i];

        // Ciphertext of wrapKey.
        QC::u8 cipher[32];
        for (int i = 0; i < 32; ++i)
            cipher[i] = wrapKey[i];
        chacha20Xor(derivedKey, 1, nonce, cipher, sizeof(cipher));

        // Poly1305 key derived from chacha20 block counter=0.
        QC::u8 polyKeyBlock[64];
        chacha20Block(derivedKey, 0, nonce, polyKeyBlock);
        QC::u8 polyKey[32];
        for (int i = 0; i < 32; ++i)
            polyKey[i] = polyKeyBlock[i];

        QC::u8 tag[16];
        poly1305TagForAead(polyKey, aad, sizeof(aad), cipher, sizeof(cipher), tag);

        // Serialize: aad(40) + tag(16) + cipher(32) = 88.
        for (int i = 0; i < 40; ++i)
            outBlob[i] = aad[i];
        for (int i = 0; i < 16; ++i)
            outBlob[40 + i] = tag[i];
        for (int i = 0; i < 32; ++i)
            outBlob[56 + i] = cipher[i];

        secureZero(aad, sizeof(aad));
        secureZero(cipher, sizeof(cipher));
        secureZero(polyKeyBlock, sizeof(polyKeyBlock));
        secureZero(polyKey, sizeof(polyKey));
        secureZero(tag, sizeof(tag));
        return QC::Status::Success;
    }

    static QC::Status unwrapWrapKeyKdfBlob(const QC::u8 derivedKey[32],
                                          const QC::u8 *blob,
                                          QC::usize blobSize,
                                          QC::u8 outWrapKey[32])
    {
        if (!derivedKey || !blob || blobSize != 88 || !outWrapKey)
            return QC::Status::InvalidParam;

        // Validate header fields.
        if (blob[0] != kWrapKeyKdfMagic[0] || blob[1] != kWrapKeyKdfMagic[1] ||
            blob[2] != kWrapKeyKdfMagic[2] || blob[3] != kWrapKeyKdfMagic[3])
            return QC::Status::Error;

        const QC::u32 ver = load_le32(blob + 4);
        if (ver != kWrapKeyKdfVersion)
            return QC::Status::Error;

        const QC::u8 *aad = blob;
        const QC::u8 *tag = blob + 40;
        const QC::u8 *cipher = blob + 56;

        QC::u8 polyKeyBlock[64];
        const QC::u8 *nonce = blob + 28;
        chacha20Block(derivedKey, 0, nonce, polyKeyBlock);
        QC::u8 polyKey[32];
        for (int i = 0; i < 32; ++i)
            polyKey[i] = polyKeyBlock[i];

        QC::u8 calcTag[16];
        poly1305TagForAead(polyKey, aad, 40, cipher, 32, calcTag);
        if (!constantTimeEq16(calcTag, tag))
        {
            secureZero(polyKeyBlock, sizeof(polyKeyBlock));
            secureZero(polyKey, sizeof(polyKey));
            secureZero(calcTag, sizeof(calcTag));
            return QC::Status::Error;
        }

        QC::u8 plain[32];
        for (int i = 0; i < 32; ++i)
            plain[i] = cipher[i];
        chacha20Xor(derivedKey, 1, nonce, plain, sizeof(plain));
        for (int i = 0; i < 32; ++i)
            outWrapKey[i] = plain[i];

        secureZero(polyKeyBlock, sizeof(polyKeyBlock));
        secureZero(polyKey, sizeof(polyKey));
        secureZero(calcTag, sizeof(calcTag));
        secureZero(plain, sizeof(plain));
        return QC::Status::Success;
    }

    static QC::Status loadWrapKey(QC::u8 outKey[32], const QK::SecureStore::Config &cfg, bool allowCreate)
    {
        constexpr const char *kWrapKeyTpmName = "WRAPKEY.TPM";

        const bool tpmEnabled = (cfg.tpmSealWrapKey != nullptr) && (cfg.tpmUnsealWrapKey != nullptr);

        if (tpmEnabled)
        {
            QC::Vector<QC::u8> blob;
            QC::Status st = QK::SecureStore::readBlob(kWrapKeyTpmName, blob, cfg);
            if (st == QC::Status::Success)
            {
                st = cfg.tpmUnsealWrapKey(cfg.tpmUser, blob, outKey, 32);
                // best-effort wipe
                if (blob.size() > 0)
                    QC::String::memset(blob.data(), 0, blob.size());
                blob.clear();
                return st;
            }

            if (st != QC::Status::NotFound)
                return st;

            if (!allowCreate)
                return QC::Status::NotFound;

            // No TPM blob yet: generate wrap key, seal it, and persist blob.
            st = QK::Entropy::fillRandom(outKey, 32);
            if (st != QC::Status::Success && st != QC::Status::Busy)
                return st;

            QC::Vector<QC::u8> outBlob;
            st = cfg.tpmSealWrapKey(cfg.tpmUser, outKey, 32, outBlob);
            if (st != QC::Status::Success)
            {
                if (outBlob.size() > 0)
                    QC::String::memset(outBlob.data(), 0, outBlob.size());
                outBlob.clear();
                return st;
            }

            st = QK::SecureStore::writeBlob(kWrapKeyTpmName, outBlob.data(), outBlob.size(), cfg);
            // best-effort wipe blob contents
            if (outBlob.size() > 0)
                QC::String::memset(outBlob.data(), 0, outBlob.size());
            outBlob.clear();

            // best-effort: delete legacy plaintext wrap key if present.
            (void)QK::SecureStore::removeBlob(kWrapKeyPlainName, cfg);
            return st;
        }

        // Non-TPM path: wrap key must be protected under a recovery-code derived key.
        QC::Vector<QC::u8> kdfBlob;
        QC::Status st = QK::SecureStore::readBlob(kWrapKeyKdfName, kdfBlob, cfg);
        if (st == QC::Status::Success)
        {
            if (!g_nonTpmDerivedKeySet)
            {
                if (kdfBlob.size() > 0)
                    QC::String::memset(kdfBlob.data(), 0, kdfBlob.size());
                kdfBlob.clear();
                return QC::Status::Busy;
            }

            if (kdfBlob.size() != 88)
            {
                if (kdfBlob.size() > 0)
                    QC::String::memset(kdfBlob.data(), 0, kdfBlob.size());
                kdfBlob.clear();
                return QC::Status::Error;
            }

            st = unwrapWrapKeyKdfBlob(g_nonTpmDerivedKey, kdfBlob.data(), kdfBlob.size(), outKey);
            if (kdfBlob.size() > 0)
                QC::String::memset(kdfBlob.data(), 0, kdfBlob.size());
            kdfBlob.clear();
            return st;
        }

        if (st != QC::Status::NotFound)
            return st;

        (void)allowCreate;
        return QC::Status::NotFound;
    }

    static const char *findChar(const char *s, char c)
    {
        if (!s)
            return nullptr;
        for (; *s; ++s)
        {
            if (*s == c)
                return s;
        }
        return nullptr;
    }

    static bool containsDotDot(const char *s)
    {
        if (!s)
            return false;
        char prev = 0;
        for (; *s; ++s)
        {
            if (prev == '.' && *s == '.')
                return true;
            prev = *s;
        }
        return false;
    }

    static QC::Status appendTo(char *dst, QC::usize dstSize, const char *src)
    {
        if (!dst || dstSize == 0 || !src)
            return QC::Status::InvalidParam;

        QC::usize dlen = QC::String::strlen(dst);
        QC::usize slen = QC::String::strlen(src);
        if (dlen + slen + 1 > dstSize)
            return QC::Status::InvalidParam;

        for (QC::usize i = 0; i < slen; ++i)
        {
            dst[dlen + i] = src[i];
        }
        dst[dlen + slen] = '\0';
        return QC::Status::Success;
    }

    static bool isValid83Key(const char *key)
    {
        if (!key || key[0] == '\0')
            return false;

        // Disallow any path separators or traversal.
        for (const char *p = key; *p; ++p)
        {
            if (*p == '/' || *p == '\\')
                return false;
        }
        if (containsDotDot(key))
            return false;

        // FAT 8.3: NAME(1-8)[.EXT(1-3)]
        const char *dot = findChar(key, '.');
        if (!dot)
        {
            const QC::usize len = QC::String::strlen(key);
            return len >= 1 && len <= 8;
        }

        const QC::usize nameLen = static_cast<QC::usize>(dot - key);
        const QC::usize extLen = QC::String::strlen(dot + 1);

        if (nameLen < 1 || nameLen > 8)
            return false;
        if (extLen < 1 || extLen > 3)
            return false;

        // Only a single dot.
        return findChar(dot + 1, '.') == nullptr;
    }

    static QC::Status buildPath(char *out, QC::usize outSize, const char *baseDir, const char *key)
    {
        if (!out || outSize == 0 || !baseDir || !key)
            return QC::Status::InvalidParam;

        const QC::usize baseLen = QC::String::strlen(baseDir);
        const QC::usize keyLen = QC::String::strlen(key);
        const bool baseHasSlash = baseLen > 0 && baseDir[baseLen - 1] == '/';

        const QC::usize needed = baseLen + (baseHasSlash ? 0 : 1) + keyLen + 1;
        if (needed > outSize)
            return QC::Status::InvalidParam;

        QC::String::strncpy(out, baseDir, outSize - 1);
        out[outSize - 1] = '\0';

        if (!baseHasSlash)
        {
            QC::Status st = appendTo(out, outSize, "/");
            if (st != QC::Status::Success)
                return st;
        }
        {
            QC::Status st = appendTo(out, outSize, key);
            if (st != QC::Status::Success)
                return st;
        }

        return QC::Status::Success;
    }

    static QC::Status ensureDirRecursive(const char *absPath)
    {
        if (!absPath || absPath[0] != '/')
            return QC::Status::InvalidParam;

        // Build up: /A, /A/B, /A/B/C
        char buf[256];
        QC::String::strncpy(buf, absPath, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        QFS::VFS &vfs = QFS::VFS::instance();

        char partial[256];
        partial[0] = '/';
        partial[1] = '\0';

        const char *p = buf + 1; // skip leading '/'
        while (*p)
        {
            // Extract next component.
            char comp[64];
            QC::usize ci = 0;
            while (*p && *p != '/' && ci + 1 < sizeof(comp))
            {
                comp[ci++] = *p++;
            }
            comp[ci] = '\0';

            while (*p == '/')
                ++p;

            if (comp[0] == '\0')
                continue;

            // Append to partial.
            if (!(partial[0] == '/' && partial[1] == '\0'))
            {
                if (QC::String::strlen(partial) + 1 >= sizeof(partial))
                    return QC::Status::InvalidParam;
                QC::Status st = appendTo(partial, sizeof(partial), "/");
                if (st != QC::Status::Success)
                    return st;
            }
            if (QC::String::strlen(partial) + QC::String::strlen(comp) + 1 >= sizeof(partial))
                return QC::Status::InvalidParam;
            {
                QC::Status st = appendTo(partial, sizeof(partial), comp);
                if (st != QC::Status::Success)
                    return st;
            }

            QFS::FileInfo info;
            QC::Status st = vfs.stat(partial, &info);
            if (st == QC::Status::Success)
            {
                if (info.type != QFS::FileType::Directory)
                    return QC::Status::Error;
                continue;
            }

            if (st == QC::Status::NotFound)
            {
                st = vfs.createDir(partial);
                if (st != QC::Status::Success)
                {
                    // If it raced into existence, accept it.
                    QC::Status st2 = vfs.stat(partial, &info);
                    if (st2 == QC::Status::Success && info.type == QFS::FileType::Directory)
                        continue;
                    return st;
                }
                continue;
            }

            return st;
        }

        return QC::Status::Success;
    }

    static QC::Status writeAll(QFS::File *file, const void *data, QC::usize size)
    {
        if (!file || !data)
            return QC::Status::InvalidParam;

        const QC::u8 *p = static_cast<const QC::u8 *>(data);
        QC::usize remaining = size;
        while (remaining > 0)
        {
            const QC::usize chunk = remaining;
            QC::isize wrote = file->write(p, chunk);
            if (wrote <= 0)
                return QC::Status::Error;
            p += static_cast<QC::usize>(wrote);
            remaining -= static_cast<QC::usize>(wrote);
        }
        return QC::Status::Success;
    }

    static QC::Status readAll(QFS::File *file, QC::Vector<QC::u8> &out)
    {
        if (!file)
            return QC::Status::InvalidParam;

        const QC::u64 fileSize64 = file->size();
        if (fileSize64 > static_cast<QC::u64>(static_cast<QC::usize>(-1)))
            return QC::Status::OutOfMemory;

        const QC::usize fileSize = static_cast<QC::usize>(fileSize64);
        out.clear();
        out.resize(fileSize);

        QC::usize offset = 0;
        while (offset < fileSize)
        {
            QC::isize got = file->read(out.data() + offset, fileSize - offset);
            if (got <= 0)
                return QC::Status::Error;
            offset += static_cast<QC::usize>(got);
        }

        return QC::Status::Success;
    }

} // namespace

namespace QK
{

    namespace SecureStore
    {

        static Config g_defaultCfg = Config{.baseDir = kDefaultHiddenBaseDir};

        Config defaultConfig()
        {
            return g_defaultCfg;
        }

        bool tpm_present(const Config &cfg)
        {
            return (cfg.tpmSealWrapKey != nullptr) && (cfg.tpmUnsealWrapKey != nullptr);
        }

        void setDefaultConfig(const Config &cfg)
        {
            g_defaultCfg = cfg;
            if (!g_defaultCfg.baseDir)
                g_defaultCfg.baseDir = kDefaultHiddenBaseDir;
        }

        QC::Status ensureBaseDir(const Config &cfg)
        {
            const char *baseDir = cfg.baseDir ? cfg.baseDir : kDefaultHiddenBaseDir;
            QC::Status st = ensureDirRecursive(baseDir);
            if (st == QC::Status::NotSupported && isDefaultHiddenBaseDir(baseDir))
            {
                const QC::Status legacySt = ensureDirRecursive(kLegacyBaseDir);
                if (legacySt == QC::Status::Success)
                    return QC::Status::Success;

                if (legacySt == QC::Status::NotSupported)
                {
                    QFS::FileInfo info;
                    if (QFS::VFS::instance().stat(kCompatBaseDir, &info) == QC::Status::Success &&
                        info.type == QFS::FileType::Directory)
                    {
                        return QC::Status::Success;
                    }
                }

                return legacySt;
            }
            return st;
        }

        QC::Status writeBlob(const char *key, const void *data, QC::usize size, const Config &cfg)
        {
            if (!isValid83Key(key) || (!data && size != 0))
                return QC::Status::InvalidParam;

            QC::Status st = ensureBaseDir(cfg);
            if (st != QC::Status::Success)
                return st;

            const char *baseDir = resolveBaseDirForPath(cfg);
            char path[256];
            st = buildPath(path, sizeof(path), baseDir, key);
            if (st != QC::Status::Success)
                return st;

            QFS::VFS &vfs = QFS::VFS::instance();
            QFS::File *file = vfs.open(path, QFS::OpenMode::Write | QFS::OpenMode::Create | QFS::OpenMode::Truncate);
            if (!file)
                return QC::Status::Error;

            st = (size == 0) ? QC::Status::Success : writeAll(file, data, size);
            (void)file->sync();
            vfs.close(file);
            return st;
        }

        QC::Status readBlob(const char *key, QC::Vector<QC::u8> &out, const Config &cfg)
        {
            if (!isValid83Key(key))
                return QC::Status::InvalidParam;

            const char *baseDir = resolveBaseDirForPath(cfg);
            char path[256];
            QC::Status st = buildPath(path, sizeof(path), baseDir, key);
            if (st != QC::Status::Success)
                return st;

            QFS::VFS &vfs = QFS::VFS::instance();
            QFS::File *file = vfs.open(path, QFS::OpenMode::Read);
            if (!file)
            {
                // Compatibility path: attempt legacy non-hidden SC storage location.
                if (isDefaultHiddenBaseDir(baseDir))
                {
                    char legacyPath[256];
                    if (buildPath(legacyPath, sizeof(legacyPath), kLegacyBaseDir, key) == QC::Status::Success)
                        file = vfs.open(legacyPath, QFS::OpenMode::Read);
                }
                if (!file)
                    return QC::Status::NotFound;
            }

            st = readAll(file, out);
            vfs.close(file);
            return st;
        }

        QC::Status readWrapKey(QC::u8 outWrapKey[32], const Config &cfg)
        {
            if (!outWrapKey)
                return QC::Status::InvalidParam;
            return loadWrapKey(outWrapKey, cfg, false);
        }

        QC::Status getOrCreateWrapKey(QC::u8 outWrapKey[32], const Config &cfg)
        {
            if (!outWrapKey)
                return QC::Status::InvalidParam;
            return loadWrapKey(outWrapKey, cfg, true);
        }

        QC::Status readTas(QC::u8 outTas[32], const Config &cfg)
        {
            return readWrapKey(outTas, cfg);
        }

        QC::Status getOrCreateTas(QC::u8 outTas[32], const Config &cfg)
        {
            return getOrCreateWrapKey(outTas, cfg);
        }

        QC::Status nonTpmUnlockOrInitializeWrapKey(const char *recoveryCode, const Config &cfg)
        {
            if (tpm_present(cfg))
                return QC::Status::NotSupported;

            char norm[96];
            QC::String::memset(norm, 0, sizeof(norm));
            const QC::usize normLen = normalizeRecoveryCode(recoveryCode, norm, sizeof(norm));
            if (normLen == 0)
                return QC::Status::InvalidParam;

            // If KDF-wrapped key exists, validate recovery code by decrypting it.
            if (exists(kWrapKeyKdfName, cfg))
            {
                QC::Vector<QC::u8> blob;
                QC::Status st = readBlob(kWrapKeyKdfName, blob, cfg);
                if (st != QC::Status::Success)
                    return st;
                if (blob.size() != 88)
                {
                    if (blob.size() > 0)
                        QC::String::memset(blob.data(), 0, blob.size());
                    blob.clear();
                    return QC::Status::Error;
                }

                // Parse salt + iterations from header.
                if (blob[0] != kWrapKeyKdfMagic[0] || blob[1] != kWrapKeyKdfMagic[1] ||
                    blob[2] != kWrapKeyKdfMagic[2] || blob[3] != kWrapKeyKdfMagic[3])
                {
                    QC::String::memset(blob.data(), 0, blob.size());
                    blob.clear();
                    return QC::Status::Error;
                }

                const QC::u32 ver = load_le32(blob.data() + 4);
                if (ver != kWrapKeyKdfVersion)
                {
                    QC::String::memset(blob.data(), 0, blob.size());
                    blob.clear();
                    return QC::Status::Error;
                }

                const QC::u32 iterations = load_le32(blob.data() + 8);
                const QC::u8 *salt = blob.data() + 12;

                QC::u8 derived[32];
                QC::String::memset(derived, 0, sizeof(derived));
                pbkdf2HmacSha256_32(reinterpret_cast<const QC::u8 *>(norm), normLen,
                                   salt, 16,
                                   iterations,
                                   derived);

                QC::u8 wrapKeyTest[32];
                QC::String::memset(wrapKeyTest, 0, sizeof(wrapKeyTest));
                st = unwrapWrapKeyKdfBlob(derived, blob.data(), blob.size(), wrapKeyTest);
                secureZero(wrapKeyTest, sizeof(wrapKeyTest));

                if (blob.size() > 0)
                    QC::String::memset(blob.data(), 0, blob.size());
                blob.clear();
                secureZero(norm, sizeof(norm));

                if (st != QC::Status::Success)
                {
                    secureZero(derived, sizeof(derived));
                    return st;
                }

                for (int i = 0; i < 32; ++i)
                    g_nonTpmDerivedKey[i] = derived[i];
                g_nonTpmDerivedKeySet = true;
                secureZero(derived, sizeof(derived));
                return QC::Status::Success;
            }

            // No KDF file yet: create/migrate it.
            QC::Status st = ensureBaseDir(cfg);
            if (st != QC::Status::Success)
                return st;

            QC::u8 wrapKey[32];
            QC::String::memset(wrapKey, 0, sizeof(wrapKey));

            if (exists(kWrapKeyPlainName, cfg))
            {
                QC::Vector<QC::u8> keyBuf;
                st = readBlob(kWrapKeyPlainName, keyBuf, cfg);
                if (st != QC::Status::Success)
                    return st;
                if (keyBuf.size() != 32)
                {
                    if (keyBuf.size() > 0)
                        QC::String::memset(keyBuf.data(), 0, keyBuf.size());
                    keyBuf.clear();
                    return QC::Status::Error;
                }
                for (int i = 0; i < 32; ++i)
                    wrapKey[i] = keyBuf[i];
                if (keyBuf.size() > 0)
                    QC::String::memset(keyBuf.data(), 0, keyBuf.size());
                keyBuf.clear();
            }
            else
            {
                st = QK::Entropy::fillRandom(wrapKey, sizeof(wrapKey));
                if (st != QC::Status::Success && st != QC::Status::Busy)
                    return st;
            }

            QC::u8 salt[16];
            QC::u8 nonce[12];
            QC::Status stSalt = QK::Entropy::fillRandom(salt, sizeof(salt));
            QC::Status stNonce = QK::Entropy::fillRandom(nonce, sizeof(nonce));
            if ((stSalt != QC::Status::Success && stSalt != QC::Status::Busy) ||
                (stNonce != QC::Status::Success && stNonce != QC::Status::Busy))
            {
                secureZero(wrapKey, sizeof(wrapKey));
                secureZero(salt, sizeof(salt));
                secureZero(nonce, sizeof(nonce));
                return QC::Status::Error;
            }

            QC::u8 derived[32];
            QC::String::memset(derived, 0, sizeof(derived));
            pbkdf2HmacSha256_32(reinterpret_cast<const QC::u8 *>(norm), normLen,
                               salt, sizeof(salt),
                               kWrapKeyKdfDefaultIterations,
                               derived);

            QC::u8 blobOut[88];
            QC::String::memset(blobOut, 0, sizeof(blobOut));
            st = wrapWrapKeyKdfBlob(derived, kWrapKeyKdfDefaultIterations, salt, nonce, wrapKey, blobOut);
            if (st != QC::Status::Success)
            {
                secureZero(wrapKey, sizeof(wrapKey));
                secureZero(salt, sizeof(salt));
                secureZero(nonce, sizeof(nonce));
                secureZero(derived, sizeof(derived));
                secureZero(norm, sizeof(norm));
                secureZero(blobOut, sizeof(blobOut));
                return st;
            }

            st = writeBlob(kWrapKeyKdfName, blobOut, sizeof(blobOut), cfg);
            secureZero(blobOut, sizeof(blobOut));
            secureZero(salt, sizeof(salt));
            secureZero(nonce, sizeof(nonce));
            secureZero(wrapKey, sizeof(wrapKey));
            secureZero(norm, sizeof(norm));

            if (st != QC::Status::Success)
            {
                secureZero(derived, sizeof(derived));
                return st;
            }

            // Best-effort: remove legacy plaintext key.
            (void)removeBlob(kWrapKeyPlainName, cfg);

            for (int i = 0; i < 32; ++i)
                g_nonTpmDerivedKey[i] = derived[i];
            g_nonTpmDerivedKeySet = true;
            secureZero(derived, sizeof(derived));
            return QC::Status::Success;
        }

        QC::Status writeSealedBlob(const char *key, const void *data, QC::usize size, const Config &cfg)
        {
            if (!isValid83Key(key) || (!data && size != 0))
                return QC::Status::InvalidParam;

            // Build sealed payload: magic(4) + ver(4) + plainSize(4) + nonce(12) + tag(16) + cipher(size)
            constexpr QC::u8 kMagic[4] = {'S', 'S', 'B', '1'};
            constexpr QC::u32 kVersion = 1;
            const QC::usize headerSize = 4 + 4 + 4 + 12 + 16;

            QC::Vector<QC::u8> sealed;
            sealed.resize(headerSize + size);

            QC::u8 *hdr = sealed.data();
            hdr[0] = kMagic[0];
            hdr[1] = kMagic[1];
            hdr[2] = kMagic[2];
            hdr[3] = kMagic[3];
            store_le32(hdr + 4, kVersion);
            store_le32(hdr + 8, static_cast<QC::u32>(size));

            QC::u8 *nonce = hdr + 12;
            QC::Status st = QK::Entropy::fillRandom(nonce, 12);
            if (st != QC::Status::Success && st != QC::Status::Busy)
                return st;

            QC::u8 wrapKey[32];
            st = getOrCreateWrapKey(wrapKey, cfg);
            if (st != QC::Status::Success)
                return st;

            // Copy plaintext into ciphertext area then encrypt in place.
            QC::u8 *cipher = hdr + headerSize;
            const QC::u8 *plain = static_cast<const QC::u8 *>(data);
            for (QC::usize i = 0; i < size; ++i)
                cipher[i] = plain[i];

            // AEAD key for Poly1305: chacha20 block counter=0
            QC::u8 polyKeyBlock[64];
            chacha20Block(wrapKey, 0, nonce, polyKeyBlock);
            QC::u8 polyKey[32];
            for (int i = 0; i < 32; ++i)
                polyKey[i] = polyKeyBlock[i];

            // Encrypt using counter=1
            chacha20Xor(wrapKey, 1, nonce, cipher, size);

            // Tag over AAD = magic||ver||plainSize||nonce
            const QC::u8 *aad = hdr;
            const QC::usize aadLen = 4 + 4 + 4 + 12;
            QC::u8 tag[16];
            poly1305TagForAead(polyKey, aad, aadLen, cipher, size, tag);

            // Store tag
            QC::u8 *tagOut = hdr + 24;
            for (int i = 0; i < 16; ++i)
                tagOut[i] = tag[i];

            // Wipe sensitive temporaries.
            QC::String::memset(wrapKey, 0, sizeof(wrapKey));
            QC::String::memset(polyKeyBlock, 0, sizeof(polyKeyBlock));
            QC::String::memset(polyKey, 0, sizeof(polyKey));
            QC::String::memset(tag, 0, sizeof(tag));

            st = writeBlob(key, sealed.data(), sealed.size(), cfg);
            QC::String::memset(sealed.data(), 0, sealed.size());
            sealed.clear();
            return st;
        }

        QC::Status readSealedBlob(const char *key, QC::Vector<QC::u8> &out, const Config &cfg)
        {
            if (!isValid83Key(key))
                return QC::Status::InvalidParam;

            QC::Vector<QC::u8> sealed;
            QC::Status st = readBlob(key, sealed, cfg);
            if (st != QC::Status::Success)
                return st;

            constexpr QC::usize headerSize = 4 + 4 + 4 + 12 + 16;
            if (sealed.size() < headerSize)
                return QC::Status::Error;

            const QC::u8 *hdr = sealed.data();
            if (!(hdr[0] == 'S' && hdr[1] == 'S' && hdr[2] == 'B' && hdr[3] == '1'))
                return QC::Status::Error;

            const QC::u32 ver = load_le32(hdr + 4);
            if (ver != 1)
                return QC::Status::NotSupported;

            const QC::u32 plainSize = load_le32(hdr + 8);
            if (sealed.size() != headerSize + static_cast<QC::usize>(plainSize))
                return QC::Status::Error;

            const QC::u8 *nonce = hdr + 12;
            const QC::u8 *tagIn = hdr + 24;
            QC::u8 wrapKey[32];
            st = getOrCreateWrapKey(wrapKey, cfg);
            if (st != QC::Status::Success)
                return st;

            QC::u8 polyKeyBlock[64];
            chacha20Block(wrapKey, 0, nonce, polyKeyBlock);
            QC::u8 polyKey[32];
            for (int i = 0; i < 32; ++i)
                polyKey[i] = polyKeyBlock[i];

            const QC::u8 *aad = hdr;
            const QC::usize aadLen = 4 + 4 + 4 + 12;
            const QC::u8 *cipher = hdr + headerSize;
            QC::u8 tagCalc[16];
            poly1305TagForAead(polyKey, aad, aadLen, cipher, plainSize, tagCalc);

            if (!constantTimeEq16(tagIn, tagCalc))
            {
                QC::String::memset(wrapKey, 0, sizeof(wrapKey));
                QC::String::memset(polyKeyBlock, 0, sizeof(polyKeyBlock));
                QC::String::memset(polyKey, 0, sizeof(polyKey));
                QC::String::memset(tagCalc, 0, sizeof(tagCalc));
                return QC::Status::Error;
            }

            out.clear();
            out.resize(plainSize);
            for (QC::usize i = 0; i < plainSize; ++i)
                out[i] = cipher[i];

            // Decrypt in place (out buffer)
            chacha20Xor(wrapKey, 1, nonce, out.data(), plainSize);

            QC::String::memset(wrapKey, 0, sizeof(wrapKey));
            QC::String::memset(polyKeyBlock, 0, sizeof(polyKeyBlock));
            QC::String::memset(polyKey, 0, sizeof(polyKey));
            QC::String::memset(tagCalc, 0, sizeof(tagCalc));

            QC::String::memset(sealed.data(), 0, sealed.size());
            sealed.clear();
            return QC::Status::Success;
        }

        QC::Status writeTpmSealedBlob(const char *key, const void *data, QC::usize size, const Config &cfg)
        {
            if (!isValid83Key(key) || (!data && size != 0))
                return QC::Status::InvalidParam;

            // If TPM secret sealing isn't wired, fall back to the existing sealed blob path.
            if (!cfg.tpmSealSecret || !cfg.tpmUnsealSecret)
                return writeSealedBlob(key, data, size, cfg);

            // Header: magic(4) + ver(4) + plainSize(4) + sealedKeySize(4) + nonce(12) + tag(16)
            constexpr QC::u8 kMagic[4] = {'T', 'S', 'B', '1'};
            constexpr QC::u32 kVersion = 1;
            const QC::usize headerSize = 4 + 4 + 4 + 4 + 12 + 16;

            // Generate per-blob content key and seal it via TPM policy.
            QC::u8 contentKey[32];
            QC::String::memset(contentKey, 0, sizeof(contentKey));
            QC::Status st = QK::Entropy::fillRandom(contentKey, sizeof(contentKey));
            if (st != QC::Status::Success && st != QC::Status::Busy)
                return st;

            QC::Vector<QC::u8> sealedKey;
            st = seal_secret(contentKey, sizeof(contentKey), sealedKey, cfg);
            if (st != QC::Status::Success)
            {
                QC::String::memset(contentKey, 0, sizeof(contentKey));
                return st;
            }

            if (sealedKey.size() == 0 || sealedKey.size() > 4096)
            {
                QC::String::memset(contentKey, 0, sizeof(contentKey));
                if (sealedKey.size() > 0)
                    QC::String::memset(sealedKey.data(), 0, sealedKey.size());
                sealedKey.clear();
                return QC::Status::Error;
            }

            QC::Vector<QC::u8> blob;
            blob.resize(headerSize + sealedKey.size() + size);

            QC::u8 *hdr = blob.data();
            hdr[0] = kMagic[0];
            hdr[1] = kMagic[1];
            hdr[2] = kMagic[2];
            hdr[3] = kMagic[3];
            store_le32(hdr + 4, kVersion);
            store_le32(hdr + 8, static_cast<QC::u32>(size));
            store_le32(hdr + 12, static_cast<QC::u32>(sealedKey.size()));

            QC::u8 *nonce = hdr + 16;
            st = QK::Entropy::fillRandom(nonce, 12);
            if (st != QC::Status::Success && st != QC::Status::Busy)
            {
                QC::String::memset(contentKey, 0, sizeof(contentKey));
                QC::String::memset(blob.data(), 0, blob.size());
                blob.clear();
                QC::String::memset(sealedKey.data(), 0, sealedKey.size());
                sealedKey.clear();
                return st;
            }

            // Copy sealedKey and plaintext into the blob.
            QC::usize off = headerSize;
            for (QC::usize i = 0; i < sealedKey.size(); ++i)
                hdr[off + i] = sealedKey[i];
            off += sealedKey.size();

            QC::u8 *cipher = hdr + off;
            const QC::u8 *plain = static_cast<const QC::u8 *>(data);
            for (QC::usize i = 0; i < size; ++i)
                cipher[i] = plain[i];

            // AEAD key for Poly1305: chacha20 block counter=0
            QC::u8 polyKeyBlock[64];
            chacha20Block(contentKey, 0, nonce, polyKeyBlock);
            QC::u8 polyKey[32];
            for (int i = 0; i < 32; ++i)
                polyKey[i] = polyKeyBlock[i];

            // Encrypt using counter=1
            chacha20Xor(contentKey, 1, nonce, cipher, size);

            // Tag over AAD = magic||ver||plainSize||sealedKeySize||nonce
            const QC::u8 *aad = hdr;
            const QC::usize aadLen = 4 + 4 + 4 + 4 + 12;
            QC::u8 tag[16];
            poly1305TagForAead(polyKey, aad, aadLen, cipher, size, tag);

            // Store tag
            QC::u8 *tagOut = hdr + 28;
            for (int i = 0; i < 16; ++i)
                tagOut[i] = tag[i];

            st = writeBlob(key, blob.data(), blob.size(), cfg);

            QC::String::memset(contentKey, 0, sizeof(contentKey));
            QC::String::memset(polyKeyBlock, 0, sizeof(polyKeyBlock));
            QC::String::memset(polyKey, 0, sizeof(polyKey));
            QC::String::memset(tag, 0, sizeof(tag));
            if (sealedKey.size() > 0)
                QC::String::memset(sealedKey.data(), 0, sealedKey.size());
            sealedKey.clear();
            if (blob.size() > 0)
                QC::String::memset(blob.data(), 0, blob.size());
            blob.clear();
            return st;
        }

        QC::Status readTpmSealedBlob(const char *key, QC::Vector<QC::u8> &out, const Config &cfg)
        {
            if (!isValid83Key(key))
                return QC::Status::InvalidParam;

            QC::Vector<QC::u8> blob;
            QC::Status st = readBlob(key, blob, cfg);
            if (st != QC::Status::Success)
                return st;

            if (blob.size() < 4)
                return QC::Status::Error;

            // If this isn't a TPM-accelerated blob, fall back to legacy sealed blob read.
            if (!(blob[0] == 'T' && blob[1] == 'S' && blob[2] == 'B' && blob[3] == '1'))
            {
                if (blob.size() > 0)
                    QC::String::memset(blob.data(), 0, blob.size());
                blob.clear();
                return readSealedBlob(key, out, cfg);
            }

            // Need TPM secret unseal to read TPM-accelerated blobs.
            if (!cfg.tpmUnsealSecret)
            {
                if (blob.size() > 0)
                    QC::String::memset(blob.data(), 0, blob.size());
                blob.clear();
                return QC::Status::NotSupported;
            }

            constexpr QC::usize headerSize = 4 + 4 + 4 + 4 + 12 + 16;
            if (blob.size() < headerSize)
                return QC::Status::Error;

            const QC::u8 *hdr = blob.data();
            const QC::u32 ver = load_le32(hdr + 4);
            if (ver != 1)
                return QC::Status::NotSupported;

            const QC::u32 plainSize32 = load_le32(hdr + 8);
            const QC::u32 sealedKeySize32 = load_le32(hdr + 12);
            const QC::usize plainSize = static_cast<QC::usize>(plainSize32);
            const QC::usize sealedKeySize = static_cast<QC::usize>(sealedKeySize32);

            if (sealedKeySize == 0 || sealedKeySize > 4096)
                return QC::Status::Error;

            const QC::usize expected = headerSize + sealedKeySize + plainSize;
            if (blob.size() != expected)
                return QC::Status::Error;

            const QC::u8 *nonce = hdr + 16;
            const QC::u8 *tagIn = hdr + 28;
            const QC::u8 *sealedKey = hdr + headerSize;
            const QC::u8 *cipher = sealedKey + sealedKeySize;

            QC::Vector<QC::u8> sealedKeyVec;
            sealedKeyVec.resize(sealedKeySize);
            for (QC::usize i = 0; i < sealedKeySize; ++i)
                sealedKeyVec[i] = sealedKey[i];

            QC::u8 contentKey[32];
            QC::String::memset(contentKey, 0, sizeof(contentKey));
            QC::usize contentKeyLen = 0;
            st = unseal_secret(sealedKeyVec, contentKey, sizeof(contentKey), &contentKeyLen, cfg);

            if (sealedKeyVec.size() > 0)
                QC::String::memset(sealedKeyVec.data(), 0, sealedKeyVec.size());
            sealedKeyVec.clear();

            if (st != QC::Status::Success)
            {
                QC::String::memset(contentKey, 0, sizeof(contentKey));
                if (blob.size() > 0)
                    QC::String::memset(blob.data(), 0, blob.size());
                blob.clear();
                return st;
            }
            if (contentKeyLen != 32)
            {
                QC::String::memset(contentKey, 0, sizeof(contentKey));
                if (blob.size() > 0)
                    QC::String::memset(blob.data(), 0, blob.size());
                blob.clear();
                return QC::Status::Error;
            }

            QC::u8 polyKeyBlock[64];
            chacha20Block(contentKey, 0, nonce, polyKeyBlock);
            QC::u8 polyKey[32];
            for (int i = 0; i < 32; ++i)
                polyKey[i] = polyKeyBlock[i];

            const QC::u8 *aad = hdr;
            const QC::usize aadLen = 4 + 4 + 4 + 4 + 12;
            QC::u8 tagCalc[16];
            poly1305TagForAead(polyKey, aad, aadLen, cipher, plainSize, tagCalc);

            if (!constantTimeEq16(tagIn, tagCalc))
            {
                QC::String::memset(contentKey, 0, sizeof(contentKey));
                QC::String::memset(polyKeyBlock, 0, sizeof(polyKeyBlock));
                QC::String::memset(polyKey, 0, sizeof(polyKey));
                QC::String::memset(tagCalc, 0, sizeof(tagCalc));
                if (blob.size() > 0)
                    QC::String::memset(blob.data(), 0, blob.size());
                blob.clear();
                return QC::Status::Error;
            }

            out.clear();
            out.resize(plainSize);
            for (QC::usize i = 0; i < plainSize; ++i)
                out[i] = cipher[i];
            chacha20Xor(contentKey, 1, nonce, out.data(), plainSize);

            QC::String::memset(contentKey, 0, sizeof(contentKey));
            QC::String::memset(polyKeyBlock, 0, sizeof(polyKeyBlock));
            QC::String::memset(polyKey, 0, sizeof(polyKey));
            QC::String::memset(tagCalc, 0, sizeof(tagCalc));

            if (blob.size() > 0)
                QC::String::memset(blob.data(), 0, blob.size());
            blob.clear();
            return QC::Status::Success;
        }

        QC::Status removeBlob(const char *key, const Config &cfg)
        {
            if (!isValid83Key(key))
                return QC::Status::InvalidParam;

            const char *baseDir = resolveBaseDirForPath(cfg);
            char path[256];
            QC::Status st = buildPath(path, sizeof(path), baseDir, key);
            if (st != QC::Status::Success)
                return st;

            st = QFS::VFS::instance().remove(path);
            if (st == QC::Status::NotFound && isDefaultHiddenBaseDir(baseDir))
            {
                char legacyPath[256];
                if (buildPath(legacyPath, sizeof(legacyPath), kLegacyBaseDir, key) == QC::Status::Success)
                    return QFS::VFS::instance().remove(legacyPath);
            }
            return st;
        }

        bool exists(const char *key, const Config &cfg)
        {
            if (!isValid83Key(key))
                return false;

            const char *baseDir = resolveBaseDirForPath(cfg);
            char path[256];
            if (buildPath(path, sizeof(path), baseDir, key) != QC::Status::Success)
                return false;

            if (QFS::VFS::instance().exists(path))
                return true;

            if (isDefaultHiddenBaseDir(baseDir))
            {
                char legacyPath[256];
                if (buildPath(legacyPath, sizeof(legacyPath), kLegacyBaseDir, key) == QC::Status::Success)
                    return QFS::VFS::instance().exists(legacyPath);
            }

            return false;
        }

        QC::Status seal_secret(const void *secret,
                               QC::usize secretLen,
                               QC::Vector<QC::u8> &outSealedBlob,
                               const Config &cfg)
        {
            if (!secret || secretLen == 0)
                return QC::Status::InvalidParam;

            if (!cfg.tpmSealSecret)
                return QC::Status::NotSupported;

            outSealedBlob.clear();
            return cfg.tpmSealSecret(cfg.tpmUser, reinterpret_cast<const QC::u8 *>(secret), secretLen, outSealedBlob);
        }

        QC::Status unseal_secret(const QC::Vector<QC::u8> &sealedBlob,
                                 QC::u8 *outSecret,
                                 QC::usize outSecretCap,
                                 QC::usize *outSecretLen,
                                 const Config &cfg)
        {
            if (!outSecret || !outSecretLen)
                return QC::Status::InvalidParam;

            if (!cfg.tpmUnsealSecret)
                return QC::Status::NotSupported;

            *outSecretLen = 0;
            return cfg.tpmUnsealSecret(cfg.tpmUser, sealedBlob, outSecret, outSecretCap, outSecretLen);
        }

    } // namespace SecureStore

} // namespace QK
