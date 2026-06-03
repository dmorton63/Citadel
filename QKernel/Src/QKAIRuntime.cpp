#include "QKAIRuntime.h"

#include "QCString.h"
#include "QCVector.h"

#include "QFSVolumeManager.h"
#include "QKSecureStore.h"

#include "QQExecutor.h"

namespace QK
{
    namespace AIRuntime
    {
        namespace
        {
            constexpr const char *kRuntimeStateKey = "AIRTIME.BIN";
            constexpr QC::u32 kRuntimeStateMagic = 0x45524941; // "AIRE"
            constexpr QC::u16 kRuntimeStateVersion = 1;
            constexpr QC::u16 kFlagMemoEnabled = 0x0001;
            constexpr QC::u16 kFlagAllowlistEnabled = 0x0002;

            struct PersistHeader
            {
                QC::u32 magic;
                QC::u16 version;
                QC::u16 flags;
                QC::u32 allowCount;
            };

            bool secureStoreReady()
            {
                return QFS::VolumeManager::instance().isMounted("QFS_SYSTEM");
            }
        }

        bool hasPersistentState()
        {
            if (!secureStoreReady())
                return false;
            return QK::SecureStore::exists(kRuntimeStateKey);
        }

        QC::Status clearPersistentState()
        {
            if (!secureStoreReady())
                return QC::Status::NotFound;
            return QK::SecureStore::removeBlob(kRuntimeStateKey);
        }

        QC::Status savePersistentState()
        {
            if (!secureStoreReady())
                return QC::Status::NotFound;
            auto &ex = QQ::Executor::instance();

            const QC::usize allowCount = ex.memoizationAllowlistCount();
            const QC::usize blobSize = sizeof(PersistHeader) + (allowCount * 32);

            QC::Vector<QC::u8> blob;
            blob.resize(blobSize);

            PersistHeader hdr{};
            hdr.magic = kRuntimeStateMagic;
            hdr.version = kRuntimeStateVersion;
            hdr.flags = 0;
            if (ex.memoizationEnabled())
                hdr.flags |= kFlagMemoEnabled;
            if (ex.memoizationAllowlistEnabled())
                hdr.flags |= kFlagAllowlistEnabled;
            hdr.allowCount = static_cast<QC::u32>(allowCount);

            QC::String::memcpy(blob.data(), &hdr, sizeof(hdr));

            QC::u8 *dst = blob.data() + sizeof(hdr);
            for (QC::usize i = 0; i < allowCount; ++i)
            {
                QC::u8 sig[32];
                if (!ex.memoizationAllowlistEntryAt(i, sig))
                    return QC::Status::Error;
                QC::String::memcpy(dst + (i * 32), sig, 32);
            }

            return QK::SecureStore::writeSealedBlob(kRuntimeStateKey, blob.data(), blob.size());
        }

        QC::Status loadPersistentState()
        {
            if (!secureStoreReady())
                return QC::Status::NotFound;
            QC::Vector<QC::u8> blob;
            QC::Status st = QK::SecureStore::readSealedBlob(kRuntimeStateKey, blob);
            if (st != QC::Status::Success)
                return st;

            if (blob.size() < sizeof(PersistHeader))
                return QC::Status::Error;

            PersistHeader hdr{};
            QC::String::memcpy(&hdr, blob.data(), sizeof(hdr));
            if (hdr.magic != kRuntimeStateMagic || hdr.version != kRuntimeStateVersion)
                return QC::Status::Error;

            const QC::usize expected = sizeof(PersistHeader) + (static_cast<QC::usize>(hdr.allowCount) * 32);
            if (blob.size() < expected)
                return QC::Status::Error;

            auto &ex = QQ::Executor::instance();
            ex.setMemoizationEnabled((hdr.flags & kFlagMemoEnabled) != 0);
            ex.setMemoizationAllowlistEnabled((hdr.flags & kFlagAllowlistEnabled) != 0);
            ex.clearMemoizationAllowlist();

            const QC::u8 *src = blob.data() + sizeof(PersistHeader);
            for (QC::u32 i = 0; i < hdr.allowCount; ++i)
            {
                if (!ex.memoizationAllowlistAdd(src + (static_cast<QC::usize>(i) * 32)))
                    return QC::Status::Error;
            }

            return QC::Status::Success;
        }
    } // namespace AIRuntime
} // namespace QK
