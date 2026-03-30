#include "QKSecurityCenter.h"

#include "QQExecutor.h"

#include "QKSecureStore.h"

#include "QKEntropy.h"

#include "QKTime.h"

#include "QKRuntimeRegistries.h"

#include "QSCSecurityCenter.h"

#include "QCString.h"
#include "QCSha256.h"

namespace QK
{

    namespace
    {
        static void updateRuntimeSecurityState(bool enforcementEnabled)
        {
            auto &regs = QK::Runtime::Registries::instance();
            QK::Runtime::SecurityState st = regs.securityState();
            st.tpmAvailable = QK::SecureStore::tpm_present();
            st.enforcementEnabled = enforcementEnabled;
            regs.setSecurityState(st);
        }
    }

    namespace
    {
        static void secureZero(void *ptr, QC::usize len)
        {
            volatile QC::u8 *p = reinterpret_cast<volatile QC::u8 *>(ptr);
            while (len--)
                *p++ = 0;
        }

        static constexpr const char *kWrappedSstKey = "SSTWRAP"; // 8.3 safe

        static QC::Status qkFillRandom(void *, void *out, QC::usize size)
        {
            return QK::Entropy::fillRandom(out, size);
        }

        static QC::Status qkReadWrappedSst(void *, void *out, QC::usize outCap, QC::usize *outSize)
        {
            if (!out || !outSize)
                return QC::Status::InvalidParam;

            QC::Vector<QC::u8> blob;
            const QC::Status st = QK::SecureStore::readSealedBlob(kWrappedSstKey, blob);
            if (st != QC::Status::Success)
                return st;
            if (blob.size() > outCap)
                return QC::Status::InvalidParam;
            QC::String::memcpy(out, blob.data(), blob.size());
            *outSize = blob.size();
            return QC::Status::Success;
        }

        static QC::Status qkWriteWrappedSst(void *, const void *data, QC::usize size)
        {
            if (!data || size == 0)
                return QC::Status::InvalidParam;

            QC::Status st = QK::SecureStore::ensureBaseDir();
            if (st != QC::Status::Success)
                return st;
            return QK::SecureStore::writeSealedBlob(kWrappedSstKey, data, size);
        }

        static void hmacSha256(const QC::u8 *key, QC::usize keyLen,
                               const QC::u8 *data, QC::usize dataLen,
                               QC::u8 outDigest[32])
        {
            constexpr QC::usize kBlockSize = 64;
            constexpr QC::usize kMaxDataLen = 128;

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

            // sha256(ipad || data)
            QC::u8 innerDigest[32];
            QC::u8 inner[kBlockSize + kMaxDataLen];
            QC::String::memcpy(inner, ipad, kBlockSize);
            if (dataLen)
                QC::String::memcpy(inner + kBlockSize, data, dataLen);
            QC::Sha256(inner, kBlockSize + dataLen, innerDigest);

            // sha256(opad || innerDigest)
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

        static QC::Status qkGetSrkFromSecureStore(void *, QSC::SrkKey &outKey)
        {
            QC::u8 wrapKey[32];
            QC::Status st = QK::SecureStore::readWrapKey(wrapKey);
            if (st == QC::Status::NotFound)
            {
                // Provisioning path: create the anchor if missing.
                st = QK::SecureStore::getOrCreateWrapKey(wrapKey);
            }
            if (st != QC::Status::Success)
                return st;

            // SRK := HMAC-SHA256(TAS, label)
            // Current TAS implementation is the SecureStore wrap key (TPM-protected when available).
            static constexpr char kSrkLabel[] = "CITADEL-QSC-SRK-v1";
            QC::u8 srk[32];
            hmacSha256(wrapKey, sizeof(wrapKey),
                       reinterpret_cast<const QC::u8 *>(kSrkLabel), sizeof(kSrkLabel) - 1,
                       srk);

            for (int i = 0; i < 32; ++i)
                outKey.bytes[i] = srk[i];
            outKey.size = 32;

            secureZero(srk, sizeof(srk));
            secureZero(wrapKey, sizeof(wrapKey));
            return QC::Status::Success;
        }
    }

    namespace
    {
        static constexpr const char *kOwnerCredKey = "OWNERCRD"; // 8.3 safe
        static constexpr QC::u32 kOwnerCredMagic = 0x4F435244;    // 'OCRD'
        static constexpr QC::u32 kOwnerCredVersionV1 = 1;
        static constexpr QC::u32 kOwnerCredVersionV2 = 2;

        struct OwnerCredRecordV1
        {
            QC::u32 magic;
            QC::u32 version;
            QC::u8 userLen;
            QC::u8 reserved[7];
            QC::u64 verifier;
            char username[32];
        };

        struct OwnerCredRecordV2
        {
            QC::u32 magic;
            QC::u32 version;
            QC::u8 userLen;
            QC::u8 saltLen;
            QC::u16 reserved0;
            QC::u32 iterations;
            QC::u8 salt[16];
            QC::u8 verifier[32];
            char username[32];
        };

        static inline QC::u64 fnv1a64(const QC::u8 *data, QC::usize len)
        {
            QC::u64 h = 1469598103934665603ULL;
            for (QC::usize i = 0; i < len; ++i)
            {
                h ^= static_cast<QC::u64>(data[i]);
                h *= 1099511628211ULL;
            }
            return h;
        }

        static QC::u64 computeVerifierV1(const char *username, const char *secret)
        {
            // Minimal deterministic verifier: FNV-1a64 over "user\nsecret".
            // NOTE: Legacy v1 record support only.
            if (!username)
                username = "";
            if (!secret)
                secret = "";

            const QC::usize uLen = QC::String::strlen(username);
            const QC::usize sLen = QC::String::strlen(secret);

            QC::u64 h = 1469598103934665603ULL;
            h = fnv1a64(reinterpret_cast<const QC::u8 *>(username), uLen) ^ h;
            // Mix delimiter to reduce trivial concatenation collisions.
            const QC::u8 delim = static_cast<QC::u8>('\n');
            h = fnv1a64(&delim, 1) ^ h;
            h = fnv1a64(reinterpret_cast<const QC::u8 *>(secret), sLen) ^ h;
            return h;
        }

        static void computeVerifierV2(const char *username,
                                      const char *secret,
                                      const QC::u8 salt[16],
                                      QC::u32 iterations,
                                      QC::u8 outVerifier[32])
        {
            if (!username)
                username = "";
            if (!secret)
                secret = "";

            if (iterations == 0)
                iterations = 1;

            // KDF-ish verifier: sha256(salt || username || '\n' || secret), iterated.
            // This is not intended as a final design; it's a minimal salted + parameterized step.
            static constexpr QC::usize kMaxUser = 32;
            static constexpr QC::usize kMaxSecret = 64;

            const QC::usize uLenRaw = QC::String::strlen(username);
            const QC::usize sLenRaw = QC::String::strlen(secret);
            const QC::usize uLen = (uLenRaw > kMaxUser) ? kMaxUser : uLenRaw;
            const QC::usize sLen = (sLenRaw > kMaxSecret) ? kMaxSecret : sLenRaw;

            QC::u8 msg[16 + kMaxUser + 1 + kMaxSecret];
            QC::usize mi = 0;
            QC::String::memcpy(msg + mi, salt, 16);
            mi += 16;
            if (uLen)
            {
                QC::String::memcpy(msg + mi, username, uLen);
                mi += uLen;
            }
            msg[mi++] = static_cast<QC::u8>('\n');
            if (sLen)
            {
                QC::String::memcpy(msg + mi, secret, sLen);
                mi += sLen;
            }

            QC::u8 digest[32];
            QC::Sha256(msg, mi, digest);

            QC::u8 iterBuf[32 + 16];
            for (QC::u32 i = 1; i < iterations; ++i)
            {
                QC::String::memcpy(iterBuf, digest, 32);
                QC::String::memcpy(iterBuf + 32, salt, 16);
                QC::Sha256(iterBuf, sizeof(iterBuf), digest);
            }

            QC::String::memcpy(outVerifier, digest, 32);
            secureZero(digest, sizeof(digest));
            secureZero(msg, sizeof(msg));
            secureZero(iterBuf, sizeof(iterBuf));
        }

        static QC::u32 computeBackoffMs(QC::u32 failCount)
        {
            // 0, 250, 500, 1000, 2000, 4000, 8000, ... capped at 30s.
            if (failCount == 0)
                return 0;
            QC::u32 ms = 250U;
            QC::u32 shifts = (failCount > 1) ? (failCount - 1) : 0;
            if (shifts > 7)
                shifts = 7;
            ms <<= shifts;
            if (ms > 30000U)
                ms = 30000U;
            return ms;
        }

        static bool streq(const char *a, const char *b)
        {
            if (a == b)
                return true;
            if (!a || !b)
                return false;
            while (*a && *b)
            {
                if (*a != *b)
                    return false;
                ++a;
                ++b;
            }
            return *a == 0 && *b == 0;
        }

        enum class OwnerCredKind : QC::u8
        {
            Unknown = 0,
            V1,
            V2
        };

        struct OwnerCredAny
        {
            OwnerCredKind kind = OwnerCredKind::Unknown;
            OwnerCredRecordV1 v1{};
            OwnerCredRecordV2 v2{};
        };

        static QC::Status readOwnerCred(OwnerCredAny &out)
        {
            QC::Vector<QC::u8> blob;
            QC::Status st = QK::SecureStore::readSealedBlob(kOwnerCredKey, blob);
            if (st != QC::Status::Success)
                return st;

            if (blob.size() >= sizeof(OwnerCredRecordV2))
            {
                OwnerCredRecordV2 rec{};
                QC::String::memcpy(&rec, blob.data(), sizeof(rec));
                if (rec.magic == kOwnerCredMagic && rec.version == kOwnerCredVersionV2)
                {
                    rec.username[sizeof(rec.username) - 1] = 0;
                    if (rec.saltLen != 16)
                        return QC::Status::Error;
                    out.kind = OwnerCredKind::V2;
                    out.v2 = rec;
                    return QC::Status::Success;
                }
            }

            if (blob.size() >= sizeof(OwnerCredRecordV1))
            {
                OwnerCredRecordV1 rec{};
                QC::String::memcpy(&rec, blob.data(), sizeof(rec));
                if (rec.magic == kOwnerCredMagic && rec.version == kOwnerCredVersionV1)
                {
                    rec.username[sizeof(rec.username) - 1] = 0;
                    out.kind = OwnerCredKind::V1;
                    out.v1 = rec;
                    return QC::Status::Success;
                }
            }

            return QC::Status::Error;
        }

        static QC::Status writeOwnerCred(const OwnerCredRecordV2 &rec)
        {
            return QK::SecureStore::writeSealedBlob(kOwnerCredKey, &rec, sizeof(rec));
        }
    }

    namespace
    {
        static QSC::Mode toQscMode(SecurityCenter::Mode m)
        {
            return (m == SecurityCenter::Mode::Enforce) ? QSC::Mode::Enforce : QSC::Mode::Bypass;
        }
    }

    SecurityCenter::SecurityCenter()
        : m_initialized(false),
          m_mode(Mode::Bypass)
    {
    }

    SecurityCenter &SecurityCenter::instance()
    {
        static SecurityCenter sc;
        return sc;
    }

    void SecurityCenter::initialize(Mode mode)
    {
        m_mode = mode;

        // Wire kernel-provided boundaries before QSC init so it can load/provision SST.
        {
            QSC::SrkProvider prov;
            prov.user = nullptr;
            prov.getSrk = &qkGetSrkFromSecureStore;
            QSC::SecurityCenter::instance().setSrkProvider(prov);
        }

        {
            QSC::RandomProvider rp;
            rp.user = nullptr;
            rp.fillRandom = &qkFillRandom;
            QSC::SecurityCenter::instance().setRandomProvider(rp);
        }

        {
            QSC::SstStorageProvider sp;
            sp.user = nullptr;
            sp.readWrappedSst = &qkReadWrappedSst;
            sp.writeWrappedSst = &qkWriteWrappedSst;
            QSC::SecurityCenter::instance().setSstStorageProvider(sp);
        }

        // Delegate policy ownership to QSC.
        QSC::SecurityCenter::instance().initialize(toQscMode(m_mode));

        QQ::Executor::instance().setFlowPolicy(QSC::SecurityCenter::instance().flowPolicy());

        updateRuntimeSecurityState(m_mode == Mode::Enforce);

        m_initialized = true;
    }

    QC::Status SecurityCenter::ensureSst()
    {
        return QSC::SecurityCenter::instance().ensureSst();
    }

    void SecurityCenter::setFlowEnforcementEnabled(bool enabled)
    {
        m_mode = enabled ? Mode::Enforce : Mode::Bypass;
        QSC::SecurityCenter::instance().setFlowEnforcementEnabled(enabled);
        QQ::Executor::instance().setFlowPolicy(QSC::SecurityCenter::instance().flowPolicy());

        updateRuntimeSecurityState(enabled);
    }

    QC::Status SecurityCenter::ownerEnroll(const char *username, const char *secret)
    {
        if (!username || !username[0] || !secret)
            return QC::Status::Error;

        if (ownerIsEnrolled())
            return QC::Status::Busy;

        static constexpr QC::u32 kDefaultIterations = 2000;

        OwnerCredRecordV2 rec;
        QC::String::memset(&rec, 0, sizeof(rec));
        rec.magic = kOwnerCredMagic;
        rec.version = kOwnerCredVersionV2;
        rec.userLen = static_cast<QC::u8>(QC::String::strlen(username));
        if (rec.userLen >= sizeof(rec.username))
            rec.userLen = static_cast<QC::u8>(sizeof(rec.username) - 1);

        rec.saltLen = 16;
        rec.iterations = kDefaultIterations;

        QC::Status st = qkFillRandom(nullptr, rec.salt, sizeof(rec.salt));
        if (st != QC::Status::Success)
            return st;

        QC::String::strncpy(rec.username, username, sizeof(rec.username) - 1);
        computeVerifierV2(rec.username, secret, rec.salt, rec.iterations, rec.verifier);

        st = QK::SecureStore::ensureBaseDir();
        if (st != QC::Status::Success)
            return st;

        st = writeOwnerCred(rec);
        if (st != QC::Status::Success)
            return st;

        // Enrollment implies an unlocked session.
        m_ownerUnlocked = true;
        m_ownerFailCount = 0;
        m_ownerBackoffMs = 0;
        return QC::Status::Success;
    }

    QC::Status SecurityCenter::ownerUnlock(const char *username, const char *secret)
    {
        if (!username || !username[0] || !secret)
            return QC::Status::Error;

        OwnerCredAny rec;
        QC::Status st = readOwnerCred(rec);
        if (st != QC::Status::Success)
            return st;

        bool ok = false;
        if (rec.kind == OwnerCredKind::V2)
        {
            if (streq(username, rec.v2.username))
            {
                QC::u8 v[32];
                computeVerifierV2(rec.v2.username, secret, rec.v2.salt, rec.v2.iterations, v);
                ok = (QC::String::memcmp(v, rec.v2.verifier, 32) == 0);
                secureZero(v, sizeof(v));
            }
        }
        else if (rec.kind == OwnerCredKind::V1)
        {
            if (streq(username, rec.v1.username))
            {
                const QC::u64 v = computeVerifierV1(rec.v1.username, secret);
                ok = (v == rec.v1.verifier);
            }
        }

        if (!ok)
        {
            ++m_ownerFailCount;
            m_ownerBackoffMs = computeBackoffMs(m_ownerFailCount);
            if (m_ownerBackoffMs)
            {
                // Best-effort delay to slow down brute force.
                QK::Time::sleep(m_ownerBackoffMs);
            }
            return QC::Status::Error;
        }

        m_ownerUnlocked = true;
        m_ownerFailCount = 0;
        m_ownerBackoffMs = 0;
        return QC::Status::Success;
    }

    void SecurityCenter::ownerLock()
    {
        m_ownerUnlocked = false;
        // Keep fail/backoff counters; caller can inspect and decide UI feedback.
    }

    bool SecurityCenter::ownerIsEnrolled() const
    {
        return QK::SecureStore::exists(kOwnerCredKey);
    }

    const char *SecurityCenter::modeName(Mode mode)
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

} // namespace QK
