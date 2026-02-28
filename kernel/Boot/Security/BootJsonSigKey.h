#pragma once

#include "QCTypes.h"

namespace QK::Boot::Security
{
    // RSA-2048 public modulus (big-endian), exponent is assumed to be 65537.
    extern const QC::u8 kBootJsonRsa2048Modulus[256];
}
