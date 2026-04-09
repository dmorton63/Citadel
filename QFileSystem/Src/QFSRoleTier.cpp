#include "QFSRoleTier.h"

namespace QFS
{

    EncryptionTier roleToEncryptionTier(RoleFlag role)
    {
        switch (role)
        {
        case RoleFlag::Everyone:
            return EncryptionTier::Level1;
        case RoleFlag::User:
            return EncryptionTier::Level2;
        case RoleFlag::Admin:
            return EncryptionTier::Level3; // Required invariant.
        case RoleFlag::System:
            return EncryptionTier::Level4; // Required invariant.
        case RoleFlag::Sc:
            return EncryptionTier::Level5;
        case RoleFlag::Protected:
            return EncryptionTier::Level6;
        default:
            return EncryptionTier::Level1;
        }
    }

    const char *encryptionTierName(EncryptionTier tier)
    {
        switch (tier)
        {
        case EncryptionTier::Level1:
            return "LEVEL1";
        case EncryptionTier::Level2:
            return "LEVEL2";
        case EncryptionTier::Level3:
            return "LEVEL3";
        case EncryptionTier::Level4:
            return "LEVEL4";
        case EncryptionTier::Level5:
            return "LEVEL5";
        case EncryptionTier::Level6:
            return "LEVEL6";
        default:
            return "UNKNOWN";
        }
    }

} // namespace QFS
