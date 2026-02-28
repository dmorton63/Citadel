#pragma once

#include "QCTypes.h"

namespace QK::Boot::Security
{
    // Computes SHA-256(data[0..Len)) into OutDigest[32].
    void Sha256(const QC::u8 *Data, QC::usize Len, QC::u8 OutDigest[32]);
}
