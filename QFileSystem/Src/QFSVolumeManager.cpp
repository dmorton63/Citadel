// QFileSystem Volume Manager - Implementation
// Namespace: QFS

#include "QFSVolumeManager.h"
#include "QFSVFS.h"
#include "QFSFAT32.h"
#include "QFSFAT16.h"
#include "QFSFATProbe.h"
#include "QCLogger.h"
#include "QCString.h"

namespace QFS
{

    static QC::Status ensureMountPathExists(VFS &vfs, const char *mountPath)
    {
        if (!mountPath || mountPath[0] != '/')
            return QC::Status::InvalidParam;

        if (QC::String::strcmp(mountPath, "/") == 0)
            return QC::Status::Success;

        char partial[256];
        QC::usize partialLen = 0;
        partial[partialLen++] = '/';
        partial[partialLen] = '\0';

        const char *p = mountPath;
        while (*p == '/')
            ++p;

        while (*p)
        {
            const char *segStart = p;
            while (*p && *p != '/')
                ++p;
            const QC::usize segLen = static_cast<QC::usize>(p - segStart);

            while (*p == '/')
                ++p;

            if (segLen == 0)
                continue;

            if (partialLen > 1 && partial[partialLen - 1] != '/')
            {
                if (partialLen + 1 >= sizeof(partial))
                    return QC::Status::InvalidParam;
                partial[partialLen++] = '/';
                partial[partialLen] = '\0';
            }

            if (partialLen + segLen >= sizeof(partial))
                return QC::Status::InvalidParam;

            QC::String::memcpy(partial + partialLen, segStart, segLen);
            partialLen += segLen;
            partial[partialLen] = '\0';

            FileInfo info{};
            QC::Status st = vfs.stat(partial, &info);
            if (st == QC::Status::Success)
            {
                if (info.type != FileType::Directory)
                    return QC::Status::Busy;
                continue;
            }

            if (st != QC::Status::NotFound)
                return st;

            st = vfs.createDir(partial);
            if (st != QC::Status::Success)
            {
                // Some filesystems return a generic error for "already exists".
                // Double-check existence before failing.
                FileInfo info2{};
                if (vfs.stat(partial, &info2) == QC::Status::Success && info2.type == FileType::Directory)
                    continue;
                return st;
            }
        }

        return QC::Status::Success;
    }

    VolumeManager &VolumeManager::instance()
    {
        static VolumeManager manager;
        return manager;
    }

    VolumeManager::VolumeRecord *VolumeManager::findRecord(const char *name)
    {
        if (!name)
            return nullptr;

        for (QC::usize i = 0; i < m_volumes.size(); ++i)
        {
            if (QC::String::strcmp(m_volumes[i].name, name) == 0)
            {
                return &m_volumes[i];
            }
        }
        return nullptr;
    }

    const VolumeManager::VolumeRecord *VolumeManager::findRecord(const char *name) const
    {
        if (!name)
            return nullptr;

        for (QC::usize i = 0; i < m_volumes.size(); ++i)
        {
            if (QC::String::strcmp(m_volumes[i].name, name) == 0)
            {
                return &m_volumes[i];
            }
        }
        return nullptr;
    }

    QC::Status VolumeManager::registerVolume(const VolumeDefinition &definition)
    {
        if (!definition.name || !definition.mountPath || !definition.device)
            return QC::Status::InvalidParam;

        if (findRecord(definition.name))
        {
            QC_LOG_WARN("QFSVOL", "Volume %s already registered", definition.name);
            return QC::Status::Busy;
        }

        for (QC::usize i = 0; i < m_volumes.size(); ++i)
        {
            if (QC::String::strcmp(m_volumes[i].mountPath, definition.mountPath) == 0)
            {
                QC_LOG_WARN("QFSVOL", "Mount path %s already in use", definition.mountPath);
                return QC::Status::Busy;
            }
        }

        VolumeRecord record{};
        QC::String::memset(&record, 0, sizeof(record));
        QC::String::strncpy(record.name, definition.name, sizeof(record.name) - 1);
        QC::String::strncpy(record.mountPath, definition.mountPath, sizeof(record.mountPath) - 1);
        record.fsKind = definition.fsKind;
        record.device = definition.device;
        record.fs = nullptr;
        record.mounted = false;
        record.autoMount = definition.autoMount;

        m_volumes.push_back(record);
        VolumeRecord &stored = m_volumes.back();
        QC_LOG_INFO("QFSVOL", "Registered volume %s -> %s", stored.name, stored.mountPath);

        if (stored.autoMount)
        {
            QC::Status mountStatus = mountRecord(stored);
            if (mountStatus != QC::Status::Success)
            {
                QC_LOG_WARN("QFSVOL", "Auto-mount pending for %s (status=%d)", stored.name, static_cast<int>(mountStatus));
            }
        }

        return QC::Status::Success;
    }

    QC::Status VolumeManager::unregisterVolume(const char *name)
    {
        if (!name)
            return QC::Status::InvalidParam;

        VolumeRecord *record = findRecord(name);
        if (!record)
            return QC::Status::NotFound;

        if (record->mounted && record->fs)
        {
            VFS::instance().unmount(record->mountPath);
            record->fs->unmount();
            delete record->fs;
            record->fs = nullptr;
            record->mounted = false;
        }

        VolumeRecord *base = m_volumes.data();
        QC::usize index = static_cast<QC::usize>(record - base);
        for (QC::usize i = index + 1; i < m_volumes.size(); ++i)
        {
            m_volumes[i - 1] = m_volumes[i];
        }
        m_volumes.pop_back();

        QC_LOG_INFO("QFSVOL", "Unregistered volume %s", name);
        return QC::Status::Success;
    }

    QC::Status VolumeManager::mountVolume(const char *name)
    {
        VolumeRecord *record = findRecord(name);
        if (!record)
            return QC::Status::NotFound;
        return mountRecord(*record);
    }

    QC::Status VolumeManager::mountAll()
    {
        QC::Status last = QC::Status::Success;
        for (QC::usize i = 0; i < m_volumes.size(); ++i)
        {
            VolumeRecord &record = m_volumes[i];
            QC::Status status = mountRecord(record);
            if (status != QC::Status::Success)
            {
                last = status;
            }
        }
        return last;
    }

    QC::Status VolumeManager::mountPending()
    {
        QC::Status last = QC::Status::Success;
        for (QC::usize i = 0; i < m_volumes.size(); ++i)
        {
            VolumeRecord &record = m_volumes[i];
            if (!record.autoMount || record.mounted)
                continue;

            QC::Status status = mountRecord(record);
            if (status != QC::Status::Success)
            {
                last = status;
            }
        }
        return last;
    }

    bool VolumeManager::isMounted(const char *name) const
    {
        const VolumeRecord *record = findRecord(name);
        return record && record->mounted;
    }

    QC::Status VolumeManager::mountRecord(VolumeRecord &record)
    {
        if (record.mounted)
            return QC::Status::Success;

        if (!record.device)
            return QC::Status::InvalidParam;

        FileSystem *fs = createFileSystem(record.fsKind, record.device);
        if (!fs)
        {
            QC_LOG_ERROR("QFSVOL", "No filesystem factory for volume %s", record.name);
            return QC::Status::NotSupported;
        }

        QC::Status status = fs->mount();
        if (status != QC::Status::Success)
        {
            QC_LOG_ERROR("QFSVOL", "Mount failed for %s (fs mount error)", record.name);
            delete fs;
            return status;
        }

        VFS &vfs = VFS::instance();

        // Ensure the mount point path exists in the underlying filesystem so directory listings
        // (e.g., `ls /`) show the mount point name.
        status = ensureMountPathExists(vfs, record.mountPath);
        if (status != QC::Status::Success)
        {
            QC_LOG_WARN("QFSVOL", "Mount point path %s not ready (status=%d)", record.mountPath, static_cast<int>(status));
            // Non-fatal: the volume can still be mounted even if the mount point isn't visible in
            // the underlying FS listing (e.g., if root FS isn't mounted yet).
        }

        status = vfs.mount(record.mountPath, fs);
        if (status != QC::Status::Success)
        {
            QC_LOG_ERROR("QFSVOL", "Mount failed for %s at %s (VFS error)", record.name, record.mountPath);
            fs->unmount();
            delete fs;
            return status;
        }

        record.fs = fs;
        record.mounted = true;
        QC_LOG_INFO("QFSVOL", "Mounted %s at %s", record.name, record.mountPath);
        return QC::Status::Success;
    }

    FileSystem *VolumeManager::createFileSystem(FileSystemKind kind, BlockDevice *device)
    {
        if (!device)
            return nullptr;

        switch (kind)
        {
        case FileSystemKind::FAT_AUTO:
        {
            QC::u8 sector0[512];
            if (device->readSector(0, sector0) != QC::Status::Success)
                return nullptr;

            FATProbeResult probe;
            if (!probeFATBootSector(sector0, probe))
            {
                QC_LOG_WARN("QFSVOL", "FAT auto-probe failed (not a FAT boot sector)");
                return nullptr;
            }

            switch (probe.kind)
            {
            case FATKind::FAT16:
                return new FAT16(device);
            case FATKind::FAT32:
                return new FAT32(device);
            default:
                QC_LOG_WARN("QFSVOL", "FAT auto-probe: unsupported FAT kind=%u", static_cast<unsigned>(probe.kind));
                return nullptr;
            }
        }
        case FileSystemKind::FAT32:
            return new FAT32(device);
        case FileSystemKind::FAT16:
            return new FAT16(device);
        default:
            break;
        }
        return nullptr;
    }

} // namespace QFS
