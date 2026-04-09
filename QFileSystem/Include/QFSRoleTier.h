#pragma once

#include "QCTypes.h"
#include "QFSVFS.h"

namespace QFS
{

    enum class EncryptionTier : QC::u8
    {
        Level1 = 1,
        Level2 = 2,
        Level3 = 3,
        Level4 = 4,
        Level5 = 5,
        Level6 = 6
    };

    EncryptionTier roleToEncryptionTier(RoleFlag role);
    const char *encryptionTierName(EncryptionTier tier);

} // namespace QFS
