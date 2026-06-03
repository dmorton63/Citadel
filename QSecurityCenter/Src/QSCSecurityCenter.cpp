#include "QSCSecurityCenter.h"

#include "QQExecutor.h"

#include "QCSha256.h"
#include "QCString.h"

namespace QSC
{

    namespace
    {
        static void secureZero(void *ptr, QC::usize len)
        {
            volatile QC::u8 *p = reinterpret_cast<volatile QC::u8 *>(ptr);
            while (len--)
                *p++ = 0;
        }

#pragma pack(push, 1)
        struct WrappedSstV1
        {
            QC::u32 magic;
            QC::u16 version;
            QC::u16 reserved;
            QC::u64 generation;
            QC::u8 nonce[12];
            QC::u8 reserved2[4];
            QC::u8 cipher[32];
            QC::u8 tag[16];
        };
#pragma pack(pop)

        static constexpr QC::u32 kWrappedSstMagic = 0x54535357; // 'WSST'
        static constexpr QC::u16 kWrappedSstVersion = 1;

        static bool constTimeEq(const QC::u8 *a, const QC::u8 *b, QC::usize n)
        {
            QC::u8 v = 0;
            for (QC::usize i = 0; i < n; ++i)
                v |= static_cast<QC::u8>(a[i] ^ b[i]);
            return v == 0;
        }

        // Minimal ChaCha20 + Poly1305 (RFC 8439) used for wrapped SST.
        struct ChaCha20State
        {
            QC::u32 st[16];
        };

        static inline QC::u32 rotl32(QC::u32 x, int b)
        {
            return (x << b) | (x >> (32 - b));
        }

        static inline void qround(QC::u32 &a, QC::u32 &b, QC::u32 &c, QC::u32 &d)
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
            ChaCha20State st;
            st.st[0] = 0x61707865;
            st.st[1] = 0x3320646e;
            st.st[2] = 0x79622d32;
            st.st[3] = 0x6b206574;

            for (int i = 0; i < 8; ++i)
            {
                st.st[4 + i] = static_cast<QC::u32>(key[i * 4]) |
                               (static_cast<QC::u32>(key[i * 4 + 1]) << 8) |
                               (static_cast<QC::u32>(key[i * 4 + 2]) << 16) |
                               (static_cast<QC::u32>(key[i * 4 + 3]) << 24);
            }

            st.st[12] = counter;
            st.st[13] = static_cast<QC::u32>(nonce[0]) | (static_cast<QC::u32>(nonce[1]) << 8) |
                        (static_cast<QC::u32>(nonce[2]) << 16) | (static_cast<QC::u32>(nonce[3]) << 24);
            st.st[14] = static_cast<QC::u32>(nonce[4]) | (static_cast<QC::u32>(nonce[5]) << 8) |
                        (static_cast<QC::u32>(nonce[6]) << 16) | (static_cast<QC::u32>(nonce[7]) << 24);
            st.st[15] = static_cast<QC::u32>(nonce[8]) | (static_cast<QC::u32>(nonce[9]) << 8) |
                        (static_cast<QC::u32>(nonce[10]) << 16) | (static_cast<QC::u32>(nonce[11]) << 24);

            QC::u32 w[16];
            for (int i = 0; i < 16; ++i)
                w[i] = st.st[i];

            for (int i = 0; i < 10; ++i)
            {
                qround(w[0], w[4], w[8], w[12]);
                qround(w[1], w[5], w[9], w[13]);
                qround(w[2], w[6], w[10], w[14]);
                qround(w[3], w[7], w[11], w[15]);

                qround(w[0], w[5], w[10], w[15]);
                qround(w[1], w[6], w[11], w[12]);
                qround(w[2], w[7], w[8], w[13]);
                qround(w[3], w[4], w[9], w[14]);
            }

            for (int i = 0; i < 16; ++i)
            {
                QC::u32 v = w[i] + st.st[i];
                out[i * 4 + 0] = static_cast<QC::u8>(v & 0xFF);
                out[i * 4 + 1] = static_cast<QC::u8>((v >> 8) & 0xFF);
                out[i * 4 + 2] = static_cast<QC::u8>((v >> 16) & 0xFF);
                out[i * 4 + 3] = static_cast<QC::u8>((v >> 24) & 0xFF);
            }

            secureZero(&st, sizeof(st));
            secureZero(w, sizeof(w));
        }

        static void chacha20Xor(const QC::u8 key[32], QC::u32 counter, const QC::u8 nonce[12], QC::u8 *data, QC::usize size)
        {
            QC::u8 block[64];
            QC::usize off = 0;
            QC::u32 ctr = counter;
            while (off < size)
            {
                chacha20Block(key, ctr++, nonce, block);
                QC::usize n = size - off;
                if (n > sizeof(block))
                    n = sizeof(block);
                for (QC::usize i = 0; i < n; ++i)
                    data[off + i] ^= block[i];
                off += n;
            }
            secureZero(block, sizeof(block));
        }

        static void poly1305Mac(const QC::u8 key[32], const QC::u8 *msg, QC::usize msgLen, QC::u8 outTag[16])
        {
            // Poly1305 with 26-bit limbs (RFC 8439). r is clamped.
            QC::u32 r0 = (static_cast<QC::u32>(key[0]) | (static_cast<QC::u32>(key[1]) << 8) |
                          (static_cast<QC::u32>(key[2]) << 16) | (static_cast<QC::u32>(key[3]) << 24)) & 0x3ffffff;
            QC::u32 r1 = ((static_cast<QC::u32>(key[3]) >> 2) | (static_cast<QC::u32>(key[4]) << 6) |
                          (static_cast<QC::u32>(key[5]) << 14) | (static_cast<QC::u32>(key[6]) << 22)) & 0x3ffff03;
            QC::u32 r2 = ((static_cast<QC::u32>(key[6]) >> 4) | (static_cast<QC::u32>(key[7]) << 4) |
                          (static_cast<QC::u32>(key[8]) << 12) | (static_cast<QC::u32>(key[9]) << 20)) & 0x3ffc0ff;
            QC::u32 r3 = ((static_cast<QC::u32>(key[9]) >> 6) | (static_cast<QC::u32>(key[10]) << 2) |
                          (static_cast<QC::u32>(key[11]) << 10) | (static_cast<QC::u32>(key[12]) << 18)) & 0x3f03fff;
            QC::u32 r4 = ((static_cast<QC::u32>(key[12]) >> 8) | (static_cast<QC::u32>(key[13]) << 0) |
                          (static_cast<QC::u32>(key[14]) << 8) | (static_cast<QC::u32>(key[15]) << 16)) & 0x00fffff;

            QC::u64 h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;

            const QC::u8 *p = msg;
            QC::usize remaining = msgLen;
            while (remaining > 0)
            {
                QC::u8 block[16] = {0};
                QC::usize n = remaining > 16 ? 16 : remaining;
                for (QC::usize i = 0; i < n; ++i)
                    block[i] = p[i];
                block[n] = 1; // append 1

                QC::u64 t0 = static_cast<QC::u64>(block[0]) | (static_cast<QC::u64>(block[1]) << 8) |
                             (static_cast<QC::u64>(block[2]) << 16) | (static_cast<QC::u64>(block[3]) << 24);
                QC::u64 t1 = static_cast<QC::u64>(block[4]) | (static_cast<QC::u64>(block[5]) << 8) |
                             (static_cast<QC::u64>(block[6]) << 16) | (static_cast<QC::u64>(block[7]) << 24);
                QC::u64 t2 = static_cast<QC::u64>(block[8]) | (static_cast<QC::u64>(block[9]) << 8) |
                             (static_cast<QC::u64>(block[10]) << 16) | (static_cast<QC::u64>(block[11]) << 24);
                QC::u64 t3 = static_cast<QC::u64>(block[12]) | (static_cast<QC::u64>(block[13]) << 8) |
                             (static_cast<QC::u64>(block[14]) << 16) | (static_cast<QC::u64>(block[15]) << 24);

                h0 += t0 & 0x3ffffff;
                h1 += ((t0 >> 26) | (t1 << 6)) & 0x3ffffff;
                h2 += ((t1 >> 20) | (t2 << 12)) & 0x3ffffff;
                h3 += ((t2 >> 14) | (t3 << 18)) & 0x3ffffff;
                h4 += (t3 >> 8);

                QC::u64 d0 = h0 * r0 + h1 * (5ULL * r4) + h2 * (5ULL * r3) + h3 * (5ULL * r2) + h4 * (5ULL * r1);
                QC::u64 d1 = h0 * r1 + h1 * r0 + h2 * (5ULL * r4) + h3 * (5ULL * r3) + h4 * (5ULL * r2);
                QC::u64 d2 = h0 * r2 + h1 * r1 + h2 * r0 + h3 * (5ULL * r4) + h4 * (5ULL * r3);
                QC::u64 d3 = h0 * r3 + h1 * r2 + h2 * r1 + h3 * r0 + h4 * (5ULL * r4);
                QC::u64 d4 = h0 * r4 + h1 * r3 + h2 * r2 + h3 * r1 + h4 * r0;

                QC::u64 c = d0 >> 26;
                h0 = d0 & 0x3ffffff;
                d1 += c;
                c = d1 >> 26;
                h1 = d1 & 0x3ffffff;
                d2 += c;
                c = d2 >> 26;
                h2 = d2 & 0x3ffffff;
                d3 += c;
                c = d3 >> 26;
                h3 = d3 & 0x3ffffff;
                d4 += c;
                c = d4 >> 26;
                h4 = d4 & 0x3ffffff;
                h0 += c * 5;
                c = h0 >> 26;
                h0 &= 0x3ffffff;
                h1 += c;

                secureZero(block, sizeof(block));

                p += n;
                remaining -= n;
            }

            // final reduction
            QC::u64 c = h1 >> 26;
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

            QC::u64 mask = (g4 >> 63) - 1;
            h0 = (h0 & ~mask) | (g0 & mask);
            h1 = (h1 & ~mask) | (g1 & mask);
            h2 = (h2 & ~mask) | (g2 & mask);
            h3 = (h3 & ~mask) | (g3 & mask);
            h4 = (h4 & ~mask) | (g4 & mask);

            // serialize + add s (second half of key)
            QC::u64 f0 = (h0 | (h1 << 26)) +
                         (static_cast<QC::u64>(key[16]) | (static_cast<QC::u64>(key[17]) << 8) |
                          (static_cast<QC::u64>(key[18]) << 16) | (static_cast<QC::u64>(key[19]) << 24));
            QC::u64 f1 = ((h1 >> 6) | (h2 << 20)) +
                         (static_cast<QC::u64>(key[20]) | (static_cast<QC::u64>(key[21]) << 8) |
                          (static_cast<QC::u64>(key[22]) << 16) | (static_cast<QC::u64>(key[23]) << 24));
            QC::u64 f2 = ((h2 >> 12) | (h3 << 14)) +
                         (static_cast<QC::u64>(key[24]) | (static_cast<QC::u64>(key[25]) << 8) |
                          (static_cast<QC::u64>(key[26]) << 16) | (static_cast<QC::u64>(key[27]) << 24));
            QC::u64 f3 = ((h3 >> 18) | (h4 << 8)) +
                         (static_cast<QC::u64>(key[28]) | (static_cast<QC::u64>(key[29]) << 8) |
                          (static_cast<QC::u64>(key[30]) << 16) | (static_cast<QC::u64>(key[31]) << 24));

            outTag[0] = static_cast<QC::u8>(f0 & 0xFF);
            outTag[1] = static_cast<QC::u8>((f0 >> 8) & 0xFF);
            outTag[2] = static_cast<QC::u8>((f0 >> 16) & 0xFF);
            outTag[3] = static_cast<QC::u8>((f0 >> 24) & 0xFF);
            f1 += (f0 >> 32);
            outTag[4] = static_cast<QC::u8>(f1 & 0xFF);
            outTag[5] = static_cast<QC::u8>((f1 >> 8) & 0xFF);
            outTag[6] = static_cast<QC::u8>((f1 >> 16) & 0xFF);
            outTag[7] = static_cast<QC::u8>((f1 >> 24) & 0xFF);
            f2 += (f1 >> 32);
            outTag[8] = static_cast<QC::u8>(f2 & 0xFF);
            outTag[9] = static_cast<QC::u8>((f2 >> 8) & 0xFF);
            outTag[10] = static_cast<QC::u8>((f2 >> 16) & 0xFF);
            outTag[11] = static_cast<QC::u8>((f2 >> 24) & 0xFF);
            f3 += (f2 >> 32);
            outTag[12] = static_cast<QC::u8>(f3 & 0xFF);
            outTag[13] = static_cast<QC::u8>((f3 >> 8) & 0xFF);
            outTag[14] = static_cast<QC::u8>((f3 >> 16) & 0xFF);
            outTag[15] = static_cast<QC::u8>((f3 >> 24) & 0xFF);
        }

        static void poly1305TagForAead(const QC::u8 aeadKey[32],
                                       const QC::u8 *aad, QC::usize aadLen,
                                       const QC::u8 *cipher, QC::usize cipherLen,
                                       QC::u8 outTag[16])
        {
            // macData = aad || pad16(aad) || cipher || pad16(cipher) || (aadLen64 || cipherLen64)
            const QC::usize aadPad = (16 - (aadLen % 16)) % 16;
            const QC::usize cipherPad = (16 - (cipherLen % 16)) % 16;
            const QC::usize total = aadLen + aadPad + cipherLen + cipherPad + 16;

            // For our wrapped SST sizes, this is small and bounded.
            QC::u8 buf[256];
            if (total > sizeof(buf))
            {
                QC::String::memset(outTag, 0, 16);
                return;
            }

            QC::usize pos = 0;
            if (aadLen)
            {
                QC::String::memcpy(buf + pos, aad, aadLen);
                pos += aadLen;
            }
            for (QC::usize i = 0; i < aadPad; ++i)
                buf[pos++] = 0;
            if (cipherLen)
            {
                QC::String::memcpy(buf + pos, cipher, cipherLen);
                pos += cipherLen;
            }
            for (QC::usize i = 0; i < cipherPad; ++i)
                buf[pos++] = 0;

            // lengths (little-endian u64)
            QC::u64 a64 = static_cast<QC::u64>(aadLen);
            QC::u64 c64 = static_cast<QC::u64>(cipherLen);
            for (int i = 0; i < 8; ++i)
                buf[pos++] = static_cast<QC::u8>((a64 >> (i * 8)) & 0xFF);
            for (int i = 0; i < 8; ++i)
                buf[pos++] = static_cast<QC::u8>((c64 >> (i * 8)) & 0xFF);

            poly1305Mac(aeadKey, buf, pos, outTag);
            secureZero(buf, sizeof(buf));
        }

        static void hmacSha256(const QC::u8 *key, QC::usize keyLen,
                               const QC::u8 *data, QC::usize dataLen,
                               QC::u8 outDigest[32])
        {
            constexpr QC::usize kBlockSize = 64;
            constexpr QC::usize kMaxDataLen = 192;

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
    }

    namespace
    {
        static QQ::FlowDecision securityCenterFlowPolicy(const QQ::TaskDescriptor &td)
        {
            const char *name = td.name;
            const char *origin = td.origin[0] ? td.origin : nullptr;
            const char *moduleId = td.moduleId[0] ? td.moduleId : nullptr;

            auto lower = [](char c) -> char {
                if (c >= 'A' && c <= 'Z')
                    return static_cast<char>(c - 'A' + 'a');
                return c;
            };

            auto containsIgnoreCase = [&](const char *hay, const char *needle) -> bool {
                if (!hay || !needle)
                    return false;
                for (const char *h = hay; *h; ++h)
                {
                    const char *a = h;
                    const char *b = needle;
                    while (*a && *b && (lower(*a) == lower(*b)))
                    {
                        ++a;
                        ++b;
                    }
                    if (*b == 0)
                        return true;
                }
                return false;
            };

            // Prefer moduleId/origin gates (real inputs).
            if (moduleId && containsIgnoreCase(moduleId, "cancel"))
                return QQ::FlowDecision{QQ::FlowDecisionType::IsolateCancel, 0};
            if (origin && containsIgnoreCase(origin, "cancel"))
                return QQ::FlowDecision{QQ::FlowDecisionType::IsolateCancel, 0};
            if (moduleId && containsIgnoreCase(moduleId, "suspend"))
                return QQ::FlowDecision{QQ::FlowDecisionType::IsolateSuspend, 0};
            if (origin && containsIgnoreCase(origin, "suspend"))
                return QQ::FlowDecision{QQ::FlowDecisionType::IsolateSuspend, 0};
            if (moduleId && containsIgnoreCase(moduleId, "delay"))
                return QQ::FlowDecision{QQ::FlowDecisionType::ThrottleDelay, 250};
            if (origin && containsIgnoreCase(origin, "delay"))
                return QQ::FlowDecision{QQ::FlowDecisionType::ThrottleDelay, 250};

            // Fallback to name substring (dev/testing).
            if (name && containsIgnoreCase(name, "cancel"))
                return QQ::FlowDecision{QQ::FlowDecisionType::IsolateCancel, 0};
            if (name && containsIgnoreCase(name, "suspend"))
                return QQ::FlowDecision{QQ::FlowDecisionType::IsolateSuspend, 0};
            if (name && containsIgnoreCase(name, "delay"))
                return QQ::FlowDecision{QQ::FlowDecisionType::ThrottleDelay, 250};

            return QQ::FlowDecision{};
        }
    }

    SecurityCenter::SecurityCenter()
        : m_initialized(false),
          m_mode(Mode::Bypass),
          m_flowPolicy(nullptr)
    {
    }

    SecurityCenter &SecurityCenter::instance()
    {
        static SecurityCenter inst;
        return inst;
    }

    void SecurityCenter::initialize(Mode mode)
    {
        m_mode = mode;
        m_flowPolicy = (m_mode == Mode::Enforce) ? &securityCenterFlowPolicy : nullptr;
        m_initialized = true;
    }

    void SecurityCenter::setFlowEnforcementEnabled(bool enabled)
    {
        m_mode = enabled ? Mode::Enforce : Mode::Bypass;
        m_flowPolicy = (m_mode == Mode::Enforce) ? &securityCenterFlowPolicy : nullptr;
    }

    TaskFlowMetrics SecurityCenter::taskFlowMetrics() const
    {
        TaskFlowMetrics m;

        // Best-effort snapshot. Executor may not be initialized yet; counters will be zero.
        auto &ex = QQ::Executor::instance();
        m.pending = static_cast<QC::u32>(ex.pendingCount());
        m.running = static_cast<QC::u32>(ex.runningCount());
        m.completed = static_cast<QC::u32>(ex.completedCount());
        m.totalExecuted = ex.totalTasksExecuted();
        m.cachedCompletions = ex.totalCachedCompletions();
        m.totalBuildMs = ex.totalBuildMilliseconds();
        m.totalExecutionMs = ex.totalExecutionMilliseconds();
        m.totalQueueDelayMs = ex.totalQueueDelayMilliseconds();
        m.averageBuildMs = ex.averageBuildMilliseconds();
        m.averageExecutionMs = ex.averageExecutionMilliseconds();
        m.averageQueueDelayMs = ex.averageQueueDelayMilliseconds();
        m.schedulerPromotions = ex.schedulerPromotions();
        m.schedulerDemotions = ex.schedulerDemotions();
        m.crossFlowPromotions = ex.crossFlowPromotions();
        m.crossFlowDemotions = ex.crossFlowDemotions();
        m.policyAllow = ex.policyAllowCount();
        m.policyThrottle = ex.policyThrottleCount();
        m.policySuspend = ex.policySuspendCount();
        m.policyCancel = ex.policyCancelCount();
        m.redundantSubmissions = ex.redundantSubmissions();
        m.memoHits = ex.memoizationHits();
        m.memoMisses = ex.memoizationMisses();
        m.memoRefused = ex.memoizationRefused();
        m.mergedSubmissions = ex.mergedSubmissions();
        m.cacheEntries = static_cast<QC::u32>(ex.memoizationCacheEntries());
        m.cacheCapacity = static_cast<QC::u32>(ex.memoizationCacheCapacity());
        m.corePoolSize = static_cast<QC::u32>(ex.corePoolSize());

        QQ::SignatureMetricsSnapshot sigs[128] = {};
        m.signatureMapEntries = static_cast<QC::u32>(ex.copySignatureMetrics(sigs, sizeof(sigs) / sizeof(sigs[0])));

        QQ::FlowStatistics flows[32] = {};
        m.flowCount = static_cast<QC::u32>(ex.copyFlowStatistics(flows, sizeof(flows) / sizeof(flows[0])));
        return m;
    }

    SstStatus SecurityCenter::sstStatus() const
    {
        SstStatus st;
        st.available = m_sstAvailable;
        st.generation = m_sstGeneration;
        return st;
    }

    QC::Status SecurityCenter::ensureSst()
    {
        return loadOrProvisionSst(true);
    }

    QC::Status SecurityCenter::requestSstRotation(const SstRotationRequest &)
    {
        // Ensure we have an SST first (create if missing).
        QC::Status st = loadOrProvisionSst(true);
        if (st != QC::Status::Success)
            return st;

        // Generate new SST bytes.
        if (!m_randProvider.fillRandom)
            return QC::Status::Error;
        QC::u8 newSst[32];
        st = m_randProvider.fillRandom(m_randProvider.user, newSst, sizeof(newSst));
        if (st != QC::Status::Success && st != QC::Status::Busy)
        {
            secureZero(newSst, sizeof(newSst));
            return st;
        }

        SrkKey srk;
        st = getSrk(srk);
        if (st != QC::Status::Success)
        {
            secureZero(newSst, sizeof(newSst));
            return st;
        }
        if (srk.size != 32)
        {
            secureZero(newSst, sizeof(newSst));
            return QC::Status::Error;
        }

        WrappedSstV1 wrapped;
        QC::String::memset(&wrapped, 0, sizeof(wrapped));
        wrapped.magic = kWrappedSstMagic;
        wrapped.version = kWrappedSstVersion;
        wrapped.generation = m_sstGeneration + 1;

        if (!m_randProvider.fillRandom || !m_sstStorage.writeWrappedSst)
        {
            secureZero(newSst, sizeof(newSst));
            return QC::Status::Error;
        }

        st = m_randProvider.fillRandom(m_randProvider.user, wrapped.nonce, sizeof(wrapped.nonce));
        if (st != QC::Status::Success && st != QC::Status::Busy)
        {
            secureZero(newSst, sizeof(newSst));
            return st;
        }

        // Encrypt SST -> cipher
        QC::String::memcpy(wrapped.cipher, newSst, sizeof(newSst));
        chacha20Xor(srk.bytes, 1, wrapped.nonce, wrapped.cipher, sizeof(wrapped.cipher));

        // AEAD tag
        QC::u8 polyKeyBlock[64];
        chacha20Block(srk.bytes, 0, wrapped.nonce, polyKeyBlock);
        QC::u8 polyKey[32];
        QC::String::memcpy(polyKey, polyKeyBlock, sizeof(polyKey));
        secureZero(polyKeyBlock, sizeof(polyKeyBlock));

        QC::u8 aad[16];
        QC::String::memset(aad, 0, sizeof(aad));
        QC::String::memcpy(aad + 0, &wrapped.magic, sizeof(wrapped.magic));
        QC::String::memcpy(aad + 4, &wrapped.version, sizeof(wrapped.version));
        QC::String::memcpy(aad + 6, &wrapped.reserved, sizeof(wrapped.reserved));
        QC::String::memcpy(aad + 8, &wrapped.generation, sizeof(wrapped.generation));
        poly1305TagForAead(polyKey, aad, sizeof(aad), wrapped.cipher, sizeof(wrapped.cipher), wrapped.tag);
        secureZero(polyKey, sizeof(polyKey));

        st = m_sstStorage.writeWrappedSst(m_sstStorage.user, &wrapped, sizeof(wrapped));
        secureZero(aad, sizeof(aad));
        if (st != QC::Status::Success)
        {
            secureZero(newSst, sizeof(newSst));
            secureZero(&wrapped, sizeof(wrapped));
            return st;
        }

        // Switch current SST (no retirement window in v1).
        QC::String::memcpy(m_sstBytes, newSst, sizeof(m_sstBytes));
        m_sstGeneration = wrapped.generation;
        m_sstAvailable = true;

        secureZero(newSst, sizeof(newSst));
        secureZero(&wrapped, sizeof(wrapped));
        return QC::Status::Success;
    }

    QC::Status SecurityCenter::deriveSstKey(const char *label, SstDerivedKey &outKey) const
    {
        if (!label)
            label = "";

        if (!m_sstAvailable)
        {
            // Best-effort attempt to load if providers are present.
            // Note: method is const; avoid mutating state here.
            return QC::Status::Error;
        }

        // Key = HMAC-SHA256(SST, "CITADEL-SST-KEY-v1\n" || label)
        static constexpr char kPrefix[] = "CITADEL-SST-KEY-v1\n";
        const QC::usize labelLen = QC::String::strlen(label);

        QC::u8 data[192];
        QC::usize pos = 0;
        const QC::usize prefixLen = sizeof(kPrefix) - 1;
        if (prefixLen + labelLen > sizeof(data))
            return QC::Status::InvalidParam;

        QC::String::memcpy(data + pos, kPrefix, prefixLen);
        pos += prefixLen;
        if (labelLen)
        {
            QC::String::memcpy(data + pos, label, labelLen);
            pos += labelLen;
        }

        hmacSha256(m_sstBytes, sizeof(m_sstBytes), data, pos, outKey.bytes);
        outKey.size = 32;
        secureZero(data, sizeof(data));
        return QC::Status::Success;
    }

    void SecurityCenter::setSrkProvider(const SrkProvider &provider)
    {
        m_srkProvider = provider;
    }

    void SecurityCenter::setRandomProvider(const RandomProvider &provider)
    {
        m_randProvider = provider;
    }

    void SecurityCenter::setSstStorageProvider(const SstStorageProvider &provider)
    {
        m_sstStorage = provider;
    }

    QC::Status SecurityCenter::getSrk(SrkKey &outKey) const
    {
        if (!m_srkProvider.getSrk)
            return QC::Status::Error;
        return m_srkProvider.getSrk(m_srkProvider.user, outKey);
    }

    QC::Status SecurityCenter::loadOrProvisionSst(bool allowCreate)
    {
        if (!m_sstStorage.readWrappedSst || !m_sstStorage.writeWrappedSst)
            return QC::Status::Error;
        if (!m_randProvider.fillRandom)
            return QC::Status::Error;

        WrappedSstV1 wrapped;
        QC::usize got = 0;
        QC::Status st = m_sstStorage.readWrappedSst(m_sstStorage.user, &wrapped, sizeof(wrapped), &got);
        if (st == QC::Status::NotFound)
        {
            if (!allowCreate)
                return QC::Status::NotFound;

            // Provision first SST.
            QC::u8 sst[32];
            st = m_randProvider.fillRandom(m_randProvider.user, sst, sizeof(sst));
            if (st != QC::Status::Success && st != QC::Status::Busy)
            {
                secureZero(sst, sizeof(sst));
                return st;
            }

            SrkKey srk;
            st = getSrk(srk);
            if (st != QC::Status::Success || srk.size != 32)
            {
                secureZero(sst, sizeof(sst));
                return QC::Status::Error;
            }

            QC::String::memset(&wrapped, 0, sizeof(wrapped));
            wrapped.magic = kWrappedSstMagic;
            wrapped.version = kWrappedSstVersion;
            wrapped.generation = 1;
            st = m_randProvider.fillRandom(m_randProvider.user, wrapped.nonce, sizeof(wrapped.nonce));
            if (st != QC::Status::Success && st != QC::Status::Busy)
            {
                secureZero(sst, sizeof(sst));
                return st;
            }

            QC::String::memcpy(wrapped.cipher, sst, sizeof(sst));
            chacha20Xor(srk.bytes, 1, wrapped.nonce, wrapped.cipher, sizeof(wrapped.cipher));

            QC::u8 polyKeyBlock[64];
            chacha20Block(srk.bytes, 0, wrapped.nonce, polyKeyBlock);
            QC::u8 polyKey[32];
            QC::String::memcpy(polyKey, polyKeyBlock, sizeof(polyKey));
            secureZero(polyKeyBlock, sizeof(polyKeyBlock));

            QC::u8 aad[16];
            QC::String::memset(aad, 0, sizeof(aad));
            QC::String::memcpy(aad + 0, &wrapped.magic, sizeof(wrapped.magic));
            QC::String::memcpy(aad + 4, &wrapped.version, sizeof(wrapped.version));
            QC::String::memcpy(aad + 6, &wrapped.reserved, sizeof(wrapped.reserved));
            QC::String::memcpy(aad + 8, &wrapped.generation, sizeof(wrapped.generation));
            poly1305TagForAead(polyKey, aad, sizeof(aad), wrapped.cipher, sizeof(wrapped.cipher), wrapped.tag);
            secureZero(polyKey, sizeof(polyKey));
            secureZero(aad, sizeof(aad));

            st = m_sstStorage.writeWrappedSst(m_sstStorage.user, &wrapped, sizeof(wrapped));
            if (st != QC::Status::Success)
            {
                secureZero(sst, sizeof(sst));
                secureZero(&wrapped, sizeof(wrapped));
                return st;
            }

            QC::String::memcpy(m_sstBytes, sst, sizeof(m_sstBytes));
            m_sstGeneration = wrapped.generation;
            m_sstAvailable = true;

            secureZero(sst, sizeof(sst));
            secureZero(&wrapped, sizeof(wrapped));
            return QC::Status::Success;
        }

        if (st != QC::Status::Success)
            return st;
        if (got != sizeof(WrappedSstV1))
            return QC::Status::Error;

        if (wrapped.magic != kWrappedSstMagic || wrapped.version != kWrappedSstVersion)
            return QC::Status::Error;

        SrkKey srk;
        st = getSrk(srk);
        if (st != QC::Status::Success || srk.size != 32)
            return QC::Status::Error;

        // Verify tag
        QC::u8 polyKeyBlock[64];
        chacha20Block(srk.bytes, 0, wrapped.nonce, polyKeyBlock);
        QC::u8 polyKey[32];
        QC::String::memcpy(polyKey, polyKeyBlock, sizeof(polyKey));
        secureZero(polyKeyBlock, sizeof(polyKeyBlock));

        QC::u8 aad[16];
        QC::String::memset(aad, 0, sizeof(aad));
        QC::String::memcpy(aad + 0, &wrapped.magic, sizeof(wrapped.magic));
        QC::String::memcpy(aad + 4, &wrapped.version, sizeof(wrapped.version));
        QC::String::memcpy(aad + 6, &wrapped.reserved, sizeof(wrapped.reserved));
        QC::String::memcpy(aad + 8, &wrapped.generation, sizeof(wrapped.generation));

        QC::u8 tagCalc[16];
        poly1305TagForAead(polyKey, aad, sizeof(aad), wrapped.cipher, sizeof(wrapped.cipher), tagCalc);
        secureZero(polyKey, sizeof(polyKey));
        secureZero(aad, sizeof(aad));

        if (!constTimeEq(tagCalc, wrapped.tag, sizeof(tagCalc)))
        {
            secureZero(tagCalc, sizeof(tagCalc));
            return QC::Status::Error;
        }
        secureZero(tagCalc, sizeof(tagCalc));

        // Decrypt
        QC::u8 sst[32];
        QC::String::memcpy(sst, wrapped.cipher, sizeof(sst));
        chacha20Xor(srk.bytes, 1, wrapped.nonce, sst, sizeof(sst));

        QC::String::memcpy(m_sstBytes, sst, sizeof(m_sstBytes));
        m_sstGeneration = wrapped.generation;
        m_sstAvailable = true;

        secureZero(sst, sizeof(sst));
        secureZero(&wrapped, sizeof(wrapped));
        return QC::Status::Success;
    }

    const char *modeName(Mode mode)
    {
        switch (mode)
        {
        case Mode::Bypass:
            return "BYPASS";
        case Mode::Enforce:
            return "ENFORCE";
        default:
            return "UNKNOWN";
        }
    }

} // namespace QSC
