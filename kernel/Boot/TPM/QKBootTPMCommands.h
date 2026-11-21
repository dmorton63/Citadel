#pragma once

// TPM 2.0 command marshalling. Every function here issues commands through the
// transport and must be called with the TPM operation lock held.

#include "QKBootTPMInternal.h"

#include "QCVector.h"

namespace QK::Boot::Tpm::Cmd
{
    using QK::Boot::Tpm::Internal::FTpmBufWriter;

    // Response code of the most recent command (0 on success).
    QC::u32 LastResponseCode();
    void ClearLastResponseCode();

    // Submits a raw command; Rsp points into a shared buffer that stays valid
    // until the next command. VerboseLog logs transport details when non-null.
    QC::u32 Exchange(FLogFn VerboseLog, const QC::u8 *Cmd, QC::usize CmdLen, const QC::u8 *&Rsp, QC::u32 &RspLen);

    bool TpmRspParams(const QC::u8 *Rsp, QC::u32 RspLen, const QC::u8 *&OutParams, QC::u32 &OutParamsLen);

    QC::Status TpmStartPolicySession(FLogFn Log, bool bTrial, QC::u32 &OutSession);
    QC::Status TpmPolicyPCR(FLogFn Log, QC::u32 PolicySession);
    QC::Status TpmPcrExtendSha256(FLogFn Log, QC::u32 PcrIndex, const QC::u8 Digest[32]);
    QC::Status TpmPolicyGetDigest(FLogFn Log, QC::u32 PolicySession, QC::u8 OutDigest[32]);
    QC::Status TpmCreatePrimaryStorageKey(FLogFn Log, QC::u32 &OutHandle);
    QC::Status TpmFlushContext(FLogFn Log, QC::u32 Handle);
    QC::Status TpmLoadExternalRsa2048RsassaSha256PublicKey(FLogFn Log, const QC::u8 Modulus[256], QC::u32 &OutHandle);
    QC::Status TpmVerifyRsa2048RsassaSha256Digest(FLogFn Log, QC::u32 KeyHandle, const QC::u8 Digest[32], const QC::u8 Signature[256]);
    QC::Status TpmCreateSealedObject(FLogFn Log,
                                     QC::u32 ParentHandle,
                                     const QC::u8 *Secret,
                                     QC::usize SecretLen,
                                     const QC::u8 PolicyDigest[32],
                                     QC::Vector<QC::u8> &OutPrivate2b,
                                     QC::Vector<QC::u8> &OutPublic2b);
    QC::Status TpmLoadSealedObject(FLogFn Log,
                                   QC::u32 ParentHandle,
                                   const QC::Vector<QC::u8> &InPrivate2b,
                                   const QC::Vector<QC::u8> &InPublic2b,
                                   QC::u32 &OutHandle);
    QC::Status TpmUnsealWithAuthSession(FLogFn Log,
                                        QC::u32 ObjectHandle,
                                        QC::u32 AuthSessionHandle,
                                        QC::u8 *Out,
                                        QC::usize OutLen);

    // Diagnostics.
    constexpr QC::u32 TPM_PT_MANUFACTURER = 0x105;
    constexpr QC::u32 TPM_PT_VENDOR_STRING_1 = 0x106;
    constexpr QC::u32 TPM_PT_FIRMWARE_VERSION_1 = 0x10B;
    constexpr QC::u32 TPM_PT_FIRMWARE_VERSION_2 = 0x10C;

    struct FTaggedProperty
    {
        QC::u32 Property;
        QC::u32 Value;
    };

    // TPM2_GetCapability(TPM_CAP_TPM_PROPERTIES). Returns the number of entries stored.
    QC::usize TpmGetFixedProperties(QC::u32 FirstProperty, FTaggedProperty *Out, QC::usize MaxCount);
    QC::Status TpmPcrReadSha256(QC::u32 PcrIndex, QC::u8 OutDigest[32], QC::u32 &OutUpdateCounter);
    QC::Status TpmSelfTest(bool bFullTest);
    QC::Status TpmGetTestResult(QC::u32 &OutTestResult);
}
