#pragma once

#include "QCTypes.h"

namespace QC
{
    // Minimal SHA-256 helper usable across modules (kernel + user-space libs).
    // Note: matches the boot-time SHA-256 behavior.
    void Sha256(const QC::u8 *data, QC::usize len, QC::u8 outDigest[32]);
    bool Sha256DigestToLowerHex(const QC::u8 digest[32], char *out, QC::usize outCap);
}
