#include "QKBootTPMSecureStore.h"
#include "QKBootTPMCommands.h"
#include "QKBootTPMDiag.h"
#include "QKBootTPMSecureBoot.h"
#include "QKBootTPMEventLog.h"
#include "QKBootTPMLock.h"
#include "QKBootTPMProbePolicy.h"
#include "QKBootTPMTransport.h"

#include "QCString.h"
#include "QCSha256.h"
#include "QFSFile.h"
#include "QFSVFS.h"
#include "QKEntropy.h"
#include "QKSecureStore.h"

#include "Debug/Serial/QKDebugSerial.h"

namespace QK::Boot::Tpm
{
    using namespace QK::Boot::Tpm::Internal;

    namespace
    {
        struct FTpmSecureStoreCtx
        {
            bool bReady = false;
        };

        static FTpmSecureStoreCtx g_TpmSecureStore;

        static void SerialStr(const char *Msg)
        {
            QK::Debug::Serial::Write(Msg);
        }

        static void SerialHex32Fixed(QC::u32 Value)
        {
            char HexBuf[9];
            for (int i = 7; i >= 0; --i)
            {
                int nibble = static_cast<int>((Value >> (i * 4)) & 0xF);
                HexBuf[7 - i] = nibble < 10 ? static_cast<char>('0' + nibble) : static_cast<char>('a' + nibble - 10);
            }
            HexBuf[8] = 0;
            SerialStr(HexBuf);
        }

        static QC::Status SecureStoreTpmSealWrapKeyLocked(void *User, const QC::u8 *WrapKey, QC::usize WrapKeyLen,
                                                          QC::Vector<QC::u8> &OutBlob, const QC::u8 *TargetPcr = nullptr)
        {
            if (!User || !WrapKey || WrapKeyLen != 32)
                return QC::Status::InvalidParam;

            auto *Dev = static_cast<FTpmSecureStoreCtx *>(User);
            if (!Dev->bReady)
                return QC::Status::Busy;

            QC::u32 PolicyDigestSession = 0;
            QC::Status St = Cmd::TpmStartPolicySession(nullptr, TargetPcr != nullptr, PolicyDigestSession);
            if (St != QC::Status::Success)
            {
                SerialStr("SecureStoreTPM: StartAuthSession(POLICY-DIGEST) failed (rsp=0x");
                SerialHex32Fixed(Cmd::LastResponseCode());
                SerialStr(")\r\n");
                return St;
            }

            QC::u8 TargetDigest[32] = {};
            if (TargetPcr)
                QC::Sha256(TargetPcr, 32, TargetDigest);
            St = Cmd::TpmPolicyPCR(nullptr, PolicyDigestSession, TargetPcr ? TargetDigest : nullptr);
            QC::String::memset(TargetDigest, 0, sizeof(TargetDigest));
            if (St != QC::Status::Success)
            {
                SerialStr("SecureStoreTPM: PolicyPCR(POLICY-DIGEST) failed (rsp=0x");
                SerialHex32Fixed(Cmd::LastResponseCode());
                SerialStr(")\r\n");
                (void)Cmd::TpmFlushContext(nullptr, PolicyDigestSession);
                return St;
            }

            QC::u8 PolicyDigest[32];
            St = Cmd::TpmPolicyGetDigest(nullptr, PolicyDigestSession, PolicyDigest);
            (void)Cmd::TpmFlushContext(nullptr, PolicyDigestSession);
            if (St != QC::Status::Success)
            {
                SerialStr("SecureStoreTPM: PolicyGetDigest failed (rsp=0x");
                SerialHex32Fixed(Cmd::LastResponseCode());
                SerialStr(")\r\n");
                return St;
            }

            QC::u32 Primary = 0;
            St = Cmd::TpmCreatePrimaryStorageKey(nullptr, Primary);
            if (St != QC::Status::Success)
            {
                SerialStr("SecureStoreTPM: CreatePrimary failed (rsp=0x");
                SerialHex32Fixed(Cmd::LastResponseCode());
                SerialStr(")\r\n");
                return St;
            }

            QC::Vector<QC::u8> Priv2b;
            QC::Vector<QC::u8> Pub2b;
            St = Cmd::TpmCreateSealedObject(nullptr, Primary, WrapKey, WrapKeyLen, PolicyDigest, Priv2b, Pub2b);
            (void)Cmd::TpmFlushContext(nullptr, Primary);

            if (St != QC::Status::Success)
            {
                SerialStr("SecureStoreTPM: Create failed (rsp=0x");
                SerialHex32Fixed(Cmd::LastResponseCode());
                SerialStr(")\r\n");
                if (Priv2b.size() > 0)
                    QC::String::memset(Priv2b.data(), 0, Priv2b.size());
                if (Pub2b.size() > 0)
                    QC::String::memset(Pub2b.data(), 0, Pub2b.size());
                Priv2b.clear();
                Pub2b.clear();
                return St;
            }

            const QC::u32 Ver = 1;
            const QC::u32 PrivLen = static_cast<QC::u32>(Priv2b.size());
            const QC::u32 PubLen = static_cast<QC::u32>(Pub2b.size());
            OutBlob.clear();
            OutBlob.resize(4 + 4 + 4 + 4 + Priv2b.size() + Pub2b.size());

            auto PutLe32 = [](QC::u8 *P, QC::u32 V)
            {
                P[0] = static_cast<QC::u8>(V & 0xFF);
                P[1] = static_cast<QC::u8>((V >> 8) & 0xFF);
                P[2] = static_cast<QC::u8>((V >> 16) & 0xFF);
                P[3] = static_cast<QC::u8>((V >> 24) & 0xFF);
            };

            OutBlob[0] = 'W';
            OutBlob[1] = 'K';
            OutBlob[2] = 'T';
            OutBlob[3] = '1';
            PutLe32(OutBlob.data() + 4, Ver);
            PutLe32(OutBlob.data() + 8, PrivLen);
            PutLe32(OutBlob.data() + 12, PubLen);

            QC::usize O = 16;
            for (QC::usize i = 0; i < Priv2b.size(); ++i)
                OutBlob[O++] = Priv2b[i];
            for (QC::usize i = 0; i < Pub2b.size(); ++i)
                OutBlob[O++] = Pub2b[i];

            QC::String::memset(Priv2b.data(), 0, Priv2b.size());
            QC::String::memset(Pub2b.data(), 0, Pub2b.size());
            Priv2b.clear();
            Pub2b.clear();
            return QC::Status::Success;
        }

        static QC::Status SecureStoreTpmUnsealWrapKeyLocked(void *User, const QC::Vector<QC::u8> &Blob,
                                                            QC::u8 *OutWrapKey, QC::usize OutWrapKeyLen,
                                                            QK::SecureStore::WrapKeyDiagnostic *Diagnostic = nullptr)
        {
            if (Diagnostic)
                Diagnostic->operation = "tpm-validate";
            if (!User || !OutWrapKey || OutWrapKeyLen != 32)
                return QC::Status::InvalidParam;

            auto *Dev = static_cast<FTpmSecureStoreCtx *>(User);
            if (!Dev->bReady)
            {
                if (Diagnostic)
                    Diagnostic->operation = "tpm-not-ready";
                return QC::Status::Busy;
            }

            if (Blob.size() < 16)
                return QC::Status::Error;
            if (!(Blob[0] == 'W' && Blob[1] == 'K' && Blob[2] == 'T' && Blob[3] == '1'))
                return QC::Status::Error;

            auto GetLe32 = [](const QC::u8 *P) -> QC::u32
            {
                return static_cast<QC::u32>(P[0]) |
                       (static_cast<QC::u32>(P[1]) << 8) |
                       (static_cast<QC::u32>(P[2]) << 16) |
                       (static_cast<QC::u32>(P[3]) << 24);
            };

            const QC::u32 Ver = GetLe32(Blob.data() + 4);
            if (Ver != 1)
                return QC::Status::Error;
            const QC::u32 PrivLen = GetLe32(Blob.data() + 8);
            const QC::u32 PubLen = GetLe32(Blob.data() + 12);
            const QC::usize Total = 16u + static_cast<QC::usize>(PrivLen) + static_cast<QC::usize>(PubLen);
            if (Total != Blob.size())
                return QC::Status::Error;

            const auto CaptureFailure = [Diagnostic]()
            {
                if (Diagnostic && Cmd::LastResponseCode() != 0)
                {
                    Diagnostic->hasTpmResult = true;
                    Diagnostic->tpmResult = Cmd::LastResponseCode();
                }
            };
            if (Diagnostic)
            {
                Cmd::ClearLastResponseCode();
                Diagnostic->operation = "start-policy-session";
            }
            QC::u32 PolicySession = 0;
            QC::Status St = Cmd::TpmStartPolicySession(nullptr, false, PolicySession);
            if (St != QC::Status::Success)
            {
                CaptureFailure();
                SerialStr("SecureStoreTPM: StartAuthSession(POLICY) failed (rsp=0x");
                SerialHex32Fixed(Cmd::LastResponseCode());
                SerialStr(")\r\n");
                return St;
            }

            if (Diagnostic)
                Diagnostic->operation = "policy-pcr";
            St = Cmd::TpmPolicyPCR(nullptr, PolicySession);
            if (St != QC::Status::Success)
            {
                CaptureFailure();
                SerialStr("SecureStoreTPM: PolicyPCR failed (rsp=0x");
                SerialHex32Fixed(Cmd::LastResponseCode());
                SerialStr(")\r\n");
                (void)Cmd::TpmFlushContext(nullptr, PolicySession);
                return St;
            }

            QC::Vector<QC::u8> Priv2b;
            QC::Vector<QC::u8> Pub2b;
            Priv2b.resize(PrivLen);
            Pub2b.resize(PubLen);

            QC::usize O = 16;
            for (QC::usize i = 0; i < Priv2b.size(); ++i)
                Priv2b[i] = Blob[O++];
            for (QC::usize i = 0; i < Pub2b.size(); ++i)
                Pub2b[i] = Blob[O++];

            QC::u32 Primary = 0;
            if (Diagnostic)
                Diagnostic->operation = "create-primary";
            St = Cmd::TpmCreatePrimaryStorageKey(nullptr, Primary);
            if (St != QC::Status::Success)
            {
                CaptureFailure();
                SerialStr("SecureStoreTPM: CreatePrimary failed (rsp=0x");
                SerialHex32Fixed(Cmd::LastResponseCode());
                SerialStr(")\r\n");
                QC::String::memset(Priv2b.data(), 0, Priv2b.size());
                QC::String::memset(Pub2b.data(), 0, Pub2b.size());
                Priv2b.clear();
                Pub2b.clear();
                (void)Cmd::TpmFlushContext(nullptr, PolicySession);
                return St;
            }

            QC::u32 Obj = 0;
            if (Diagnostic)
                Diagnostic->operation = "load-object";
            St = Cmd::TpmLoadSealedObject(nullptr, Primary, Priv2b, Pub2b, Obj);
            if (St != QC::Status::Success)
                CaptureFailure();

            QC::String::memset(Priv2b.data(), 0, Priv2b.size());
            QC::String::memset(Pub2b.data(), 0, Pub2b.size());
            Priv2b.clear();
            Pub2b.clear();

            if (St != QC::Status::Success)
            {
                SerialStr("SecureStoreTPM: Load failed (rsp=0x");
                SerialHex32Fixed(Cmd::LastResponseCode());
                SerialStr(")\r\n");
                (void)Cmd::TpmFlushContext(nullptr, Primary);
                (void)Cmd::TpmFlushContext(nullptr, PolicySession);
                return St;
            }

            if (Diagnostic)
                Diagnostic->operation = "unseal";
            St = Cmd::TpmUnsealWithAuthSession(nullptr, Obj, PolicySession, OutWrapKey, OutWrapKeyLen);
            if (St != QC::Status::Success)
            {
                CaptureFailure();
                SerialStr("SecureStoreTPM: Unseal failed (rsp=0x");
                SerialHex32Fixed(Cmd::LastResponseCode());
                SerialStr(")\r\n");
            }

            (void)Cmd::TpmFlushContext(nullptr, Obj);
            (void)Cmd::TpmFlushContext(nullptr, Primary);
            // continueSession is clear, so a successful Unseal already closed the session.
            if (St != QC::Status::Success)
                (void)Cmd::TpmFlushContext(nullptr, PolicySession);
            else if (Diagnostic)
                Diagnostic->operation = "ok";
            return St;
        }

        static QC::Status SecureStoreTpmSealSecretLocked(void *User, const QC::u8 *Secret, QC::usize SecretLen, QC::Vector<QC::u8> &OutBlob)
        {
            if (!User || !Secret || SecretLen == 0)
                return QC::Status::InvalidParam;

            // Current sealed-object template only supports small secrets.
            if (SecretLen > 64)
                return QC::Status::NotSupported;

            auto *Dev = static_cast<FTpmSecureStoreCtx *>(User);
            if (!Dev->bReady)
                return QC::Status::Busy;

            QC::u32 PolicyDigestSession = 0;
            QC::Status St = Cmd::TpmStartPolicySession(nullptr, false, PolicyDigestSession);
            if (St != QC::Status::Success)
                return St;

            St = Cmd::TpmPolicyPCR(nullptr, PolicyDigestSession);
            if (St != QC::Status::Success)
            {
                (void)Cmd::TpmFlushContext(nullptr, PolicyDigestSession);
                return St;
            }

            QC::u8 PolicyDigest[32];
            St = Cmd::TpmPolicyGetDigest(nullptr, PolicyDigestSession, PolicyDigest);
            (void)Cmd::TpmFlushContext(nullptr, PolicyDigestSession);
            if (St != QC::Status::Success)
                return St;

            QC::u32 Primary = 0;
            St = Cmd::TpmCreatePrimaryStorageKey(nullptr, Primary);
            if (St != QC::Status::Success)
                return St;

            QC::Vector<QC::u8> Priv2b;
            QC::Vector<QC::u8> Pub2b;
            St = Cmd::TpmCreateSealedObject(nullptr, Primary, Secret, SecretLen, PolicyDigest, Priv2b, Pub2b);
            (void)Cmd::TpmFlushContext(nullptr, Primary);

            if (St != QC::Status::Success)
            {
                if (Priv2b.size() > 0)
                    QC::String::memset(Priv2b.data(), 0, Priv2b.size());
                if (Pub2b.size() > 0)
                    QC::String::memset(Pub2b.data(), 0, Pub2b.size());
                Priv2b.clear();
                Pub2b.clear();
                return St;
            }

            const QC::u32 Ver = 1;
            const QC::u32 SecretLen32 = static_cast<QC::u32>(SecretLen);
            const QC::u32 PrivLen = static_cast<QC::u32>(Priv2b.size());
            const QC::u32 PubLen = static_cast<QC::u32>(Pub2b.size());
            OutBlob.clear();
            OutBlob.resize(4 + 4 + 4 + 4 + 4 + Priv2b.size() + Pub2b.size());

            auto PutLe32 = [](QC::u8 *P, QC::u32 V)
            {
                P[0] = static_cast<QC::u8>(V & 0xFF);
                P[1] = static_cast<QC::u8>((V >> 8) & 0xFF);
                P[2] = static_cast<QC::u8>((V >> 16) & 0xFF);
                P[3] = static_cast<QC::u8>((V >> 24) & 0xFF);
            };

            OutBlob[0] = 'S';
            OutBlob[1] = 'S';
            OutBlob[2] = 'T';
            OutBlob[3] = '1';
            PutLe32(OutBlob.data() + 4, Ver);
            PutLe32(OutBlob.data() + 8, SecretLen32);
            PutLe32(OutBlob.data() + 12, PrivLen);
            PutLe32(OutBlob.data() + 16, PubLen);

            QC::usize O = 20;
            for (QC::usize i = 0; i < Priv2b.size(); ++i)
                OutBlob[O++] = Priv2b[i];
            for (QC::usize i = 0; i < Pub2b.size(); ++i)
                OutBlob[O++] = Pub2b[i];

            QC::String::memset(Priv2b.data(), 0, Priv2b.size());
            QC::String::memset(Pub2b.data(), 0, Pub2b.size());
            Priv2b.clear();
            Pub2b.clear();
            return QC::Status::Success;
        }

        static QC::Status SecureStoreTpmUnsealSecretLocked(void *User,
                                                           const QC::Vector<QC::u8> &Blob,
                                                           QC::u8 *OutSecret,
                                                           QC::usize OutSecretCap,
                                                           QC::usize *OutSecretLen)
        {
            if (!User || !OutSecret || !OutSecretLen)
                return QC::Status::InvalidParam;

            auto *Dev = static_cast<FTpmSecureStoreCtx *>(User);
            if (!Dev->bReady)
                return QC::Status::Busy;

            if (Blob.size() < 20)
                return QC::Status::Error;
            if (!(Blob[0] == 'S' && Blob[1] == 'S' && Blob[2] == 'T' && Blob[3] == '1'))
                return QC::Status::Error;

            auto GetLe32 = [](const QC::u8 *P) -> QC::u32
            {
                return static_cast<QC::u32>(P[0]) |
                       (static_cast<QC::u32>(P[1]) << 8) |
                       (static_cast<QC::u32>(P[2]) << 16) |
                       (static_cast<QC::u32>(P[3]) << 24);
            };

            const QC::u32 Ver = GetLe32(Blob.data() + 4);
            if (Ver != 1)
                return QC::Status::Error;

            const QC::u32 SecretLen32 = GetLe32(Blob.data() + 8);
            const QC::u32 PrivLen = GetLe32(Blob.data() + 12);
            const QC::u32 PubLen = GetLe32(Blob.data() + 16);

            if (SecretLen32 == 0 || SecretLen32 > 64)
                return QC::Status::Error;

            const QC::usize Total = 20u + static_cast<QC::usize>(PrivLen) + static_cast<QC::usize>(PubLen);
            if (Total != Blob.size())
                return QC::Status::Error;

            const QC::usize SecretLen = static_cast<QC::usize>(SecretLen32);
            if (OutSecretCap < SecretLen)
                return QC::Status::OutOfMemory;

            QC::u32 PolicySession = 0;
            QC::Status St = Cmd::TpmStartPolicySession(nullptr, false, PolicySession);
            if (St != QC::Status::Success)
                return St;

            St = Cmd::TpmPolicyPCR(nullptr, PolicySession);
            if (St != QC::Status::Success)
            {
                (void)Cmd::TpmFlushContext(nullptr, PolicySession);
                return St;
            }

            QC::Vector<QC::u8> Priv2b;
            QC::Vector<QC::u8> Pub2b;
            Priv2b.resize(PrivLen);
            Pub2b.resize(PubLen);

            QC::usize O = 20;
            for (QC::usize i = 0; i < Priv2b.size(); ++i)
                Priv2b[i] = Blob[O++];
            for (QC::usize i = 0; i < Pub2b.size(); ++i)
                Pub2b[i] = Blob[O++];

            QC::u32 Primary = 0;
            St = Cmd::TpmCreatePrimaryStorageKey(nullptr, Primary);
            if (St != QC::Status::Success)
            {
                QC::String::memset(Priv2b.data(), 0, Priv2b.size());
                QC::String::memset(Pub2b.data(), 0, Pub2b.size());
                Priv2b.clear();
                Pub2b.clear();
                (void)Cmd::TpmFlushContext(nullptr, PolicySession);
                return St;
            }

            QC::u32 Obj = 0;
            St = Cmd::TpmLoadSealedObject(nullptr, Primary, Priv2b, Pub2b, Obj);

            QC::String::memset(Priv2b.data(), 0, Priv2b.size());
            QC::String::memset(Pub2b.data(), 0, Pub2b.size());
            Priv2b.clear();
            Pub2b.clear();

            if (St != QC::Status::Success)
            {
                (void)Cmd::TpmFlushContext(nullptr, Primary);
                (void)Cmd::TpmFlushContext(nullptr, PolicySession);
                return St;
            }

            St = Cmd::TpmUnsealWithAuthSession(nullptr, Obj, PolicySession, OutSecret, SecretLen);
            if (St == QC::Status::Success)
                *OutSecretLen = SecretLen;
            else
                *OutSecretLen = 0;

            (void)Cmd::TpmFlushContext(nullptr, Obj);
            (void)Cmd::TpmFlushContext(nullptr, Primary);
            // continueSession is clear, so a successful Unseal already closed the session.
            if (St != QC::Status::Success)
                (void)Cmd::TpmFlushContext(nullptr, PolicySession);
            return St;
        }

        // SecureStore callbacks: each runs a multi-command TPM session, so the
        // whole callback holds the TPM operation lock.
        static QC::Status SecureStoreTpmSealWrapKey(void *User, const QC::u8 *WrapKey, QC::usize WrapKeyLen, QC::Vector<QC::u8> &OutBlob)
        {
            FTpmOpGuard Guard;
            if (!Guard.Acquired())
                return QC::Status::Busy;
            return SecureStoreTpmSealWrapKeyLocked(User, WrapKey, WrapKeyLen, OutBlob);
        }

        static QC::Status SecureStoreTpmSealWrapKeyForPcr7(void *User, const QC::u8 *WrapKey,
                                                            QC::usize WrapKeyLen, const QC::u8 TargetPcr[32],
                                                            QC::Vector<QC::u8> &OutBlob)
        {
            if (!User || !TargetPcr)
                return QC::Status::InvalidParam;
            FTpmOpGuard Guard;
            if (!Guard.Acquired())
                return QC::Status::Busy;
            QC::u8 CurrentPcr[32];
            QC::u32 Counter = 0;
            const QC::Status ReadSt = Cmd::TpmPcrReadSha256(7, CurrentPcr, Counter);
            if (ReadSt != QC::Status::Success)
                return ReadSt;
            if (QC::String::memcmp(CurrentPcr, TargetPcr, sizeof(CurrentPcr)) == 0)
                return QC::Status::InvalidParam;
            return SecureStoreTpmSealWrapKeyLocked(User, WrapKey, WrapKeyLen, OutBlob, TargetPcr);
        }

        static QC::Status SecureStoreTpmUnsealWrapKey(void *User, const QC::Vector<QC::u8> &Blob, QC::u8 *OutWrapKey, QC::usize OutWrapKeyLen)
        {
            FTpmOpGuard Guard;
            if (!Guard.Acquired())
                return QC::Status::Busy;
            return SecureStoreTpmUnsealWrapKeyLocked(User, Blob, OutWrapKey, OutWrapKeyLen);
        }

        static QC::Status SecureStoreTpmDiagnoseUnsealWrapKey(void *User, const QC::Vector<QC::u8> &Blob,
                                                               QC::u8 *OutWrapKey, QC::usize OutWrapKeyLen,
                                                               QK::SecureStore::WrapKeyDiagnostic &Diagnostic)
        {
            FTpmOpGuard Guard;
            if (!Guard.Acquired())
            {
                Diagnostic.operation = "tpm-lock";
                return QC::Status::Busy;
            }
            return SecureStoreTpmUnsealWrapKeyLocked(User, Blob, OutWrapKey, OutWrapKeyLen, &Diagnostic);
        }

        static QC::Status SecureStoreTpmSealSecret(void *User, const QC::u8 *Secret, QC::usize SecretLen, QC::Vector<QC::u8> &OutBlob)
        {
            FTpmOpGuard Guard;
            if (!Guard.Acquired())
                return QC::Status::Busy;
            return SecureStoreTpmSealSecretLocked(User, Secret, SecretLen, OutBlob);
        }

        static QC::Status SecureStoreTpmUnsealSecret(void *User,
                                                     const QC::Vector<QC::u8> &Blob,
                                                     QC::u8 *OutSecret,
                                                     QC::usize OutSecretCap,
                                                     QC::usize *OutSecretLen)
        {
            FTpmOpGuard Guard;
            if (!Guard.Acquired())
                return QC::Status::Busy;
            return SecureStoreTpmUnsealSecretLocked(User, Blob, OutSecret, OutSecretCap, OutSecretLen);
        }

        static void SecureStoreSelfTest(FLogFn Log)
        {
            LogStr(Log, "SecureStore: self-test...\r\n");

            QC::Status St = QK::SecureStore::ensureBaseDir();
            if (St != QC::Status::Success)
            {
                LogStr(Log, "SecureStore: FAIL (ensureBaseDir)\r\n");
                return;
            }

            if (!QK::SecureStore::tpm_present())
            {
                QC::u8 WrapKey[32];
                St = QK::SecureStore::readWrapKey(WrapKey);
                QC::String::memset(WrapKey, 0, sizeof(WrapKey));

                if (St == QC::Status::Busy)
                {
                    LogStr(Log, "SecureStore: self-test SKIP (non-TPM wrap key locked)\r\n");
                    return;
                }

                if (St == QC::Status::NotFound)
                {
                    LogStr(Log, "SecureStore: self-test SKIP (non-TPM wrap key not provisioned)\r\n");
                    return;
                }

                if (St != QC::Status::Success)
                {
                    LogStr(Log, "SecureStore: FAIL (readWrapKey)\r\n");
                    return;
                }
            }

            QC::u8 Plain[96];
            (void)QK::Entropy::fillRandom(Plain, sizeof(Plain));

            St = QK::SecureStore::writeSealedBlob("SSTEST.BIN", Plain, sizeof(Plain));
            if (St != QC::Status::Success)
            {
                LogStr(Log, "SecureStore: FAIL (writeSealedBlob)\r\n");
                return;
            }

            QC::Vector<QC::u8> Out;
            St = QK::SecureStore::readSealedBlob("SSTEST.BIN", Out);
            if (St != QC::Status::Success)
            {
                LogStr(Log, "SecureStore: FAIL (readSealedBlob)\r\n");
                (void)QK::SecureStore::removeBlob("SSTEST.BIN");
                return;
            }

            bool bOk = (Out.size() == sizeof(Plain));
            if (bOk)
            {
                bOk = (QC::String::memcmp(Out.data(), Plain, sizeof(Plain)) == 0);
            }

            (void)QK::SecureStore::removeBlob("SSTEST.BIN");
            QC::String::memset(Plain, 0, sizeof(Plain));
            if (Out.size() > 0)
                QC::String::memset(Out.data(), 0, Out.size());
            Out.clear();

            LogStr(Log, bOk ? "SecureStore: PASS\r\n" : "SecureStore: FAIL (mismatch)\r\n");
        }

        static bool ShouldRunPcrMismatchTest(QFS::VFS *Vfs)
        {
            if (!Vfs)
                return false;
            QFS::File *F = Vfs->open("/PCRTEST.FLG", QFS::OpenMode::Read);
            if (!F)
                return false;
            delete F;
            return true;
        }

        static void SecureStorePcrMismatchTest(QFS::VFS *Vfs, FLogFn Log)
        {
            if (!ShouldRunPcrMismatchTest(Vfs))
                return;

            if (!g_TpmSecureStore.bReady)
            {
                LogStr(Log, "SecureStore: PCR mismatch test SKIP (no TPM)\r\n");
                return;
            }

            LogStr(Log, "SecureStore: PCR mismatch test...\r\n");

            QC::u8 Plain[64];
            (void)QK::Entropy::fillRandom(Plain, sizeof(Plain));
            QC::Status St = QK::SecureStore::writeSealedBlob("PCRNEG.BIN", Plain, sizeof(Plain));
            if (St != QC::Status::Success)
            {
                LogStr(Log, "SecureStore: PCR mismatch test FAIL (write)\r\n");
                return;
            }

            QC::u8 ExtendDigest[32];
            for (QC::usize i = 0; i < sizeof(ExtendDigest); ++i)
                ExtendDigest[i] = static_cast<QC::u8>(0x42u ^ static_cast<QC::u8>(i));

            if (!ExtendPcrSha256Digest(7, ExtendDigest, Log, "pcr-mismatch-test"))
            {
                LogStr(Log, "SecureStoreTPM: PCR_Extend failed (rsp=0x");
                LogHex32Fixed(Log, Cmd::LastResponseCode());
                LogStr(Log, ")\r\n");
                LogStr(Log, "SecureStore: PCR mismatch test FAIL (extend)\r\n");
                (void)QK::SecureStore::removeBlob("PCRNEG.BIN");
                return;
            }

            QC::Vector<QC::u8> Out;
            St = QK::SecureStore::readSealedBlob("PCRNEG.BIN", Out);

            const bool bExpectedFail = (St != QC::Status::Success);
            LogStr(Log, bExpectedFail ? "SecureStore: PCR mismatch test PASS (unseal blocked)\r\n"
                                      : "SecureStore: PCR mismatch test FAIL (unexpected unseal)\r\n");

            (void)QK::SecureStore::removeBlob("PCRNEG.BIN");
            (void)QK::SecureStore::removeBlob("WRAPKEY.TPM");
            (void)QK::SecureStore::removeBlob("WRAPKEY.BIN");
            QC::String::memset(Plain, 0, sizeof(Plain));
            if (Out.size() > 0)
                QC::String::memset(Out.data(), 0, Out.size());
            Out.clear();
        }
    }


    void TryTpm2Startup(QC::u32 StartMethod, QC::PhysAddr ControlAreaPhys, QC::u64 EventLogPhys, QC::u32 EventLogLen, FLogFn Log)
    {
        EventLog::SetFirmwareLog(EventLogPhys, EventLogLen);

        if (!Transport::Init(StartMethod, ControlAreaPhys, Log))
            return;

        FTpmOpGuard Guard;
        if (!Guard.Acquired())
        {
            LogStr(Log, "TPM2: busy during startup; skipping TPM commands\r\n");
            return;
        }

        const QC::u8 StartupCmd[12] = {0x80, 0x01, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x01, 0x44, 0x00, 0x00};
        const QC::u8 GetRandomCmd[12] = {0x80, 0x01, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x01, 0x7B, 0x00, 0x10};
        constexpr QC::u32 kMaxTpmWarmupAttempts = QK::Boot::Tpm::kTpmWarmupAttempts;
        const char *TransportName = Transport::TypeName(Transport::Info().Type);

        QC::u8 Entropy[16];
        QC::usize EntropyLen = 0;

        for (QC::u32 attempt = 0; attempt < kMaxTpmWarmupAttempts; ++attempt)
        {
            if (attempt > 0)
            {
                const QC::u32 delayMs = QK::Boot::Tpm::kTpmWarmupDelayMsBase * (attempt + 1u);
                LogStr(Log, "TPM2: warm-up retry delay ms ");
                LogDecU32(Log, "", delayMs);
                BusyWaitMilliseconds(delayMs);
            }

            LogStr(Log, "TPM2: attempting TPM2_Startup via ");
            LogStr(Log, TransportName);
            LogStr(Log, "\r\n");
            const QC::u8 *Rsp = nullptr;
            QC::u32 RspLen = 0;
            const QC::u32 StartupRspCode = Cmd::Exchange(Log, StartupCmd, sizeof(StartupCmd), Rsp, RspLen);
            if (IsTransportFailure(StartupRspCode))
            {
                LogStr(Log, "TPM2: Startup transport failed\r\n");
                if (attempt + 1u < kMaxTpmWarmupAttempts)
                    continue;
                return;
            }

            if (StartupRspCode == kTpmStartupAlreadyInitialized)
            {
                LogStr(Log, "TPM2: TPM_RC_INITIALIZE (already started)\r\n");
            }
            else if (StartupRspCode != 0)
            {
                LogStr(Log, "TPM2: Startup failed\r\n");
                if (IsRetryableTpmResponse(StartupRspCode) && attempt + 1u < kMaxTpmWarmupAttempts)
                    continue;
                return;
            }
            else
            {
                LogStr(Log, "TPM2: Startup OK\r\n");
            }

            LogStr(Log, "TPM2: attempting TPM2_GetRandom(16)\r\n");
            const QC::u32 RandRspCode = Cmd::Exchange(Log, GetRandomCmd, sizeof(GetRandomCmd), Rsp, RspLen);
            if (RandRspCode != 0)
            {
                LogStr(Log, "TPM2: GetRandom failed\r\n");
                if (IsRetryableTpmResponse(RandRspCode) && attempt + 1u < kMaxTpmWarmupAttempts)
                    continue;
                return;
            }

            if (RspLen < 12)
            {
                LogStr(Log, "TPM2: GetRandom response too short\r\n");
                if (attempt + 1u < kMaxTpmWarmupAttempts)
                    continue;
                return;
            }

            const QC::u16 BytesSize = ReadBe16(Rsp + 10);
            LogDecU32(Log, "TPM2: GetRandom bytes ", static_cast<QC::u32>(BytesSize));

            const QC::usize Avail = RspLen - 12;
            EntropyLen = (BytesSize < Avail) ? BytesSize : Avail;
            if (EntropyLen > sizeof(Entropy))
                EntropyLen = sizeof(Entropy);
            if (EntropyLen == 0)
            {
                LogStr(Log, "TPM2: GetRandom returned no entropy bytes\r\n");
                if (attempt + 1u < kMaxTpmWarmupAttempts)
                    continue;
                return;
            }

            LogStr(Log, "TPM2: RAND ");
            for (QC::usize i = 0; i < EntropyLen; ++i)
            {
                Entropy[i] = Rsp[12 + i];
                const QC::u8 hi = (Entropy[i] >> 4) & 0xF;
                const QC::u8 lo = Entropy[i] & 0xF;
                char Buf[3];
                Buf[0] = hi < 10 ? static_cast<char>('0' + hi) : static_cast<char>('a' + (hi - 10));
                Buf[1] = lo < 10 ? static_cast<char>('0' + lo) : static_cast<char>('a' + (lo - 10));
                Buf[2] = 0;
                LogStr(Log, Buf);
            }
            LogStr(Log, "\r\n");
            break;
        }

        if (EntropyLen == 0)
            return;

        QK::Entropy::addEntropy(Entropy, EntropyLen);
        QC::String::memset(Entropy, 0, sizeof(Entropy));

        g_TpmSecureStore.bReady = true;
        Diag::CaptureStartupInfo(Log);
        SecureBoot::Evaluate(EventLogPhys, EventLogLen, Log);

        // Verify wrap-key seal/unseal support before enabling TPM mode in SecureStore.
        // Some TPM/runtime combinations advertise presence but fail these operations.
        bool enableSecureStoreTpm = false;
        {
            QC::u8 probeKey[32];
            QC::u8 probeUnsealed[32];
            QC::String::memset(probeKey, 0, sizeof(probeKey));
            QC::String::memset(probeUnsealed, 0, sizeof(probeUnsealed));

            const QC::Status randSt = QK::Entropy::fillRandom(probeKey, sizeof(probeKey));
            if (randSt == QC::Status::Success || randSt == QC::Status::Busy)
            {
                QC::Vector<QC::u8> probeBlob;
                const QC::Status sealSt = SecureStoreTpmSealWrapKeyLocked(&g_TpmSecureStore, probeKey, sizeof(probeKey), probeBlob);
                if (sealSt == QC::Status::Success)
                {
                    const QC::Status unsealSt = SecureStoreTpmUnsealWrapKeyLocked(&g_TpmSecureStore, probeBlob, probeUnsealed, sizeof(probeUnsealed));
                    if (unsealSt == QC::Status::Success && QC::String::memcmp(probeKey, probeUnsealed, sizeof(probeKey)) == 0)
                        enableSecureStoreTpm = true;
                }

                if (probeBlob.size() > 0)
                    QC::String::memset(probeBlob.data(), 0, probeBlob.size());
                probeBlob.clear();
            }

            QC::String::memset(probeKey, 0, sizeof(probeKey));
            QC::String::memset(probeUnsealed, 0, sizeof(probeUnsealed));
        }

        if (enableSecureStoreTpm)
        {
            auto ScCfg = QK::SecureStore::defaultConfig();
            ScCfg.tpmUser = &g_TpmSecureStore;
            ScCfg.tpmSealWrapKey = &SecureStoreTpmSealWrapKey;
            ScCfg.tpmUnsealWrapKey = &SecureStoreTpmUnsealWrapKey;
            ScCfg.tpmDiagnoseUnsealWrapKey = &SecureStoreTpmDiagnoseUnsealWrapKey;
            ScCfg.tpmSealWrapKeyForPcr7 = &SecureStoreTpmSealWrapKeyForPcr7;
            ScCfg.tpmSealSecret = &SecureStoreTpmSealSecret;
            ScCfg.tpmUnsealSecret = &SecureStoreTpmUnsealSecret;
            QK::SecureStore::setDefaultConfig(ScCfg);
            LogStr(Log, "SecureStore: TPM wrap-key enabled\r\n");
        }
        else
        {
            LogStr(Log, "SecureStore: TPM wrap-key unsupported; falling back to non-TPM mode\r\n");
        }
    }

    bool IsReady()
    {
        return g_TpmSecureStore.bReady;
    }

    void RunSecureStoreSelfTests(QFS::VFS *Vfs, FLogFn Log)
    {
        SecureStoreSelfTest(Log);
        SecureStorePcrMismatchTest(Vfs, Log);
    }
}
