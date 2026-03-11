#pragma once

#include "QCTypes.h"

namespace QFS
{
    class VFS;
}

namespace QK::Boot::Tpm
{
    using FLogFn = void (*)(const char *);

    void TryTpm2CrbStartup(QC::u32 StartMethod, QC::PhysAddr ControlAreaPhys, FLogFn Log);

    bool IsReady();

    // Extends a PCR with a SHA-256 digest (TPM2_PCR_Extend with SHA-256 bank).
    // Returns true on success.
    bool ExtendPcrSha256Digest(QC::u32 PcrIndex, const QC::u8 Digest[32], FLogFn Log);

    // Verifies an RSA-2048 RSASSA(SHA256) signature over a SHA-256 digest using the TPM.
    // Returns true on successful verification.
    bool VerifyRsa2048RsassaSha256Digest(const QC::u8 Modulus[256], const QC::u8 Digest[32], const QC::u8 Signature[256], FLogFn Log);

    void RunSecureStoreSelfTests(QFS::VFS *Vfs, FLogFn Log);
}
