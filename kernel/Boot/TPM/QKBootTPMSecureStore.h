#pragma once

#include "QCTypes.h"

namespace QFS
{
    class VFS;
}

namespace QK::Boot::Tpm
{
    using FLogFn = void (*)(const char *);

    // Detects the TPM transport (CRB or TIS/FIFO) from the ACPI TPM2 table,
    // starts the TPM, and enables TPM-backed SecureStore when sealing works.
    // EventLogPhys/EventLogLen are the TPM2 table LASA/LAML (0 when absent).
    void TryTpm2Startup(QC::u32 StartMethod, QC::PhysAddr ControlAreaPhys, QC::u64 EventLogPhys, QC::u32 EventLogLen, FLogFn Log);

    bool IsReady();

    // Extends a PCR with a SHA-256 digest (TPM2_PCR_Extend with SHA-256 bank)
    // and records it in the kernel measurement log. Returns true on success.
    bool ExtendPcrSha256Digest(QC::u32 PcrIndex, const QC::u8 Digest[32], FLogFn Log, const char *Label = nullptr);

    // Verifies an RSA-2048 RSASSA(SHA256) signature over a SHA-256 digest using the TPM.
    // Returns true on successful verification.
    bool VerifyRsa2048RsassaSha256Digest(const QC::u8 Modulus[256], const QC::u8 Digest[32], const QC::u8 Signature[256], FLogFn Log);

    void RunSecureStoreSelfTests(QFS::VFS *Vfs, FLogFn Log);

    // Runs a diagnostics subcommand ("status", "pcrs [n]", "selftest",
    // "eventlog"), emitting one line per call to Out. Safe to call from IRQ
    // context: it never waits on timers and reports busy instead of blocking
    // when the TPM is in use on the same CPU.
    using FLineFn = void (*)(void *User, const char *Line);
    bool RunDiagnosticCommand(const char *Args, FLineFn Out, void *User);
}
