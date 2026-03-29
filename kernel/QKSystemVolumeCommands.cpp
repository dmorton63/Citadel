// QKSystemVolumeCommands - kernel-only command registrations
// Namespace: QK::CmdCenter

#include "QKSystemVolumeCommands.h"

#include "QCCommandRegistry.h"
#include "QCLogger.h"
#include "QCString.h"

#include "IDE/QKDrvIDE.h"
#include "QFSVolumeManager.h"

namespace QK
{
    namespace CmdCenter
    {
        namespace
        {
            static const char *statusName(QC::Status st)
            {
                switch (st)
                {
                case QC::Status::Success:
                    return "Success";
                case QC::Status::Error:
                    return "Error";
                case QC::Status::InvalidParam:
                    return "InvalidParam";
                case QC::Status::OutOfMemory:
                    return "OutOfMemory";
                case QC::Status::NotFound:
                    return "NotFound";
                case QC::Status::Timeout:
                    return "Timeout";
                case QC::Status::Busy:
                    return "Busy";
                case QC::Status::NotSupported:
                    return "NotSupported";
                default:
                    return "(unknown)";
                }
            }

            static void writeStatusLine(const QC::Cmd::Context &ctx, const char *prefix, QC::Status st)
            {
                char line[128];
                QC::String::memset(line, 0, sizeof(line));
                // Build: "<prefix> <Name> (<int>)"
                QC::usize pos = 0;
                if (prefix && *prefix)
                {
                    for (QC::usize i = 0; prefix[i] && pos + 1 < sizeof(line); ++i)
                        line[pos++] = prefix[i];
                }
                if (pos + 1 < sizeof(line))
                    line[pos++] = ' ';

                const char *name = statusName(st);
                for (QC::usize i = 0; name && name[i] && pos + 1 < sizeof(line); ++i)
                    line[pos++] = name[i];

                if (pos + 2 < sizeof(line))
                {
                    line[pos++] = ' ';
                    line[pos++] = '(';
                }

                // signed decimal
                int v = static_cast<int>(st);
                if (v == 0)
                {
                    if (pos + 1 < sizeof(line))
                        line[pos++] = '0';
                }
                else
                {
                    if (v < 0)
                    {
                        if (pos + 1 < sizeof(line))
                            line[pos++] = '-';
                        v = -v;
                    }

                    char tmp[16];
                    int ti = 0;
                    while (v > 0 && ti < 15)
                    {
                        tmp[ti++] = static_cast<char>('0' + (v % 10));
                        v /= 10;
                    }
                    for (int i = ti - 1; i >= 0 && pos + 1 < sizeof(line); --i)
                        line[pos++] = tmp[i];
                }

                if (pos + 1 < sizeof(line))
                    line[pos++] = ')';
                line[pos] = '\0';
                ctx.writeLine(line);
            }

            static bool cmdSysformat(const char *args, const QC::Cmd::Context &ctx, void *)
            {
                (void)args;

                ctx.writeLine("sysformat: formatting system volume as FAT32");

                const QC::Status st = QKDrv::IDE::formatSystemVolumeFAT32();
                if (st == QC::Status::Busy)
                {
                    ctx.writeLine("sysformat: already partitioned/formatted; attempting mount");
                }
                else if (st != QC::Status::Success)
                {
                    ctx.writeLine("sysformat: format failed");
                    writeStatusLine(ctx, "sysformat: status=", st);
                    return true;
                }
                else
                {
                    ctx.writeLine("sysformat: format ok");
                }

                // Re-run system volume probe and mount pending volumes so /system becomes available immediately.
                QKDrv::IDE::resetSystemProbe();
                QKDrv::IDE::probeAndRegisterSystemVolume();
                (void)QFS::VolumeManager::instance().mountPending();

                if (QFS::VolumeManager::instance().isMounted("QFS_SYSTEM"))
                    ctx.writeLine("sysformat: /system mounted");
                else
                    ctx.writeLine("sysformat: /system not mounted (probe or mount failed)");

                return true;
            }

            static bool cmdSysmount(const char *args, const QC::Cmd::Context &ctx, void *)
            {
                (void)args;

                ctx.writeLine("sysmount: probing for system volume and mounting /system");

                QKDrv::IDE::resetSystemProbe();
                QKDrv::IDE::probeAndRegisterSystemVolume();
                (void)QFS::VolumeManager::instance().mountPending();

                if (QFS::VolumeManager::instance().isMounted("QFS_SYSTEM"))
                    ctx.writeLine("sysmount: /system mounted");
                else
                    ctx.writeLine("sysmount: /system not mounted (probe or mount failed)");

                return true;
            }
        }

        void registerSystemVolumeCommands()
        {
            static bool registered = false;
            if (registered)
                return;

            auto &reg = QC::Cmd::Registry::instance();
            (void)reg.registerCommandExAccess(
                "sysformat",
                QC::Cmd::AccessLevel::Admin,
                &cmdSysformat,
                nullptr,
                "Partition+format the system disk as FAT32 and mount /system (sysformat)");

            (void)reg.registerCommandExAccess(
                "sysmount",
                QC::Cmd::AccessLevel::Admin,
                &cmdSysmount,
                nullptr,
                "Probe and mount the system disk at /system (sysmount)");

            QC_LOG_INFO("QKCmd", "Registered sysformat command");
            registered = true;
        }

    } // namespace CmdCenter
} // namespace QK
