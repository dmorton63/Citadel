#pragma once

#include "QCTypes.h"

namespace QK::Boot::Security
{
    // Computes SHA-256(data[0..Len)) into OutDigest[32].
    void Sha256(const QC::u8 *Data, QC::usize Len, QC::u8 OutDigest[32]);
    
    // Writes 64 lowercase hex chars plus NUL (outCap must be >= 65).
    // Returns false if output buffer is too small.
    bool Sha256DigestToLowerHex(const QC::u8 Digest[32], char *Out, QC::usize OutCap);
    
    // Compares a digest against a hex string (case-insensitive, allows optional 0x prefix).
    // Returns true if Hex encodes exactly 32 bytes.
    bool Sha256DigestEqualsHex(const QC::u8 Digest[32], const char *Hex);
}
