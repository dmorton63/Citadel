#include "QKBootTPMCommands.h"
#include "QKBootTPMProbePolicy.h"
#include "QKBootTPMTransport.h"

namespace QK::Boot::Tpm::Cmd
{
    using namespace QK::Boot::Tpm::Internal;

    namespace
    {
        static QC::u8 g_RspBuf[Transport::kMaxResponseSize];
        static QC::u32 g_TpmLastRspCode = 0;
    }

    QC::u32 LastResponseCode()
    {
        return g_TpmLastRspCode;
    }

    QC::u32 Exchange(FLogFn VerboseLog, const QC::u8 *Cmd, QC::usize CmdLen, const QC::u8 *&Rsp, QC::u32 &RspLen)
    {
        Rsp = g_RspBuf;
        RspLen = 0;
        const QC::u32 Rc = Transport::Submit(Cmd, CmdLen, g_RspBuf, sizeof(g_RspBuf), RspLen, VerboseLog);
        if (Rc == kTpmTransportFailure)
            RspLen = 0;
        return Rc;
    }

    bool TpmRspParams(const QC::u8 *Rsp, QC::u32 RspLen, const QC::u8 *&OutParams, QC::u32 &OutParamsLen)
    {
        OutParams = nullptr;
        OutParamsLen = 0;
        if (!Rsp || RspLen < 10)
            return false;

        const QC::u16 Tag = ReadBe16(Rsp);
        if (Tag == 0x8001)
        {
            OutParams = Rsp + 10;
            OutParamsLen = (RspLen >= 10) ? (RspLen - 10) : 0;
            return true;
        }

        if (Tag != 0x8002)
            return false;

        if (RspLen >= 14)
        {
            const QC::u32 ParameterSize = ReadBe32(Rsp + 10);
            if (14u + ParameterSize <= RspLen)
            {
                OutParams = Rsp + 14;
                OutParamsLen = ParameterSize;
                return true;
            }
        }

        OutParams = Rsp + 10;
        OutParamsLen = RspLen - 10;
        return true;
    }

    static bool TpmAppendSessionAuthHandle(FTpmBufWriter &W, QC::u32 SessionHandle);

    static bool TpmAppendPwSessionAuth(FTpmBufWriter &W)
    {
        return TpmAppendSessionAuthHandle(W, 0x40000009u);
    }

    static bool TpmAppendSessionAuthHandle(FTpmBufWriter &W, QC::u32 SessionHandle)
    {
        if (!W.Be32(SessionHandle))
            return false;
        if (!W.Be16(0))
            return false;
        if (!W.U8(0))
            return false;
        if (!W.Be16(0))
            return false;
        return true;
    }

    QC::Status TpmStartPolicySession(FLogFn Log, bool bTrial, QC::u32 &OutSession)
    {
        QC::u8 Cmd[128];
        FTpmBufWriter W{Cmd, sizeof(Cmd), 0};

        constexpr QC::u16 TPM_ST_NO_SESSIONS = 0x8001;
        constexpr QC::u32 TPM_CC_StartAuthSession = 0x00000176u;
        constexpr QC::u32 TPM_RH_NULL = 0x40000007u;
        constexpr QC::u16 TPM_ALG_NULL = 0x0010;
        constexpr QC::u16 TPM_ALG_SHA256 = 0x000B;

        if (!W.Be16(TPM_ST_NO_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_StartAuthSession))
            return QC::Status::Error;

        if (!W.Be32(TPM_RH_NULL) || !W.Be32(TPM_RH_NULL))
            return QC::Status::Error;

        QC::u8 NonceCaller[16];
        for (QC::usize i = 0; i < sizeof(NonceCaller); ++i)
            NonceCaller[i] = static_cast<QC::u8>(0xA5u ^ static_cast<QC::u8>(i));
        if (!W.Be16(static_cast<QC::u16>(sizeof(NonceCaller))) || !W.Push(NonceCaller, sizeof(NonceCaller)))
            return QC::Status::Error;

        if (!W.Be16(0))
            return QC::Status::Error;
        if (!W.U8(bTrial ? static_cast<QC::u8>(0x03) : static_cast<QC::u8>(0x01)))
            return QC::Status::Error;
        if (!W.Be16(TPM_ALG_NULL))
            return QC::Status::Error;
        if (!W.Be16(TPM_ALG_SHA256))
            return QC::Status::Error;

        WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

        QC::u32 RspLen = 0;
        const QC::u8 *Rsp = nullptr;
        QC::u32 RspCode = Exchange(nullptr, Cmd, W.Len, Rsp, RspLen);
        g_TpmLastRspCode = RspCode;
        if (RspCode != 0)
            return QC::Status::Error;
        const QC::u8 *Params = nullptr;
        QC::u32 ParamsLen = 0;
        if (!TpmRspParams(Rsp, RspLen, Params, ParamsLen))
            return QC::Status::Error;
        if (!Params || ParamsLen < 4)
            return QC::Status::Error;

        OutSession = ReadBe32(Params);
        g_TpmLastRspCode = 0;
        return QC::Status::Success;
    }

    QC::Status TpmPolicyPCR(FLogFn Log, QC::u32 PolicySession)
    {
        QC::u8 Cmd[96];
        FTpmBufWriter W{Cmd, sizeof(Cmd), 0};

        constexpr QC::u16 TPM_ST_NO_SESSIONS = 0x8001;
        constexpr QC::u32 TPM_CC_PolicyPCR = 0x0000017Fu;
        constexpr QC::u16 TPM_ALG_SHA256 = 0x000B;

        if (!W.Be16(TPM_ST_NO_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_PolicyPCR))
            return QC::Status::Error;
        if (!W.Be32(PolicySession))
            return QC::Status::Error;
        if (!W.Be16(0))
            return QC::Status::Error;
        if (!W.Be32(1))
            return QC::Status::Error;
        if (!W.Be16(TPM_ALG_SHA256))
            return QC::Status::Error;
        if (!W.U8(3))
            return QC::Status::Error;
        if (!W.U8(0x80) || !W.U8(0x00) || !W.U8(0x00))
            return QC::Status::Error;

        WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

        QC::u32 RspLen = 0;
        const QC::u8 *Rsp = nullptr;
        QC::u32 RspCode = Exchange(nullptr, Cmd, W.Len, Rsp, RspLen);
        g_TpmLastRspCode = RspCode;
        if (RspCode != 0)
            return QC::Status::Error;
        g_TpmLastRspCode = 0;
        return QC::Status::Success;
    }

    QC::Status TpmPcrExtendSha256(FLogFn Log, QC::u32 PcrIndex, const QC::u8 Digest[32])
    {
        QC::u8 Cmd[128];
        FTpmBufWriter W{Cmd, sizeof(Cmd), 0};

        constexpr QC::u16 TPM_ST_SESSIONS = 0x8002;
        constexpr QC::u32 TPM_CC_PCR_Extend = 0x00000182u;
        constexpr QC::u16 TPM_ALG_SHA256 = 0x000B;

        const QC::u32 PcrHandle = PcrIndex;

        if (!W.Be16(TPM_ST_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_PCR_Extend))
            return QC::Status::Error;
        if (!W.Be32(PcrHandle))
            return QC::Status::Error;
        if (!W.Be32(9))
            return QC::Status::Error;
        if (!TpmAppendPwSessionAuth(W))
            return QC::Status::Error;
        if (!W.Be32(1))
            return QC::Status::Error;
        if (!W.Be16(TPM_ALG_SHA256))
            return QC::Status::Error;
        if (!W.Push(Digest, 32))
            return QC::Status::Error;

        WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

        QC::u32 RspLen = 0;
        const QC::u8 *Rsp = nullptr;
        QC::u32 RspCode = Exchange(nullptr, Cmd, W.Len, Rsp, RspLen);
        g_TpmLastRspCode = RspCode;
        if (RspCode != 0)
            return QC::Status::Error;
        g_TpmLastRspCode = 0;
        return QC::Status::Success;
    }

    QC::Status TpmPolicyGetDigest(FLogFn Log, QC::u32 PolicySession, QC::u8 OutDigest[32])
    {
        QC::u8 Cmd[64];
        FTpmBufWriter W{Cmd, sizeof(Cmd), 0};

        constexpr QC::u16 TPM_ST_NO_SESSIONS = 0x8001;
        constexpr QC::u32 TPM_CC_PolicyGetDigest = 0x00000189u;

        if (!W.Be16(TPM_ST_NO_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_PolicyGetDigest))
            return QC::Status::Error;
        if (!W.Be32(PolicySession))
            return QC::Status::Error;

        WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

        QC::u32 RspLen = 0;
        const QC::u8 *Rsp = nullptr;
        QC::u32 RspCode = Exchange(nullptr, Cmd, W.Len, Rsp, RspLen);
        g_TpmLastRspCode = RspCode;
        if (RspCode != 0)
            return QC::Status::Error;
        const QC::u8 *Params = nullptr;
        QC::u32 ParamsLen = 0;
        if (!TpmRspParams(Rsp, RspLen, Params, ParamsLen))
            return QC::Status::Error;
        if (!Params || ParamsLen < 2)
            return QC::Status::Error;

        const QC::u16 Sz = ReadBe16(Params);
        if (Sz != 32)
            return QC::Status::Error;
        if (2u + Sz > ParamsLen)
            return QC::Status::Error;
        for (int i = 0; i < 32; ++i)
            OutDigest[i] = Params[2 + i];

        g_TpmLastRspCode = 0;
        return QC::Status::Success;
    }

    QC::Status TpmCreatePrimaryStorageKey(FLogFn Log, QC::u32 &OutHandle)
    {
        QC::u8 Cmd[512];
        FTpmBufWriter W{Cmd, sizeof(Cmd), 0};

        constexpr QC::u16 TPM_ST_SESSIONS = 0x8002;
        constexpr QC::u32 TPM_CC_CreatePrimary = 0x00000131u;
        constexpr QC::u32 TPM_RH_OWNER = 0x40000001u;

        if (!W.Be16(TPM_ST_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_CreatePrimary))
            return QC::Status::Error;
        if (!W.Be32(TPM_RH_OWNER))
            return QC::Status::Error;
        if (!W.Be32(9))
            return QC::Status::Error;
        if (!TpmAppendPwSessionAuth(W))
            return QC::Status::Error;
        if (!W.Be16(4) || !W.Be16(0) || !W.Be16(0))
            return QC::Status::Error;

        const QC::usize InPublicSizeOffset = W.Len;
        if (!W.Be16(0))
            return QC::Status::Error;
        const QC::usize InPublicStart = W.Len;

        constexpr QC::u16 TPM_ALG_RSA = 0x0001;
        constexpr QC::u16 TPM_ALG_SHA256 = 0x000B;
        constexpr QC::u16 TPM_ALG_AES = 0x0006;
        constexpr QC::u16 TPM_ALG_CFB = 0x0043;
        constexpr QC::u16 TPM_ALG_NULL = 0x0010;

        constexpr QC::u32 TPMA_FIXEDTPM = 0x00000002u;
        constexpr QC::u32 TPMA_FIXEDPARENT = 0x00000010u;
        constexpr QC::u32 TPMA_SENSITIVEDATAORIGIN = 0x00000020u;
        constexpr QC::u32 TPMA_USERWITHAUTH = 0x00000040u;
        constexpr QC::u32 TPMA_NODA = 0x00000400u;
        constexpr QC::u32 TPMA_RESTRICTED = 0x00010000u;
        constexpr QC::u32 TPMA_DECRYPT = 0x00020000u;

        const QC::u32 ObjectAttributes = TPMA_FIXEDTPM | TPMA_FIXEDPARENT | TPMA_SENSITIVEDATAORIGIN |
                                         TPMA_USERWITHAUTH | TPMA_NODA | TPMA_RESTRICTED | TPMA_DECRYPT;

        if (!W.Be16(TPM_ALG_RSA) || !W.Be16(TPM_ALG_SHA256) || !W.Be32(ObjectAttributes))
            return QC::Status::Error;
        if (!W.Be16(0))
            return QC::Status::Error;
        if (!W.Be16(TPM_ALG_AES) || !W.Be16(128) || !W.Be16(TPM_ALG_CFB))
            return QC::Status::Error;
        if (!W.Be16(TPM_ALG_NULL))
            return QC::Status::Error;
        if (!W.Be16(2048) || !W.Be32(0))
            return QC::Status::Error;
        if (!W.Be16(0))
            return QC::Status::Error;

        const QC::u16 InPublicSize = static_cast<QC::u16>(W.Len - InPublicStart);
        WriteBe16(Cmd + InPublicSizeOffset, InPublicSize);

        if (!W.Be16(0))
            return QC::Status::Error;
        if (!W.Be32(0))
            return QC::Status::Error;

        WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

        QC::u32 RspLen = 0;
        const QC::u8 *Rsp = nullptr;
        QC::u32 RspCode = Exchange(nullptr, Cmd, W.Len, Rsp, RspLen);
        g_TpmLastRspCode = RspCode;
        if (RspCode != 0)
            return QC::Status::Error;
        if (RspLen < 14)
            return QC::Status::Error;
        const QC::u8 *Params = nullptr;
        QC::u32 ParamsLen = 0;
        if (!TpmRspParams(Rsp, RspLen, Params, ParamsLen))
            return QC::Status::Error;
        if (!Params || ParamsLen < 4)
            return QC::Status::Error;

        OutHandle = ReadBe32(Params);
        g_TpmLastRspCode = 0;
        return QC::Status::Success;
    }

    QC::Status TpmFlushContext(FLogFn Log, QC::u32 Handle)
    {
        (void)Log;
        QC::u8 Cmd[64];
        FTpmBufWriter W{Cmd, sizeof(Cmd), 0};
        constexpr QC::u16 TPM_ST_NO_SESSIONS = 0x8001;
        constexpr QC::u32 TPM_CC_FlushContext = 0x00000165u;

        if (!W.Be16(TPM_ST_NO_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_FlushContext))
            return QC::Status::Error;
        if (!W.Be32(Handle))
            return QC::Status::Error;

        WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

        QC::u32 RspLen = 0;
        const QC::u8 *Rsp = nullptr;
        QC::u32 RspCode = Exchange(nullptr, Cmd, W.Len, Rsp, RspLen);
        g_TpmLastRspCode = RspCode;
        if (RspCode != 0)
            return QC::Status::Error;
        g_TpmLastRspCode = 0;
        return QC::Status::Success;
    }

    QC::Status TpmLoadExternalRsa2048RsassaSha256PublicKey(FLogFn Log, const QC::u8 Modulus[256], QC::u32 &OutHandle)
    {
        OutHandle = 0;
        if (!Modulus)
            return QC::Status::InvalidParam;

        QC::u8 Cmd[1024];
        FTpmBufWriter W{Cmd, sizeof(Cmd), 0};

        constexpr QC::u16 TPM_ST_NO_SESSIONS = 0x8001;
        constexpr QC::u32 TPM_CC_LoadExternal = 0x00000167u;
        constexpr QC::u32 TPM_RH_NULL = 0x40000007u;

        constexpr QC::u16 TPM_ALG_RSA = 0x0001;
        constexpr QC::u16 TPM_ALG_SHA256 = 0x000B;
        constexpr QC::u16 TPM_ALG_NULL = 0x0010;
        constexpr QC::u16 TPM_ALG_RSASSA = 0x0014;

        constexpr QC::u32 TPMA_USERWITHAUTH = 0x00000040u;
        constexpr QC::u32 TPMA_SIGN_ENCRYPT = 0x00040000u;
        constexpr QC::u32 TPMA_NODA = 0x00000400u;
        const QC::u32 ObjectAttributes = TPMA_USERWITHAUTH | TPMA_SIGN_ENCRYPT | TPMA_NODA;

        if (!W.Be16(TPM_ST_NO_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_LoadExternal))
            return QC::Status::Error;

        // TPM2_LoadExternal has no handle area; hierarchy is a parameter.
        // inPrivate: TPM2B_SENSITIVE (empty)
        if (!W.Be16(0))
            return QC::Status::Error;

        // inPublic: TPM2B_PUBLIC
        const QC::usize InPublicSizeOffset = W.Len;
        if (!W.Be16(0))
            return QC::Status::Error;
        const QC::usize InPublicStart = W.Len;

        // TPMT_PUBLIC
        if (!W.Be16(TPM_ALG_RSA) || !W.Be16(TPM_ALG_SHA256) || !W.Be32(ObjectAttributes))
            return QC::Status::Error;

        // authPolicy: TPM2B_DIGEST (empty)
        if (!W.Be16(0))
            return QC::Status::Error;

        // parameters: TPMU_PUBLIC_PARMS -> TPMS_RSA_PARMS
        // symmetric: TPMT_SYM_DEF_OBJECT (NULL)
        if (!W.Be16(TPM_ALG_NULL))
            return QC::Status::Error;
        // scheme: TPMT_RSA_SCHEME (RSASSA + SHA256)
        if (!W.Be16(TPM_ALG_RSASSA) || !W.Be16(TPM_ALG_SHA256))
            return QC::Status::Error;
        // keyBits, exponent
        if (!W.Be16(2048) || !W.Be32(0))
            return QC::Status::Error;

        // unique: TPM2B_PUBLIC_KEY_RSA
        if (!W.Be16(256))
            return QC::Status::Error;
        if (!W.Push(Modulus, 256))
            return QC::Status::Error;

        const QC::u16 InPublicSize = static_cast<QC::u16>(W.Len - InPublicStart);
        WriteBe16(Cmd + InPublicSizeOffset, InPublicSize);

        // hierarchy: TPMI_RH_HIERARCHY
        if (!W.Be32(TPM_RH_NULL))
            return QC::Status::Error;

        WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

        QC::u32 RspLen = 0;
        const QC::u8 *Rsp = nullptr;
        QC::u32 RspCode = Exchange(nullptr, Cmd, W.Len, Rsp, RspLen);
        g_TpmLastRspCode = RspCode;
        if (RspCode != 0)
            return QC::Status::Error;
        const QC::u8 *Params = nullptr;
        QC::u32 ParamsLen = 0;
        if (!TpmRspParams(Rsp, RspLen, Params, ParamsLen))
            return QC::Status::Error;
        if (!Params || ParamsLen < 4)
            return QC::Status::Error;

        OutHandle = ReadBe32(Params);
        g_TpmLastRspCode = 0;
        return QC::Status::Success;
    }

    QC::Status TpmVerifyRsa2048RsassaSha256Digest(FLogFn Log, QC::u32 KeyHandle, const QC::u8 Digest[32], const QC::u8 Signature[256])
    {
        if (!Digest || !Signature)
            return QC::Status::InvalidParam;

        QC::u8 Cmd[768];
        FTpmBufWriter W{Cmd, sizeof(Cmd), 0};

        constexpr QC::u16 TPM_ST_NO_SESSIONS = 0x8001;
        constexpr QC::u32 TPM_CC_VerifySignature = 0x00000177u;
        constexpr QC::u16 TPM_ALG_RSASSA = 0x0014;
        constexpr QC::u16 TPM_ALG_SHA256 = 0x000B;

        if (!W.Be16(TPM_ST_NO_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_VerifySignature))
            return QC::Status::Error;
        if (!W.Be32(KeyHandle))
            return QC::Status::Error;

        // digest: TPM2B_DIGEST
        if (!W.Be16(32) || !W.Push(Digest, 32))
            return QC::Status::Error;

        // signature: TPMT_SIGNATURE -> RSASSA(SHA256) + TPM2B_PUBLIC_KEY_RSA
        if (!W.Be16(TPM_ALG_RSASSA) || !W.Be16(TPM_ALG_SHA256))
            return QC::Status::Error;
        if (!W.Be16(256) || !W.Push(Signature, 256))
            return QC::Status::Error;

        WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

        QC::u32 RspLen = 0;
        const QC::u8 *Rsp = nullptr;
        QC::u32 RspCode = Exchange(nullptr, Cmd, W.Len, Rsp, RspLen);
        g_TpmLastRspCode = RspCode;
        if (RspCode != 0)
            return QC::Status::Error;

        g_TpmLastRspCode = 0;
        return QC::Status::Success;
    }

    QC::Status TpmCreateSealedObject(FLogFn Log,
                                     QC::u32 ParentHandle,
                                     const QC::u8 *Secret,
                                     QC::usize SecretLen,
                                     const QC::u8 PolicyDigest[32],
                                     QC::Vector<QC::u8> &OutPrivate2b,
                                     QC::Vector<QC::u8> &OutPublic2b)
    {
        if (!Secret || SecretLen == 0 || SecretLen > 64)
            return QC::Status::InvalidParam;

        QC::u8 Cmd[768];
        FTpmBufWriter W{Cmd, sizeof(Cmd), 0};
        constexpr QC::u16 TPM_ST_SESSIONS = 0x8002;
        constexpr QC::u32 TPM_CC_Create = 0x00000153u;
        constexpr QC::u16 TPM_ALG_KEYEDHASH = 0x0008;
        constexpr QC::u16 TPM_ALG_SHA256 = 0x000B;
        constexpr QC::u16 TPM_ALG_NULL = 0x0010;

        constexpr QC::u32 TPMA_FIXEDTPM = 0x00000002u;
        constexpr QC::u32 TPMA_FIXEDPARENT = 0x00000010u;
        constexpr QC::u32 TPMA_ADMINWITHPOLICY = 0x00000080u;
        constexpr QC::u32 TPMA_NODA = 0x00000400u;
        const QC::u32 ObjectAttributes = TPMA_FIXEDTPM | TPMA_FIXEDPARENT | TPMA_ADMINWITHPOLICY | TPMA_NODA;

        if (!W.Be16(TPM_ST_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_Create))
            return QC::Status::Error;
        if (!W.Be32(ParentHandle))
            return QC::Status::Error;
        if (!W.Be32(9))
            return QC::Status::Error;
        if (!TpmAppendPwSessionAuth(W))
            return QC::Status::Error;

        const QC::u16 SensInnerSize = static_cast<QC::u16>(2 + 0 + 2 + SecretLen);
        if (!W.Be16(SensInnerSize))
            return QC::Status::Error;
        if (!W.Be16(0))
            return QC::Status::Error;
        if (!W.Be16(static_cast<QC::u16>(SecretLen)))
            return QC::Status::Error;
        if (!W.Push(Secret, SecretLen))
            return QC::Status::Error;

        const QC::usize InPublicSizeOffset = W.Len;
        if (!W.Be16(0))
            return QC::Status::Error;
        const QC::usize InPublicStart = W.Len;

        if (!W.Be16(TPM_ALG_KEYEDHASH) || !W.Be16(TPM_ALG_SHA256) || !W.Be32(ObjectAttributes))
            return QC::Status::Error;
        if (!W.Be16(32))
            return QC::Status::Error;
        if (!W.Push(PolicyDigest, 32))
            return QC::Status::Error;
        if (!W.Be16(TPM_ALG_NULL))
            return QC::Status::Error;
        if (!W.Be16(0))
            return QC::Status::Error;

        const QC::u16 InPublicSize = static_cast<QC::u16>(W.Len - InPublicStart);
        WriteBe16(Cmd + InPublicSizeOffset, InPublicSize);

        if (!W.Be16(0))
            return QC::Status::Error;
        if (!W.Be32(0))
            return QC::Status::Error;

        WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

        QC::u32 RspLen = 0;
        const QC::u8 *Rsp = nullptr;
        QC::u32 RspCode = Exchange(nullptr, Cmd, W.Len, Rsp, RspLen);
        g_TpmLastRspCode = RspCode;
        if (RspCode != 0)
            return QC::Status::Error;

        const QC::u8 *Params = nullptr;
        QC::u32 ParamsLen = 0;
        if (!TpmRspParams(Rsp, RspLen, Params, ParamsLen))
            return QC::Status::Error;
        if (!Params || ParamsLen < 4)
            return QC::Status::Error;

        QC::usize Off = 0;
        const QC::u16 PrivSize = ReadBe16(Params + Off);
        Off += 2;
        if (Off + PrivSize > ParamsLen)
            return QC::Status::Error;
        OutPrivate2b.clear();
        OutPrivate2b.resize(static_cast<QC::usize>(2 + PrivSize));
        OutPrivate2b[0] = Params[0];
        OutPrivate2b[1] = Params[1];
        for (QC::usize i = 0; i < PrivSize; ++i)
            OutPrivate2b[2 + i] = Params[Off + i];
        Off += PrivSize;

        if (Off + 2 > ParamsLen)
            return QC::Status::Error;
        const QC::u16 PubSize = ReadBe16(Params + Off);
        Off += 2;
        if (Off + PubSize > ParamsLen)
            return QC::Status::Error;
        OutPublic2b.clear();
        OutPublic2b.resize(static_cast<QC::usize>(2 + PubSize));
        OutPublic2b[0] = Params[Off - 2];
        OutPublic2b[1] = Params[Off - 1];
        for (QC::usize i = 0; i < PubSize; ++i)
            OutPublic2b[2 + i] = Params[Off + i];

        g_TpmLastRspCode = 0;
        return QC::Status::Success;
    }

    QC::Status TpmLoadSealedObject(FLogFn Log,
                                   QC::u32 ParentHandle,
                                   const QC::Vector<QC::u8> &InPrivate2b,
                                   const QC::Vector<QC::u8> &InPublic2b,
                                   QC::u32 &OutHandle)
    {
        if (InPrivate2b.size() < 2 || InPublic2b.size() < 2)
            return QC::Status::InvalidParam;

        QC::u8 Cmd[1024];
        FTpmBufWriter W{Cmd, sizeof(Cmd), 0};
        constexpr QC::u16 TPM_ST_SESSIONS = 0x8002;
        constexpr QC::u32 TPM_CC_Load = 0x00000157u;

        if (!W.Be16(TPM_ST_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_Load))
            return QC::Status::Error;
        if (!W.Be32(ParentHandle))
            return QC::Status::Error;
        if (!W.Be32(9))
            return QC::Status::Error;
        if (!TpmAppendPwSessionAuth(W))
            return QC::Status::Error;
        if (!W.Push(InPrivate2b.data(), InPrivate2b.size()))
            return QC::Status::Error;
        if (!W.Push(InPublic2b.data(), InPublic2b.size()))
            return QC::Status::Error;

        WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

        QC::u32 RspLen = 0;
        const QC::u8 *Rsp = nullptr;
        QC::u32 RspCode = Exchange(nullptr, Cmd, W.Len, Rsp, RspLen);
        g_TpmLastRspCode = RspCode;
        if (RspCode != 0)
            return QC::Status::Error;
        const QC::u8 *Params = nullptr;
        QC::u32 ParamsLen = 0;
        if (!TpmRspParams(Rsp, RspLen, Params, ParamsLen))
            return QC::Status::Error;
        if (!Params || ParamsLen < 4)
            return QC::Status::Error;
        OutHandle = ReadBe32(Params);
        g_TpmLastRspCode = 0;
        return QC::Status::Success;
    }

    QC::Status TpmUnsealWithAuthSession(FLogFn Log,
                                        QC::u32 ObjectHandle,
                                        QC::u32 AuthSessionHandle,
                                        QC::u8 *Out,
                                        QC::usize OutLen)
    {
        if (!Out || OutLen == 0)
            return QC::Status::InvalidParam;

        QC::u8 Cmd[128];
        FTpmBufWriter W{Cmd, sizeof(Cmd), 0};
        constexpr QC::u16 TPM_ST_SESSIONS = 0x8002;
        constexpr QC::u32 TPM_CC_Unseal = 0x0000015Eu;

        if (!W.Be16(TPM_ST_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_Unseal))
            return QC::Status::Error;
        if (!W.Be32(ObjectHandle))
            return QC::Status::Error;
        if (!W.Be32(9))
            return QC::Status::Error;
        if (!TpmAppendSessionAuthHandle(W, AuthSessionHandle))
            return QC::Status::Error;

        WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

        QC::u32 RspLen = 0;
        const QC::u8 *Rsp = nullptr;
        QC::u32 RspCode = Exchange(nullptr, Cmd, W.Len, Rsp, RspLen);
        g_TpmLastRspCode = RspCode;
        if (RspCode != 0)
            return QC::Status::Error;
        const QC::u8 *Params = nullptr;
        QC::u32 ParamsLen = 0;
        if (!TpmRspParams(Rsp, RspLen, Params, ParamsLen))
            return QC::Status::Error;
        if (!Params || ParamsLen < 2)
            return QC::Status::Error;

        const QC::u16 Sz = ReadBe16(Params);
        if (static_cast<QC::usize>(Sz) != OutLen)
            return QC::Status::Error;
        if (2u + Sz > ParamsLen)
            return QC::Status::Error;
        for (QC::usize i = 0; i < OutLen; ++i)
            Out[i] = Params[2 + i];

        g_TpmLastRspCode = 0;
        return QC::Status::Success;
    }

    QC::usize TpmGetFixedProperties(QC::u32 FirstProperty, FTaggedProperty *Out, QC::usize MaxCount)
    {
        if (!Out || MaxCount == 0)
            return 0;

        QC::u8 Cmd[32];
        FTpmBufWriter W{Cmd, sizeof(Cmd), 0};
        constexpr QC::u16 TPM_ST_NO_SESSIONS = 0x8001;
        constexpr QC::u32 TPM_CC_GetCapability = 0x0000017Au;
        constexpr QC::u32 TPM_CAP_TPM_PROPERTIES = 6;

        if (!W.Be16(TPM_ST_NO_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_GetCapability))
            return 0;
        if (!W.Be32(TPM_CAP_TPM_PROPERTIES) || !W.Be32(FirstProperty) || !W.Be32(static_cast<QC::u32>(MaxCount)))
            return 0;
        WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

        QC::u32 RspLen = 0;
        const QC::u8 *Rsp = nullptr;
        g_TpmLastRspCode = Exchange(nullptr, Cmd, W.Len, Rsp, RspLen);
        if (g_TpmLastRspCode != 0)
            return 0;

        const QC::u8 *Params = nullptr;
        QC::u32 ParamsLen = 0;
        // moreData(1) + capability(4) + count(4)
        if (!TpmRspParams(Rsp, RspLen, Params, ParamsLen) || ParamsLen < 9)
            return 0;
        if (ReadBe32(Params + 1) != TPM_CAP_TPM_PROPERTIES)
            return 0;

        const QC::u32 Count = ReadBe32(Params + 5);
        QC::usize Stored = 0;
        for (QC::u32 i = 0; i < Count && Stored < MaxCount; ++i)
        {
            const QC::usize Off = 9u + static_cast<QC::usize>(i) * 8u;
            if (Off + 8u > ParamsLen)
                break;
            Out[Stored].Property = ReadBe32(Params + Off);
            Out[Stored].Value = ReadBe32(Params + Off + 4);
            ++Stored;
        }
        return Stored;
    }

    QC::Status TpmPcrReadSha256(QC::u32 PcrIndex, QC::u8 OutDigest[32], QC::u32 &OutUpdateCounter)
    {
        if (PcrIndex >= 24 || !OutDigest)
            return QC::Status::InvalidParam;

        QC::u8 Cmd[32];
        FTpmBufWriter W{Cmd, sizeof(Cmd), 0};
        constexpr QC::u16 TPM_ST_NO_SESSIONS = 0x8001;
        constexpr QC::u32 TPM_CC_PCR_Read = 0x0000017Eu;
        constexpr QC::u16 TPM_ALG_SHA256 = 0x000B;

        QC::u8 Select[3] = {0, 0, 0};
        Select[PcrIndex / 8] = static_cast<QC::u8>(1u << (PcrIndex % 8));
        if (!W.Be16(TPM_ST_NO_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_PCR_Read))
            return QC::Status::Error;
        if (!W.Be32(1) || !W.Be16(TPM_ALG_SHA256) || !W.U8(3) || !W.Push(Select, sizeof(Select)))
            return QC::Status::Error;
        WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

        QC::u32 RspLen = 0;
        const QC::u8 *Rsp = nullptr;
        g_TpmLastRspCode = Exchange(nullptr, Cmd, W.Len, Rsp, RspLen);
        if (g_TpmLastRspCode != 0)
            return QC::Status::Error;

        const QC::u8 *Params = nullptr;
        QC::u32 ParamsLen = 0;
        if (!TpmRspParams(Rsp, RspLen, Params, ParamsLen) || ParamsLen < 8)
            return QC::Status::Error;

        OutUpdateCounter = ReadBe32(Params);
        QC::usize Off = 4;
        const QC::u32 SelCount = ReadBe32(Params + Off);
        Off += 4;
        for (QC::u32 i = 0; i < SelCount; ++i)
        {
            if (Off + 3u > ParamsLen)
                return QC::Status::Error;
            const QC::u8 SizeOfSelect = Params[Off + 2];
            Off += 3u + SizeOfSelect;
        }

        if (Off + 4u > ParamsLen)
            return QC::Status::Error;
        const QC::u32 DigestCount = ReadBe32(Params + Off);
        Off += 4;
        if (DigestCount != 1 || Off + 2u > ParamsLen)
            return QC::Status::NotFound;
        const QC::u16 Size = ReadBe16(Params + Off);
        Off += 2;
        if (Size != 32 || Off + Size > ParamsLen)
            return QC::Status::Error;
        for (QC::usize i = 0; i < 32; ++i)
            OutDigest[i] = Params[Off + i];
        g_TpmLastRspCode = 0;
        return QC::Status::Success;
    }

    QC::Status TpmSelfTest(bool bFullTest)
    {
        QC::u8 Cmd[16];
        FTpmBufWriter W{Cmd, sizeof(Cmd), 0};
        constexpr QC::u16 TPM_ST_NO_SESSIONS = 0x8001;
        constexpr QC::u32 TPM_CC_SelfTest = 0x00000143u;

        if (!W.Be16(TPM_ST_NO_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_SelfTest) || !W.U8(bFullTest ? 1 : 0))
            return QC::Status::Error;
        WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

        QC::u32 RspLen = 0;
        const QC::u8 *Rsp = nullptr;
        g_TpmLastRspCode = Exchange(nullptr, Cmd, W.Len, Rsp, RspLen);
        return g_TpmLastRspCode == 0 ? QC::Status::Success : QC::Status::Error;
    }

    QC::Status TpmGetTestResult(QC::u32 &OutTestResult)
    {
        QC::u8 Cmd[16];
        FTpmBufWriter W{Cmd, sizeof(Cmd), 0};
        constexpr QC::u16 TPM_ST_NO_SESSIONS = 0x8001;
        constexpr QC::u32 TPM_CC_GetTestResult = 0x0000017Cu;

        if (!W.Be16(TPM_ST_NO_SESSIONS) || !W.Be32(0) || !W.Be32(TPM_CC_GetTestResult))
            return QC::Status::Error;
        WriteBe32(Cmd + 2, static_cast<QC::u32>(W.Len));

        QC::u32 RspLen = 0;
        const QC::u8 *Rsp = nullptr;
        g_TpmLastRspCode = Exchange(nullptr, Cmd, W.Len, Rsp, RspLen);
        if (g_TpmLastRspCode != 0)
            return QC::Status::Error;

        const QC::u8 *Params = nullptr;
        QC::u32 ParamsLen = 0;
        if (!TpmRspParams(Rsp, RspLen, Params, ParamsLen) || ParamsLen < 2)
            return QC::Status::Error;
        const QC::u16 OutDataSize = ReadBe16(Params);
        if (2u + OutDataSize + 4u > ParamsLen)
            return QC::Status::Error;
        OutTestResult = ReadBe32(Params + 2 + OutDataSize);
        return QC::Status::Success;
    }
}
