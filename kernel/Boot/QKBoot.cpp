#include "Boot/QKBoot.h"

#include "Boot/Limine/LimineRequests.h"
#include "Boot/Memory/AddressMapping.h"
#include "Boot/Memory/EarlyMemory.h"

#include "Boot/Config/BootJson.h"
#include "Boot/Config/RamdiskFile.h"
#include "Boot/Config/SysConfig.h"

#include "Boot/Security/BootJsonSignature.h"

#include "Boot/Acpi/AcpiTables.h"
#include "Boot/Arch/ArchInit.h"
#include "Boot/Desktop/DesktopSession.h"
#include "Boot/Tpm/TpmSecureStore.h"

#include "QCJson.h"
#include "QCString.h"

namespace
{
    QKBoot::FLogFn g_Log = nullptr;
    QKBoot::LimineRequests g_Req{};
    bool g_DesktopPrepared = false;

    static constexpr bool kProductionMode =
#if defined(CITADEL_PRODUCTION) && (CITADEL_PRODUCTION != 0)
    true;
#else
    false;
#endif

    struct CPUIDRegs
    {
        QC::u32 eax;
        QC::u32 ebx;
        QC::u32 ecx;
        QC::u32 edx;
    };

    static CPUIDRegs Cpuid(QC::u32 leaf, QC::u32 subleaf = 0)
    {
        CPUIDRegs r{};
        asm volatile("cpuid" : "=a"(r.eax), "=b"(r.ebx), "=c"(r.ecx), "=d"(r.edx) : "a"(leaf), "c"(subleaf));
        return r;
    }

    static bool CpuHasSse2()
    {
        const CPUIDRegs r = Cpuid(1);
        return (r.edx & (1u << 26)) != 0;
    }

    static bool CpuHasNx()
    {
        const CPUIDRegs maxExt = Cpuid(0x80000000u);
        if (maxExt.eax < 0x80000001u)
            return false;
        const CPUIDRegs r = Cpuid(0x80000001u);
        return (r.edx & (1u << 20)) != 0;
    }

    static bool CpuHasLongMode()
    {
        const CPUIDRegs maxExt = Cpuid(0x80000000u);
        if (maxExt.eax < 0x80000001u)
            return false;
        const CPUIDRegs r = Cpuid(0x80000001u);
        return (r.edx & (1u << 29)) != 0;
    }

    static void LogU64(QKBoot::FLogFn Log, QC::u64 value)
    {
        if (!Log)
            return;

        char buf[32];
        QC::usize pos = 0;

        if (value == 0)
        {
            buf[pos++] = '0';
        }
        else
        {
            while (value > 0 && pos < sizeof(buf) - 1)
            {
                const QC::u64 digit = value % 10;
                buf[pos++] = static_cast<char>('0' + digit);
                value /= 10;
            }

            // reverse in-place
            for (QC::usize i = 0; i < pos / 2; ++i)
            {
                const char tmp = buf[i];
                buf[i] = buf[pos - 1 - i];
                buf[pos - 1 - i] = tmp;
            }
        }

        buf[pos] = '\0';
        Log(buf);
    }

    static void LogBool(QKBoot::FLogFn Log, bool v)
    {
        if (!Log)
            return;
        Log(v ? "true" : "false");
    }

    static const QK::Boot::Config::SysConfigModule *FindSysConfigModuleById(const QK::Boot::Config::SysConfig &cfg, const char *id)
    {
        if (!id || !*id)
            return nullptr;

        for (QC::u32 i = 0; i < cfg.moduleCount && i < 32; ++i)
        {
            const auto &m = cfg.modules[i];
            if (m.id[0] == 0)
                continue;
            if (QC::String::strcmp(m.id, id) == 0)
                return &m;
        }

        return nullptr;
    }

    static bool EndsWithJsn(const char *path)
    {
        if (!path)
            return false;

        const QC::usize n = static_cast<QC::usize>(QC::String::strlen(path));
        if (n < 4)
            return false;

        const char a = path[n - 3];
        const char b = path[n - 2];
        const char c = path[n - 1];

        auto up = [](char ch) -> char {
            if (ch >= 'a' && ch <= 'z')
                return static_cast<char>(ch - 'a' + 'A');
            return ch;
        };

        return (up(a) == 'J' && up(b) == 'S' && up(c) == 'N');
    }

    static bool IsHexDigit(char c)
    {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    static bool IsSafeTokenChar(char c)
    {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-' || c == '.' ||
               c == ':';
    }

    static bool ValidateColorString(const char *s)
    {
        if (!s)
            return false;

        const QC::usize n = static_cast<QC::usize>(QC::String::strlen(s));
        if (n == 0 || n > 40)
            return false;

        // Accept CSS-ish hex colors: #RRGGBB or #RRGGBBAA
        if (s[0] == '#')
        {
            if (!(n == 7 || n == 9))
                return false;
            for (QC::usize i = 1; i < n; ++i)
            {
                if (!IsHexDigit(s[i]))
                    return false;
            }
            return true;
        }

        // Also allow a short token (for future theme tokens), but keep it very constrained.
        for (QC::usize i = 0; i < n; ++i)
        {
            if (!IsSafeTokenChar(s[i]))
                return false;
        }
        return true;
    }

    static bool ValidateLayoutEntry(const QC::JSON::Value &v)
    {
        if (!v.isObject())
            return false;

        const QC::JSON::Object *obj = v.asObject();
        const QC::usize n = obj ? obj->size() : 0;

        for (QC::usize i = 0; i < n; ++i)
        {
            const auto &ent = (*obj)[i];
            const char *k = ent.key;
            const QC::JSON::Value *val = ent.value;

            if (!k || !val)
                return false;

            const bool isAllowed = (QC::String::strcmp(k, "x") == 0) || (QC::String::strcmp(k, "y") == 0) ||
                                   (QC::String::strcmp(k, "width") == 0) || (QC::String::strcmp(k, "height") == 0);
            if (!isAllowed)
                return false;

            if (!(val->isNumber() || val->isString()))
                return false;
        }

        return true;
    }

    static bool ValidateDesktopOverridesJson(QKBoot::FLogFn Log, const QC::JSON::Value &root)
    {
        if (!root.isObject())
        {
            if (Log)
                Log("DesktopOverrides: root is not an object\r\n");
            return false;
        }

        const QC::JSON::Object *obj = root.asObject();
        const QC::usize n = obj ? obj->size() : 0;

        for (QC::usize i = 0; i < n; ++i)
        {
            const auto &ent = (*obj)[i];
            const char *k = ent.key;
            const QC::JSON::Value *v = ent.value;

            if (!k || !v)
                return false;

            const bool allowed = (QC::String::strcmp(k, "version") == 0) || (QC::String::strcmp(k, "layout") == 0) ||
                                 (QC::String::strcmp(k, "colors") == 0) || (QC::String::strcmp(k, "banner_text") == 0);
            if (!allowed)
            {
                if (Log)
                {
                    Log("DesktopOverrides: unknown key: ");
                    Log(k);
                    Log("\r\n");
                }
                return false;
            }
        }

        if (const QC::JSON::Value *ver = root.find("version"); ver && !ver->isNumber())
        {
            if (Log)
                Log("DesktopOverrides: version must be a number\r\n");
            return false;
        }

        if (const QC::JSON::Value *banner = root.find("banner_text"); banner)
        {
            if (!banner->isString())
            {
                if (Log)
                    Log("DesktopOverrides: banner_text must be a string\r\n");
                return false;
            }
            const char *s = banner->asString(nullptr);
            const QC::usize len = s ? static_cast<QC::usize>(QC::String::strlen(s)) : 0;
            if (len > 160)
            {
                if (Log)
                    Log("DesktopOverrides: banner_text too long\r\n");
                return false;
            }
        }

        if (const QC::JSON::Value *layout = root.find("layout"); layout)
        {
            if (!layout->isObject())
            {
                if (Log)
                    Log("DesktopOverrides: layout must be an object\r\n");
                return false;
            }

            const QC::JSON::Object *layoutObj = layout->asObject();
            const QC::usize ln = layoutObj ? layoutObj->size() : 0;
            for (QC::usize i = 0; i < ln; ++i)
            {
                const auto &ent = (*layoutObj)[i];
                if (!ent.key || !ent.value)
                    return false;
                if (!ValidateLayoutEntry(*ent.value))
                {
                    if (Log)
                    {
                        Log("DesktopOverrides: invalid layout entry for id: ");
                        Log(ent.key);
                        Log("\r\n");
                    }
                    return false;
                }
            }
        }

        if (const QC::JSON::Value *colors = root.find("colors"); colors)
        {
            if (!colors->isObject())
            {
                if (Log)
                    Log("DesktopOverrides: colors must be an object\r\n");
                return false;
            }

            const QC::JSON::Object *colorsObj = colors->asObject();
            const QC::usize cn = colorsObj ? colorsObj->size() : 0;
            for (QC::usize i = 0; i < cn; ++i)
            {
                const auto &ent = (*colorsObj)[i];
                if (!ent.key || !ent.value)
                    return false;

                const bool allowed = (QC::String::strcmp(ent.key, "background") == 0) || (QC::String::strcmp(ent.key, "accent") == 0) ||
                                     (QC::String::strcmp(ent.key, "text") == 0);
                if (!allowed)
                {
                    if (Log)
                    {
                        Log("DesktopOverrides: unknown colors key: ");
                        Log(ent.key);
                        Log("\r\n");
                    }
                    return false;
                }

                if (!ent.value->isString())
                {
                    if (Log)
                        Log("DesktopOverrides: color values must be strings\r\n");
                    return false;
                }

                const char *s = ent.value->asString(nullptr);
                if (!ValidateColorString(s))
                {
                    if (Log)
                    {
                        Log("DesktopOverrides: invalid color value for key: ");
                        Log(ent.key);
                        Log("\r\n");
                    }
                    return false;
                }
            }
        }

        return true;
    }

    static bool ValidateSimpleListManifest(QKBoot::FLogFn Log, const QC::JSON::Value &root, const char *ListKey, const char *Label)
    {
        if (!root.isObject())
        {
            if (Log)
            {
                Log(Label);
                Log(": root is not an object\r\n");
            }
            return false;
        }

        const QC::JSON::Object *obj = root.asObject();
        const QC::usize n = obj ? obj->size() : 0;

        for (QC::usize i = 0; i < n; ++i)
        {
            const auto &ent = (*obj)[i];
            const char *k = ent.key;
            const QC::JSON::Value *v = ent.value;
            if (!k || !v)
                return false;

            const bool allowed = (QC::String::strcmp(k, "version") == 0) || (QC::String::strcmp(k, ListKey) == 0);
            if (!allowed)
            {
                if (Log)
                {
                    Log(Label);
                    Log(": unknown key: ");
                    Log(k);
                    Log("\r\n");
                }
                return false;
            }
        }

        if (const QC::JSON::Value *ver = root.find("version"); !ver || !ver->isNumber())
        {
            if (Log)
            {
                Log(Label);
                Log(": missing/invalid version\r\n");
            }
            return false;
        }

        if (const QC::JSON::Value *lst = root.find(ListKey); !lst || !lst->isArray())
        {
            if (Log)
            {
                Log(Label);
                Log(": missing/invalid list: ");
                Log(ListKey);
                Log("\r\n");
            }
            return false;
        }

        // Shape-only: ensure list items are strings or objects (no semantics yet).
        const QC::JSON::Array *arr = root.find(ListKey)->asArray();
        const QC::usize an = arr ? arr->size() : 0;
        for (QC::usize i = 0; i < an; ++i)
        {
            const QC::JSON::Value *it = (*arr)[i];
            if (!it)
                return false;
            if (!(it->isString() || it->isObject()))
            {
                if (Log)
                {
                    Log(Label);
                    Log(": invalid list item type\r\n");
                }
                return false;
            }
        }

        return true;
    }

    static bool ValidateSecurityJson(QKBoot::FLogFn Log, const QC::JSON::Value &root)
    {
        if (!root.isObject())
        {
            if (Log)
                Log("Security: root is not an object\r\n");
            return false;
        }

        const QC::JSON::Object *obj = root.asObject();
        const QC::usize n = obj ? obj->size() : 0;

        for (QC::usize i = 0; i < n; ++i)
        {
            const auto &ent = (*obj)[i];
            const char *k = ent.key;
            const QC::JSON::Value *v = ent.value;
            if (!k || !v)
                return false;

            // Shape-only allowlist.
            const bool allowed = (QC::String::strcmp(k, "version") == 0) || (QC::String::strcmp(k, "mode") == 0) ||
                                 (QC::String::strcmp(k, "params") == 0);
            if (!allowed)
            {
                if (Log)
                {
                    Log("Security: unknown key: ");
                    Log(k);
                    Log("\r\n");
                }
                return false;
            }
        }

        if (const QC::JSON::Value *ver = root.find("version"); !ver || !ver->isNumber())
        {
            if (Log)
                Log("Security: missing/invalid version\r\n");
            return false;
        }

        if (const QC::JSON::Value *mode = root.find("mode"); mode && !mode->isString())
        {
            if (Log)
                Log("Security: mode must be a string\r\n");
            return false;
        }

        if (const QC::JSON::Value *params = root.find("params"); params && !params->isObject())
        {
            if (Log)
                Log("Security: params must be an object\r\n");
            return false;
        }

        return true;
    }

    static bool SysConfigEarlyLoadPhase(QKBoot::FLogFn Log, QC::u64 ModuleRequest[], const QK::Boot::Config::SysConfig &syscfg)
    {
        if (!Log)
            return true;

        if (syscfg.earlyCount == 0)
        {
            Log("SysConfig: no boot.early entries\r\n");
            return true;
        }

        Log("SysConfig: early phase begin (count=");
        LogU64(Log, syscfg.earlyCount);
        Log(")\r\n");

        for (QC::u32 i = 0; i < syscfg.earlyCount && i < 16; ++i)
        {
            const char *id = syscfg.earlyIds[i];
            if (!id || id[0] == 0)
                continue;

            const auto *m = FindSysConfigModuleById(syscfg, id);
            if (!m)
            {
                Log("SysConfig: early missing module id='");
                Log(id);
                Log("'\r\n");
                if (kProductionMode)
                    return false;
                continue;
            }

            Log("SysConfig: early load id='");
            Log(m->id);
            Log("' type='");
            Log(m->type);
            Log("' required=");
            LogBool(Log, m->required);
            Log(" path='");
            Log(m->path);
            Log("'\r\n");

            if (m->path[0] == 0)
            {
                Log("SysConfig: early module has empty path; skipping\r\n");
                if (kProductionMode && m->required)
                    return false;
                continue;
            }

            char *buffer = nullptr;
            QC::usize len = 0;
            if (!QK::Boot::Config::ReadFileFromLimineRamdiskModule(Log, ModuleRequest, m->path, buffer, len))
            {
                Log("SysConfig: early read failed for '");
                Log(m->path);
                Log("'\r\n");
                if (kProductionMode && m->required)
                    return false;
                continue;
            }

            if (EndsWithJsn(m->path))
            {
                QC::JSON::Value root;
                const bool ok = QC::JSON::parse(buffer, root);
                if (!ok)
                {
                    Log("SysConfig: early JSON parse failed for '");
                    Log(m->path);
                    Log("'\r\n");
                    operator delete[](buffer);
                    if (kProductionMode && m->required)
                        return false;
                    continue;
                }

                if (QC::String::strcmp(m->type, "desktop_overrides") == 0)
                {
                    if (!ValidateDesktopOverridesJson(Log, root))
                    {
                        Log("SysConfig: desktop overrides validation failed\r\n");
                        operator delete[](buffer);

                        // Overrides are optional, but if present they must be valid in production.
                        if (kProductionMode)
                            return false;

                        continue;
                    }
                }

                if (QC::String::strcmp(m->type, "security") == 0)
                {
                    if (!ValidateSecurityJson(Log, root))
                    {
                        Log("SysConfig: security manifest validation failed\r\n");
                        operator delete[](buffer);
                        if (kProductionMode)
                            return false;
                        continue;
                    }
                }

                if (QC::String::strcmp(m->type, "services") == 0)
                {
                    if (!ValidateSimpleListManifest(Log, root, "services", "Services"))
                    {
                        Log("SysConfig: services manifest validation failed\r\n");
                        operator delete[](buffer);
                        if (kProductionMode)
                            return false;
                        continue;
                    }
                }

                if (QC::String::strcmp(m->type, "drivers") == 0)
                {
                    if (!ValidateSimpleListManifest(Log, root, "drivers", "Drivers"))
                    {
                        Log("SysConfig: drivers manifest validation failed\r\n");
                        operator delete[](buffer);
                        if (kProductionMode)
                            return false;
                        continue;
                    }
                }

                if (QC::String::strcmp(m->type, "apps") == 0)
                {
                    if (!ValidateSimpleListManifest(Log, root, "apps", "Apps"))
                    {
                        Log("SysConfig: apps manifest validation failed\r\n");
                        operator delete[](buffer);
                        if (kProductionMode)
                            return false;
                        continue;
                    }
                }
            }

            Log("SysConfig: early loaded '");
            Log(m->id);
            Log("' bytes=");
            LogU64(Log, static_cast<QC::u64>(len));
            Log("\r\n");

            operator delete[](buffer);
        }

        Log("SysConfig: early phase done\r\n");
        return true;
    }

    static const char *firmwareTypeToString(QC::u64 t)
    {
        switch (t)
        {
        case LIMINE_FIRMWARE_TYPE_X86BIOS:
            return "x86 BIOS";
        case LIMINE_FIRMWARE_TYPE_UEFI32:
            return "UEFI32";
        case LIMINE_FIRMWARE_TYPE_UEFI64:
            return "UEFI64";
        case LIMINE_FIRMWARE_TYPE_SBI:
            return "SBI";
        default:
            return "UNKNOWN";
        }
    }

}

namespace QKBoot
{
    void setLogFn(FLogFn log)
    {
        g_Log = log;
    }

    void setLimineRequests(const LimineRequests &req)
    {
        g_Req = req;
    }

    void initializeMemory()
    {
        QK::Boot::Memory::InitFromLimineRequests(g_Req.hhdm, g_Req.kernelAddress, g_Log);
    }

    void initializeDrivers()
    {
        if (g_Log)
        {
            const limine_firmware_type_response *FwResp = QK::Boot::Limine::GetFirmwareTypeResponse(g_Req.firmwareType);
            if (FwResp)
            {
                g_Log("Firmware: ");
                g_Log(firmwareTypeToString(FwResp->firmware_type));
                g_Log("\r\n");
            }
            else
            {
                g_Log("Firmware: unknown (no response)\r\n");
            }

            const limine_rsdp_response *RsdpResp = QK::Boot::Limine::GetRsdpResponse(g_Req.rsdp);
            if (RsdpResp && RsdpResp->address)
            {
                const QC::PhysAddr rsdpPhys = static_cast<QC::PhysAddr>(reinterpret_cast<QC::uptr>(RsdpResp->address));
                QK::Boot::Acpi::EnumerateTables(rsdpPhys, g_Log, &QK::Boot::Tpm::TryTpm2CrbStartup);
            }
            else
            {
                g_Log("ACPI: no RSDP response\r\n");
            }
        }
        else
        {
            const limine_rsdp_response *RsdpResp = QK::Boot::Limine::GetRsdpResponse(g_Req.rsdp);
            if (RsdpResp && RsdpResp->address)
            {
                const QC::PhysAddr rsdpPhys = static_cast<QC::PhysAddr>(reinterpret_cast<QC::uptr>(RsdpResp->address));
                QK::Boot::Acpi::EnumerateTables(rsdpPhys, nullptr, &QK::Boot::Tpm::TryTpm2CrbStartup);
            }
        }

        QK::Boot::Arch::InitCpuGdtIdtAndInterrupts(g_Log);
    }

    void initializeBootPolicyAndGate()
    {
        if (!QK::Boot::Security::VerifyBootJsnSignatureFromLimineRamdiskModule(g_Log, g_Req.modules, g_Req.executableFile))
        {
            if (g_Log)
            {
                g_Log("Refusal Mode: boot config signature invalid\r\n");
                g_Log("Will not run in this configuration!  This machine does not meet the hardware requirements as listed above.\r\n");
            }
            for (;;)
                asm volatile("hlt");
        }

        QK::Boot::Config::BootPolicy policy{};
        (void)QK::Boot::Config::LoadBootPolicyFromLimineRamdiskModule(g_Log, g_Req.modules, policy);

        QK::Boot::Config::SysConfig syscfg{};
        const bool syscfgLoaded = QK::Boot::Config::LoadSysConfigFromLimineRamdiskModule(g_Log, g_Req.modules, syscfg);

        const auto &cpuReq = policy.requirements.cpu;
        if (cpuReq.requireSse2 && !CpuHasSse2())
        {
            if (g_Log)
            {
                g_Log("Refusal Mode: missing CPU feature\r\n");
                g_Log("Required: SSE2\r\n");
                g_Log("Will not run in this configuration!  This machine does not meet the hardware requirements as listed above.\r\n");
            }
            for (;;)
                asm volatile("hlt");
        }

        if (cpuReq.requireNx && !CpuHasNx())
        {
            if (g_Log)
            {
                g_Log("Refusal Mode: missing CPU feature\r\n");
                g_Log("Required: NX\r\n");
                g_Log("Will not run in this configuration!  This machine does not meet the hardware requirements as listed above.\r\n");
            }
            for (;;)
                asm volatile("hlt");
        }

        if ((cpuReq.require64bit || cpuReq.requireLongmode) && !CpuHasLongMode())
        {
            if (g_Log)
            {
                g_Log("Refusal Mode: missing CPU feature\r\n");
                g_Log("Required: Long Mode (x86_64)\r\n");
                g_Log("Will not run in this configuration!  This machine does not meet the hardware requirements as listed above.\r\n");
            }
            for (;;)
                asm volatile("hlt");
        }

        const QC::u64 compiledMinMiB = QK::Boot::Config::kCompiledMinRamMiB;
        const QC::u64 compiledRecMiB = QK::Boot::Config::kCompiledRecommendedRamMiB;

        QC::u64 effectiveMinMiB = compiledMinMiB;
        QC::u64 effectiveRecMiB = compiledRecMiB;

        if (policy.requirements.minRamMiB > effectiveMinMiB)
            effectiveMinMiB = policy.requirements.minRamMiB;
        if (policy.requirements.recommendedRamMiB > effectiveRecMiB)
            effectiveRecMiB = policy.requirements.recommendedRamMiB;
        if (effectiveRecMiB < effectiveMinMiB)
            effectiveRecMiB = effectiveMinMiB;

        QC::u64 availBytes = 0;
        if (!QK::Boot::Limine::GetAvailableMemoryBytes(g_Req.memmap, availBytes))
        {
            if (g_Log)
                g_Log("BootGate: no memmap response; skipping RAM gate\r\n");
            return;
        }

        const QC::u64 availMiB = availBytes / (1024ull * 1024ull);

        if (g_Log)
        {
            g_Log("BootGate: available RAM MiB = ");
            LogU64(g_Log, availMiB);
            g_Log("\r\n");
        }

        if (availMiB < effectiveMinMiB)
        {
            const QC::u64 gap = effectiveMinMiB - availMiB;
            if (g_Log)
            {
                g_Log("Refusal Mode: insufficient RAM\r\n");
                g_Log("Required MiB: ");
                LogU64(g_Log, effectiveMinMiB);
                g_Log("\r\nDetected MiB: ");
                LogU64(g_Log, availMiB);
                g_Log("\r\nGap MiB: ");
                LogU64(g_Log, gap);
                g_Log("\r\n");
                g_Log("Will not run in this configuration!  This machine does not meet the hardware requirements as listed above.\r\n");
            }

            for (;;)
                asm volatile("hlt");
        }

        if (syscfgLoaded)
        {
            if (!SysConfigEarlyLoadPhase(g_Log, g_Req.modules, syscfg))
            {
                if (g_Log)
                {
                    g_Log("Refusal Mode: sysconfig early load failed\r\n");
                    g_Log("Will not run in this configuration!\r\n");
                }
                for (;;)
                    asm volatile("hlt");
            }
        }

        if (availMiB < effectiveRecMiB)
        {
            if (g_Log)
                g_Log("Reduced Memory Mode: some services/features may be limited\r\n");
        }
    }

    void initializeGraphics()
    {
        const auto BootHeap = QK::Boot::Memory::GetEarlyHeap();
        QK::Boot::Desktop::EarlyHeap Heap{};
        Heap.Buffer = BootHeap.Buffer;
        Heap.Size = BootHeap.Size;

        g_DesktopPrepared = QK::Boot::Desktop::PrepareFromLimineRequests(g_Req.framebuffer, g_Req.modules, Heap, g_Log);
        if (g_Log)
        {
            if (g_DesktopPrepared)
                g_Log("Graphics: framebuffer present\r\n");
            else
                g_Log("Graphics: no framebuffer response\r\n");
        }
    }

    void initializeInput()
    {
        if (!g_DesktopPrepared)
        {
            if (g_Log)
                g_Log("Input: no framebuffer; skipping\r\n");
            return;
        }

        QK::Boot::Desktop::InitializeInput();
    }

    void initializeWindowSystem()
    {
        if (!g_DesktopPrepared)
        {
            if (g_Log)
                g_Log("WindowSystem: no framebuffer; skipping\r\n");
            return;
        }

        QK::Boot::Desktop::InitializeWindowSystem();
    }

    [[noreturn]] void initializeDesktop()
    {
        if (g_Log)
            g_Log("Desktop: starting session\r\n");

        if (!g_DesktopPrepared)
        {
            if (g_Log)
                g_Log("Desktop: no framebuffer response; halting\r\n");
            for (;;)
                asm volatile("hlt");
        }

        QK::Boot::Desktop::InitializeDesktopAndRunLoop();

        // If the desktop session ever returns, halt.
        for (;;)
        {
            asm volatile("hlt");
        }
    }
}
