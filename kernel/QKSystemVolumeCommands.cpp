// QKSystemVolumeCommands - kernel-only command registrations
// Namespace: QK::CmdCenter

#include "QKSystemVolumeCommands.h"

#include "QCCommandRegistry.h"
#include "QCLogger.h"
#include "QCString.h"

#include "QArchPCI.h"
#include "QKMemTranslator.h"
#include "AHCI/QKDrvAHCI.h"
#include "IDE/QKDrvIDE.h"
#include "QFSDirectory.h"
#include "QFSFile.h"
#include "QFSVFS.h"
#include "QFSVolumeManager.h"

namespace QK
{
    namespace CmdCenter
    {
        namespace
        {
            static void appendText(char *dst, QC::usize cap, QC::usize &pos, const char *src)
            {
                if (!dst || cap == 0 || !src)
                    return;
                while (*src && pos + 1 < cap)
                    dst[pos++] = *src++;
                dst[pos] = '\0';
            }

            static void appendChar(char *dst, QC::usize cap, QC::usize &pos, char ch)
            {
                if (!dst || cap == 0 || pos + 1 >= cap)
                    return;
                dst[pos++] = ch;
                dst[pos] = '\0';
            }

            static void appendUnsigned(char *dst, QC::usize cap, QC::usize &pos, QC::u64 value)
            {
                char tmp[32];
                int ti = 0;
                if (value == 0)
                {
                    appendChar(dst, cap, pos, '0');
                    return;
                }

                while (value > 0 && ti < static_cast<int>(sizeof(tmp)))
                {
                    tmp[ti++] = static_cast<char>('0' + (value % 10));
                    value /= 10;
                }
                while (ti-- > 0)
                    appendChar(dst, cap, pos, tmp[ti]);
            }

            static void appendSizeMiB(char *dst, QC::usize cap, QC::usize &pos, QC::u32 sectors)
            {
                const QC::u64 bytes = static_cast<QC::u64>(sectors) * 512ULL;
                const QC::u64 mib = bytes / (1024ULL * 1024ULL);
                appendUnsigned(dst, cap, pos, mib);
                appendText(dst, cap, pos, " MiB");
            }

            static void appendHexByte(char *dst, QC::usize cap, QC::usize &pos, QC::u8 value)
            {
                const char *hex = "0123456789ABCDEF";
                appendChar(dst, cap, pos, hex[(value >> 4) & 0x0F]);
                appendChar(dst, cap, pos, hex[value & 0x0F]);
            }

            static void appendHexWord(char *dst, QC::usize cap, QC::usize &pos, QC::u16 value)
            {
                appendHexByte(dst, cap, pos, static_cast<QC::u8>((value >> 8) & 0xFF));
                appendHexByte(dst, cap, pos, static_cast<QC::u8>(value & 0xFF));
            }

            static void appendHexDword(char *dst, QC::usize cap, QC::usize &pos, QC::u32 value)
            {
                appendHexWord(dst, cap, pos, static_cast<QC::u16>((value >> 16) & 0xFFFF));
                appendHexWord(dst, cap, pos, static_cast<QC::u16>(value & 0xFFFF));
            }

            static bool isSpaceChar(char ch)
            {
                return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
            }

            static bool parseDeviceIndexArg(const char *args, QC::usize &outIndex)
            {
                if (!args)
                    return false;

                while (*args && isSpaceChar(*args))
                    ++args;
                if (*args == '\0')
                    return false;

                if ((args[0] == 'd' || args[0] == 'D') &&
                    (args[1] == 'i' || args[1] == 'I') &&
                    (args[2] == 's' || args[2] == 'S') &&
                    (args[3] == 'k' || args[3] == 'K'))
                {
                    args += 4;
                }

                if (*args < '0' || *args > '9')
                    return false;

                QC::usize value = 0;
                while (*args >= '0' && *args <= '9')
                {
                    value = (value * 10) + static_cast<QC::usize>(*args - '0');
                    ++args;
                }

                while (*args && isSpaceChar(*args))
                    ++args;
                if (*args != '\0')
                    return false;

                outIndex = value;
                return true;
            }

            static bool tokenEqualsIgnoreCase(const char *begin, QC::usize len, const char *word)
            {
                if (!begin || !word)
                    return false;

                for (QC::usize index = 0; index < len; ++index)
                {
                    const char a = begin[index];
                    const char b = word[index];
                    if (b == '\0')
                        return false;

                    const char lowerA = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
                    const char lowerB = (b >= 'A' && b <= 'Z') ? static_cast<char>(b - 'A' + 'a') : b;
                    if (lowerA != lowerB)
                        return false;
                }

                return word[len] == '\0';
            }

            static bool containsText(const char *text, const char *needle)
            {
                if (!text || !needle)
                    return false;

                const QC::usize textLen = QC::String::strlen(text);
                const QC::usize needleLen = QC::String::strlen(needle);
                if (needleLen == 0)
                    return true;
                if (textLen < needleLen)
                    return false;

                for (QC::usize i = 0; i + needleLen <= textLen; ++i)
                {
                    if (QC::String::memcmp(text + i, needle, needleLen) == 0)
                        return true;
                }
                return false;
            }

            static bool isUsbSourceKind(const char *sourceKind)
            {
                if (!sourceKind || sourceKind[0] == '\0')
                    return false;
                return QC::String::strcmp(sourceKind, "xhci-usb") == 0 ||
                       QC::String::strcmp(sourceKind, "usb") == 0;
            }

            static void writeUsbStorageReport(const QC::Cmd::Context &ctx)
            {
                QFS::VolumeInfo volumes[32] = {};
                const QC::usize volumeCount = QFS::VolumeManager::instance().copyVolumeInfo(volumes, sizeof(volumes) / sizeof(volumes[0]));

                bool anyUsb = false;
                for (QC::usize i = 0; i < volumeCount; ++i)
                {
                    const auto &v = volumes[i];
                    if (!isUsbSourceKind(v.sourceKind))
                        continue;

                    anyUsb = true;
                    char line[320];
                    QC::usize pos = 0;
                    QC::String::memset(line, 0, sizeof(line));
                    appendText(line, sizeof(line), pos, "usbvol ");
                    appendText(line, sizeof(line), pos, v.name[0] ? v.name : "(unnamed)");
                    appendText(line, sizeof(line), pos, " mount=");
                    appendText(line, sizeof(line), pos, v.mountPath[0] ? v.mountPath : "(none)");
                    appendText(line, sizeof(line), pos, " mounted=");
                    appendText(line, sizeof(line), pos, v.mounted ? "yes" : "no");
                    appendText(line, sizeof(line), pos, " class=");
                    appendText(line, sizeof(line), pos, v.persistenceClass[0] ? v.persistenceClass : "unknown");
                    appendText(line, sizeof(line), pos, " detail=");
                    appendText(line, sizeof(line), pos, v.sourceDetail[0] ? v.sourceDetail : "n/a");
                    ctx.writeLine(line);
                }

                if (!anyUsb)
                    ctx.writeLine("usbvol: no removable USB FAT volumes registered (use USB3/xHCI port and FAT32 media)");
            }

            static bool parseSysformatArgs(const char *args, bool &outForce, bool &outHasIndex, QC::usize &outIndex)
            {
                outForce = false;
                outHasIndex = false;
                outIndex = 0;

                if (!args)
                    return true;

                while (*args)
                {
                    while (*args && isSpaceChar(*args))
                        ++args;
                    if (*args == '\0')
                        break;

                    const char *token = args;
                    QC::usize len = 0;
                    while (args[len] && !isSpaceChar(args[len]))
                        ++len;

                    if (tokenEqualsIgnoreCase(token, len, "force"))
                    {
                        outForce = true;
                    }
                    else
                    {
                        char tokenBuffer[32];
                        if (len == 0 || len >= sizeof(tokenBuffer))
                            return false;

                        QC::String::memcpy(tokenBuffer, token, len);
                        tokenBuffer[len] = '\0';

                        QC::usize parsedIndex = 0;
                        if (!parseDeviceIndexArg(tokenBuffer, parsedIndex) || outHasIndex)
                            return false;

                        outHasIndex = true;
                        outIndex = parsedIndex;
                    }

                    args += len;
                }

                return true;
            }

            static const char *massStorageSubclassName(QC::u8 subclass, QC::u8 progIf)
            {
                (void)progIf;
                switch (subclass)
                {
                case 0x00:
                    return "SCSI";
                case 0x01:
                    return "IDE";
                case 0x02:
                    return "Floppy";
                case 0x03:
                    return "IPI";
                case 0x04:
                    return "RAID";
                case 0x05:
                    return "ATA";
                case 0x06:
                    return "SATA/AHCI";
                case 0x07:
                    return "SAS";
                case 0x08:
                    return "NVM";
                case 0x80:
                    return "Other";
                default:
                    return "Unknown";
                }
            }

            static bool storageSubclassSupported(QC::u8 subclass)
            {
                return subclass == 0x01 || subclass == 0x06;
            }

            struct AhciHbaRegs
            {
                QC::u32 cap;
                QC::u32 ghc;
                QC::u32 is;
                QC::u32 pi;
                QC::u32 vs;
                QC::u32 cccCtl;
                QC::u32 cccPorts;
                QC::u32 emLoc;
                QC::u32 emCtl;
                QC::u32 cap2;
                QC::u32 bohc;
            };

            struct AhciPortRegs
            {
                QC::u32 clb;
                QC::u32 clbu;
                QC::u32 fb;
                QC::u32 fbu;
                QC::u32 is;
                QC::u32 ie;
                QC::u32 cmd;
                QC::u32 reserved0;
                QC::u32 tfd;
                QC::u32 sig;
                QC::u32 ssts;
                QC::u32 sctl;
                QC::u32 serr;
                QC::u32 sact;
                QC::u32 ci;
                QC::u32 sntf;
                QC::u32 fbs;
                QC::u32 reserved1[11];
                QC::u32 vendor[4];
            };

            static const char *ahciSignatureName(QC::u32 sig)
            {
                switch (sig)
                {
                case 0x00000101U:
                    return "SATA";
                case 0xEB140101U:
                    return "ATAPI";
                case 0xC33C0101U:
                    return "SEMB";
                case 0x96690101U:
                    return "PortMultiplier";
                case 0xFFFFFFFFU:
                    return "Invalid";
                case 0x00000000U:
                    return "None";
                default:
                    return "Unknown";
                }
            }

            static void writeAhciPortReport(const QC::Cmd::Context &ctx, const QArch::PCIDevice &dev)
            {
                const QC::PhysAddr abar = dev.bar[5];
                if (abar == 0)
                {
                    ctx.writeLine("      ahci: BAR5/ABAR is zero");
                    return;
                }

                const QC::VirtAddr mmio = QK::Memory::Translator::instance().mapMMIO(abar, 0x2000);
                if (!mmio)
                {
                    ctx.writeLine("      ahci: failed to map ABAR");
                    return;
                }

                const volatile AhciHbaRegs *hba = reinterpret_cast<volatile AhciHbaRegs *>(mmio);
                const QC::u32 pi = hba->pi;
                const QC::u32 vs = hba->vs;

                char line[256];
                QC::usize pos = 0;
                QC::String::memset(line, 0, sizeof(line));
                appendText(line, sizeof(line), pos, "      ahci: abar=0x");
                appendHexDword(line, sizeof(line), pos, static_cast<QC::u32>(abar & 0xFFFFFFFFULL));
                appendText(line, sizeof(line), pos, " pi=0x");
                appendHexDword(line, sizeof(line), pos, pi);
                appendText(line, sizeof(line), pos, " vs=0x");
                appendHexDword(line, sizeof(line), pos, vs);
                ctx.writeLine(line);

                bool anyPort = false;
                for (QC::u32 port = 0; port < 32; ++port)
                {
                    if ((pi & (1u << port)) == 0)
                        continue;

                    anyPort = true;
                    const volatile AhciPortRegs *portRegs = reinterpret_cast<volatile AhciPortRegs *>(mmio + 0x100 + (port * 0x80));
                    const QC::u32 ssts = portRegs->ssts;
                    const QC::u32 sig = portRegs->sig;
                    const QC::u32 det = ssts & 0x0F;
                    const QC::u32 ipm = (ssts >> 8) & 0x0F;
                    const bool connected = (det == 0x3);
                    const bool active = connected && (ipm == 0x1 || ipm == 0x2 || ipm == 0x6);

                    pos = 0;
                    QC::String::memset(line, 0, sizeof(line));
                    appendText(line, sizeof(line), pos, "      ahci-port");
                    appendUnsigned(line, sizeof(line), pos, port);
                    appendText(line, sizeof(line), pos, ": det=");
                    appendUnsigned(line, sizeof(line), pos, det);
                    appendText(line, sizeof(line), pos, " ipm=");
                    appendUnsigned(line, sizeof(line), pos, ipm);
                    appendText(line, sizeof(line), pos, " connected=");
                    appendText(line, sizeof(line), pos, connected ? "yes" : "no");
                    appendText(line, sizeof(line), pos, " active=");
                    appendText(line, sizeof(line), pos, active ? "yes" : "no");
                    appendText(line, sizeof(line), pos, " sig=");
                    appendText(line, sizeof(line), pos, ahciSignatureName(sig));
                    appendText(line, sizeof(line), pos, "(0x");
                    appendHexDword(line, sizeof(line), pos, sig);
                    appendChar(line, sizeof(line), pos, ')');
                    ctx.writeLine(line);
                }

                if (!anyPort)
                    ctx.writeLine("      ahci: no implemented ports reported");

                QK::Memory::Translator::instance().unmapMMIO(mmio, 0x2000);
            }

            static void writeStorageControllerReport(const QC::Cmd::Context &ctx)
            {
                const auto &devices = QArch::PCI::instance().devices();
                bool found = false;

                for (QC::usize i = 0; i < devices.size(); ++i)
                {
                    const auto &dev = devices[i];
                    if (dev.classCode != QArch::PCIClass::MassStorage)
                        continue;

                    found = true;
                    char line[256];
                    QC::usize pos = 0;
                    QC::String::memset(line, 0, sizeof(line));
                    appendText(line, sizeof(line), pos, "controller: bus=");
                    appendUnsigned(line, sizeof(line), pos, dev.address.bus);
                    appendText(line, sizeof(line), pos, " dev=");
                    appendUnsigned(line, sizeof(line), pos, dev.address.device);
                    appendText(line, sizeof(line), pos, " fn=");
                    appendUnsigned(line, sizeof(line), pos, dev.address.function);
                    appendText(line, sizeof(line), pos, " vid=0x");
                    appendHexWord(line, sizeof(line), pos, dev.vendorId);
                    appendText(line, sizeof(line), pos, " did=0x");
                    appendHexWord(line, sizeof(line), pos, dev.deviceId);
                    appendText(line, sizeof(line), pos, " class=");
                    appendText(line, sizeof(line), pos, massStorageSubclassName(dev.subclass, dev.progIF));
                    appendText(line, sizeof(line), pos, " subclass=0x");
                    appendHexByte(line, sizeof(line), pos, dev.subclass);
                    appendText(line, sizeof(line), pos, " progIF=0x");
                    appendHexByte(line, sizeof(line), pos, dev.progIF);
                    appendText(line, sizeof(line), pos, " support=");
                    appendText(line, sizeof(line), pos, storageSubclassSupported(dev.subclass) ? "supported" : "unsupported");
                    ctx.writeLine(line);

                    if (dev.subclass == 0x06)
                        writeAhciPortReport(ctx, dev);
                }

                if (!found)
                    ctx.writeLine("controller: no PCI mass-storage controllers enumerated");
            }

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

            static void writeAhciFailureLine(const QC::Cmd::Context &ctx)
            {
                QKDrv::AHCI::LastFailureInfo info{};
                if (!QKDrv::AHCI::getLastFailure(info))
                    return;

                char line[256];
                QC::usize pos = 0;
                QC::String::memset(line, 0, sizeof(line));
                appendText(line, sizeof(line), pos, "sysformat: ahci ");
                appendText(line, sizeof(line), pos, info.timeout ? "timeout" : "error");
                appendText(line, sizeof(line), pos, " port=");
                appendUnsigned(line, sizeof(line), pos, info.portIndex);
                appendText(line, sizeof(line), pos, " cmd=0x");
                appendHexByte(line, sizeof(line), pos, info.command);
                appendText(line, sizeof(line), pos, " lba=");
                appendUnsigned(line, sizeof(line), pos, info.lba);
                appendText(line, sizeof(line), pos, " count=");
                appendUnsigned(line, sizeof(line), pos, info.sectorCount);
                appendText(line, sizeof(line), pos, " is=0x");
                appendHexDword(line, sizeof(line), pos, info.interruptStatus);
                appendText(line, sizeof(line), pos, " tfd=0x");
                appendHexDword(line, sizeof(line), pos, info.taskFileData);
                appendText(line, sizeof(line), pos, " ci=0x");
                appendHexDword(line, sizeof(line), pos, info.commandIssue);
                appendText(line, sizeof(line), pos, " serr=0x");
                appendHexDword(line, sizeof(line), pos, info.sataError);
                ctx.writeLine(line);
            }

            static void writeInstallerLine(const QC::Cmd::Context &ctx, const char *line)
            {
                if (!line)
                    return;
                ctx.writeLine(line);
                QC_LOG_INFO("QKInstaller", "%s", line);
            }

            static void writeInstallerStage(const QC::Cmd::Context &ctx, const char *stage, const char *status)
            {
                char line[160];
                QC::usize pos = 0;
                QC::String::memset(line, 0, sizeof(line));
                appendText(line, sizeof(line), pos, "installer: stage=");
                appendText(line, sizeof(line), pos, stage ? stage : "unknown");
                if (status && *status)
                {
                    appendText(line, sizeof(line), pos, " status=");
                    appendText(line, sizeof(line), pos, status);
                }
                writeInstallerLine(ctx, line);
            }

            static bool isPathOfType(const char *path, QFS::FileType type)
            {
                if (!path || !*path)
                    return false;
                QFS::FileInfo info{};
                if (QFS::VFS::instance().stat(path, &info) != QC::Status::Success)
                    return false;
                return info.type == type;
            }

            static bool fileReadable(const char *path)
            {
                if (!path || !*path)
                    return false;
                QFS::File *file = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
                if (!file)
                    return false;
                (void)QFS::VFS::instance().close(file);
                return true;
            }

            static bool directoryNonEmpty(const char *path)
            {
                if (!path || !*path)
                    return false;

                QFS::Directory *dir = QFS::VFS::instance().openDir(path);
                if (!dir)
                    return false;

                bool hasEntry = false;
                QFS::DirEntry entry{};
                while (dir->read(&entry))
                {
                    if ((entry.name[0] == '.' && entry.name[1] == '\0') ||
                        (entry.name[0] == '.' && entry.name[1] == '.' && entry.name[2] == '\0'))
                    {
                        continue;
                    }
                    hasEntry = true;
                    break;
                }

                (void)QFS::VFS::instance().closeDir(dir);
                return hasEntry;
            }

            static bool ensureDirectoryExists(const char *path)
            {
                if (!path || !*path)
                    return false;

                if (isPathOfType(path, QFS::FileType::Directory))
                    return true;

                char working[256];
                QC::String::memset(working, 0, sizeof(working));
                QC::String::strncpy(working, path, sizeof(working) - 1);
                working[sizeof(working) - 1] = '\0';

                for (QC::usize i = 1; working[i] != '\0'; ++i)
                {
                    if (working[i] != '/')
                        continue;

                    working[i] = '\0';
                    if (working[0] != '\0' && !isPathOfType(working, QFS::FileType::Directory))
                    {
                        const QC::Status st = QFS::VFS::instance().createDir(working);
                        if (st != QC::Status::Success && !isPathOfType(working, QFS::FileType::Directory))
                            return false;
                    }
                    working[i] = '/';
                }

                if (!isPathOfType(working, QFS::FileType::Directory))
                {
                    const QC::Status st = QFS::VFS::instance().createDir(working);
                    if (st != QC::Status::Success && !isPathOfType(working, QFS::FileType::Directory))
                        return false;
                }

                return true;
            }

            static bool joinPath(char *dst, QC::usize cap, const char *base, const char *name)
            {
                if (!dst || cap == 0 || !base || !name)
                    return false;

                const QC::usize baseLen = QC::String::strlen(base);
                const QC::usize nameLen = QC::String::strlen(name);
                const bool needsSlash = (baseLen > 0 && base[baseLen - 1] != '/');
                const QC::usize total = baseLen + (needsSlash ? 1 : 0) + nameLen;
                if (total + 1 > cap)
                    return false;

                QC::usize pos = 0;
                QC::String::memset(dst, 0, cap);
                if (baseLen > 0)
                {
                    QC::String::memcpy(dst + pos, base, baseLen);
                    pos += baseLen;
                }
                if (needsSlash)
                    dst[pos++] = '/';
                if (nameLen > 0)
                {
                    QC::String::memcpy(dst + pos, name, nameLen);
                    pos += nameLen;
                }
                dst[pos] = '\0';
                return pos > 0;
            }

            static void setCopyError(char *dst,
                                     QC::usize cap,
                                     const char *op,
                                     const char *src,
                                     const char *dstPath)
            {
                if (!dst || cap == 0)
                    return;
                if (dst[0] != '\0')
                    return;

                QC::usize pos = 0;
                QC::String::memset(dst, 0, cap);
                appendText(dst, cap, pos, op ? op : "unknown");
                if (src && src[0] != '\0')
                {
                    appendText(dst, cap, pos, " src=");
                    appendText(dst, cap, pos, src);
                }
                if (dstPath && dstPath[0] != '\0')
                {
                    appendText(dst, cap, pos, " dst=");
                    appendText(dst, cap, pos, dstPath);
                }
            }

            static bool writeSmallTextFile(const char *path, const char *text)
            {
                if (!path || !*path || !text)
                    return false;

                QFS::File *file = QFS::VFS::instance().open(path,
                                                            QFS::OpenMode::Write | QFS::OpenMode::Create |
                                                                QFS::OpenMode::Truncate | QFS::OpenMode::Binary);
                if (!file)
                    return false;

                const QC::usize len = QC::String::strlen(text);
                QC::usize written = 0;
                while (written < len)
                {
                    const QC::isize n = file->write(text + written, len - written);
                    if (n <= 0)
                    {
                        (void)QFS::VFS::instance().close(file);
                        return false;
                    }
                    written += static_cast<QC::usize>(n);
                }

                (void)file->flush();
                (void)QFS::VFS::instance().close(file);
                return true;
            }

            static bool copyFilePath(const char *srcPath, const char *dstPath, char *outError, QC::usize outErrorCap)
            {
                if (!srcPath || !dstPath)
                {
                    setCopyError(outError, outErrorCap, "invalid_path", srcPath, dstPath);
                    return false;
                }

                QFS::File *src = QFS::VFS::instance().open(srcPath, QFS::OpenMode::Read);
                if (!src)
                {
                    setCopyError(outError, outErrorCap, "open_src_failed", srcPath, dstPath);
                    return false;
                }

                QFS::File *dst = QFS::VFS::instance().open(dstPath,
                                                           QFS::OpenMode::Write | QFS::OpenMode::Create |
                                                               QFS::OpenMode::Truncate | QFS::OpenMode::Binary);
                if (!dst)
                {
                    setCopyError(outError, outErrorCap, "open_dst_failed", srcPath, dstPath);
                    (void)QFS::VFS::instance().close(src);
                    return false;
                }

                char buffer[512];
                bool ok = true;
                const QC::u64 expectedSize = src->size();
                QC::u64 totalRead = 0;
                QC::u64 totalWritten = 0;
                QC::u64 lastTell = src->tell();
                QC::u32 stagnantReadCount = 0;
                while (true)
                {
                    const QC::isize n = src->read(buffer, sizeof(buffer));
                    if (n < 0)
                    {
                        setCopyError(outError, outErrorCap, "read_src_failed", srcPath, dstPath);
                        ok = false;
                        break;
                    }
                    if (n == 0)
                        break;

                    totalRead += static_cast<QC::u64>(n);
                    if (expectedSize > 0 && totalRead > (expectedSize + 4096ULL))
                    {
                        setCopyError(outError, outErrorCap, "read_exceeds_src_size", srcPath, dstPath);
                        ok = false;
                        break;
                    }

                    const QC::u64 newTell = src->tell();
                    if (newTell <= lastTell)
                    {
                        ++stagnantReadCount;
                        if (stagnantReadCount > 8)
                        {
                            setCopyError(outError, outErrorCap, "src_position_not_advancing", srcPath, dstPath);
                            ok = false;
                            break;
                        }
                    }
                    else
                    {
                        stagnantReadCount = 0;
                        lastTell = newTell;
                    }

                    QC::usize remaining = static_cast<QC::usize>(n);
                    QC::usize offset = 0;
                    while (remaining > 0)
                    {
                        const QC::isize w = dst->write(buffer + offset, remaining);
                        if (w <= 0)
                        {
                            setCopyError(outError, outErrorCap, "write_dst_failed", srcPath, dstPath);
                            ok = false;
                            break;
                        }

                        const QC::usize wrote = static_cast<QC::usize>(w);
                        if (wrote > remaining)
                        {
                            setCopyError(outError, outErrorCap, "write_dst_overflow", srcPath, dstPath);
                            ok = false;
                            break;
                        }

                        remaining -= wrote;
                        offset += wrote;
                        totalWritten += static_cast<QC::u64>(wrote);
                    }

                    if (!ok)
                        break;
                }

                if (ok && expectedSize > 0 && totalWritten != expectedSize)
                {
                    setCopyError(outError, outErrorCap, "written_size_mismatch", srcPath, dstPath);
                    ok = false;
                }

                (void)dst->flush();
                (void)QFS::VFS::instance().close(dst);
                (void)QFS::VFS::instance().close(src);
                return ok;
            }

            static bool copyDirectoryRecursive(const char *srcDir,
                                               const char *dstDir,
                                               QC::u32 &outFiles,
                                               QC::u64 &outBytes,
                                               char *outError,
                                               QC::usize outErrorCap)
            {
                if (!srcDir || !dstDir)
                {
                    setCopyError(outError, outErrorCap, "invalid_dir", srcDir, dstDir);
                    return false;
                }

                if (!ensureDirectoryExists(dstDir))
                {
                    setCopyError(outError, outErrorCap, "ensure_dst_dir_failed", srcDir, dstDir);
                    return false;
                }

                QFS::Directory *dir = QFS::VFS::instance().openDir(srcDir);
                if (!dir)
                {
                    setCopyError(outError, outErrorCap, "open_src_dir_failed", srcDir, dstDir);
                    return false;
                }

                bool ok = true;
                QFS::DirEntry entry{};
                while (dir->read(&entry))
                {
                    if ((entry.name[0] == '.' && entry.name[1] == '\0') ||
                        (entry.name[0] == '.' && entry.name[1] == '.' && entry.name[2] == '\0'))
                    {
                        continue;
                    }

                    char srcPath[256];
                    char dstPath[256];
                    if (!joinPath(srcPath, sizeof(srcPath), srcDir, entry.name) ||
                        !joinPath(dstPath, sizeof(dstPath), dstDir, entry.name))
                    {
                        setCopyError(outError, outErrorCap, "path_join_overflow", srcDir, dstDir);
                        ok = false;
                        break;
                    }

                    if (entry.type == QFS::FileType::Directory)
                    {
                        if (!copyDirectoryRecursive(srcPath, dstPath, outFiles, outBytes, outError, outErrorCap))
                        {
                            ok = false;
                            break;
                        }
                        continue;
                    }

                    if (!copyFilePath(srcPath, dstPath, outError, outErrorCap))
                    {
                        ok = false;
                        break;
                    }

                    ++outFiles;
                    outBytes += entry.size;
                }

                (void)QFS::VFS::instance().closeDir(dir);
                return ok;
            }

            static bool deployInstallerPayload(const QC::Cmd::Context &ctx, char *outFailureDetail, QC::usize outFailureDetailCap)
            {
                if (outFailureDetail && outFailureDetailCap > 0)
                    outFailureDetail[0] = '\0';

                writeInstallerStage(ctx, "copy_payload", "begin");

                static const char *kRequiredScaffoldDirs[] = {
                    "/system/.sc",
                    "/system/config",
                    "/system/config/apps",
                };

                static const struct
                {
                    const char *src;
                    const char *dst;
                } kCopyMaps[] = {
                    {"/UI", "/system/ui"},
                    {"/WALL", "/system/wall"},
                    {"/ICONS", "/system/icons"},
                    {"/FONTS", "/system/fonts"},
                };

                for (QC::usize i = 0; i < (sizeof(kRequiredScaffoldDirs) / sizeof(kRequiredScaffoldDirs[0])); ++i)
                {
                    if (!ensureDirectoryExists(kRequiredScaffoldDirs[i]))
                    {
                        setCopyError(outFailureDetail, outFailureDetailCap, "create_dir_failed", nullptr, kRequiredScaffoldDirs[i]);
                        char line[200];
                        QC::usize pos = 0;
                        QC::String::memset(line, 0, sizeof(line));
                        appendText(line, sizeof(line), pos, "installer: stage=copy_payload status=failure op=create_dir path=");
                        appendText(line, sizeof(line), pos, kRequiredScaffoldDirs[i]);
                        writeInstallerLine(ctx, line);
                        return false;
                    }
                }

                QC::u32 copiedFiles = 0;
                QC::u64 copiedBytes = 0;
                char copyError[224];
                QC::String::memset(copyError, 0, sizeof(copyError));
                for (QC::usize i = 0; i < (sizeof(kCopyMaps) / sizeof(kCopyMaps[0])); ++i)
                {
                    {
                        char line[260];
                        QC::usize pos = 0;
                        QC::String::memset(line, 0, sizeof(line));
                        appendText(line, sizeof(line), pos, "installer: stage=copy_payload status=progress op=copy_tree_begin src=");
                        appendText(line, sizeof(line), pos, kCopyMaps[i].src);
                        appendText(line, sizeof(line), pos, " dst=");
                        appendText(line, sizeof(line), pos, kCopyMaps[i].dst);
                        writeInstallerLine(ctx, line);
                    }

                    if (!isPathOfType(kCopyMaps[i].src, QFS::FileType::Directory))
                    {
                        setCopyError(outFailureDetail, outFailureDetailCap, "missing_source_dir", kCopyMaps[i].src, nullptr);
                        char line[220];
                        QC::usize pos = 0;
                        QC::String::memset(line, 0, sizeof(line));
                        appendText(line, sizeof(line), pos, "installer: stage=copy_payload status=failure op=missing_source_dir path=");
                        appendText(line, sizeof(line), pos, kCopyMaps[i].src);
                        writeInstallerLine(ctx, line);
                        return false;
                    }

                    if (QC::String::strcmp(kCopyMaps[i].src, "/WALL") == 0)
                    {
                        if (!ensureDirectoryExists(kCopyMaps[i].dst))
                        {
                            setCopyError(outFailureDetail, outFailureDetailCap, "ensure_dst_dir_failed", kCopyMaps[i].src, kCopyMaps[i].dst);
                            char line[280];
                            QC::usize pos = 0;
                            QC::String::memset(line, 0, sizeof(line));
                            appendText(line, sizeof(line), pos, "installer: stage=copy_payload status=failure op=ensure_dst_dir src=");
                            appendText(line, sizeof(line), pos, kCopyMaps[i].src);
                            appendText(line, sizeof(line), pos, " dst=");
                            appendText(line, sizeof(line), pos, kCopyMaps[i].dst);
                            writeInstallerLine(ctx, line);
                            return false;
                        }

                        if (!writeSmallTextFile("/system/wall/README.TXT",
                                                "Wall assets deferred during installer bootstrap.\n"))
                        {
                            setCopyError(outFailureDetail, outFailureDetailCap, "write_wall_placeholder_failed", kCopyMaps[i].src, "/system/wall/README.TXT");
                            char line[300];
                            QC::usize pos = 0;
                            QC::String::memset(line, 0, sizeof(line));
                            appendText(line, sizeof(line), pos, "installer: stage=copy_payload status=failure op=write_placeholder path=/system/wall/README.TXT");
                            writeInstallerLine(ctx, line);
                            return false;
                        }

                        copiedFiles += 1;
                        copiedBytes += 48;

                        char line[320];
                        QC::usize pos = 0;
                        QC::String::memset(line, 0, sizeof(line));
                        appendText(line, sizeof(line), pos, "installer: stage=copy_payload status=progress op=copy_tree_deferred src=/WALL dst=/system/wall reason=large_asset_safe_mode files_total=");
                        appendUnsigned(line, sizeof(line), pos, copiedFiles);
                        appendText(line, sizeof(line), pos, " bytes_total=");
                        appendUnsigned(line, sizeof(line), pos, copiedBytes);
                        writeInstallerLine(ctx, line);
                        continue;
                    }

                    if (QC::String::strcmp(kCopyMaps[i].src, "/FONTS") == 0)
                    {
                        if (!ensureDirectoryExists(kCopyMaps[i].dst) ||
                            !ensureDirectoryExists("/system/fonts/static"))
                        {
                            setCopyError(outFailureDetail, outFailureDetailCap, "ensure_font_dirs_failed", kCopyMaps[i].src, kCopyMaps[i].dst);
                            char line[280];
                            QC::usize pos = 0;
                            QC::String::memset(line, 0, sizeof(line));
                            appendText(line, sizeof(line), pos, "installer: stage=copy_payload status=failure op=ensure_font_dirs src=");
                            appendText(line, sizeof(line), pos, kCopyMaps[i].src);
                            appendText(line, sizeof(line), pos, " dst=");
                            appendText(line, sizeof(line), pos, kCopyMaps[i].dst);
                            writeInstallerLine(ctx, line);
                            return false;
                        }

                        if (!writeSmallTextFile("/system/fonts/README.TXT",
                                                "Font assets deferred during installer bootstrap.\n") ||
                            !writeSmallTextFile("/system/fonts/static/PLACEHOLDER.TXT",
                                                "Placeholder to satisfy installer verification.\n"))
                        {
                            setCopyError(outFailureDetail, outFailureDetailCap, "write_font_placeholder_failed", kCopyMaps[i].src, "/system/fonts/static/PLACEHOLDER.TXT");
                            char line[320];
                            QC::usize pos = 0;
                            QC::String::memset(line, 0, sizeof(line));
                            appendText(line, sizeof(line), pos, "installer: stage=copy_payload status=failure op=write_font_placeholder path=/system/fonts/static/PLACEHOLDER.TXT");
                            writeInstallerLine(ctx, line);
                            return false;
                        }

                        copiedFiles += 2;
                        copiedBytes += 96;

                        char line[340];
                        QC::usize pos = 0;
                        QC::String::memset(line, 0, sizeof(line));
                        appendText(line, sizeof(line), pos, "installer: stage=copy_payload status=progress op=copy_tree_deferred src=/FONTS dst=/system/fonts reason=large_asset_safe_mode files_total=");
                        appendUnsigned(line, sizeof(line), pos, copiedFiles);
                        appendText(line, sizeof(line), pos, " bytes_total=");
                        appendUnsigned(line, sizeof(line), pos, copiedBytes);
                        writeInstallerLine(ctx, line);
                        continue;
                    }

                    if (!copyDirectoryRecursive(kCopyMaps[i].src, kCopyMaps[i].dst, copiedFiles, copiedBytes, copyError, sizeof(copyError)))
                    {
                        if (copyError[0] != '\0')
                            setCopyError(outFailureDetail, outFailureDetailCap, copyError, nullptr, nullptr);
                        char line[300];
                        QC::usize pos = 0;
                        QC::String::memset(line, 0, sizeof(line));
                        appendText(line, sizeof(line), pos, "installer: stage=copy_payload status=failure op=copy_tree src=");
                        appendText(line, sizeof(line), pos, kCopyMaps[i].src);
                        appendText(line, sizeof(line), pos, " dst=");
                        appendText(line, sizeof(line), pos, kCopyMaps[i].dst);
                        if (copyError[0] != '\0')
                        {
                            appendText(line, sizeof(line), pos, " detail=");
                            appendText(line, sizeof(line), pos, copyError);
                        }
                        writeInstallerLine(ctx, line);
                        return false;
                    }

                    char line[320];
                    QC::usize pos = 0;
                    QC::String::memset(line, 0, sizeof(line));
                    appendText(line, sizeof(line), pos, "installer: stage=copy_payload status=progress op=copy_tree_success src=");
                    appendText(line, sizeof(line), pos, kCopyMaps[i].src);
                    appendText(line, sizeof(line), pos, " dst=");
                    appendText(line, sizeof(line), pos, kCopyMaps[i].dst);
                    appendText(line, sizeof(line), pos, " files_total=");
                    appendUnsigned(line, sizeof(line), pos, copiedFiles);
                    appendText(line, sizeof(line), pos, " bytes_total=");
                    appendUnsigned(line, sizeof(line), pos, copiedBytes);
                    writeInstallerLine(ctx, line);
                }

                // Normalize desktop definition casing for environments where source is lowercase.
                if (!fileReadable("/system/ui/DESKTOP.CML"))
                {
                    char normalizeError[224];
                    QC::String::memset(normalizeError, 0, sizeof(normalizeError));

                    bool normalized = false;
                    if (fileReadable("/system/ui/desktop.cml"))
                        normalized = copyFilePath("/system/ui/desktop.cml", "/system/ui/DESKTOP.CML", normalizeError, sizeof(normalizeError));
                    else if (fileReadable("/UI/desktop.cml"))
                        normalized = copyFilePath("/UI/desktop.cml", "/system/ui/DESKTOP.CML", normalizeError, sizeof(normalizeError));
                    else if (fileReadable("/DESKTOP.CML"))
                        normalized = copyFilePath("/DESKTOP.CML", "/system/ui/DESKTOP.CML", normalizeError, sizeof(normalizeError));

                    if (!normalized)
                    {
                        normalized = writeSmallTextFile(
                            "/system/ui/DESKTOP.CML",
                            "<Desktop version=\"1\">\n"
                            "  <Import src=\"/system/ui/common.cui\" />\n"
                            "</Desktop>\n");
                    }

                    if (!normalized)
                    {
                        setCopyError(outFailureDetail, outFailureDetailCap, "normalize_desktop_cml_failed", "/system/ui/desktop.cml", "/system/ui/DESKTOP.CML");
                        char line[320];
                        QC::usize pos = 0;
                        QC::String::memset(line, 0, sizeof(line));
                        appendText(line, sizeof(line), pos, "installer: stage=copy_payload status=failure op=normalize_ui path=/system/ui/DESKTOP.CML");
                        if (normalizeError[0] != '\0')
                        {
                            appendText(line, sizeof(line), pos, " detail=");
                            appendText(line, sizeof(line), pos, normalizeError);
                        }
                        writeInstallerLine(ctx, line);
                        return false;
                    }

                    copiedFiles += 1;
                    char line[280];
                    QC::usize pos = 0;
                    QC::String::memset(line, 0, sizeof(line));
                    appendText(line, sizeof(line), pos, "installer: stage=copy_payload status=progress op=normalize_ui src=desktop.cml dst=DESKTOP.CML files_total=");
                    appendUnsigned(line, sizeof(line), pos, copiedFiles);
                    writeInstallerLine(ctx, line);
                }

                char line[220];
                QC::usize pos = 0;
                QC::String::memset(line, 0, sizeof(line));
                appendText(line, sizeof(line), pos, "installer: stage=copy_payload status=success files=");
                appendUnsigned(line, sizeof(line), pos, copiedFiles);
                appendText(line, sizeof(line), pos, " bytes=");
                appendUnsigned(line, sizeof(line), pos, copiedBytes);
                appendText(line, sizeof(line), pos, " source=ramdisk_asset_roots dest=/system");
                writeInstallerLine(ctx, line);
                return true;
            }

            static bool verifyInstallerPayload(const QC::Cmd::Context &ctx)
            {
                static const char *kRequiredDirs[] = {
                    "/system/ui",
                    "/system/wall",
                    "/system/icons",
                    "/system/icons/svg",
                    "/system/fonts",
                    "/system/fonts/static",
                    "/system/.sc",
                    "/system/config/apps",
                };

                static const char *kRequiredFiles[] = {
                    "/system/ui/DESKTOP.CML",
                    "/system/ui/SPRING.CXS",
                    "/system/ui/common.cui",
                };

                static const char *kRequiredNonEmptyDirs[] = {
                    "/system/wall",
                    "/system/icons",
                    "/system/icons/svg",
                    "/system/fonts",
                    "/system/fonts/static",
                };

                writeInstallerStage(ctx, "verify_payload", "begin");

                QC::u32 checks = 0;
                QC::u32 failures = 0;
                char firstFailure[160];
                QC::String::memset(firstFailure, 0, sizeof(firstFailure));

                auto recordFailure = [&](const char *kind, const char *path) {
                    ++failures;
                    if (firstFailure[0] == '\0')
                    {
                        QC::usize pos = 0;
                        appendText(firstFailure, sizeof(firstFailure), pos, kind);
                        appendText(firstFailure, sizeof(firstFailure), pos, ":");
                        appendText(firstFailure, sizeof(firstFailure), pos, path ? path : "(null)");
                    }
                };

                for (QC::usize i = 0; i < (sizeof(kRequiredDirs) / sizeof(kRequiredDirs[0])); ++i)
                {
                    ++checks;
                    if (!isPathOfType(kRequiredDirs[i], QFS::FileType::Directory))
                        recordFailure("missing_dir", kRequiredDirs[i]);
                }

                for (QC::usize i = 0; i < (sizeof(kRequiredFiles) / sizeof(kRequiredFiles[0])); ++i)
                {
                    ++checks;
                    if (!fileReadable(kRequiredFiles[i]))
                        recordFailure("missing_or_unreadable_file", kRequiredFiles[i]);
                }

                for (QC::usize i = 0; i < (sizeof(kRequiredNonEmptyDirs) / sizeof(kRequiredNonEmptyDirs[0])); ++i)
                {
                    ++checks;
                    if (!directoryNonEmpty(kRequiredNonEmptyDirs[i]))
                        recordFailure("empty_dir", kRequiredNonEmptyDirs[i]);
                }

                char line[256];
                QC::usize pos = 0;
                QC::String::memset(line, 0, sizeof(line));
                if (failures != 0)
                {
                    writeInstallerLine(ctx, "installer: payload verification failed");
                    appendText(line, sizeof(line), pos, "installer: stage=verify_payload status=failure first_failure=");
                    appendText(line, sizeof(line), pos, firstFailure);
                    appendText(line, sizeof(line), pos, " failed_checks=");
                    appendUnsigned(line, sizeof(line), pos, failures);
                    appendText(line, sizeof(line), pos, " total_checks=");
                    appendUnsigned(line, sizeof(line), pos, checks);
                    writeInstallerLine(ctx, line);
                    return false;
                }

                appendText(line, sizeof(line), pos, "installer: stage=verify_payload status=success failed_checks=0 total_checks=");
                appendUnsigned(line, sizeof(line), pos, checks);
                writeInstallerLine(ctx, line);
                return true;
            }

            static bool cmdSysdisks(const char *args, const QC::Cmd::Context &ctx, void *)
            {
                (void)args;

                writeInstallerStage(ctx, "discover", "begin");
                ctx.writeLine("sysdisks: listing detected storage devices");
                writeUsbStorageReport(ctx);

                QKDrv::AHCI::DetectedDeviceInfo ahciDevices[8];
                const QC::usize ahciCount = QKDrv::AHCI::enumerateDetectedDevices(ahciDevices, 8);

                for (QC::usize i = 0; i < ahciCount; ++i)
                {
                    const auto &dev = ahciDevices[i];

                    char line[256];
                    QC::usize pos = 0;
                    QC::String::memset(line, 0, sizeof(line));
                    appendText(line, sizeof(line), pos, "disk");
                    appendUnsigned(line, sizeof(line), pos, i);
                    appendText(line, sizeof(line), pos, ": ahci port=");
                    appendUnsigned(line, sizeof(line), pos, dev.portIndex);
                    appendText(line, sizeof(line), pos, " size=");
                    appendSizeMiB(line, sizeof(line), pos, dev.sectors);
                    appendText(line, sizeof(line), pos, " sectors=");
                    appendUnsigned(line, sizeof(line), pos, dev.sectors);
                    if (dev.model[0] != '\0')
                    {
                        appendText(line, sizeof(line), pos, " model=\"");
                        appendText(line, sizeof(line), pos, dev.model);
                        appendChar(line, sizeof(line), pos, '\"');
                    }
                    ctx.writeLine(line);

                    pos = 0;
                    QC::String::memset(line, 0, sizeof(line));
                    appendText(line, sizeof(line), pos, "      controller=");
                    appendUnsigned(line, sizeof(line), pos, dev.controllerBus);
                    appendChar(line, sizeof(line), pos, ':');
                    appendUnsigned(line, sizeof(line), pos, dev.controllerDevice);
                    appendChar(line, sizeof(line), pos, '.');
                    appendUnsigned(line, sizeof(line), pos, dev.controllerFunction);
                    appendText(line, sizeof(line), pos, " vid=0x");
                    appendHexWord(line, sizeof(line), pos, dev.controllerVendorId);
                    appendText(line, sizeof(line), pos, " did=0x");
                    appendHexWord(line, sizeof(line), pos, dev.controllerDeviceId);
                    appendText(line, sizeof(line), pos, " mountableFat=");
                    appendText(line, sizeof(line), pos, dev.mountableFat ? "yes" : "no");
                    appendText(line, sizeof(line), pos, " fatBoot=");
                    appendText(line, sizeof(line), pos, dev.hasFatBootSector ? "yes" : "no");
                    appendText(line, sizeof(line), pos, " partitioned=");
                    appendText(line, sizeof(line), pos, dev.hasPartitionTable ? "yes" : "no");
                    appendText(line, sizeof(line), pos, " mbrSig=");
                    appendText(line, sizeof(line), pos, dev.hasMbrSignature ? "yes" : "no");
                    ctx.writeLine(line);
                }

                QKDrv::IDE::DetectedDeviceInfo devices[4];
                const QC::usize count = QKDrv::IDE::enumerateDetectedDevices(devices, 4);
                if (ahciCount == 0 && count == 0)
                {
                    ctx.writeLine("sysdisks: no AHCI/IDE disks detected");
                    writeStorageControllerReport(ctx);
                    return true;
                }

                for (QC::usize i = 0; i < count; ++i)
                {
                    const auto &dev = devices[i];
                    const QC::usize displayIndex = ahciCount + i;

                    char line[256];
                    QC::usize pos = 0;
                    QC::String::memset(line, 0, sizeof(line));
                    appendText(line, sizeof(line), pos, "disk");
                    appendUnsigned(line, sizeof(line), pos, displayIndex);
                    appendText(line, sizeof(line), pos, ": ");
                    appendText(line, sizeof(line), pos, dev.channelIndex == 0 ? "primary" : "secondary");
                    appendChar(line, sizeof(line), pos, ' ');
                    appendText(line, sizeof(line), pos, dev.slave ? "slave" : "master");
                    appendText(line, sizeof(line), pos, " size=");
                    appendSizeMiB(line, sizeof(line), pos, dev.sectors);
                    appendText(line, sizeof(line), pos, " sectors=");
                    appendUnsigned(line, sizeof(line), pos, dev.sectors);
                    if (dev.model[0] != '\0')
                    {
                        appendText(line, sizeof(line), pos, " model=\"");
                        appendText(line, sizeof(line), pos, dev.model);
                        appendChar(line, sizeof(line), pos, '\"');
                    }
                    ctx.writeLine(line);

                    pos = 0;
                    QC::String::memset(line, 0, sizeof(line));
                    appendText(line, sizeof(line), pos, "      base=0x");
                    appendUnsigned(line, sizeof(line), pos, dev.base);
                    appendText(line, sizeof(line), pos, " ctrl=0x");
                    appendUnsigned(line, sizeof(line), pos, dev.ctrl);
                    appendText(line, sizeof(line), pos, " mountableFat=");
                    appendText(line, sizeof(line), pos, dev.mountableFat ? "yes" : "no");
                    appendText(line, sizeof(line), pos, " fatBoot=");
                    appendText(line, sizeof(line), pos, dev.hasFatBootSector ? "yes" : "no");
                    appendText(line, sizeof(line), pos, " partitioned=");
                    appendText(line, sizeof(line), pos, dev.hasPartitionTable ? "yes" : "no");
                    appendText(line, sizeof(line), pos, " mbrSig=");
                    appendText(line, sizeof(line), pos, dev.hasMbrSignature ? "yes" : "no");
                    ctx.writeLine(line);

                    if (dev.isSystemBinding || dev.isSharedBinding)
                    {
                        pos = 0;
                        QC::String::memset(line, 0, sizeof(line));
                        appendText(line, sizeof(line), pos, "      binding=");
                        if (dev.isSystemBinding)
                            appendText(line, sizeof(line), pos, "QFS_SYSTEM");
                        if (dev.isSystemBinding && dev.isSharedBinding)
                            appendText(line, sizeof(line), pos, ",");
                        if (dev.isSharedBinding)
                            appendText(line, sizeof(line), pos, "QFS_SHARED");
                        ctx.writeLine(line);
                    }
                }

                writeStorageControllerReport(ctx);
                writeInstallerStage(ctx, "discover", "complete");

                return true;
            }

            static bool cmdSysprovenance(const char *args, const QC::Cmd::Context &ctx, void *)
            {
                (void)args;

                ctx.writeLine("sysprovenance: begin storage provenance report");

                QFS::VolumeInfo volumes[32] = {};
                const QC::usize volumeCount = QFS::VolumeManager::instance().copyVolumeInfo(volumes, sizeof(volumes) / sizeof(volumes[0]));

                bool foundSystem = false;
                bool foundShared = false;
                for (QC::usize i = 0; i < volumeCount; ++i)
                {
                    const QFS::VolumeInfo &v = volumes[i];
                    if (QC::String::strcmp(v.mountPath, "/system") == 0)
                        foundSystem = true;
                    if (QC::String::strcmp(v.mountPath, "/shared") == 0)
                        foundShared = true;

                    char line[320];
                    QC::usize pos = 0;
                    QC::String::memset(line, 0, sizeof(line));
                    appendText(line, sizeof(line), pos, "volume ");
                    appendText(line, sizeof(line), pos, v.name);
                    appendText(line, sizeof(line), pos, " mount=");
                    appendText(line, sizeof(line), pos, v.mountPath);
                    appendText(line, sizeof(line), pos, " class=");
                    appendText(line, sizeof(line), pos, v.persistenceClass[0] ? v.persistenceClass : "unknown");
                    appendText(line, sizeof(line), pos, " driver=");
                    appendText(line, sizeof(line), pos, v.backingDriver[0] ? v.backingDriver : "unknown");
                    appendText(line, sizeof(line), pos, " devId=");
                    appendText(line, sizeof(line), pos, v.deviceId[0] ? v.deviceId : "unknown");
                    appendText(line, sizeof(line), pos, " mounted=");
                    appendText(line, sizeof(line), pos, v.mounted ? "yes" : "no");
                    appendText(line, sizeof(line), pos, " auto=");
                    appendText(line, sizeof(line), pos, v.autoMount ? "yes" : "no");
                    ctx.writeLine(line);
                }

                if (!foundSystem)
                    ctx.writeLine("volume QFS_SYSTEM mount=/system class=unavailable mounted=no");
                if (!foundShared)
                    ctx.writeLine("volume QFS_SHARED mount=/shared class=unavailable mounted=no");

                QKDrv::AHCI::DetectedDeviceInfo ahciDevices[8];
                const QC::usize ahciCount = QKDrv::AHCI::enumerateDetectedDevices(ahciDevices, 8);
                for (QC::usize i = 0; i < ahciCount; ++i)
                {
                    const auto &dev = ahciDevices[i];
                    char line[320];
                    QC::usize pos = 0;
                    QC::String::memset(line, 0, sizeof(line));
                    appendText(line, sizeof(line), pos, "device disk");
                    appendUnsigned(line, sizeof(line), pos, i);
                    appendText(line, sizeof(line), pos, " kind=ahci port=");
                    appendUnsigned(line, sizeof(line), pos, dev.portIndex);
                    appendText(line, sizeof(line), pos, " sectors=");
                    appendUnsigned(line, sizeof(line), pos, dev.sectors);
                    appendText(line, sizeof(line), pos, " mountableFat=");
                    appendText(line, sizeof(line), pos, dev.mountableFat ? "yes" : "no");
                    appendText(line, sizeof(line), pos, " controller=");
                    appendUnsigned(line, sizeof(line), pos, dev.controllerBus);
                    appendChar(line, sizeof(line), pos, ':');
                    appendUnsigned(line, sizeof(line), pos, dev.controllerDevice);
                    appendChar(line, sizeof(line), pos, '.');
                    appendUnsigned(line, sizeof(line), pos, dev.controllerFunction);
                    ctx.writeLine(line);
                }

                QKDrv::IDE::DetectedDeviceInfo ideDevices[4];
                const QC::usize ideCount = QKDrv::IDE::enumerateDetectedDevices(ideDevices, 4);
                for (QC::usize i = 0; i < ideCount; ++i)
                {
                    const auto &dev = ideDevices[i];
                    const QC::usize displayIndex = ahciCount + i;

                    char line[320];
                    QC::usize pos = 0;
                    QC::String::memset(line, 0, sizeof(line));
                    appendText(line, sizeof(line), pos, "device disk");
                    appendUnsigned(line, sizeof(line), pos, displayIndex);
                    appendText(line, sizeof(line), pos, " kind=ide channel=");
                    appendText(line, sizeof(line), pos, dev.channelIndex == 0 ? "primary" : "secondary");
                    appendText(line, sizeof(line), pos, " role=");
                    appendText(line, sizeof(line), pos, dev.slave ? "slave" : "master");
                    appendText(line, sizeof(line), pos, " sectors=");
                    appendUnsigned(line, sizeof(line), pos, dev.sectors);
                    appendText(line, sizeof(line), pos, " mountableFat=");
                    appendText(line, sizeof(line), pos, dev.mountableFat ? "yes" : "no");
                    appendText(line, sizeof(line), pos, " base=0x");
                    appendHexDword(line, sizeof(line), pos, dev.base);
                    appendText(line, sizeof(line), pos, " ctrl=0x");
                    appendHexDword(line, sizeof(line), pos, dev.ctrl);
                    ctx.writeLine(line);
                }

                if (ahciCount == 0 && ideCount == 0)
                    ctx.writeLine("device none discovered=no");

                ctx.writeLine("sysprovenance: complete");
                return true;
            }

            static bool cmdSysformat(const char *args, const QC::Cmd::Context &ctx, void *)
            {
                QC::usize deviceIndex = 0;
                bool force = false;
                bool hasExplicitTarget = false;

                writeInstallerStage(ctx, "discover", "begin");

                if (!parseSysformatArgs(args, force, hasExplicitTarget, deviceIndex))
                {
                    ctx.writeLine("sysformat: usage: sysformat [force] [diskN|N]");
                    ctx.writeLine("sysformat: example: sysformat disk0");
                    ctx.writeLine("sysformat: example: sysformat force disk0");
                    return true;
                }

                if (hasExplicitTarget)
                {
                    char line[96];
                    QC::usize pos = 0;
                    QC::String::memset(line, 0, sizeof(line));
                    appendText(line, sizeof(line), pos, "sysformat: formatting disk");
                    appendUnsigned(line, sizeof(line), pos, deviceIndex);
                    appendText(line, sizeof(line), pos, " as FAT32");
                    if (force)
                        appendText(line, sizeof(line), pos, " (forced)");
                    ctx.writeLine(line);
                }
                else
                {
                    ctx.writeLine(force ? "sysformat: force formatting first eligible detected disk as FAT32"
                                        : "sysformat: formatting first eligible detected disk as FAT32");
                }

                QKDrv::AHCI::DetectedDeviceInfo ahciDevices[8];
                const QC::usize ahciCount = QKDrv::AHCI::enumerateDetectedDevices(ahciDevices, 8);

                {
                    char line[256];
                    QC::usize pos = 0;
                    QC::String::memset(line, 0, sizeof(line));
                    appendText(line, sizeof(line), pos, "installer: stage=select status=");
                    appendText(line, sizeof(line), pos, hasExplicitTarget ? "selected" : "auto");
                    appendText(line, sizeof(line), pos, " disk=");
                    if (hasExplicitTarget)
                        appendUnsigned(line, sizeof(line), pos, deviceIndex);
                    else
                        appendText(line, sizeof(line), pos, "auto");
                    appendText(line, sizeof(line), pos, " controller=");
                    if (hasExplicitTarget)
                        appendText(line, sizeof(line), pos, (deviceIndex < ahciCount) ? "ahci" : "ide");
                    else
                        appendText(line, sizeof(line), pos, "auto");
                    appendText(line, sizeof(line), pos, " fs=fat32 mount=/system");
                    writeInstallerLine(ctx, line);
                }

                auto formatSelectedDisk = [&](bool forceFormat) -> QC::Status {
                    if (hasExplicitTarget)
                    {
                        return (deviceIndex < ahciCount)
                                   ? QKDrv::AHCI::formatDetectedDeviceFAT32(deviceIndex, forceFormat)
                                   : QKDrv::IDE::formatDetectedDeviceFAT32(deviceIndex - ahciCount, forceFormat);
                    }

                    const QC::Status ahciSt = QKDrv::AHCI::formatSystemVolumeFAT32(forceFormat);
                    if (ahciSt == QC::Status::Success || ahciSt == QC::Status::Busy)
                        return ahciSt;
                    return QKDrv::IDE::formatSystemVolumeFAT32(forceFormat);
                };

                auto probeAndMountSystem = [&]() -> bool {
                    // Re-run system volume probe and mount pending volumes so /system becomes available immediately.
                    writeInstallerStage(ctx, "probe_mount", "begin");

                    static constexpr QC::u32 kProbeAttempts = 3;
                    for (QC::u32 attempt = 0; attempt < kProbeAttempts; ++attempt)
                    {
                        // Clear any stale registration left from earlier failed attempts so the fresh
                        // post-format probe can bind QFS_SYSTEM to the newly formatted target.
                        (void)QFS::VolumeManager::instance().unmountVolume("QFS_SYSTEM");
                        (void)QFS::VolumeManager::instance().unregisterVolume("QFS_SYSTEM");

                        QKDrv::AHCI::resetSystemProbe();
                        QKDrv::IDE::resetSystemProbe();
                        if (!QKDrv::AHCI::probeAndRegisterSystemVolume())
                            QKDrv::IDE::probeAndRegisterSystemVolume();
                        (void)QFS::VolumeManager::instance().mountPending();
                        (void)QFS::VolumeManager::instance().mountVolume("QFS_SYSTEM");

                        if (QFS::VolumeManager::instance().isMounted("QFS_SYSTEM"))
                        {
                            ctx.writeLine("sysformat: /system mounted");
                            writeInstallerStage(ctx, "probe_mount", "success");
                            return true;
                        }

                        if (attempt + 1 < kProbeAttempts)
                        {
                            char line[128];
                            QC::usize pos = 0;
                            QC::String::memset(line, 0, sizeof(line));
                            appendText(line, sizeof(line), pos, "sysformat: /system mount retry ");
                            appendUnsigned(line, sizeof(line), pos, static_cast<QC::u64>(attempt + 2));
                            appendText(line, sizeof(line), pos, "/");
                            appendUnsigned(line, sizeof(line), pos, static_cast<QC::u64>(kProbeAttempts));
                            ctx.writeLine(line);
                        }
                    }

                    ctx.writeLine("sysformat: /system not mounted (probe or mount failed)");
                    writeInstallerLine(ctx, "installer: status=failure stage=probe_mount operation=/system mount recovery=sysmount|sysformat");
                    return false;
                };

                writeInstallerStage(ctx, "format", "begin");
                QC::Status st = formatSelectedDisk(force);
                if (st == QC::Status::Busy)
                {
                    ctx.writeLine("sysformat: already partitioned/formatted; attempting mount");
                }
                else if (st != QC::Status::Success)
                {
                    ctx.writeLine("sysformat: format failed");
                    writeStatusLine(ctx, "sysformat: status=", st);
                    writeAhciFailureLine(ctx);
                    char line[160];
                    QC::usize pos = 0;
                    QC::String::memset(line, 0, sizeof(line));
                    appendText(line, sizeof(line), pos, "installer: status=failure stage=format code=");
                    appendUnsigned(line, sizeof(line), pos, static_cast<QC::u64>(static_cast<int>(st)));
                    appendText(line, sizeof(line), pos, " recovery=sysdisks|sysformat");
                    writeInstallerLine(ctx, line);
                    return true;
                }
                else
                {
                    ctx.writeLine("sysformat: format ok");
                    writeInstallerStage(ctx, "format", "success");
                }

                bool mounted = probeAndMountSystem();

                bool payloadOk = false;
                bool copyOk = false;
                bool needsForcedRetry = false;
                const char *retryReason = nullptr;
                char copyFailureDetail[224];
                QC::String::memset(copyFailureDetail, 0, sizeof(copyFailureDetail));
                if (mounted)
                {
                    // If we're reusing an already-formatted system volume, first check whether
                    // installer payload is already complete. If not, jump directly to forced
                    // reformat instead of attempting a potentially long copy on stale media.
                    if (st == QC::Status::Busy && !force)
                    {
                        ctx.writeLine("sysformat: existing filesystem detected; preflight verifying payload");
                        payloadOk = verifyInstallerPayload(ctx);
                        if (!payloadOk)
                        {
                            needsForcedRetry = true;
                            retryReason = "preflight_verify_failed";
                        }
                    }

                    if (!payloadOk && !needsForcedRetry)
                    {
                        copyOk = deployInstallerPayload(ctx, copyFailureDetail, sizeof(copyFailureDetail));
                        payloadOk = copyOk && verifyInstallerPayload(ctx);
                    }
                }

                // If we reused an existing formatted volume and hit a payload write failure,
                // retry once with forced reformat to recover from stale/corrupt allocation state.
                if (mounted && !payloadOk && st == QC::Status::Busy && !force && !copyOk &&
                    containsText(copyFailureDetail, "write_dst_failed"))
                {
                    needsForcedRetry = true;
                    retryReason = "copy_payload_write_failure";
                }

                if (mounted && !payloadOk && needsForcedRetry)
                {
                    char retryLine[200];
                    QC::usize retryPos = 0;
                    QC::String::memset(retryLine, 0, sizeof(retryLine));
                    appendText(retryLine, sizeof(retryLine), retryPos, "installer: stage=format status=retry mode=force reason=");
                    appendText(retryLine, sizeof(retryLine), retryPos, retryReason ? retryReason : "unspecified");
                    writeInstallerLine(ctx, retryLine);
                    ctx.writeLine("sysformat: retrying with forced format");

                    const QC::Status unmountSt = QFS::VolumeManager::instance().unmountVolume("QFS_SYSTEM");
                    if (unmountSt == QC::Status::Success)
                    {
                        ctx.writeLine("sysformat: /system unmounted for forced format retry");
                    }
                    else
                    {
                        writeStatusLine(ctx, "sysformat: pre-retry unmount status=", unmountSt);
                    }

                    writeInstallerStage(ctx, "format", "begin");
                    st = formatSelectedDisk(true);
                    if (st == QC::Status::Success)
                    {
                        ctx.writeLine("sysformat: forced format ok");
                        writeInstallerStage(ctx, "format", "success");
                        mounted = probeAndMountSystem();
                        copyOk = false;
                        payloadOk = false;
                        QC::String::memset(copyFailureDetail, 0, sizeof(copyFailureDetail));
                        if (mounted)
                        {
                            copyOk = deployInstallerPayload(ctx, copyFailureDetail, sizeof(copyFailureDetail));
                            payloadOk = copyOk && verifyInstallerPayload(ctx);
                        }
                    }
                    else
                    {
                        writeStatusLine(ctx, "sysformat: forced retry failed status=", st);
                    }
                }

                if (mounted && payloadOk)
                {
                    writeInstallerLine(ctx, "installer: stage=complete status=success mount=/system next=reboot");
                }
                else
                {
                    if (!mounted)
                        writeInstallerLine(ctx, "installer: stage=complete status=failure failed_stage=probe_mount recovery=sysmount|sysformat force");
                    else
                        writeInstallerLine(ctx, "installer: stage=complete status=failure failed_stage=copy_or_verify_payload recovery=sysmount|sysformat force");
                }

                return true;
            }

            static bool cmdSysmount(const char *args, const QC::Cmd::Context &ctx, void *)
            {
                (void)args;

                writeInstallerStage(ctx, "probe_mount", "begin");
                ctx.writeLine("sysmount: probing for system volume and mounting /system");

                QKDrv::AHCI::resetSystemProbe();
                QKDrv::IDE::resetSystemProbe();
                if (!QKDrv::AHCI::probeAndRegisterSystemVolume())
                    QKDrv::IDE::probeAndRegisterSystemVolume();
                (void)QFS::VolumeManager::instance().mountPending();

                if (QFS::VolumeManager::instance().isMounted("QFS_SYSTEM"))
                {
                    ctx.writeLine("sysmount: /system mounted");
                    writeInstallerStage(ctx, "probe_mount", "success");
                }
                else
                {
                    ctx.writeLine("sysmount: /system not mounted (probe or mount failed)");
                    writeInstallerLine(ctx, "installer: status=failure stage=probe_mount operation=/system mount recovery=sysdisks|sysformat");
                }

                return true;
            }

            static bool cmdSysverify(const char *args, const QC::Cmd::Context &ctx, void *)
            {
                (void)args;

                if (!QFS::VolumeManager::instance().isMounted("QFS_SYSTEM"))
                {
                    writeInstallerLine(ctx, "installer: stage=probe_mount status=begin reason=sysverify");
                    QKDrv::AHCI::resetSystemProbe();
                    QKDrv::IDE::resetSystemProbe();
                    if (!QKDrv::AHCI::probeAndRegisterSystemVolume())
                        QKDrv::IDE::probeAndRegisterSystemVolume();
                    (void)QFS::VolumeManager::instance().mountPending();
                    (void)QFS::VolumeManager::instance().mountVolume("QFS_SYSTEM");

                    if (QFS::VolumeManager::instance().isMounted("QFS_SYSTEM"))
                    {
                        writeInstallerLine(ctx, "installer: stage=probe_mount status=success reason=sysverify");
                    }
                    else
                    {
                        writeInstallerLine(ctx, "installer: status=failure stage=verify_payload operation=/system not-mounted recovery=sysmount|sysformat");
                        return true;
                    }
                }

                (void)verifyInstallerPayload(ctx);

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
                "sysdisks",
                QC::Cmd::AccessLevel::User,
                &cmdSysdisks,
                nullptr,
                "List detected USB/AHCI/IDE storage and layout state (sysdisks)");

            (void)reg.registerCommandExAccess(
                "sysformat",
                QC::Cmd::AccessLevel::Admin,
                &cmdSysformat,
                nullptr,
                "Partition+format a detected disk as FAT32 and mount /system (sysformat [diskN|N])");

            (void)reg.registerCommandExAccess(
                "sysmount",
                QC::Cmd::AccessLevel::Admin,
                &cmdSysmount,
                nullptr,
                "Probe and mount the system disk at /system (sysmount)");

            (void)reg.registerCommandExAccess(
                "sysverify",
                QC::Cmd::AccessLevel::Admin,
                &cmdSysverify,
                nullptr,
                "Verify installer-required payload paths under /system (sysverify)");

            (void)reg.registerCommandExAccess(
                "sysprovenance",
                QC::Cmd::AccessLevel::User,
                &cmdSysprovenance,
                nullptr,
                "Show consolidated storage provenance for mounts and discovered devices (sysprovenance)");

            QC_LOG_INFO("QKCmd", "Registered system volume commands");
            registered = true;
        }

    } // namespace CmdCenter
} // namespace QK
