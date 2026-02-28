#include "RamdiskMount.h"

#include "Boot/Config/StartupConfig.h"
#include "Boot/Limine/LimineModules.h"
#include "Boot/Tpm/TpmSecureStore.h"

#include "QCString.h"
#include "QFSDirectory.h"
#include "QFSFile.h"
#include "QFSVFS.h"
#include "QFSVolumeManager.h"
#include "QKMemoryBlockDevice.h"
#include "QKSecurityCenter.h"
#include "QKStorageRegistry.h"

#include "IDE/QKDrvIDE.h"

namespace QK::Boot::Ramdisk
{
    namespace
    {
        static QKStorage::MemoryBlockDevice *g_RamdiskDevice = nullptr;
        static bool g_VfsInitialized = false;

        static constexpr QC::usize kRamdiskSectorSize = 512;

        static void LogStr(FLogFn Log, const char *Msg)
        {
            if (Log)
                Log(Msg);
        }

        static QFS::VFS *EnsureVfsReady(FLogFn Log)
        {
            auto &vfs = QFS::VFS::instance();
            if (!g_VfsInitialized)
            {
                vfs.initialize();
                g_VfsInitialized = true;
                LogStr(Log, "VFS initialized\r\n");
            }
            return &vfs;
        }

        static void ReadHelloFileDemo(QFS::VFS *vfs, FLogFn Log)
        {
            if (!vfs)
                return;

            QFS::File *file = vfs->open("/HELLO.TXT", QFS::OpenMode::Read);
            if (!file)
            {
                LogStr(Log, "Failed to open /HELLO.TXT\r\n");
                return;
            }

            char buffer[256];
            QC::isize bytesRead = file->read(buffer, sizeof(buffer) - 1);
            if (bytesRead > 0)
            {
                buffer[bytesRead] = '\0';
                LogStr(Log, "/HELLO.TXT contents: ");
                LogStr(Log, buffer);
                LogStr(Log, "\r\n");
            }
            else
            {
                LogStr(Log, "Read returned no data for /HELLO.TXT\r\n");
            }

            vfs->close(file);
        }

        static void FileIoDemo(QFS::VFS *vfs, FLogFn Log)
        {
            if (!vfs)
                return;

            LogStr(Log, "Root dir listing:\r\n");
            QFS::Directory *dir = vfs->openDir("/");
            if (dir)
            {
                QFS::DirEntry entry;
                while (dir->read(&entry))
                {
                    LogStr(Log, "  ");
                    LogStr(Log, entry.name);
                    LogStr(Log, "\r\n");
                }
                vfs->closeDir(dir);
            }

            const char *path = "/QFSDEMO.TXT";
            QFS::File *out = vfs->open(path, QFS::OpenMode::Write | QFS::OpenMode::Create | QFS::OpenMode::Truncate);
            if (!out)
            {
                LogStr(Log, "Failed to create demo file\r\n");
                return;
            }

            const char *msg = "QAIOS+ FileIO demo\n";
            out->write(msg, QC::String::strlen(msg));
            vfs->close(out);

            QFS::File *in = vfs->open(path, QFS::OpenMode::Read);
            if (!in)
            {
                LogStr(Log, "Failed to open demo file for read\r\n");
                return;
            }

            char buffer[64];
            QC::isize bytes = in->read(buffer, sizeof(buffer) - 1);
            if (bytes > 0)
            {
                buffer[bytes] = '\0';
                LogStr(Log, "Demo file contents: ");
                LogStr(Log, buffer);
                LogStr(Log, "\r\n");
            }
            vfs->close(in);
        }

        static void ApplyStartupConfigSideEffects(FLogFn Log)
        {
            QKDrv::IDE::setSharedProbeEnabled(QK::Boot::Config::GetIdeSharedProbeEnabled());
            QK::SecurityCenter::instance().initialize(QK::Boot::Config::GetSecurityCenterMode());

            // Leave SAVETERM policy available, but do not auto-run it here.
            (void)Log;
        }

        static bool MountRamdiskVolume(QFS::VFS *vfs, QC::u64 ModuleRequest[], FLogFn Log, const InitOptions &Options)
        {
            if (!vfs)
                return false;

            auto &volumeManager = QFS::VolumeManager::instance();
            constexpr const char *kRamdiskVolumeName = "QFS_RAMDISK0";

            const limine_file *ramdisk = QK::Boot::Limine::FindRamdiskModule(ModuleRequest);
            if (!ramdisk)
            {
                LogStr(Log, "No ramdisk module provided by Limine\r\n");
                return false;
            }

            auto *base = reinterpret_cast<QC::u8 *>(ramdisk->address);
            QC::u64 size = ramdisk->size;

            if (!base || size == 0)
            {
                LogStr(Log, "Ramdisk module is empty or null\r\n");
                return false;
            }

            if (!g_RamdiskDevice)
            {
                g_RamdiskDevice = new QKStorage::MemoryBlockDevice(base, size, kRamdiskSectorSize);
            }

            QKStorage::BlockDeviceRegistration ramdiskReg{};
            ramdiskReg.name = kRamdiskVolumeName;
            ramdiskReg.mountPath = "/";
            ramdiskReg.fsKind = QFS::FileSystemKind::FAT32;
            ramdiskReg.device = g_RamdiskDevice;

            QC::Status registerStatus = QKStorage::registerBlockDevice(ramdiskReg);
            if (registerStatus != QC::Status::Success && registerStatus != QC::Status::Busy)
            {
                LogStr(Log, "Failed to register ramdisk volume\r\n");
                return false;
            }

            if (!volumeManager.isMounted(kRamdiskVolumeName))
            {
                QC::Status mountStatus = volumeManager.mountVolume(kRamdiskVolumeName);
                if (mountStatus != QC::Status::Success)
                {
                    LogStr(Log, "Failed to mount ramdisk filesystem\r\n");
                    return false;
                }
            }

            LogStr(Log, "Ramdisk mounted at /\r\n");

            if (Options.runDemos)
            {
                // Keep the existing boot-time demos/tests as-is.
                FileIoDemo(vfs, Log);
                QK::Boot::Tpm::RunSecureStoreSelfTests(vfs, Log);
                ReadHelloFileDemo(vfs, Log);
            }

            return true;
        }
    } // namespace

    bool InitializeFromLimineModules(QC::u64 ModuleRequest[], FLogFn Log, const InitOptions &Options)
    {
        auto *vfs = EnsureVfsReady(Log);
        if (!vfs)
            return false;

        auto &volumeManager = QFS::VolumeManager::instance();
        constexpr const char *kRamdiskVolumeName = "QFS_RAMDISK0";

        if (volumeManager.isMounted(kRamdiskVolumeName))
        {
            if (Options.loadStartupConfig)
            {
                QK::Boot::Config::LoadFromVfs(Log);
                if (Options.applyStartupSideEffects)
                    ApplyStartupConfigSideEffects(Log);
            }
            return true;
        }

        if (!MountRamdiskVolume(vfs, ModuleRequest, Log, Options))
            return false;

        if (Options.loadStartupConfig)
        {
            QK::Boot::Config::LoadFromVfs(Log);
            if (Options.applyStartupSideEffects)
                ApplyStartupConfigSideEffects(Log);
        }
        return true;
    }
}
