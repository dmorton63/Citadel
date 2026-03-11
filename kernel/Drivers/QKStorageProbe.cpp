// QKStorageProbe - Implementation
// Namespace: QKStorage

#include "QKStorageProbe.h"
#include "QKStorageRegistry.h"
#include "QKMemoryBlockDevice.h"
#include "QCLogger.h"
#include "QCString.h"
#include "QCVector.h"
#include "limine.h"

extern "C"
{
    extern QC::u64 limine_module_request[];
}

namespace QKStorage
{
    namespace
    {
        struct ModuleDescriptor
        {
            char name[32];
            char mountPath[128];
            QFS::FileSystemKind fsKind;
        };

        struct ModuleDeviceRecord
        {
            MemoryBlockDevice *device;
        };

        QC::Vector<ModuleDeviceRecord> g_moduleDevices;

        bool startsWith(const char *str, const char *prefix)
        {
            if (!str || !prefix)
                return false;
            while (*prefix)
            {
                if (*str++ != *prefix++)
                    return false;
            }
            return true;
        }

        const char *findChar(const char *str, char ch)
        {
            while (str && *str)
            {
                if (*str == ch)
                    return str;
                ++str;
            }
            return nullptr;
        }

        bool parseDescriptor(const char *cmdline, ModuleDescriptor &out)
        {
            if (!cmdline)
                return false;

            constexpr const char prefix[] = "volume:";
            if (!startsWith(cmdline, prefix))
                return false;

            const char *nameStart = cmdline + (sizeof(prefix) - 1);
            const char *firstSep = findChar(nameStart, ':');
            if (!firstSep)
                return false;
            const char *mountStart = firstSep + 1;
            const char *secondSep = findChar(mountStart, ':');

            QC::usize nameLen = static_cast<QC::usize>(firstSep - nameStart);
            if (nameLen == 0 || nameLen >= sizeof(out.name))
                return false;
            QC::String::memset(out.name, 0, sizeof(out.name));
            QC::String::memcpy(out.name, nameStart, nameLen);

            const char *fsToken = nullptr;
            QC::usize mountLen = 0;
            if (secondSep)
            {
                mountLen = static_cast<QC::usize>(secondSep - mountStart);
                fsToken = secondSep + 1;
            }
            else
            {
                mountLen = QC::String::strlen(mountStart);
            }

            if (mountLen == 0 || mountLen >= sizeof(out.mountPath))
                return false;
            QC::String::memset(out.mountPath, 0, sizeof(out.mountPath));
            QC::String::memcpy(out.mountPath, mountStart, mountLen);

            if (fsToken && QC::String::strlen(fsToken) > 0)
            {
                if (QC::String::strcmp(fsToken, "fat32") == 0 || QC::String::strcmp(fsToken, "FAT32") == 0)
                {
                    out.fsKind = QFS::FileSystemKind::FAT32;
                }
                else if (QC::String::strcmp(fsToken, "fat16") == 0 || QC::String::strcmp(fsToken, "FAT16") == 0)
                {
                    out.fsKind = QFS::FileSystemKind::FAT16;
                }
                else if (QC::String::strcmp(fsToken, "auto") == 0 || QC::String::strcmp(fsToken, "AUTO") == 0)
                {
                    out.fsKind = QFS::FileSystemKind::FAT_AUTO;
                }
                else
                {
                    QC_LOG_WARN("QKStorage", "Unsupported filesystem token '%s'", fsToken);
                    return false;
                }
            }
            else
            {
                out.fsKind = QFS::FileSystemKind::FAT32;
            }

            return true;
        }

        const limine_module_response *moduleResponse()
        {
            return reinterpret_cast<const limine_module_response *>(limine_module_request[5]);
        }

    } // namespace

    void probeLimineModules()
    {
        const limine_module_response *response = moduleResponse();
        if (!response || response->module_count == 0 || !response->modules)
            return;

        QC_LOG_INFO("QKStorage", "Probing Limine modules for volumes...");

        for (QC::u64 i = 0; i < response->module_count; ++i)
        {
            const limine_file *module = response->modules[i];
            if (!module || !module->address || module->size == 0)
                continue;

            if (module->cmdline && QC::String::strcmp(module->cmdline, "ramdisk") == 0)
            {
                // Ramdisk handled separately
                continue;
            }

            ModuleDescriptor descriptor{};
            if (!parseDescriptor(module->cmdline, descriptor))
                continue;

            auto *base = reinterpret_cast<QC::u8 *>(module->address);
            auto *device = new MemoryBlockDevice(base, module->size, 512);

            BlockDeviceRegistration registration{};
            registration.name = descriptor.name;
            registration.mountPath = descriptor.mountPath;
            registration.fsKind = descriptor.fsKind;
            registration.device = device;

            QC::Status status = registerBlockDevice(registration);
            if (status == QC::Status::Success)
            {
                ModuleDeviceRecord record{};
                record.device = device;
                g_moduleDevices.push_back(record);
                QC_LOG_INFO("QKStorage", "Mounted module '%s' at %s", descriptor.name, descriptor.mountPath);
            }
            else
            {
                delete device;
            }
        }
    }

} // namespace QKStorage
