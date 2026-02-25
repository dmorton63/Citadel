// CITADEL Kernel Main Entry Point
// QKMain.cpp

#include "QCTypes.h"
#define LIMINE_API_REVISION 2
#include "limine.h"
#include "QKConsole.h"
#include "Debug/Serial/SerialDebug.h"
#include "Debug/Terminal/LimineTerminal.h"
#include "Boot/Limine/LimineRequests.h"
#include "Boot/Arch/ArchInit.h"
#include "Boot/Acpi/AcpiTables.h"
#include "Boot/Tpm/TpmSecureStore.h"
#include "Boot/Desktop/DesktopSession.h"
#include "Boot/Memory/AddressMapping.h"
#include "Boot/Memory/EarlyMemory.h"
#include "Boot/QKBoot.h"

#include "QKMemHeap.h"

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
    extern QC::u64 limine_module_request[];
    extern QC::u64 limine_terminal_request[];
    extern QC::u64 limine_firmware_type_request[];
    extern QC::u64 limine_rsdp_request[];
}

// Kernel main entry point
extern "C" void kernel_main()
{
    // Initialize serial first for debug output
    QK::Debug::Serial::Init();
    QK::Debug::Serial::Write("\r\n=== CITADEL Kernel ===\r\n");
    QK::Debug::Serial::Write("Serial initialized, kernel starting...\r\n");

    // --- Early Boot ---
    QKBoot::setLogFn(QK::Debug::Serial::Write);
    {
        QKBoot::LimineRequests req{};
        req.framebuffer = limine_framebuffer_request;
        req.hhdm = limine_hhdm_request;
        req.kernelAddress = limine_kernel_address_request;
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

    if (QK::Debug::Terminal::InitFromLimineRequest(limine_terminal_request))
    {
        QK::Debug::Serial::SetMirror(QK::Debug::Terminal::Write);
        QK::Debug::Terminal::Write("Boot terminal initialized\r\n");
    }

    QKBoot::initializeDrivers();

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
