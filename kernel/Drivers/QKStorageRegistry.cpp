// QKStorageRegistry - Implementation
// Namespace: QKStorage

#include "QKStorageRegistry.h"
#include "QCLogger.h"
#include "QCString.h"

namespace QKStorage
{

    static bool hasValidNamePrefix(const char *name)
    {
        if (!name)
            return false;
        constexpr const char prefix[] = "QFS_";
        for (QC::usize i = 0; prefix[i] != '\0'; ++i)
        {
            if (name[i] == '\0' || name[i] != prefix[i])
                return false;
        }
        return true;
    }

    QC::Status registerBlockDevice(const BlockDeviceRegistration &registration)
    {
        if (!registration.name || !registration.mountPath || !registration.device)
            return QC::Status::InvalidParam;

        if (!hasValidNamePrefix(registration.name))
        {
            QC_LOG_WARN("QKStorage", "Volume name %s must start with QFS_ per naming standard", registration.name ? registration.name : "<null>");
            return QC::Status::InvalidParam;
        }

        if (registration.mountPath[0] != '/')
        {
            QC_LOG_WARN("QKStorage", "Mount path %s must be absolute", registration.mountPath);
            return QC::Status::InvalidParam;
        }

        QFS::VolumeDefinition def{};
        def.name = registration.name;
        def.mountPath = registration.mountPath;
        def.fsKind = registration.fsKind;
        def.device = registration.device;
        def.autoMount = registration.autoMount;

        QC::Status status = QFS::VolumeManager::instance().registerVolume(def);
        if (status != QC::Status::Success)
        {
            QC_LOG_WARN("QKStorage", "registerVolume failed for %s (status=%d)", registration.name, static_cast<int>(status));
        }
        return status;
    }

} // namespace QKStorage
