// CITADEL Kernel Main Entry Point
// QKMain.cpp

#include "QCTypes.h"
#define LIMINE_API_REVISION 2
#include "limine.h"
#include "QKConsole.h"
#include "Debug/Serial/QKDebugSerial.h"
#include "Debug/Terminal/QKDebugLimineTerminal.h"
#include "Boot/Limine/QKBootLimineRequests.h"
#include "Boot/Arch/QKBootArchInit.h"
#include "Boot/ACPI/QKBootACPITables.h"
#include "Boot/TPM/QKBootTPMSecureStore.h"
#include "Boot/Desktop/QKBootDesktopSession.h"
#include "Boot/Memory/QKBootAddressMapping.h"
#include "Boot/Memory/QKBootEarlyMemory.h"
#include "Boot/QKBoot.h"

#include "QKMemHeap.h"
#include "QCLogger.h"

namespace
{
    static bool isSpace(char c)
    {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }

    static bool tokenEqualsIgnoreCase(const char *tokenBegin, const char *tokenEnd, const char *lit)
    {
        if (!lit)
            return false;

        const char *p = tokenBegin;
        const char *q = lit;
        while (p < tokenEnd && *q)
        {
            char a = *p++;
            char b = *q++;
            if (a >= 'A' && a <= 'Z')
                a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z')
                b = static_cast<char>(b - 'A' + 'a');
            if (a != b)
                return false;
        }
        return (p == tokenEnd) && (*q == 0);
    }

    static QC::LogLevel parseLogLevelToken(const char *tokenBegin, const char *tokenEnd, QC::LogLevel fallback)
    {
        if (!tokenBegin || !tokenEnd || tokenEnd <= tokenBegin)
            return fallback;

        if (tokenEqualsIgnoreCase(tokenBegin, tokenEnd, "trace"))
            return QC::LogLevel::Trace;
        if (tokenEqualsIgnoreCase(tokenBegin, tokenEnd, "debug"))
            return QC::LogLevel::Debug;
        if (tokenEqualsIgnoreCase(tokenBegin, tokenEnd, "info"))
            return QC::LogLevel::Info;
        if (tokenEqualsIgnoreCase(tokenBegin, tokenEnd, "warn") || tokenEqualsIgnoreCase(tokenBegin, tokenEnd, "warning"))
            return QC::LogLevel::Warning;
        if (tokenEqualsIgnoreCase(tokenBegin, tokenEnd, "error") || tokenEqualsIgnoreCase(tokenBegin, tokenEnd, "err"))
            return QC::LogLevel::Error;
        if (tokenEqualsIgnoreCase(tokenBegin, tokenEnd, "fatal"))
            return QC::LogLevel::Fatal;

        return fallback;
    }

    static void applyCmdlineLogConfig(const char *cmdline)
    {
        if (!cmdline || !*cmdline)
            return;

        // Defaults remain unchanged unless explicitly overridden.
        QC::LogLevel level = QC::Logger::instance().getLevel();

        const char *p = cmdline;
        while (*p)
        {
            while (*p && isSpace(*p))
                ++p;
            if (!*p)
                break;

            const char *tokBegin = p;
            while (*p && !isSpace(*p))
                ++p;
            const char *tokEnd = p;

            // Handle bare flags: quiet
            if (tokenEqualsIgnoreCase(tokBegin, tokEnd, "quiet"))
            {
                level = QC::LogLevel::Warning;
                continue;
            }

            // Handle key=value: loglevel=warn, log=debug (alias)
            const char *eq = tokBegin;
            while (eq < tokEnd && *eq != '=')
                ++eq;
            if (eq < tokEnd && *eq == '=')
            {
                const char *keyBegin = tokBegin;
                const char *keyEnd = eq;
                const char *valBegin = eq + 1;
                const char *valEnd = tokEnd;

                if (tokenEqualsIgnoreCase(keyBegin, keyEnd, "loglevel") || tokenEqualsIgnoreCase(keyBegin, keyEnd, "log"))
                {
                    level = parseLogLevelToken(valBegin, valEnd, level);
                }
            }
        }

        QC::Logger::instance().setLevel(level);
    }

    static const char *getLimineKernelCmdline(QC::u64 executableFileRequest[])
    {
        if (!executableFileRequest)
            return nullptr;
        const auto *resp = reinterpret_cast<const limine_executable_file_response *>(executableFileRequest[5]);
        const limine_file *kf = resp ? resp->executable_file : nullptr;
        return kf ? kf->cmdline : nullptr;
    }
}

// Limine requests are defined in QKBoot.asm

// External symbols from linker
extern "C"
{
    extern QC::u8 _kernel_start[];
    extern QC::u8 _kernel_end[];
    extern QC::u8 _bss_start[];
    extern QC::u8 _bss_end[];

    // Limine requests from QKBoot.asm
    extern QC::u64 limine_framebuffer_request[];
    extern QC::u64 limine_hhdm_request[];
    extern QC::u64 limine_kernel_address_request[];
    extern QC::u64 limine_executable_file_request[];
    extern QC::u64 limine_memmap_request[];
    extern QC::u64 limine_module_request[];
    extern QC::u64 limine_terminal_request[];
    extern QC::u64 limine_firmware_type_request[];
    extern QC::u64 limine_rsdp_request[];
}

// Kernel main entry point
extern "C" void kernel_main()
{
    // Defensive: ensure interrupts are disabled until we install our own IDT.
    asm volatile("cli");

    // The x86_64 ABI and modern compilers may emit SSE/XMM instructions even for
    // simple operations (like zeroing small structs). Ensure SSE is enabled
    // immediately to avoid #UD -> double/triple fault before our IDT is ready.
    {
        QC::u64 cr0 = 0;
        QC::u64 cr4 = 0;
        asm volatile("mov %%cr0, %0" : "=r"(cr0));
        // Clear EM (bit 2), clear TS (bit 3), set MP (bit 1)
        cr0 &= ~(1ull << 2);
        cr0 &= ~(1ull << 3);
        cr0 |= (1ull << 1);
        asm volatile("mov %0, %%cr0" : : "r"(cr0));

        asm volatile("mov %%cr4, %0" : "=r"(cr4));
        // Set OSFXSR (bit 9) and OSXMMEXCPT (bit 10)
        cr4 |= (1ull << 9);
        cr4 |= (1ull << 10);
        asm volatile("mov %0, %%cr4" : : "r"(cr4));
    }

    // Initialize serial first for debug output
    QK::Debug::Serial::Init();
    QK::Debug::Serial::Write("\r\n=== CITADEL Kernel ===\r\n");
    QK::Debug::Serial::Write("Serial initialized, kernel starting...\r\n");

    // If Limine's terminal is available, mirror serial output to it as early as possible
    // so early boot messages (including the kernel console prompt) are visible on-screen.
    if (QK::Debug::Terminal::InitFromLimineRequest(limine_terminal_request))
    {
        QK::Debug::Serial::SetMirror(QK::Debug::Terminal::Write);
        QK::Debug::Terminal::Write("Boot terminal initialized\r\n");
    }

    // Apply logging verbosity overrides from Limine kernel cmdline as early as possible,
    // so they affect the entire boot (not just post-memory-init).
    applyCmdlineLogConfig(getLimineKernelCmdline(limine_executable_file_request));

    // --- Early Boot ---
    QKBoot::setLogFn(QK::Debug::Serial::Write);
    {
        QKBoot::LimineRequests req{};
        req.framebuffer = limine_framebuffer_request;
        req.hhdm = limine_hhdm_request;
        req.kernelAddress = limine_kernel_address_request;
        req.executableFile = limine_executable_file_request;
        req.memmap = limine_memmap_request;
        req.modules = limine_module_request;
        req.firmwareType = limine_firmware_type_request;
        req.rsdp = limine_rsdp_request;
        QKBoot::setLimineRequests(req);
    }
    QKBoot::initializeMemory();

    // Bring up the heap as early as possible so any subsystem that uses `new`
    // (command registry, network stack, UI, etc.) doesn't reboot-loop.
    const auto earlyHeap = QK::Boot::Memory::GetEarlyHeap();
    QK::Memory::Heap::instance().initialize(earlyHeap.Buffer, earlyHeap.Size);

    QK::Console::initialize(QK::Debug::Serial::Write);
    // Limine already clears BSS for us.
    QK::Debug::Serial::Write("BSS (skipped - Limine does it)\r\n");

    QKBoot::initializeDrivers();

    // Validate boot policy (boot.json) and enforce minimum hardware floors
    // before enabling interrupts and bringing up higher-level subsystems.
    QKBoot::initializeBootPolicyAndGate();

    // Desktop/driver bring-up expects interrupts to be enabled (as before, when
    // DesktopSession ran after sti). Enable them right after IDT/interrupt init.
    asm volatile("sti");
    QKBoot::initializeGraphics();

    // --- Input Pipeline (QER / QM / QES) ---
    QKBoot::initializeInput();

    // --- Window System ---
    QKBoot::initializeWindowSystem();

    // This currently also enters the runtime loop.
    QKBoot::initializeDesktop();
}
