#include "QKSecurityCenter.h"

#include "QQExecutor.h"

#include "QKSecureStore.h"

#include "QKEntropy.h"

#include "QKTime.h"

#include "QKRuntimeRegistries.h"
#include "QKMsgBus.h"

#include "QSCSecurityCenter.h"

#include "QFSFile.h"
#include "QFSVFS.h"

#include "QCString.h"
#include "QCSha256.h"
#include "QCVector.h"

namespace QK
{

    namespace
    {
        static char lowerAscii(char c)
        {
            if (c >= 'A' && c <= 'Z')
                return static_cast<char>(c + 32);
            return c;
        }

        static bool containsCaseInsensitive(const char *haystack, const char *needle)
        {
            if (!haystack || !needle || !*needle)
                return false;
            for (const char *h = haystack; *h; ++h)
            {
                const char *a = h;
                const char *b = needle;
                while (*a && *b && lowerAscii(*a) == lowerAscii(*b))
                {
                    ++a;
                    ++b;
                }
                if (*b == 0)
                    return true;
            }
            return false;
        }

        static bool containsBytesCaseInsensitive(const QC::u8 *data, QC::usize size, const char *needle)
        {
            if (!data || size == 0 || !needle || !*needle)
                return false;
            const QC::usize needleLen = QC::String::strlen(needle);
            if (needleLen == 0 || needleLen > size)
                return false;
            for (QC::usize i = 0; i + needleLen <= size; ++i)
            {
                bool match = true;
                for (QC::usize j = 0; j < needleLen; ++j)
                {
                    if (lowerAscii(static_cast<char>(data[i + j])) != lowerAscii(needle[j]))
                    {
                        match = false;
                        break;
                    }
                }
                if (match)
                    return true;
            }
            return false;
        }

        static void setText(char *dst, QC::usize cap, const char *src)
        {
            if (!dst || cap == 0)
                return;
            QC::String::memset(dst, 0, cap);
            if (src && *src)
                QC::String::strncpy(dst, src, cap - 1);
        }

        static bool isSystemLikePath(const char *path)
        {
            if (!path)
                return false;

            const char *sys = "/system";
            const QC::usize sysLen = QC::String::strlen(sys);
            if (QC::String::memcmp(path, sys, sysLen) == 0 && (path[sysLen] == '\0' || path[sysLen] == '/'))
                return true;

            const char *prod = "/PROD";
            const QC::usize prodLen = QC::String::strlen(prod);
            if (QC::String::memcmp(path, prod, prodLen) == 0 && (path[prodLen] == '\0' || path[prodLen] == '/'))
                return true;

            return false;
        }

        static bool isDeniedSecurePath(const char *path)
        {
            if (!path)
                return false;
            return containsCaseInsensitive(path, "/system/.sc") || containsCaseInsensitive(path, "/system/sc");
        }

        static bool ensureDir(const char *path)
        {
            if (!path || !*path)
                return false;
            const QC::Status st = QFS::VFS::instance().createDir(path);
            return st == QC::Status::Success || st == QC::Status::Busy;
        }

        static bool ensureProtectedStorageLayout()
        {
            if (!ensureDir("/system"))
                return false;
            if (!ensureDir("/system/.sc"))
                return false;
            if (!ensureDir("/system/.sc/audit"))
                return false;
            const QC::Status st = QK::SecureStore::ensureBaseDir();
            return st == QC::Status::Success || st == QC::Status::Busy;
        }

        static bool ensureArtifactDirs(bool quarantine)
        {
            const bool okSystem = ensureDir("/system");
            if (quarantine)
                return okSystem && ensureDir("/system/quarantine");
            return okSystem && ensureDir("/system/updates") && ensureDir("/system/updates/verified") && ensureDir("/system/updates/verified/modules");
        }

        static void sanitizeArtifactId(const char *input, char *output, QC::usize cap)
        {
            if (!output || cap == 0)
                return;
            QC::String::memset(output, 0, cap);
            if (!input || !*input)
            {
                QC::String::strncpy(output, "artifact", cap - 1);
                return;
            }

            QC::usize oi = 0;
            for (QC::usize i = 0; input[i] && oi + 1 < cap; ++i)
            {
                const char c = input[i];
                const bool safe = (c >= 'a' && c <= 'z') ||
                                  (c >= 'A' && c <= 'Z') ||
                                  (c >= '0' && c <= '9') ||
                                  c == '-' || c == '_' || c == '.';
                output[oi++] = safe ? c : '_';
            }
            output[oi] = '\0';
            if (oi == 0)
                QC::String::strncpy(output, "artifact", cap - 1);
        }

        static QC::Status writeArtifactFile(const char *path, const void *data, QC::usize size)
        {
            if (!path || !*path || !data || size == 0)
                return QC::Status::InvalidParam;

            QFS::File *f = QFS::VFS::instance().open(path, QFS::OpenMode::Write | QFS::OpenMode::Create | QFS::OpenMode::Truncate);
            if (!f)
                return QC::Status::Error;

            QC::usize off = 0;
            while (off < size)
            {
                const QC::isize w = f->write(reinterpret_cast<const QC::u8 *>(data) + off, size - off);
                if (w <= 0)
                {
                    QFS::VFS::instance().close(f);
                    return QC::Status::Error;
                }
                off += static_cast<QC::usize>(w);
            }

            QFS::VFS::instance().close(f);
            return QC::Status::Success;
        }

        static void updateRuntimeSecurityState(bool enforcementEnabled)
        {
            auto &regs = QK::Runtime::Registries::instance();
            QK::Runtime::SecurityState st = regs.securityState();
            st.tpmAvailable = QK::SecureStore::tpm_present();
            st.enforcementEnabled = enforcementEnabled;
            st.scNoSwap = true;
            st.scNoDump = true;
            st.scMinimalExposure = true;
            st.guardedExecutionEnabled = true;
            st.protectedAppExecutionSpace = true;
            st.hiddenEncryptedScStorage = true;
            st.scInternalOnly = true;
            st.scBackgroundSystemTask = true;
            regs.setSecurityState(st);
        }

        static void registerBackgroundSecurityRuntime()
        {
            static QC::u32 s_serviceId = 0;
            static QC::u32 s_pid = 0;

            auto &regs = QK::Runtime::Registries::instance();
            if (s_serviceId == 0)
            {
                QK::Runtime::ServiceRecord svc{};
                svc.desired = QK::Runtime::ServiceDesiredState::Running;
                svc.capabilities = 0x1ULL;
                s_serviceId = regs.createService(svc);
            }
            if (s_pid == 0)
            {
                QK::Runtime::ProcessRecord proc{};
                proc.parentPid = 0;
                proc.state = QK::Runtime::ProcessState::Running;
                proc.imageId = 0x5343494EU;
                proc.capabilities = 0x1ULL;
                s_pid = regs.createProcess(proc);
            }
        }

        static void publishScEvent(QC::u32 topic, QC::u64 param1, QC::u64 param2)
        {
            QK::Msg::Envelope *env = QK::Msg::makeEnvelope(topic);
            if (!env)
                return;
            env->senderId = 1;
            env->targetId = 0;
            env->param1 = param1;
            env->param2 = param2;
            if (!QK::Msg::Bus::instance().publish(env))
            {
                QK::Msg::release(env);
            }
            else
            {
                QK::Msg::release(env);
            }
        }

        static QC::u64 g_auditChainSeq = 0;
        static QC::u8 g_auditChainPrev[32] = {0};

        static bool appendText(char *buf, QC::usize cap, QC::usize &used, const char *text)
        {
            if (!buf || cap == 0 || !text)
                return false;
            for (QC::usize i = 0; text[i]; ++i)
            {
                if (used + 1 >= cap)
                    return false;
                buf[used++] = text[i];
            }
            buf[used] = '\0';
            return true;
        }

        static bool appendU64(char *buf, QC::usize cap, QC::usize &used, QC::u64 value)
        {
            char rev[32];
            QC::usize n = 0;
            if (value == 0)
                rev[n++] = '0';
            else
            {
                while (value && n < sizeof(rev))
                {
                    rev[n++] = static_cast<char>('0' + (value % 10ULL));
                    value /= 10ULL;
                }
            }
            while (n > 0)
            {
                if (used + 1 >= cap)
                    return false;
                buf[used++] = rev[--n];
            }
            buf[used] = '\0';
            return true;
        }

        static const char *statusName(QC::Status st)
        {
            switch (st)
            {
            case QC::Status::Success:
                return "Success";
            case QC::Status::Error:
                return "Error";
            case QC::Status::InvalidParam:
                return "InvalidParam";
            case QC::Status::OutOfMemory:
                return "OutOfMemory";
            case QC::Status::NotFound:
                return "NotFound";
            case QC::Status::Timeout:
                return "Timeout";
            case QC::Status::Busy:
                return "Busy";
            case QC::Status::NotSupported:
                return "NotSupported";
            default:
                return "Unknown";
            }
        }

        static bool appendAuditChainRecord(QC::u64 code, QC::u64 value)
        {
            if (!ensureProtectedStorageLayout())
                return false;

            char payload[192];
            QC::String::memset(payload, 0, sizeof(payload));
            QC::usize payloadUsed = 0;
            if (!appendText(payload, sizeof(payload), payloadUsed, "seq=") ||
                !appendU64(payload, sizeof(payload), payloadUsed, g_auditChainSeq + 1) ||
                !appendText(payload, sizeof(payload), payloadUsed, " code=") ||
                !appendU64(payload, sizeof(payload), payloadUsed, code) ||
                !appendText(payload, sizeof(payload), payloadUsed, " value=") ||
                !appendU64(payload, sizeof(payload), payloadUsed, value))
                return false;

            QC::u8 digest[32];
            QC::u8 input[32 + 192];
            QC::String::memset(input, 0, sizeof(input));
            QC::String::memcpy(input, g_auditChainPrev, 32);
            const QC::usize payloadLen = static_cast<QC::usize>(QC::String::strlen(payload));
            if (payloadLen > 0)
                QC::String::memcpy(input + 32, payload, payloadLen);
            QC::Sha256(input, 32 + payloadLen, digest);

            char hashHex[65];
            QC::String::memset(hashHex, 0, sizeof(hashHex));
            (void)QC::Sha256DigestToLowerHex(digest, hashHex, sizeof(hashHex));

            QFS::File *f = QFS::VFS::instance().open("/system/.sc/audit/AUDIT.CHAIN", QFS::OpenMode::Write | QFS::OpenMode::Create);
            if (!f)
                return false;
            const QC::isize sz = f->size();
            if (sz > 0)
                (void)f->seek(sz, QFS::SeekOrigin::Begin);

            char line[300];
            QC::String::memset(line, 0, sizeof(line));
            QC::usize lineUsed = 0;
            bool ok = appendText(line, sizeof(line), lineUsed, payload) &&
                      appendText(line, sizeof(line), lineUsed, " hash=") &&
                      appendText(line, sizeof(line), lineUsed, hashHex) &&
                      appendText(line, sizeof(line), lineUsed, "\n");
            if (ok)
                ok = (f->write(line, lineUsed) == static_cast<QC::isize>(lineUsed));
            QFS::VFS::instance().close(f);

            if (!ok)
                return false;

            QC::String::memcpy(g_auditChainPrev, digest, sizeof(g_auditChainPrev));
            ++g_auditChainSeq;
            return true;
        }

        static void setDispatchDetail(SecurityCenter::DispatchResult *out, QC::Status st, const char *msg)
        {
            if (!out)
                return;
            out->status = st;
            QC::String::memset(out->detail, 0, sizeof(out->detail));
            if (msg && *msg)
            {
                QC::String::strncpy(out->detail, msg, sizeof(out->detail) - 1);
                out->detail[sizeof(out->detail) - 1] = '\0';
            }
        }

        static QC::u64 rotationAuditCode(bool started, bool success)
        {
            if (started)
                return 0x524F5453ULL; // ROTS
            return success ? 0x524F5443ULL : 0x524F5446ULL; // ROTC / ROTF
        }

        static QC::u64 decisionAuditCode(SecurityCenter::DispatchOp op, bool approved)
        {
            return (static_cast<QC::u64>(op) << 8) | (approved ? 1ULL : 0ULL);
        }

        static bool requestPayloadLooksSafe(const char *payload)
        {
            if (!payload || !payload[0])
                return false;

            // Minimal denylist for command/script injection surfaces.
            static const char *kDenied[] = {
                "..", "|", ";", "&&", "||", "`", "$(", "/system/.sc", "/system/sc", "WRAPKEY", "SSTWRAP"
            };
            for (QC::usize i = 0; i < (sizeof(kDenied) / sizeof(kDenied[0])); ++i)
            {
                if (containsCaseInsensitive(payload, kDenied[i]))
                    return false;
            }
            return true;
        }

        static QFS::RoleFlag parseVaultRole(const char *payload)
        {
            if (!payload)
                return QFS::RoleFlag::User;

            if (containsCaseInsensitive(payload, "role=sc"))
                return QFS::RoleFlag::Sc;
            if (containsCaseInsensitive(payload, "role=protected"))
                return QFS::RoleFlag::Protected;
            if (containsCaseInsensitive(payload, "role=system"))
                return QFS::RoleFlag::System;
            if (containsCaseInsensitive(payload, "role=admin"))
                return QFS::RoleFlag::Admin;
            if (containsCaseInsensitive(payload, "role=user"))
                return QFS::RoleFlag::User;
            if (containsCaseInsensitive(payload, "role=everyone"))
                return QFS::RoleFlag::Everyone;

            return QFS::RoleFlag::User;
        }

        static bool ownerRoleAllows(QFS::RoleFlag required)
        {
            // Single-owner MVP profile: owner session has admin clearance,
            // but SYSTEM/SC/PROTECTED remain SC-mediated.
            switch (required)
            {
            case QFS::RoleFlag::Everyone:
            case QFS::RoleFlag::User:
            case QFS::RoleFlag::Admin:
                return true;
            case QFS::RoleFlag::System:
            case QFS::RoleFlag::Sc:
            case QFS::RoleFlag::Protected:
            default:
                return false;
            }
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
        static constexpr const char *kRetiringSstMarkerKey = "SST.RET";
        static constexpr const char *kOldSstRetiredKey = "SSTOLD.DEL";

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
            QC::u8 tas[32];
            QC::Status st = QK::SecureStore::readTas(tas);
            if (st == QC::Status::NotFound)
            {
                // Provisioning path: create TAS if missing.
                st = QK::SecureStore::getOrCreateTas(tas);
            }
            if (st != QC::Status::Success)
                return st;

            // SRK := HMAC-SHA256(TAS, label)
            static constexpr char kSrkLabel[] = "CITADEL-QSC-SRK-v1";
            QC::u8 srk[32];
            hmacSha256(tas, sizeof(tas),
                       reinterpret_cast<const QC::u8 *>(kSrkLabel), sizeof(kSrkLabel) - 1,
                       srk);

            for (int i = 0; i < 32; ++i)
                outKey.bytes[i] = srk[i];
            outKey.size = 32;

            secureZero(srk, sizeof(srk));
            secureZero(tas, sizeof(tas));
            return QC::Status::Success;
        }
    }

    namespace
    {
        static constexpr const char *kOwnerCredKey = "OWNERCRD"; // 8.3 safe
        static constexpr const char *kOwnerCredCompatBaseDir = "/system";
        static constexpr const char *kRecoveryCodeKey = "RCOVR1";
        static constexpr const char *kVaultHeaderKey = "VAULTHDR";
        static constexpr QC::u32 kOwnerCredMagic = 0x4F435244;    // 'OCRD'
        static constexpr QC::u32 kOwnerCredVersionV1 = 1;
        static constexpr QC::u32 kOwnerCredVersionV2 = 2;
        static constexpr QC::u32 kRecoveryCodeMagic = 0x52435631; // 'RCV1'
        static constexpr QC::u32 kVaultHeaderMagic = 0x56484431;  // 'VHD1'

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

        struct RecoveryCodeRecordV1
        {
            QC::u32 magic;
            QC::u32 version;
            QC::u8 salt[16];
            QC::u32 iterations;
            QC::u8 verifier[32];
        };

        struct VaultHeaderRecordV1
        {
            QC::u32 magic;
            QC::u32 version;
            char username[32];
            QC::u8 salt[16];
            QC::u32 iterations;
            QC::u8 wrappedVrk[32];
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

        static QC::Status deriveUserMasterKeyMemoryHard(const char *username,
                                                        const char *secret,
                                                        const QC::u8 salt[16],
                                                        QC::u32 iterations,
                                                        QC::u8 outUmk[32])
        {
            if (!secret || !salt || !outUmk)
                return QC::Status::InvalidParam;

            if (!username)
                username = "";
            if (iterations == 0)
                iterations = 1;

            // Memory-hard MVP: iterate over a 64 KiB mixing arena.
            static constexpr QC::usize kArenaBytes = 64 * 1024;
            static constexpr QC::usize kChunk = 32;
            static constexpr QC::usize kChunks = kArenaBytes / kChunk;

            QC::u8 seed[64];
            QC::String::memset(seed, 0, sizeof(seed));
            const QC::usize uLen = QC::String::strlen(username);
            const QC::usize sLen = QC::String::strlen(secret);

            QC::usize pos = 0;
            const QC::usize copyU = (uLen < 16) ? uLen : 16;
            const QC::usize copyS = (sLen < 16) ? sLen : 16;
            if (copyU)
            {
                QC::String::memcpy(seed + pos, username, copyU);
                pos += copyU;
            }
            QC::String::memcpy(seed + pos, salt, 16);
            pos += 16;
            if (copyS)
            {
                QC::String::memcpy(seed + pos, secret, copyS);
                pos += copyS;
            }

            QC::u8 state[32];
            QC::Sha256(seed, pos, state);

            QC::Vector<QC::u8> arena;
            arena.resize(kArenaBytes);
            if (arena.size() != kArenaBytes)
            {
                secureZero(state, sizeof(state));
                secureZero(seed, sizeof(seed));
                return QC::Status::OutOfMemory;
            }
            for (QC::usize i = 0; i < kChunks; ++i)
            {
                QC::u8 mix[64];
                QC::String::memcpy(mix, state, 32);
                mix[32] = static_cast<QC::u8>(i & 0xFF);
                mix[33] = static_cast<QC::u8>((i >> 8) & 0xFF);
                mix[34] = static_cast<QC::u8>((i >> 16) & 0xFF);
                mix[35] = static_cast<QC::u8>((i >> 24) & 0xFF);
                QC::String::memcpy(mix + 36, salt, 16);
                QC::Sha256(mix, 52, state);
                QC::String::memcpy(arena.data() + (i * kChunk), state, kChunk);
                secureZero(mix, sizeof(mix));
            }

            for (QC::u32 round = 0; round < iterations; ++round)
            {
                for (QC::usize i = 0; i < kChunks; ++i)
                {
                    const QC::usize j = (static_cast<QC::usize>(state[0]) + i + round) % kChunks;
                    QC::u8 mix[96];
                    QC::String::memcpy(mix, state, 32);
                    QC::String::memcpy(mix + 32, arena.data() + (i * kChunk), kChunk);
                    QC::String::memcpy(mix + 64, arena.data() + (j * kChunk), kChunk);
                    QC::Sha256(mix, sizeof(mix), state);
                    QC::String::memcpy(arena.data() + (i * kChunk), state, kChunk);
                    secureZero(mix, sizeof(mix));
                }
            }

            QC::u8 finalMix[32 + 16 + 16];
            QC::String::memcpy(finalMix, state, 32);
            QC::String::memcpy(finalMix + 32, arena.data() + ((kChunks / 3) * kChunk), 16);
            QC::String::memcpy(finalMix + 48, arena.data() + ((kChunks * 2 / 3) * kChunk), 16);
            QC::Sha256(finalMix, sizeof(finalMix), outUmk);

            secureZero(finalMix, sizeof(finalMix));
            secureZero(arena.data(), arena.size());
            arena.clear();
            secureZero(state, sizeof(state));
            secureZero(seed, sizeof(seed));
            return QC::Status::Success;
        }

        static QC::Status deriveVaultRootKey(const QC::u8 umk[32],
                                             const char *userId,
                                             QC::u32 vaultVersion,
                                             QC::u8 outVrk[32])
        {
            if (!umk || !outVrk || !userId || !userId[0])
                return QC::Status::InvalidParam;

            if (vaultVersion == 0)
                return QC::Status::InvalidParam;

            QSC::SstDerivedKey sstMix{};
            QC::Status st = QSC::SecurityCenter::instance().deriveSstKey("VRK", sstMix);
            if (st != QC::Status::Success)
                return st;

            const QSC::SstStatus sst = QSC::SecurityCenter::instance().sstStatus();
            if (!sst.available || sst.generation == 0)
            {
                secureZero(sstMix.bytes, sizeof(sstMix.bytes));
                return QC::Status::Error;
            }

            static constexpr char kPrefix[] = "CITADEL-VRK-v1\n";
            static constexpr QC::usize kMaxUser = 32;
            const QC::usize userLenRaw = QC::String::strlen(userId);
            const QC::usize userLen = (userLenRaw > kMaxUser) ? kMaxUser : userLenRaw;

            QC::u8 data[96];
            QC::usize pos = 0;
            QC::String::memcpy(data + pos, kPrefix, sizeof(kPrefix) - 1);
            pos += sizeof(kPrefix) - 1;

            QC::String::memcpy(data + pos, sstMix.bytes, 32);
            pos += 32;

            data[pos++] = static_cast<QC::u8>('\n');
            if (userLen)
            {
                QC::String::memcpy(data + pos, userId, userLen);
                pos += userLen;
            }

            data[pos++] = static_cast<QC::u8>('\n');

            const QC::u64 sstGen = static_cast<QC::u64>(sst.generation);
            for (QC::usize i = 0; i < 8; ++i)
                data[pos++] = static_cast<QC::u8>((sstGen >> (i * 8)) & 0xFF);

            for (QC::usize i = 0; i < 4; ++i)
                data[pos++] = static_cast<QC::u8>((vaultVersion >> (i * 8)) & 0xFF);

            hmacSha256(umk, 32, data, pos, outVrk);

            secureZero(data, sizeof(data));
            secureZero(sstMix.bytes, sizeof(sstMix.bytes));
            return QC::Status::Success;
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

        static char lowerAsciiLocal(char c)
        {
            if (c >= 'A' && c <= 'Z')
                return static_cast<char>(c + 32);
            return c;
        }

        static bool streqIgnoreCase(const char *a, const char *b)
        {
            if (a == b)
                return true;
            if (!a || !b)
                return false;
            while (*a && *b)
            {
                if (lowerAsciiLocal(*a) != lowerAsciiLocal(*b))
                    return false;
                ++a;
                ++b;
            }
            return *a == 0 && *b == 0;
        }

        static QC::Status normalizeOwnerUsername(const char *username, char out[32])
        {
            if (!out)
                return QC::Status::InvalidParam;

            QC::String::memset(out, 0, 32);
            if (!username || !username[0])
                return QC::Status::InvalidParam;

            QC::usize n = QC::String::strlen(username);
            if (n >= 32)
                n = 31;

            for (QC::usize i = 0; i < n; ++i)
                out[i] = lowerAsciiLocal(username[i]);
            out[n] = 0;
            return QC::Status::Success;
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

        static bool ownerCredEquivalent(const OwnerCredAny &a, const OwnerCredAny &b)
        {
            if (a.kind != b.kind)
                return false;

            if (a.kind == OwnerCredKind::V2)
            {
                return QC::String::memcmp(&a.v2, &b.v2, sizeof(OwnerCredRecordV2)) == 0;
            }

            if (a.kind == OwnerCredKind::V1)
            {
                return QC::String::memcmp(&a.v1, &b.v1, sizeof(OwnerCredRecordV1)) == 0;
            }

            return false;
        }

        static QC::Status readOwnerCredUncached(OwnerCredAny &out)
        {
            QC::Vector<QC::u8> blob;
            QC::Status st = QK::SecureStore::readSealedBlob(kOwnerCredKey, blob);
            if (st == QC::Status::NotSupported || st == QC::Status::NotFound)
            {
                QK::SecureStore::Config compatCfg = QK::SecureStore::defaultConfig();
                compatCfg.baseDir = kOwnerCredCompatBaseDir;
                st = QK::SecureStore::readSealedBlob(kOwnerCredKey, blob, compatCfg);
            }
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
                    if (rec.userLen == 0 || rec.userLen >= sizeof(rec.username))
                        return QC::Status::Error;
                    if (rec.username[0] == 0)
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
                    if (rec.userLen == 0 || rec.userLen >= sizeof(rec.username))
                        return QC::Status::Error;
                    if (rec.username[0] == 0)
                        return QC::Status::Error;
                    out.kind = OwnerCredKind::V1;
                    out.v1 = rec;
                    return QC::Status::Success;
                }
            }

            return QC::Status::Error;
        }

        static QC::Status readOwnerCred(OwnerCredAny &out)
        {
            OwnerCredAny first{};
            QC::Status st = readOwnerCredUncached(first);
            if (st != QC::Status::Success)
                return st;

            OwnerCredAny second{};
            st = readOwnerCredUncached(second);
            if (st != QC::Status::Success)
                return st;

            if (!ownerCredEquivalent(first, second))
                return QC::Status::Error;

            out = first;
            return QC::Status::Success;
        }

        static QC::Status writeOwnerCred(const OwnerCredRecordV2 &rec)
        {
            QC::Status st = QK::SecureStore::writeSealedBlob(kOwnerCredKey, &rec, sizeof(rec));
            if (st == QC::Status::NotSupported)
            {
                QK::SecureStore::Config compatCfg = QK::SecureStore::defaultConfig();
                compatCfg.baseDir = kOwnerCredCompatBaseDir;
                st = QK::SecureStore::writeSealedBlob(kOwnerCredKey, &rec, sizeof(rec), compatCfg);
            }
            return st;
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

    void SecurityCenter::clearOwnerSessionKeys()
    {
        m_ownerUmkReady = false;
        m_ownerVrkReady = false;
        secureZero(m_ownerUmk, sizeof(m_ownerUmk));
        secureZero(m_ownerVrk, sizeof(m_ownerVrk));
    }

    SecurityCenter &SecurityCenter::instance()
    {
        static SecurityCenter sc;
        return sc;
    }

    void SecurityCenter::initialize(Mode mode)
    {
        m_mode = mode;
        QC::String::memset(m_pendingRecoveryCode, 0, sizeof(m_pendingRecoveryCode));
        m_deferInstallRecoveryCode = false;
        m_ownerLockoutUntilMs = 0;
        m_ownerUnlockAttempts = 0;
        m_ownerUnlockFailures = 0;
        m_auditViewWindowStartMs = 0;
        m_auditViewWindowCount = 0;
        m_auditExportWindowStartMs = 0;
        m_auditExportWindowCount = 0;
        m_rotationSchedule = RotationScheduleConfig{};
        m_sstRetiring = false;
        m_protectedStorageInitialized = ensureProtectedStorageLayout();

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

        // Migration hardening: wrapped SST is the only persisted form.
        // Remove any legacy plaintext SST blob if present.
        if (QK::SecureStore::exists("SST.BIN"))
            (void)QK::SecureStore::removeBlob("SST.BIN");

        // Delegate policy ownership to QSC.
        QSC::SecurityCenter::instance().initialize(toQscMode(m_mode));

        QQ::Executor::instance().setFlowPolicy(QSC::SecurityCenter::instance().flowPolicy());

        updateRuntimeSecurityState(m_mode == Mode::Enforce);
        registerBackgroundSecurityRuntime();
        publishScEvent(QK::Msg::Topic::ScControl, 1, static_cast<QC::u64>(m_mode));
        publishScEvent(QK::Msg::Topic::ScFlow, static_cast<QC::u64>(m_mode), 0);

        m_lastRotationMs = QK::Time::milliseconds();
        m_lastRotationExecCount = QQ::Executor::instance().performanceCounters().totalExecuted;

        m_initialized = true;
    }

    QC::Status SecurityCenter::ensureSst()
    {
        const QSC::SstStatus before = QSC::SecurityCenter::instance().sstStatus();
        const QC::Status st = QSC::SecurityCenter::instance().ensureSst();
        if (st != QC::Status::Success)
            return st;

        const QSC::SstStatus after = QSC::SecurityCenter::instance().sstStatus();
        if ((!before.available || before.generation == 0) && after.available && after.generation == 1)
        {
            m_deferInstallRecoveryCode = true;
            emitProvisioningCompletedAuditEvent();
        }

        if (shouldForceSstRotation())
            (void)maybeForceRotateSst(25);

        return QC::Status::Success;
    }

    QC::Status SecurityCenter::checkBootTrustGate()
    {
        if (!m_initialized)
            return QC::Status::Error;

        const QC::Status st = QSC::SecurityCenter::instance().ensureSst();
        if (st != QC::Status::Success)
            return st;

        const QSC::SstStatus s = QSC::SecurityCenter::instance().sstStatus();
        if (!s.available || s.generation == 0)
            return QC::Status::Error;

        return QC::Status::Success;
    }

    void SecurityCenter::setFlowEnforcementEnabled(bool enabled)
    {
        m_mode = enabled ? Mode::Enforce : Mode::Bypass;
        QSC::SecurityCenter::instance().setFlowEnforcementEnabled(enabled);
        QQ::Executor::instance().setFlowPolicy(QSC::SecurityCenter::instance().flowPolicy());

        updateRuntimeSecurityState(enabled);
        publishScEvent(QK::Msg::Topic::ScFlow, static_cast<QC::u64>(m_mode), enabled ? 1 : 0);
    }

    QC::Status SecurityCenter::dispatch(const DispatchRequest &req, DispatchResult *outResult)
    {
        switch (req.op)
        {
        case DispatchOp::TrustCheck:
            return handleTrustCheck(outResult);
        case DispatchOp::UpdateVerify:
            return handleUpdateVerify(req, outResult);
        case DispatchOp::RotateSst:
            return handleRotateSst(req, outResult);
        case DispatchOp::ExecRequest:
            return handleExecRequest(req, outResult);
        case DispatchOp::VaultRequest:
            return handleVaultRequest(req, outResult);
        case DispatchOp::AuditView:
            return handleAuditView(outResult);
        case DispatchOp::AuditExport:
            return handleAuditExport(outResult);
        default:
            (void)req.payload;
            setDispatchDetail(outResult, QC::Status::NotSupported, "unsupported dispatch op");
            return QC::Status::NotSupported;
        }
    }

    QC::Status SecurityCenter::handleTrustCheck(DispatchResult *outResult)
    {
        const QC::Status st = checkBootTrustGate();
        setDispatchDetail(outResult, st, st == QC::Status::Success ? "trust check passed" : "trust check failed");
        publishScEvent(QK::Msg::Topic::ScTrust, static_cast<QC::u64>(DispatchOp::TrustCheck), static_cast<QC::u64>(st));
        return st;
    }

    QC::Status SecurityCenter::handleUpdateVerify(const DispatchRequest &, DispatchResult *outResult)
    {
        const QC::Status st = checkBootTrustGate();
        setDispatchDetail(outResult, st, st == QC::Status::Success ? "update verify gate passed" : "update verify gate failed");
        publishScEvent(QK::Msg::Topic::ScTrust, static_cast<QC::u64>(DispatchOp::UpdateVerify), static_cast<QC::u64>(st));
        return st;
    }

    QC::Status SecurityCenter::handleRotateSst(const DispatchRequest &req, DispatchResult *outResult)
    {
        const bool forced = (req.flags & 0x1U) != 0;
        const bool due = shouldForceSstRotation();
        const QC::Status st = forced ? maybeForceRotateSst(250) : (due ? maybeForceRotateSst(250) : QC::Status::Success);
        if (!forced && st == QC::Status::Success && !due)
            setDispatchDetail(outResult, QC::Status::Success, "sst rotation not due");
        else
            setDispatchDetail(outResult, st, st == QC::Status::Success ? "sst rotated" : "sst rotation failed");
        publishScEvent(QK::Msg::Topic::ScControl, static_cast<QC::u64>(DispatchOp::RotateSst), static_cast<QC::u64>(st));
        return st;
    }

    QC::Status SecurityCenter::handleExecRequest(const DispatchRequest &req, DispatchResult *outResult)
    {
        if (!ownerUnlocked())
        {
            setDispatchDetail(outResult, QC::Status::Error, "owner unlock required");
            auditDecision(DispatchOp::ExecRequest, false, QC::Status::Error);
            return QC::Status::Error;
        }
        if (!req.payload || !req.payload[0])
        {
            setDispatchDetail(outResult, QC::Status::InvalidParam, "exec payload required");
            auditDecision(DispatchOp::ExecRequest, false, QC::Status::InvalidParam);
            return QC::Status::InvalidParam;
        }
        if (!requestPayloadLooksSafe(req.payload))
        {
            setDispatchDetail(outResult, QC::Status::Error, "exec payload denied by guard");
            auditDecision(DispatchOp::ExecRequest, false, QC::Status::Error);
            return QC::Status::Error;
        }
        setDispatchDetail(outResult, QC::Status::Success, "exec request approved");
        auditDecision(DispatchOp::ExecRequest, true, QC::Status::Success);
        return QC::Status::Success;
    }

    QC::Status SecurityCenter::handleVaultRequest(const DispatchRequest &req, DispatchResult *outResult)
    {
        if (!ownerUnlocked())
        {
            setDispatchDetail(outResult, QC::Status::Error, "owner unlock required");
            auditDecision(DispatchOp::VaultRequest, false, QC::Status::Error);
            return QC::Status::Error;
        }
        if (!req.payload || !req.payload[0])
        {
            setDispatchDetail(outResult, QC::Status::InvalidParam, "vault payload required");
            auditDecision(DispatchOp::VaultRequest, false, QC::Status::InvalidParam);
            return QC::Status::InvalidParam;
        }
        if (!requestPayloadLooksSafe(req.payload))
        {
            setDispatchDetail(outResult, QC::Status::Error, "vault payload denied by guard");
            auditDecision(DispatchOp::VaultRequest, false, QC::Status::Error);
            return QC::Status::Error;
        }

        const QFS::RoleFlag requiredRole = parseVaultRole(req.payload);
        if (!ownerRoleAllows(requiredRole))
        {
            setDispatchDetail(outResult, QC::Status::Error, "vault role denied");
            auditDecision(DispatchOp::VaultRequest, false, QC::Status::Error);
            return QC::Status::Error;
        }

        QC::u8 tierKey[32];
        const QC::Status keySt = deriveRoleTierKey(static_cast<QC::u32>(requiredRole) + 1U, 1U, tierKey);
        secureZero(tierKey, sizeof(tierKey));
        if (keySt != QC::Status::Success)
        {
            setDispatchDetail(outResult, QC::Status::Error, "vault key gate denied");
            auditDecision(DispatchOp::VaultRequest, false, keySt);
            return keySt;
        }

        setDispatchDetail(outResult, QC::Status::Success, "vault request approved");
        auditDecision(DispatchOp::VaultRequest, true, QC::Status::Success);
        return QC::Status::Success;
    }

    QC::Status SecurityCenter::handleAuditView(DispatchResult *outResult)
    {
        const QC::Status rateSt = allowAuditLogAccess(false);
        if (rateSt != QC::Status::Success)
        {
            setDispatchDetail(outResult, rateSt, rateSt == QC::Status::Busy ? "audit view rate limited" : "owner unlock required");
            auditDecision(DispatchOp::AuditView, false, rateSt);
            return rateSt;
        }
        setDispatchDetail(outResult, QC::Status::Success, "audit view approved");
        auditDecision(DispatchOp::AuditView, true, QC::Status::Success);
        return QC::Status::Success;
    }

    QC::Status SecurityCenter::handleAuditExport(DispatchResult *outResult)
    {
        const QC::Status rateSt = allowAuditLogAccess(true);
        if (rateSt != QC::Status::Success)
        {
            setDispatchDetail(outResult, rateSt, rateSt == QC::Status::Busy ? "audit export rate limited" : "owner unlock required");
            auditDecision(DispatchOp::AuditExport, false, rateSt);
            return rateSt;
        }
        setDispatchDetail(outResult, QC::Status::Success, "audit export approved");
        auditDecision(DispatchOp::AuditExport, true, QC::Status::Success);
        return QC::Status::Success;
    }

    QC::Status SecurityCenter::ownerEnroll(const char *username, const char *secret, bool activateSession)
    {
        if (!username || !username[0] || !secret)
            return QC::Status::Error;

        char canonicalUser[32];
        QC::Status st = normalizeOwnerUsername(username, canonicalUser);
        if (st != QC::Status::Success)
            return st;

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

        st = qkFillRandom(nullptr, rec.salt, sizeof(rec.salt));
        if (st != QC::Status::Success && st != QC::Status::Busy)
            return st;

        QC::String::strncpy(rec.username, canonicalUser, sizeof(rec.username) - 1);
        computeVerifierV2(rec.username, secret, rec.salt, rec.iterations, rec.verifier);

        st = QK::SecureStore::ensureBaseDir();
        if (st != QC::Status::Success)
            return st;

        st = writeOwnerCred(rec);
        if (st != QC::Status::Success)
            return st;

        if (!activateSession)
        {
            clearOwnerSessionKeys();
            m_ownerUnlocked = false;
            m_unlockState = UnlockState::Locked;
            m_ownerFailCount = 0;
            m_ownerBackoffMs = 0;
            m_ownerLockoutUntilMs = 0;
            return QC::Status::Success;
        }

        st = deriveUserMasterKeyMemoryHard(rec.username, secret, rec.salt, rec.iterations, m_ownerUmk);
        if (st != QC::Status::Success)
            return st;
        m_ownerUmkReady = true;
        m_ownerVrkReady = false;

        static constexpr QC::u32 kVaultVersion = 1;
        st = deriveVaultRootKey(m_ownerUmk, rec.username, kVaultVersion, m_ownerVrk);
        if (st != QC::Status::Success)
        {
            if (st == QC::Status::NotSupported && m_mode == Mode::Bypass)
            {
                m_ownerVrkReady = false;
            }
            else
            {
                clearOwnerSessionKeys();
                return st;
            }
        }
        else
        {
            m_ownerVrkReady = true;
        }

        if (m_ownerVrkReady)
        {
            OwnerVaultHeader vaultHeader{};
            st = generatePerUserVaultHeader(rec.username, vaultHeader);
            if (st != QC::Status::Success)
            {
                if (!(st == QC::Status::NotSupported && m_mode == Mode::Bypass))
                {
                    clearOwnerSessionKeys();
                    return st;
                }
            }
        }

        // Enrollment implies an unlocked session.
        m_ownerUnlocked = true;
        m_unlockState = UnlockState::Unlocked;
        m_ownerFailCount = 0;
        m_ownerBackoffMs = 0;
        m_ownerLockoutUntilMs = 0;
        return QC::Status::Success;
    }

    QC::Status SecurityCenter::ownerUnlock(const char *username, const char *secret, bool activateSession)
    {
        if (!username || !username[0] || !secret)
            return QC::Status::Error;

        char canonicalUser[32];
        QC::Status st = normalizeOwnerUsername(username, canonicalUser);
        if (st != QC::Status::Success)
            return st;

        ++m_ownerUnlockAttempts;

        if (ownerLockedOut())
        {
            m_ownerBackoffMs = static_cast<QC::u32>(ownerLockoutRemainingMs());
            ++m_ownerUnlockFailures;
            return QC::Status::Timeout;
        }

        OwnerCredAny rec;
        st = readOwnerCred(rec);
        if (st != QC::Status::Success)
            return st;

        bool ok = false;
        if (rec.kind == OwnerCredKind::V2)
        {
            if (streqIgnoreCase(canonicalUser, rec.v2.username))
            {
                QC::u8 v[32];
                computeVerifierV2(rec.v2.username, secret, rec.v2.salt, rec.v2.iterations, v);
                ok = (QC::String::memcmp(v, rec.v2.verifier, 32) == 0);
                secureZero(v, sizeof(v));
            }
        }
        else if (rec.kind == OwnerCredKind::V1)
        {
            if (streqIgnoreCase(canonicalUser, rec.v1.username))
            {
                const QC::u64 v = computeVerifierV1(rec.v1.username, secret);
                ok = (v == rec.v1.verifier);
            }
        }

        if (!ok)
        {
            clearOwnerSessionKeys();
            ++m_ownerFailCount;
            ++m_ownerUnlockFailures;
            m_ownerBackoffMs = computeBackoffMs(m_ownerFailCount);
            const QC::u64 now = QK::Time::milliseconds();
            if (now != 0 && m_ownerFailCount >= m_ownerLockoutThreshold)
                m_ownerLockoutUntilMs = now + m_ownerLockoutDurationMs;
            if (m_ownerBackoffMs)
            {
                // Best-effort delay to slow down brute force.
                QK::Time::sleep(m_ownerBackoffMs);
            }
            return QC::Status::Error;
        }

        if (!activateSession)
        {
            clearOwnerSessionKeys();
            m_ownerUnlocked = true;
            m_unlockState = UnlockState::Unlocked;
            m_ownerFailCount = 0;
            m_ownerBackoffMs = 0;
            m_ownerLockoutUntilMs = 0;
            return QC::Status::Success;
        }

        if (rec.kind == OwnerCredKind::V2)
        {
            st = deriveUserMasterKeyMemoryHard(rec.v2.username, secret, rec.v2.salt, rec.v2.iterations, m_ownerUmk);
            if (st != QC::Status::Success)
            {
                m_ownerUnlocked = false;
                clearOwnerSessionKeys();
                return st;
            }
            m_ownerUmkReady = true;

            m_ownerVrkReady = false;
            static constexpr QC::u32 kVaultVersion = 1;
            st = deriveVaultRootKey(m_ownerUmk, rec.v2.username, kVaultVersion, m_ownerVrk);
            if (st != QC::Status::Success)
            {
                if (st == QC::Status::NotSupported && m_mode == Mode::Bypass)
                {
                    m_ownerVrkReady = false;
                }
                else
                {
                    m_ownerUnlocked = false;
                    clearOwnerSessionKeys();
                    return st;
                }
            }
            else
            {
                m_ownerVrkReady = true;
            }
        }
        else
        {
            // Legacy verifier format has no UMK material.
            clearOwnerSessionKeys();
        }

        m_ownerUnlocked = true;
        m_unlockState = UnlockState::Unlocked;
        m_ownerFailCount = 0;
        m_ownerBackoffMs = 0;
        m_ownerLockoutUntilMs = 0;
        return QC::Status::Success;
    }

    QC::Status SecurityCenter::ownerUnlockPasskey(const char *username, const char *passkey)
    {
        // MVP bridge: passkeys follow the same verifier path until a dedicated attestation format lands.
        return ownerUnlock(username, passkey, true);
    }

    QC::Status SecurityCenter::getEnrolledOwnerUsername(char *outUsername, QC::usize outCap) const
    {
        if (!outUsername || outCap == 0)
            return QC::Status::InvalidParam;

        QC::String::memset(outUsername, 0, outCap);

        OwnerCredAny rec;
        const QC::Status st = readOwnerCred(rec);
        if (st != QC::Status::Success)
            return st;

        const char *storedUser = nullptr;
        if (rec.kind == OwnerCredKind::V2)
            storedUser = rec.v2.username;
        else if (rec.kind == OwnerCredKind::V1)
            storedUser = rec.v1.username;

        if (!storedUser || !storedUser[0])
            return QC::Status::NotFound;

        QC::String::strncpy(outUsername, storedUser, outCap - 1);
        outUsername[outCap - 1] = '\0';
        return QC::Status::Success;
    }

    void SecurityCenter::debugDescribeOwnerRecord(char *outSummary, QC::usize outCap) const
    {
        if (!outSummary || outCap == 0)
            return;

        QC::String::memset(outSummary, 0, outCap);
        QC::usize used = 0;

        QC::Vector<QC::u8> rawBlob;
        const QC::Status rawSt = QK::SecureStore::readBlob(kOwnerCredKey, rawBlob);
        (void)appendText(outSummary, outCap, used, "raw=");
        (void)appendText(outSummary, outCap, used, statusName(rawSt));
        if (rawSt == QC::Status::Success)
        {
            (void)appendText(outSummary, outCap, used, " bytes=");
            (void)appendU64(outSummary, outCap, used, static_cast<QC::u64>(rawBlob.size()));
        }

        QC::Vector<QC::u8> plainBlob;
        const QC::Status plainSt = QK::SecureStore::readSealedBlob(kOwnerCredKey, plainBlob);
        (void)appendText(outSummary, outCap, used, " plain=");
        (void)appendText(outSummary, outCap, used, statusName(plainSt));
        if (plainSt == QC::Status::Success)
        {
            (void)appendText(outSummary, outCap, used, " plain_bytes=");
            (void)appendU64(outSummary, outCap, used, static_cast<QC::u64>(plainBlob.size()));
        }

        OwnerCredAny first{};
        const QC::Status firstSt = readOwnerCredUncached(first);
        (void)appendText(outSummary, outCap, used, " first=");
        (void)appendText(outSummary, outCap, used, statusName(firstSt));
        if (firstSt == QC::Status::Success)
        {
            const char *user = (first.kind == OwnerCredKind::V2) ? first.v2.username : first.v1.username;
            (void)appendText(outSummary, outCap, used, " user=");
            (void)appendText(outSummary, outCap, used, user && *user ? user : "(empty)");
            (void)appendText(outSummary, outCap, used, " kind=");
            (void)appendText(outSummary, outCap, used, (first.kind == OwnerCredKind::V2) ? "V2" : "V1");
        }

        OwnerCredAny second{};
        const QC::Status secondSt = readOwnerCredUncached(second);
        (void)appendText(outSummary, outCap, used, " second=");
        (void)appendText(outSummary, outCap, used, statusName(secondSt));
        if (secondSt == QC::Status::Success)
        {
            const char *user = (second.kind == OwnerCredKind::V2) ? second.v2.username : second.v1.username;
            (void)appendText(outSummary, outCap, used, " user2=");
            (void)appendText(outSummary, outCap, used, user && *user ? user : "(empty)");
            (void)appendText(outSummary, outCap, used, " kind2=");
            (void)appendText(outSummary, outCap, used, (second.kind == OwnerCredKind::V2) ? "V2" : "V1");
        }

        if (firstSt == QC::Status::Success && secondSt == QC::Status::Success)
        {
            (void)appendText(outSummary, outCap, used, " stable=");
            (void)appendText(outSummary, outCap, used, ownerCredEquivalent(first, second) ? "yes" : "no");
        }
    }

    void SecurityCenter::ownerLock()
    {
        m_ownerUnlocked = false;
        m_unlockState = UnlockState::Locked;
        clearOwnerSessionKeys();
        // Keep fail/backoff counters; caller can inspect and decide UI feedback.
    }

    void SecurityCenter::setTimedLock(bool enabled)
    {
        if (enabled)
        {
            m_ownerUnlocked = false;
            m_unlockState = UnlockState::TimedLock;
            clearOwnerSessionKeys();
            return;
        }

        if (m_unlockState == UnlockState::TimedLock)
            m_unlockState = UnlockState::Locked;
    }

    bool SecurityCenter::ownerIsEnrolled() const
    {
        OwnerCredAny rec;
        return readOwnerCred(rec) == QC::Status::Success;
    }

    bool SecurityCenter::ownerLockedOut() const
    {
        const QC::u64 now = QK::Time::milliseconds();
        return m_ownerLockoutUntilMs != 0 && now != 0 && now < m_ownerLockoutUntilMs;
    }

    QC::u64 SecurityCenter::ownerLockoutRemainingMs() const
    {
        const QC::u64 now = QK::Time::milliseconds();
        if (m_ownerLockoutUntilMs == 0 || now == 0 || now >= m_ownerLockoutUntilMs)
            return 0;
        return m_ownerLockoutUntilMs - now;
    }

    QC::Status SecurityCenter::deriveRoleTierKey(QC::u32 roleId, QC::u32 version, QC::u8 outKey[32]) const
    {
        if (!outKey || roleId == 0 || version == 0)
            return QC::Status::InvalidParam;

        if (!m_ownerUnlocked || !m_ownerUmkReady || !m_ownerVrkReady)
            return QC::Status::Error;

        QSC::SstDerivedKey sstMix{};
        QC::Status st = QSC::SecurityCenter::instance().deriveSstKey("TIERKEY", sstMix);
        if (st != QC::Status::Success)
            return st;

        QC::u8 ikm[96];
        QC::String::memcpy(ikm, m_ownerUmk, 32);
        QC::String::memcpy(ikm + 32, m_ownerVrk, 32);
        QC::String::memcpy(ikm + 64, sstMix.bytes, 32);

        QC::u8 data[32];
        QC::usize pos = 0;
        static constexpr char kPrefix[] = "CITADEL-TIER-KEY-v1\n";
        QC::String::memcpy(data + pos, kPrefix, sizeof(kPrefix) - 1);
        pos += sizeof(kPrefix) - 1;
        for (QC::usize i = 0; i < 4; ++i)
            data[pos++] = static_cast<QC::u8>((roleId >> (i * 8)) & 0xFF);
        for (QC::usize i = 0; i < 4; ++i)
            data[pos++] = static_cast<QC::u8>((version >> (i * 8)) & 0xFF);

        hmacSha256(ikm, sizeof(ikm), data, pos, outKey);

        secureZero(ikm, sizeof(ikm));
        secureZero(data, sizeof(data));
        secureZero(sstMix.bytes, sizeof(sstMix.bytes));
        return QC::Status::Success;
    }

    QC::Status SecurityCenter::rewrapTierKeyMaterial(QC::u32 roleId,
                                                     QC::u32 fromVersion,
                                                     QC::u32 toVersion,
                                                     const QC::u8 wrappedTierKey[32],
                                                     QC::u8 outWrappedTierKey[32]) const
    {
        if (!wrappedTierKey || !outWrappedTierKey || roleId == 0 || fromVersion == 0 || toVersion == 0)
            return QC::Status::InvalidParam;

        QC::u8 fromWrap[32];
        QC::u8 toWrap[32];
        QC::u8 plainTierKey[32];

        QC::Status st = deriveRoleTierKey(roleId, fromVersion, fromWrap);
        if (st != QC::Status::Success)
            return st;

        st = deriveRoleTierKey(roleId, toVersion, toWrap);
        if (st != QC::Status::Success)
        {
            secureZero(fromWrap, sizeof(fromWrap));
            return st;
        }

        for (QC::usize i = 0; i < 32; ++i)
            plainTierKey[i] = static_cast<QC::u8>(wrappedTierKey[i] ^ fromWrap[i]);

        for (QC::usize i = 0; i < 32; ++i)
            outWrappedTierKey[i] = static_cast<QC::u8>(plainTierKey[i] ^ toWrap[i]);

        secureZero(fromWrap, sizeof(fromWrap));
        secureZero(toWrap, sizeof(toWrap));
        secureZero(plainTierKey, sizeof(plainTierKey));
        return QC::Status::Success;
    }

    SecurityCenter::CorruptionPolicyDecision SecurityCenter::decideVaultCorruptionPolicy(bool headerCorrupt, bool contentCorrupt) const
    {
        CorruptionPolicyDecision d{};
        d.allowOperation = false;
        d.requireRecovery = true;
        d.markCompromised = true;
        d.enterSafeMode = false;

        if (headerCorrupt)
        {
            QC::String::strncpy(d.detail, "vault header corrupt: recovery required", sizeof(d.detail) - 1);
            d.enterSafeMode = true;
            return d;
        }

        if (contentCorrupt)
        {
            QC::String::strncpy(d.detail, "vault content corrupt: deny + recovery flow", sizeof(d.detail) - 1);
            return d;
        }

        d.allowOperation = true;
        d.requireRecovery = false;
        d.markCompromised = false;
        d.enterSafeMode = false;
        QC::String::strncpy(d.detail, "vault integrity ok", sizeof(d.detail) - 1);
        return d;
    }

    SecurityCenter::CorruptionPolicyDecision SecurityCenter::decideAuditChainCorruptionPolicy(bool chainInvalid) const
    {
        CorruptionPolicyDecision d{};
        d.allowOperation = true;
        d.requireRecovery = false;
        d.markCompromised = false;
        d.enterSafeMode = false;

        if (chainInvalid)
        {
            d.allowOperation = false;
            d.requireRecovery = true;
            d.markCompromised = true;
            d.enterSafeMode = true;
            QC::String::strncpy(d.detail, "audit chain invalid: mark compromised + safe mode", sizeof(d.detail) - 1);
            return d;
        }

        QC::String::strncpy(d.detail, "audit chain valid", sizeof(d.detail) - 1);
        return d;
    }

    SecurityCenter::RotationFailurePolicyDecision SecurityCenter::decideSstRotationMidCutoverFailurePolicy(bool persistedNewWrappedSst,
                                                                                                            bool switchedGeneration,
                                                                                                            bool retiredOldSst) const
    {
        RotationFailurePolicyDecision d{};
        d.rollbackToPreviousGeneration = true;
        d.markDegraded = true;
        d.enterSafeMode = false;
        d.continueBoot = true;

        if (!persistedNewWrappedSst)
        {
            QC::String::strncpy(d.detail, "rotation failed before persistence: keep previous generation", sizeof(d.detail) - 1);
            return d;
        }

        if (persistedNewWrappedSst && !switchedGeneration)
        {
            QC::String::strncpy(d.detail, "rotation failed before cutover: rollback pointer", sizeof(d.detail) - 1);
            return d;
        }

        if (switchedGeneration && !retiredOldSst)
        {
            d.continueBoot = true;
            d.enterSafeMode = false;
            QC::String::strncpy(d.detail, "rotation degraded: old generation retained for recovery window", sizeof(d.detail) - 1);
            return d;
        }

        d.continueBoot = false;
        d.enterSafeMode = true;
        QC::String::strncpy(d.detail, "rotation failure post-cutover: force safe mode", sizeof(d.detail) - 1);
        return d;
    }

    SecurityCenter::TasFailurePolicyDecision SecurityCenter::decideTasUnsealFailurePolicy() const
    {
        TasFailurePolicyDecision d{};
        d.enterSafeMode = true;
        d.enterRecovery = true;
        d.allowNormalOperation = false;
        QC::String::strncpy(d.detail, "tas unseal/unwrap failed: safe mode + recovery", sizeof(d.detail) - 1);
        return d;
    }

    bool SecurityCenter::exportPolicyAllows(const char *channel) const
    {
        // Default deny export policy. Internal owner-mediated audit viewing is separate.
        if (!channel)
            return false;
        if (streq(channel, "none"))
            return false;
        return false;
    }

    QC::Status SecurityCenter::deriveInitialKeyHierarchyFromTas(KeyHierarchySnapshot &out) const
    {
        QC::String::memset(&out, 0, sizeof(out));

        QC::u8 tas[32];
        QC::Status st = QK::SecureStore::readTas(tas);
        if (st != QC::Status::Success)
            return st;

        static constexpr char kSrkLabel[] = "CITADEL-SRK-v1";
        hmacSha256(tas, sizeof(tas), reinterpret_cast<const QC::u8 *>(kSrkLabel), sizeof(kSrkLabel) - 1, out.srk);

        QSC::SstDerivedKey sstMix{};
        st = QSC::SecurityCenter::instance().deriveSstKey("ROOT", sstMix);
        if (st != QC::Status::Success)
        {
            secureZero(tas, sizeof(tas));
            secureZero(out.srk, sizeof(out.srk));
            return st;
        }
        QC::String::memcpy(out.sstMix, sstMix.bytes, sizeof(out.sstMix));

        QC::u8 rootData[64];
        QC::String::memcpy(rootData, out.sstMix, 32);
        QC::String::memcpy(rootData + 32, out.srk, 32);
        hmacSha256(out.srk, sizeof(out.srk), rootData, sizeof(rootData), out.root);

        static constexpr char kVaultLabel[] = "CITADEL-VAULT-v1";
        hmacSha256(out.root, sizeof(out.root), reinterpret_cast<const QC::u8 *>(kVaultLabel), sizeof(kVaultLabel) - 1, out.vault);

        out.valid = true;
        secureZero(rootData, sizeof(rootData));
        secureZero(tas, sizeof(tas));
        secureZero(sstMix.bytes, sizeof(sstMix.bytes));
        return QC::Status::Success;
    }

    QC::Status SecurityCenter::deriveRecoveryKeyMemoryHard(const char *recoveryCode,
                                                            const QC::u8 salt[16],
                                                            QC::u32 iterations,
                                                            QC::u8 outKey[32]) const
    {
        if (!recoveryCode || !recoveryCode[0] || !salt || !outKey)
            return QC::Status::InvalidParam;

        if (iterations == 0)
            iterations = 1;

        static constexpr QC::usize kArenaBytes = 64 * 1024;
        static constexpr QC::usize kChunk = 32;
        static constexpr QC::usize kChunks = kArenaBytes / kChunk;

        QC::u8 seed[64];
        QC::String::memset(seed, 0, sizeof(seed));
        const QC::usize codeLenRaw = QC::String::strlen(recoveryCode);
        const QC::usize codeLen = (codeLenRaw > 32) ? 32 : codeLenRaw;
        QC::String::memcpy(seed, salt, 16);
        if (codeLen)
            QC::String::memcpy(seed + 16, recoveryCode, codeLen);

        QC::u8 state[32];
        QC::Sha256(seed, 16 + codeLen, state);

        QC::Vector<QC::u8> arena;
        arena.resize(kArenaBytes);
        if (arena.size() != kArenaBytes)
        {
            secureZero(seed, sizeof(seed));
            secureZero(state, sizeof(state));
            return QC::Status::OutOfMemory;
        }

        for (QC::usize i = 0; i < kChunks; ++i)
        {
            QC::u8 mix[64];
            QC::String::memcpy(mix, state, 32);
            QC::String::memcpy(mix + 32, salt, 16);
            mix[48] = static_cast<QC::u8>(i & 0xFF);
            mix[49] = static_cast<QC::u8>((i >> 8) & 0xFF);
            mix[50] = static_cast<QC::u8>((i >> 16) & 0xFF);
            mix[51] = static_cast<QC::u8>((i >> 24) & 0xFF);
            QC::Sha256(mix, 52, state);
            QC::String::memcpy(arena.data() + i * kChunk, state, kChunk);
            secureZero(mix, sizeof(mix));
        }

        for (QC::u32 r = 0; r < iterations; ++r)
        {
            for (QC::usize i = 0; i < kChunks; ++i)
            {
                const QC::usize j = (static_cast<QC::usize>(state[0]) + i + r) % kChunks;
                QC::u8 mix[96];
                QC::String::memcpy(mix, state, 32);
                QC::String::memcpy(mix + 32, arena.data() + (i * kChunk), kChunk);
                QC::String::memcpy(mix + 64, arena.data() + (j * kChunk), kChunk);
                QC::Sha256(mix, sizeof(mix), state);
                QC::String::memcpy(arena.data() + i * kChunk, state, kChunk);
                secureZero(mix, sizeof(mix));
            }
        }

        QC::u8 finalMix[64];
        QC::String::memcpy(finalMix, state, 32);
        QC::String::memcpy(finalMix + 32, arena.data() + ((kChunks / 2) * kChunk), 32);
        QC::Sha256(finalMix, sizeof(finalMix), outKey);

        secureZero(finalMix, sizeof(finalMix));
        secureZero(seed, sizeof(seed));
        secureZero(state, sizeof(state));
        secureZero(arena.data(), arena.size());
        arena.clear();
        return QC::Status::Success;
    }

    QC::Status SecurityCenter::waitForRotationBoundary(QC::u64 timeoutMs, QC::u32 *outActiveTasks) const
    {
        const QC::u64 start = QK::Time::milliseconds();
        for (;;)
        {
            const QC::u32 activeTasks = static_cast<QC::u32>(QQ::Executor::instance().pendingCount() + QQ::Executor::instance().runningCount());
            if (outActiveTasks)
                *outActiveTasks = activeTasks;
            if (activeTasks == 0)
                return QC::Status::Success;

            const QC::u64 now = QK::Time::milliseconds();
            if (timeoutMs != 0 && now >= start && (now - start) >= timeoutMs)
                return QC::Status::Timeout;
            QK::Time::sleep(5);
        }
    }

    void SecurityCenter::auditDecision(DispatchOp op, bool approved, QC::Status st) const
    {
        publishScEvent(QK::Msg::Topic::ScAudit, decisionAuditCode(op, approved), static_cast<QC::u64>(st));
        publishScEvent(QK::Msg::Topic::ScPolicy, static_cast<QC::u64>(op), approved ? 1ULL : 0ULL);
        (void)appendAuditChainRecord(decisionAuditCode(op, approved), static_cast<QC::u64>(st));
    }

    QC::Status SecurityCenter::allowAuditLogAccess(bool exportRequest)
    {
        if (!ownerUnlocked())
            return QC::Status::Error;

        const QC::u64 now = QK::Time::milliseconds();
        if (now == 0)
            return QC::Status::Success;

        QC::u64 &windowStart = exportRequest ? m_auditExportWindowStartMs : m_auditViewWindowStartMs;
        QC::u32 &windowCount = exportRequest ? m_auditExportWindowCount : m_auditViewWindowCount;
        const QC::u64 windowMs = exportRequest ? 30000ULL : 10000ULL;
        const QC::u32 burst = exportRequest ? 1U : 4U;

        if (windowStart == 0 || now < windowStart || (now - windowStart) >= windowMs)
        {
            windowStart = now;
            windowCount = 0;
        }

        if (windowCount >= burst)
            return QC::Status::Busy;

        ++windowCount;
        return QC::Status::Success;
    }

    void SecurityCenter::redactAuditText(const char *input, char *output, QC::usize cap) const
    {
        if (!output || cap == 0)
            return;
        QC::String::memset(output, 0, cap);
        if (!input || !*input)
            return;

        static const char *kSensitive[] = {
            "WRAPKEY", "SSTWRAP", "recovery code", "owner password", "secret=", "/system/.sc", "/system/sc"
        };
        for (QC::usize i = 0; i < (sizeof(kSensitive) / sizeof(kSensitive[0])); ++i)
        {
            if (containsCaseInsensitive(input, kSensitive[i]))
            {
                setText(output, cap, "[redacted sensitive detail]");
                return;
            }
        }

        QC::String::strncpy(output, input, cap - 1);
        output[cap - 1] = '\0';
    }

    QC::Status SecurityCenter::secureReadFile(const char *path, void *outData, QC::usize cap, QC::usize *outRead) const
    {
        if (!path || !outData || cap == 0)
            return QC::Status::InvalidParam;
        if (outRead)
            *outRead = 0;

        if (isDeniedSecurePath(path))
            return QC::Status::Error;

        if (isSystemLikePath(path) && (!ownerUnlocked() || ownerLockedOut()))
            return QC::Status::Error;

        QFS::FileInfo info{};
        const QC::Status st = QFS::VFS::instance().stat(path, &info);
        if (st != QC::Status::Success)
            return st;
        if (info.type != QFS::FileType::Regular)
            return QC::Status::InvalidParam;

        const QC::usize toRead = static_cast<QC::usize>(info.size);
        if (toRead > cap)
            return QC::Status::InvalidParam;

        QFS::File *f = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
        if (!f)
            return QC::Status::Error;

        QC::usize off = 0;
        while (off < toRead)
        {
            const QC::isize n = f->read(reinterpret_cast<QC::u8 *>(outData) + off, toRead - off);
            if (n <= 0)
            {
                QFS::VFS::instance().close(f);
                return QC::Status::Error;
            }
            off += static_cast<QC::usize>(n);
        }

        QFS::VFS::instance().close(f);
        if (outRead)
            *outRead = off;
        return QC::Status::Success;
    }

    QC::Status SecurityCenter::secureWriteFile(const char *path, const void *data, QC::usize size, bool append) const
    {
        if (!path)
            return QC::Status::InvalidParam;
        if (size > 0 && !data)
            return QC::Status::InvalidParam;

        if (isDeniedSecurePath(path))
            return QC::Status::Error;

        if (isSystemLikePath(path) && (!ownerUnlocked() || ownerLockedOut()))
            return QC::Status::Error;

        QFS::OpenMode mode = QFS::OpenMode::Write | QFS::OpenMode::Create;
        if (!append)
            mode = mode | QFS::OpenMode::Truncate;

        QFS::File *f = QFS::VFS::instance().open(path, mode);
        if (!f)
            return QC::Status::Error;

        if (append)
            (void)f->seek(0, QFS::SeekOrigin::End);

        QC::usize off = 0;
        while (off < size)
        {
            const QC::isize w = f->write(reinterpret_cast<const QC::u8 *>(data) + off, size - off);
            if (w <= 0)
            {
                QFS::VFS::instance().close(f);
                return QC::Status::Error;
            }
            off += static_cast<QC::usize>(w);
        }

        QFS::VFS::instance().close(f);
        return QC::Status::Success;
    }

    QC::Status SecurityCenter::generateInstallRecoveryCode(char outCode[48])
    {
        RecoveryCodeRecordV1 rec{};
        rec.magic = kRecoveryCodeMagic;
        rec.version = 1;
        rec.iterations = 1024;

        QC::u8 raw[10];
        QC::Status st = qkFillRandom(nullptr, raw, sizeof(raw));
        if (st != QC::Status::Success)
            return st;
        st = qkFillRandom(nullptr, rec.salt, sizeof(rec.salt));
        if (st != QC::Status::Success)
        {
            secureZero(raw, sizeof(raw));
            return st;
        }

        static constexpr char kHex[] = "0123456789ABCDEF";
        char code[48];
        QC::String::memset(code, 0, sizeof(code));
        QC::usize pos = 0;
        for (QC::usize i = 0; i < sizeof(raw) && pos + 2 < sizeof(code); ++i)
        {
            if (i != 0 && (i % 2) == 0 && pos + 1 < sizeof(code))
                code[pos++] = '-';
            code[pos++] = kHex[(raw[i] >> 4) & 0xF];
            code[pos++] = kHex[raw[i] & 0xF];
        }
        code[pos] = '\0';

        st = deriveRecoveryKeyMemoryHard(code, rec.salt, rec.iterations, rec.verifier);
        if (st == QC::Status::Success)
            st = QK::SecureStore::writeSealedBlob(kRecoveryCodeKey, &rec, sizeof(rec));

        if (st == QC::Status::Success)
        {
            QC::String::strncpy(m_pendingRecoveryCode, code, sizeof(m_pendingRecoveryCode) - 1);
            m_deferInstallRecoveryCode = false;
            if (outCode)
            {
                QC::String::memset(outCode, 0, 48);
                QC::String::strncpy(outCode, code, 47);
            }
            publishScEvent(QK::Msg::Topic::ScRecovery, 0x52435631ULL, 1);
        }

        secureZero(raw, sizeof(raw));
        return st;
    }

    QC::Status SecurityCenter::consumePendingInstallRecoveryCode(char outCode[48])
    {
        if (!outCode)
            return QC::Status::InvalidParam;
        if (!m_pendingRecoveryCode[0] && m_deferInstallRecoveryCode)
            return QC::Status::Busy;
        if (!m_pendingRecoveryCode[0])
            return QC::Status::NotFound;
        QC::String::memset(outCode, 0, 48);
        QC::String::strncpy(outCode, m_pendingRecoveryCode, 47);
        secureZero(m_pendingRecoveryCode, sizeof(m_pendingRecoveryCode));
        return QC::Status::Success;
    }

    QC::Status SecurityCenter::generatePerUserVaultHeader(const char *username, OwnerVaultHeader &outHeader) const
    {
        if (!username || !username[0] || !m_ownerVrkReady)
            return QC::Status::InvalidParam;

        VaultHeaderRecordV1 rec{};
        rec.magic = kVaultHeaderMagic;
        rec.version = 1;
        rec.iterations = 4096;
        QC::String::strncpy(rec.username, username, sizeof(rec.username) - 1);

        QC::Status st = qkFillRandom(nullptr, rec.salt, sizeof(rec.salt));
        if (st != QC::Status::Success)
            return st;

        QSC::SstDerivedKey sstMix{};
        st = QSC::SecurityCenter::instance().deriveSstKey("VH", sstMix);
        if (st != QC::Status::Success)
            return st;

        QC::u8 mix[32 + 16 + 4];
        QC::String::memcpy(mix, m_ownerVrk, 32);
        QC::String::memcpy(mix + 32, rec.salt, 16);
        QC::String::memcpy(mix + 48, &rec.iterations, sizeof(rec.iterations));
        hmacSha256(sstMix.bytes, sstMix.size, mix, sizeof(mix), rec.wrappedVrk);
        secureZero(mix, sizeof(mix));
        secureZero(sstMix.bytes, sizeof(sstMix.bytes));

        st = QK::SecureStore::writeSealedBlob(kVaultHeaderKey, &rec, sizeof(rec));
        if (st != QC::Status::Success)
            return st;

        outHeader.valid = true;
        QC::String::strncpy(outHeader.username, rec.username, sizeof(outHeader.username) - 1);
        QC::String::memcpy(outHeader.salt, rec.salt, sizeof(outHeader.salt));
        outHeader.iterations = rec.iterations;
        QC::String::memcpy(outHeader.wrappedVrk, rec.wrappedVrk, sizeof(outHeader.wrappedVrk));
        publishScEvent(QK::Msg::Topic::ScVault, 0x56484431ULL, 1);
        return QC::Status::Success;
    }

    QC::Status SecurityCenter::scanPayload(const char *label, const void *data, QC::usize size, PayloadScanResult &out) const
    {
        out = PayloadScanResult{};
        if (!data || size == 0)
        {
            setText(out.detail, sizeof(out.detail), "empty payload denied");
            return QC::Status::InvalidParam;
        }

        static const char *kDeniedTokens[] = {
            "#!/", "<script", "powershell", "wget ", "curl ", "rm -rf", "WRAPKEY", "SSTWRAP", "/system/.sc", "recovery code"
        };

        if (label && (containsCaseInsensitive(label, "../") || containsCaseInsensitive(label, "..\\") || containsCaseInsensitive(label, "/system/.sc")))
        {
            out.suspicious = true;
            setText(out.detail, sizeof(out.detail), "payload label denied");
            return QC::Status::Error;
        }

        const QC::usize inspectSize = (size > 4096) ? 4096 : size;
        const QC::u8 *bytes = reinterpret_cast<const QC::u8 *>(data);
        for (QC::usize i = 0; i < (sizeof(kDeniedTokens) / sizeof(kDeniedTokens[0])); ++i)
        {
            if (containsBytesCaseInsensitive(bytes, inspectSize, kDeniedTokens[i]))
            {
                out.suspicious = true;
                setText(out.detail, sizeof(out.detail), "payload denied by scan policy");
                return QC::Status::Error;
            }
        }

        out.allowed = true;
        setText(out.detail, sizeof(out.detail), "payload scan passed");
        return QC::Status::Success;
    }

    QC::Status SecurityCenter::quarantinePayload(const char *artifactId, const void *data, QC::usize size, char outPath[192]) const
    {
        if (outPath)
            QC::String::memset(outPath, 0, 192);
        if (!ensureArtifactDirs(true))
            return QC::Status::Error;

        char safeId[96];
        sanitizeArtifactId(artifactId, safeId, sizeof(safeId));

        char path[192];
        QC::String::memset(path, 0, sizeof(path));
        QC::String::strncpy(path, "/system/quarantine/", sizeof(path) - 1);
        const QC::usize used = QC::String::strlen(path);
        if (used + 1 < sizeof(path))
            QC::String::strncpy(path + used, safeId, sizeof(path) - used - 1);
        const QC::usize used2 = QC::String::strlen(path);
        if (used2 + 5 < sizeof(path))
            QC::String::strncpy(path + used2, ".bad", sizeof(path) - used2 - 1);

        const QC::Status st = writeArtifactFile(path, data, size);
        if (st == QC::Status::Success && outPath)
            QC::String::strncpy(outPath, path, 191);
        return st;
    }

    QC::Status SecurityCenter::parkVerifiedUpdate(const char *artifactId, const void *data, QC::usize size, char outPath[192]) const
    {
        if (outPath)
            QC::String::memset(outPath, 0, 192);
        if (!ensureArtifactDirs(false))
            return QC::Status::Error;

        char safeId[96];
        sanitizeArtifactId(artifactId, safeId, sizeof(safeId));

        char path[192];
        QC::String::memset(path, 0, sizeof(path));
        QC::String::strncpy(path, "/system/updates/verified/modules/", sizeof(path) - 1);
        const QC::usize used = QC::String::strlen(path);
        if (used + 1 < sizeof(path))
            QC::String::strncpy(path + used, safeId, sizeof(path) - used - 1);
        const QC::usize used2 = QC::String::strlen(path);
        if (used2 + 5 < sizeof(path))
            QC::String::strncpy(path + used2, ".bin", sizeof(path) - used2 - 1);

        const QC::Status st = writeArtifactFile(path, data, size);
        if (st == QC::Status::Success && outPath)
            QC::String::strncpy(outPath, path, 191);
        return st;
    }

    void SecurityCenter::setRotationScheduleConfig(const RotationScheduleConfig &config)
    {
        m_rotationSchedule = config;
        if (m_rotationSchedule.minExecutedTasks == 0)
            m_rotationSchedule.minExecutedTasks = 1;
    }

    bool SecurityCenter::shouldForceSstRotation() const
    {
        if (!m_rotationSchedule.enabled)
            return false;

        const QSC::SstStatus st = QSC::SecurityCenter::instance().sstStatus();
        if (!st.available || st.generation == 0)
            return false;

        const QQ::Executor::PerformanceCounters perf = QQ::Executor::instance().performanceCounters();
        const bool execDue = perf.totalExecuted >= m_lastRotationExecCount && (perf.totalExecuted - m_lastRotationExecCount) >= m_rotationSchedule.minExecutedTasks;

        bool timeDue = false;
        const QC::u64 now = QK::Time::milliseconds();
        if (now != 0 && m_lastRotationMs != 0 && now >= m_lastRotationMs)
            timeDue = (now - m_lastRotationMs) >= m_rotationSchedule.minIntervalMs;

        return execDue || timeDue || (st.generation >= 8 && (st.generation % 8) == 0);
    }

    QC::Status SecurityCenter::maybeForceRotateSst(QC::u64 boundaryTimeoutMs)
    {
        m_sstRetiring = true;
        const QC::u8 retiringMark = 1;
        (void)QK::SecureStore::writeBlob(kRetiringSstMarkerKey, &retiringMark, 1);

        const QC::u64 effectiveTimeout = (boundaryTimeoutMs != 0) ? boundaryTimeoutMs : m_rotationSchedule.boundaryTimeoutMs;
        QC::u32 activeTasks = 0;
        QC::Status st = waitForRotationBoundary(effectiveTimeout, &activeTasks);
        if (st != QC::Status::Success)
        {
            publishScEvent(QK::Msg::Topic::ScAudit, rotationAuditCode(false, false), static_cast<QC::u64>(st));
            m_sstRetiring = false;
            return st;
        }

        const QSC::SstStatus before = QSC::SecurityCenter::instance().sstStatus();
        publishScEvent(QK::Msg::Topic::ScAudit, rotationAuditCode(true, false), before.generation);

        QSC::SstRotationRequest rot{};
        rot.forced = true;
        rot.reasonCode = 0x53535431u;
        st = QSC::SecurityCenter::instance().requestSstRotation(rot);
        const QSC::SstStatus after = QSC::SecurityCenter::instance().sstStatus();
        if (st == QC::Status::Success)
        {
            m_lastRotationMs = QK::Time::milliseconds();
            m_lastRotationExecCount = QQ::Executor::instance().performanceCounters().totalExecuted;
            const QC::u8 oldDestroyed = 1;
            (void)QK::SecureStore::writeBlob(kOldSstRetiredKey, &oldDestroyed, 1);
        }
        publishScEvent(QK::Msg::Topic::ScAudit, rotationAuditCode(false, st == QC::Status::Success), after.generation);
        (void)QK::SecureStore::removeBlob(kRetiringSstMarkerKey);
        m_sstRetiring = false;
        return st;
    }

    void SecurityCenter::emitProvisioningCompletedAuditEvent(QC::u64 code) const
    {
        publishScEvent(QK::Msg::Topic::ScAudit, code, 1);
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
