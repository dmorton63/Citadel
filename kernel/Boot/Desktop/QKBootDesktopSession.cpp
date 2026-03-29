#include "Boot/Desktop/QKBootDesktopSession.h"

#include "QCLogger.h"

#include "QKMemHeap.h"
#include "QArchPCI.h"
#include "QDrvTimer.h"
#include "QDrvVmwareSVGA.h"
#include "QKDrvManager.h"
#include "PS2/QKDrvPS2Keyboard.h"

#include "QWFramebuffer.h"
#include "QWWindowManager.h"
#include "QWWindow.h"

#include "QKEventManager.h"
#include "QKEventListener.h"
#include "QKShutdownController.h"

#include "QKConsole.h"
#include "QKStorageProbe.h"
#include "QKSecurityCenter.h"

#include "QNetDHCP.h"
#include "QNetStack.h"

#include "QKSystemPump.h"

#include "QDDesktop.h"

#include "Boot/Config/QKBootStartupConfig.h"
#include "Boot/Ramdisk/QKBootRamdiskMount.h"

namespace
{
    static QK::Boot::Desktop::FLogFn g_Log = nullptr;

    static char keyToChar(QKDrv::PS2::Key key, bool shift, bool caps)
    {
        auto letter = [&](char c) -> char
        {
            const bool upper = (shift ^ caps);
            return upper ? static_cast<char>(c - 32) : c;
        };

        switch (key)
        {
        // Letters
        case QKDrv::PS2::Key::Q:
            return letter('q');
        case QKDrv::PS2::Key::W:
            return letter('w');
        case QKDrv::PS2::Key::E:
            return letter('e');
        case QKDrv::PS2::Key::R:
            return letter('r');
        case QKDrv::PS2::Key::T:
            return letter('t');
        case QKDrv::PS2::Key::Y:
            return letter('y');
        case QKDrv::PS2::Key::U:
            return letter('u');
        case QKDrv::PS2::Key::I:
            return letter('i');
        case QKDrv::PS2::Key::O:
            return letter('o');
        case QKDrv::PS2::Key::P:
            return letter('p');
        case QKDrv::PS2::Key::A:
            return letter('a');
        case QKDrv::PS2::Key::S:
            return letter('s');
        case QKDrv::PS2::Key::D:
            return letter('d');
        case QKDrv::PS2::Key::F:
            return letter('f');
        case QKDrv::PS2::Key::G:
            return letter('g');
        case QKDrv::PS2::Key::H:
            return letter('h');
        case QKDrv::PS2::Key::J:
            return letter('j');
        case QKDrv::PS2::Key::K:
            return letter('k');
        case QKDrv::PS2::Key::L:
            return letter('l');
        case QKDrv::PS2::Key::Z:
            return letter('z');
        case QKDrv::PS2::Key::X:
            return letter('x');
        case QKDrv::PS2::Key::C:
            return letter('c');
        case QKDrv::PS2::Key::V:
            return letter('v');
        case QKDrv::PS2::Key::B:
            return letter('b');
        case QKDrv::PS2::Key::N:
            return letter('n');
        case QKDrv::PS2::Key::M:
            return letter('m');

        // Digits + shifted symbols
        case QKDrv::PS2::Key::Num1:
            return shift ? '!' : '1';
        case QKDrv::PS2::Key::Num2:
            return shift ? '@' : '2';
        case QKDrv::PS2::Key::Num3:
            return shift ? '#' : '3';
        case QKDrv::PS2::Key::Num4:
            return shift ? '$' : '4';
        case QKDrv::PS2::Key::Num5:
            return shift ? '%' : '5';
        case QKDrv::PS2::Key::Num6:
            return shift ? '^' : '6';
        case QKDrv::PS2::Key::Num7:
            return shift ? '&' : '7';
        case QKDrv::PS2::Key::Num8:
            return shift ? '*' : '8';
        case QKDrv::PS2::Key::Num9:
            return shift ? '(' : '9';
        case QKDrv::PS2::Key::Num0:
            return shift ? ')' : '0';

        // Punctuation
        case QKDrv::PS2::Key::Space:
            return ' ';
        case QKDrv::PS2::Key::Enter:
            return '\n';
        case QKDrv::PS2::Key::Tab:
            return '\t';
        case QKDrv::PS2::Key::Backspace:
            return '\b';

        case QKDrv::PS2::Key::Minus:
            return shift ? '_' : '-';
        case QKDrv::PS2::Key::Equals:
            return shift ? '+' : '=';
        case QKDrv::PS2::Key::LeftBracket:
            return shift ? '{' : '[';
        case QKDrv::PS2::Key::RightBracket:
            return shift ? '}' : ']';
        case QKDrv::PS2::Key::Backslash:
            return shift ? '|' : '\\';
        case QKDrv::PS2::Key::Semicolon:
            return shift ? ':' : ';';
        case QKDrv::PS2::Key::Apostrophe:
            return shift ? '"' : '\'';
        case QKDrv::PS2::Key::Backtick:
            return shift ? '~' : '`';
        case QKDrv::PS2::Key::Comma:
            return shift ? '<' : ',';
        case QKDrv::PS2::Key::Period:
            return shift ? '>' : '.';
        case QKDrv::PS2::Key::Slash:
            return shift ? '?' : '/';

        default:
            return 0;
        }
    }

    struct DesktopSessionState
    {
        QC::u64 *FramebufferRequest = nullptr;
        QC::u64 *ModuleRequest = nullptr;
        QK::Boot::Desktop::EarlyHeap Heap{};

        QC::uptr FbAddress = 0;
        QC::u32 Width = 0;
        QC::u32 Height = 0;
        QC::u32 Pitch = 0;

        bool Prepared = false;
        bool InputInitialized = false;
        bool WindowSystemInitialized = false;
        bool DesktopInitialized = false;
    };

    static DesktopSessionState g_State;
    static QW::Framebuffer g_Framebuffer;
    static QD::Desktop g_Desktop;
    static QK::Event::EventListener g_CtrlQListener;
    static QK::Event::ListenerId g_CtrlQId = QK::Event::InvalidListenerId;

    static bool g_prevLeftBtn = false;
    static bool g_prevRightBtn = false;

    static bool g_prevPosValid = false;
    static QC::i32 g_prevX = 0;
    static QC::i32 g_prevY = 0;

    static void logInt(QC::i32 value)
    {
        if (!g_Log)
            return;

        char buf[16];
        int idx = 0;

        if (value == 0)
        {
            buf[idx++] = '0';
        }
        else
        {
            bool neg = false;
            QC::u32 mag = 0;
            if (value < 0)
            {
                neg = true;
                // Avoid UB on INT_MIN.
                mag = static_cast<QC::u32>(-(value + 1)) + 1u;
            }
            else
            {
                mag = static_cast<QC::u32>(value);
            }

            char tmp[12];
            int t = 0;
            while (mag > 0 && t < static_cast<int>(sizeof(tmp)))
            {
                tmp[t++] = static_cast<char>('0' + (mag % 10));
                mag /= 10;
            }

            if (neg)
                buf[idx++] = '-';

            while (t > 0)
            {
                buf[idx++] = tmp[--t];
            }
        }

        buf[idx] = 0;
        g_Log(buf);
    }

    static void logDecU64(QC::u64 value)
    {
        if (!g_Log)
            return;

        char buf[21];
        QC::usize pos = 0;

        if (value == 0)
        {
            buf[pos++] = '0';
        }
        else
        {
            char tmp[20];
            QC::usize tmpPos = 0;
            while (value > 0 && tmpPos < sizeof(tmp))
            {
                tmp[tmpPos++] = static_cast<char>('0' + (value % 10));
                value /= 10;
            }
            while (tmpPos > 0)
            {
                buf[pos++] = tmp[--tmpPos];
            }
        }

        buf[pos] = 0;
        g_Log(buf);
    }

    static void logIPv4(QNet::IPv4Address addr)
    {
        if (!g_Log)
            return;

        logDecU64(addr.octets[0]);
        g_Log(".");
        logDecU64(addr.octets[1]);
        g_Log(".");
        logDecU64(addr.octets[2]);
        g_Log(".");
        logDecU64(addr.octets[3]);
    }

    [[noreturn]] static void enterTerminalOnlyLoop()
    {
        g_Log("Entering console-only startup path (mode: ");
        g_Log(QK::Boot::Config::StartupModeName(QK::Boot::Config::GetStartupMode()));
        g_Log(")\r\n");

        while (true)
        {
            QNet::Stack::instance().poll(QDrv::Timer::instance().milliseconds());
            QKDrv::Manager::instance().poll();
            QKDrv::PS2::Keyboard::instance().poll();
            asm volatile("hlt");
        }
    }

} // namespace

namespace QK::Boot::Desktop
{
    bool PrepareFromLimineRequests(QC::u64 FramebufferRequest[], QC::u64 ModuleRequest[], const EarlyHeap &Heap, FLogFn Log)
    {
        g_Log = Log;
        if (!g_Log)
            return false;

        g_State.FramebufferRequest = FramebufferRequest;
        g_State.ModuleRequest = ModuleRequest;
        g_State.Heap = Heap;

        // Draw to Limine framebuffer if available.
        // Access the framebuffer response from our Limine request.
        QC::u64 *fb_response = reinterpret_cast<QC::u64 *>(FramebufferRequest[5]);

        if (fb_response == nullptr)
        {
            g_Log("No framebuffer response!\r\n");
            return false;
        }

        g_Log("Framebuffer response received!\r\n");

        // Limine response structure:
        // [0] = revision
        // [1] = framebuffer_count
        // [2] = framebuffers array pointer
        QC::u64 revision = fb_response[0];
        QC::u64 fb_count = fb_response[1];

        g_Log("  Revision: ");
        logDecU64(revision);
        g_Log("\r\n");

        g_Log("  Count: ");
        logDecU64(fb_count);
        g_Log("\r\n");

        if (fb_count == 0)
        {
            g_Log("No framebuffers available!\r\n");
            return false;
        }

        g_Log("Getting framebuffer pointer...\r\n");

        // Get framebuffers array (pointer to pointer)
        QC::u64 **fb_array = reinterpret_cast<QC::u64 **>(fb_response[2]);
        g_Log("Got fb_array\r\n");

        // Get first framebuffer struct
        QC::u64 *fb = fb_array[0];
        g_Log("Got fb struct\r\n");

        // Limine framebuffer struct layout:
        // [0] = address (void*)
        // [1] = width (uint64_t)
        // [2] = height (uint64_t)
        // [3] = pitch (uint64_t)
        // [4] = bpp (uint16_t, but padded)
        g_State.FbAddress = static_cast<QC::uptr>(fb[0]);
        g_State.Width = static_cast<QC::u32>(fb[1]);
        g_State.Height = static_cast<QC::u32>(fb[2]);
        g_State.Pitch = static_cast<QC::u32>(fb[3]);

        g_State.Prepared = true;
        return true;
    }

    void InitializeInput()
    {
        if (!g_State.Prepared)
        {
            if (g_Log)
                g_Log("Desktop: InitializeInput called before Prepare\r\n");
            return;
        }
        if (g_State.InputInitialized)
            return;

        const QC::u32 width = g_State.Width;
        const QC::u32 height = g_State.Height;

        g_Log("Initializing QWindowing...\r\n");

        // Initialize heap first - required for memory allocations.
        // Heap is normally initialized during early boot; this call is a safe fallback.
        if (!QK::Memory::Heap::instance().isInitialized())
        {
            g_Log("Initializing heap...\r\n");
            QK::Memory::Heap::instance().initialize(g_State.Heap.Buffer, g_State.Heap.Size);
            g_Log("Heap initialized\r\n");
        }

        g_Log("Bringing up filesystem...\r\n");
        if (QK::Boot::Ramdisk::InitializeFromLimineModules(g_State.ModuleRequest, g_Log))
        {
            g_Log("Filesystem ready\r\n");
        }
        else
        {
            g_Log("Filesystem initialization failed\r\n");
        }

        // Initialize the event system.
        QK::Event::EventManager::instance().initialize();
        g_Log("Event system initialized\r\n");

        // Bring up shutdown controller early so it can register event listeners.
        QK::Shutdown::Controller::instance();
        g_Log("Shutdown controller ready\r\n");

        // Initialize timer (higher tick reduces input polling latency).
        g_Log("Initializing timer...\r\n");
        QDrv::Timer::instance().initialize(1000);
        g_Log("Timer initialized\r\n");

        // Initialize PCI bus and enumerate devices.
        g_Log("Initializing PCI...\r\n");
        QArch::PCI::instance().initialize();
        g_Log("PCI initialized\r\n");

        // Initialize driver manager (probes USB and PS/2).
        g_Log("Initializing drivers...\r\n");
        QKDrv::Manager::instance().setScreenSize(width, height);
        QKDrv::Manager::instance().initialize();
        g_Log("Drivers initialized\r\n");

        // Ensure SST exists after /system is mounted from the system volume.
        (void)QK::SecurityCenter::instance().ensureSst();

        // Allow subsystems/commands to pump RX/IO without pulling driver headers into QKernel.
        QK::System::setPumpFn([]()
                     {
                         const QC::u64 nowMs = QDrv::Timer::instance().milliseconds();
                         QNet::Stack::instance().poll(nowMs);
                         QKDrv::Manager::instance().poll();
                     });

        // Best-effort DHCPv4 (bounded wait); falls back to manual `ip set`.
        g_Log("Attempting DHCPv4...\r\n");
        {
            QNet::DHCPv4Client dhcp;
            if (dhcp.begin() == QC::Status::Success)
            {
                QNet::DHCPv4Lease lease{};

                const QC::u64 deadlineMs = QDrv::Timer::instance().milliseconds() + 2500;
                while (QDrv::Timer::instance().milliseconds() < deadlineMs)
                {
                    // Pump NIC RX -> QNetwork.
                    QKDrv::Manager::instance().poll();
                    QKDrv::PS2::Keyboard::instance().poll();

                    if (dhcp.poll(&lease))
                    {
                        QNet::Stack::instance().ip()->setAddress(lease.address);
                        QNet::Stack::instance().ip()->setSubnetMask(lease.subnetMask);
                        QNet::Stack::instance().ip()->setGateway(lease.gateway);
                        QNet::Stack::instance().ip()->setDnsServer(lease.dnsServer);

                        g_Log("DHCPv4 lease acquired: ip=");
                        logIPv4(lease.address);
                        g_Log(" mask=");
                        logIPv4(lease.subnetMask);
                        g_Log(" gw=");
                        logIPv4(lease.gateway);

                        if (lease.dnsServer.value != 0)
                        {
                            g_Log(" dns=");
                            logIPv4(lease.dnsServer);
                        }
                        g_Log("\r\n");
                        break;
                    }

                    QDrv::Timer::instance().sleep(10);
                }

                if (lease.address.value == 0)
                {
                    g_Log("DHCPv4 timeout (manual config still available)\r\n");
                }
            }
            else
            {
                g_Log("DHCPv4 skipped (no NIC/MAC or UDP bind failed)\r\n");
            }
        }

        QKStorage::probeLimineModules();

        // Set up keyboard callback so the console works in every startup mode.
        g_Log("Setting up keyboard...\r\n");
        auto *kbd = QKDrv::Manager::instance().keyboardDriver();
        if (!kbd)
        {
            g_Log("WARNING: No keyboard driver available\r\n");
        }
        else
        {
            kbd->setCallback([](const QKDrv::KeyboardReport &rep)
                             {
                QKDrv::PS2::KeyEvent evt;
                evt.key = static_cast<QKDrv::PS2::Key>(rep.scancode);
                evt.pressed = rep.pressed;

                evt.shift = (rep.modifiers & 0x01) != 0;
                evt.ctrl = (rep.modifiers & 0x02) != 0;
                evt.alt = (rep.modifiers & 0x04) != 0;
                const bool caps = (rep.modifiers & 0x08) != 0;
                evt.character = keyToChar(evt.key, evt.shift, caps);

                // In Desktop mode, keyboard input is owned by the windowing/event system.
                // Routing keys to the serial console too causes accidental command execution.
                if (QK::Boot::Config::GetStartupMode() != QK::Boot::Config::StartupMode::Desktop)
                {
                    QK::Console::handleKeyEvent(evt);
                    return;
                }

                auto &eventMgr = QK::Event::EventManager::instance();

                QK::Event::Modifiers mods = QK::Event::Modifiers::None;
                if (evt.shift)
                    mods = mods | QK::Event::Modifiers::Shift;
                if (evt.ctrl)
                    mods = mods | QK::Event::Modifiers::Ctrl;
                if (evt.alt)
                    mods = mods | QK::Event::Modifiers::Alt;

                eventMgr.postKeyEvent(
                    evt.pressed ? QK::Event::Type::KeyDown : QK::Event::Type::KeyUp,
                    static_cast<QC::u8>(evt.key),
                    static_cast<QC::u8>(evt.key),
                    evt.character,
                    mods,
                    false); });
        }
        g_Log("Keyboard initialized\r\n");

        // Desktop owns keyboard input; keep serial console non-interactive.
        QK::Console::setInputEnabled(QK::Boot::Config::GetStartupMode() != QK::Boot::Config::StartupMode::Desktop);

        if (QK::Boot::Config::GetStartupMode() != QK::Boot::Config::StartupMode::Desktop)
        {
            g_Log("Startup mode ");
            g_Log(QK::Boot::Config::StartupModeName(QK::Boot::Config::GetStartupMode()));
            g_Log(" selected - skipping desktop bring-up\r\n");
            enterTerminalOnlyLoop();
        }

        g_State.InputInitialized = true;
    }

    void InitializeWindowSystem()
    {
        if (!g_State.Prepared)
        {
            if (g_Log)
                g_Log("Desktop: InitializeWindowSystem called before Prepare\r\n");
            return;
        }
        if (!g_State.InputInitialized)
        {
            if (g_Log)
                g_Log("Desktop: InitializeWindowSystem called before Input init\r\n");
            return;
        }
        if (g_State.WindowSystemInitialized)
            return;

        QC::u32 width = g_State.Width;
        QC::u32 height = g_State.Height;
        QC::u32 pitch = g_State.Pitch;
        const QC::uptr fbAddress = g_State.FbAddress;

        // Create and initialize framebuffer.
        // If we're running under VMware SVGA II (QEMU `-vga vmware`), the device exposes
        // the authoritative pitch via SVGA_REG_BYTES_PER_LINE. Compare it with Limine's
        // pitch and use SVGA's value when it looks safer.
        {
            auto &svga = QDrv::VmwareSVGA::instance();
            if (svga.initialize())
            {
                const QC::u32 svgaPitch = svga.bytesPerLine();
                const QC::u32 svgaFbSize = svga.framebufferSizeBytes();
                QC_LOG_INFO("QKMain", "Framebuffer pitch: limine=%u svga=%u (fb_size=%u)", pitch, svgaPitch, svgaFbSize);

                const QC::u32 minPitch = width * 4u; // ARGB8888
                if (svgaPitch >= minPitch && svgaPitch <= (1024u * 1024u))
                {
                    const QC::u64 needed = static_cast<QC::u64>(svgaPitch) * static_cast<QC::u64>(height);
                    if (svgaFbSize == 0 || needed <= svgaFbSize)
                    {
                        if (svgaPitch != pitch)
                        {
                            QC_LOG_WARN("QKMain", "Overriding Limine pitch %u -> SVGA bytes-per-line %u", pitch, svgaPitch);
                            pitch = svgaPitch;
                        }
                    }
                    else
                    {
                        QC_LOG_WARN("QKMain", "SVGA pitch rejected: need=%llu > fb_size=%u", (unsigned long long)needed, svgaFbSize);
                    }
                }
            }
        }

        g_Framebuffer.initialize(fbAddress, width, height, pitch, QW::PixelFormat::ARGB8888);
        g_Log("Framebuffer initialized\r\n");

        // Initialize window manager.
        g_Log("About to initialize WindowManager...\r\n");
        QW::WindowManager::instance().initialize(&g_Framebuffer);
        g_Log("WindowManager initialized\r\n");

        // Get active mouse driver from manager.
        g_Log("Setting up mouse...\r\n");
        auto *mouseDriver = QKDrv::Manager::instance().mouseDriver();

        // Debug: Print screen dimensions.
        g_Log("Screen: ");
        char dimBuf[32];
        int idx = 0;
        if (width >= 1000)
            dimBuf[idx++] = '0' + (width / 1000) % 10;
        if (width >= 100)
            dimBuf[idx++] = '0' + (width / 100) % 10;
        if (width >= 10)
            dimBuf[idx++] = '0' + (width / 10) % 10;
        dimBuf[idx++] = '0' + width % 10;
        dimBuf[idx++] = 'x';
        if (height >= 1000)
            dimBuf[idx++] = '0' + (height / 1000) % 10;
        if (height >= 100)
            dimBuf[idx++] = '0' + (height / 100) % 10;
        if (height >= 10)
            dimBuf[idx++] = '0' + (height / 10) % 10;
        dimBuf[idx++] = '0' + height % 10;
        dimBuf[idx++] = '\r';
        dimBuf[idx++] = '\n';
        dimBuf[idx] = 0;
        g_Log(dimBuf);

        // Debug: Print button location.
        g_Log("Button at: ");
        idx = 0;
        QC::u32 btnX = width - 120;
        if (btnX >= 1000)
            dimBuf[idx++] = '0' + (btnX / 1000) % 10;
        if (btnX >= 100)
            dimBuf[idx++] = '0' + (btnX / 100) % 10;
        if (btnX >= 10)
            dimBuf[idx++] = '0' + (btnX / 10) % 10;
        dimBuf[idx++] = '0' + btnX % 10;
        dimBuf[idx++] = ',';
        dimBuf[idx++] = '1';
        dimBuf[idx++] = '0';
        dimBuf[idx++] = '-';
        dimBuf[idx++] = '4';
        dimBuf[idx++] = '0';
        dimBuf[idx++] = '\r';
        dimBuf[idx++] = '\n';
        dimBuf[idx] = 0;
        g_Log(dimBuf);

        if (mouseDriver != nullptr)
        {
            mouseDriver->setCallback([](const QKDrv::MouseReport &report)
                                     {
                auto *mouse = QKDrv::Manager::instance().mouseDriver();
                if (!mouse)
                    return;

                auto &eventMgr = QK::Event::EventManager::instance();

                // For absolute devices, report.x/y are screen coordinates.
                // For relative devices, report.x/y are absolute cursor position; deltaX/deltaY are movement.
                const QC::i32 curX = report.isAbsolute ? report.x : mouse->x();
                const QC::i32 curY = report.isAbsolute ? report.y : mouse->y();

                QC::i32 dx = 0;
                QC::i32 dy = 0;
                if (report.isAbsolute)
                {
                    if (g_prevPosValid)
                    {
                        dx = curX - g_prevX;
                        dy = curY - g_prevY;
                    }
                    g_prevX = curX;
                    g_prevY = curY;
                    g_prevPosValid = true;
                }
                else
                {
                    dx = report.deltaX;
                    dy = report.deltaY;
                }

                // Movement telemetry is useful for driver bring-up, but it's very noisy.
                // Log:
                //  - always when buttons change
                //  - periodically on movement (dx/dy != 0)
                //  - and a slow heartbeat even when idle
                static QC::u32 s_mouseReportCount = 0;
                static QC::u8 s_prevButtons = 0;
                ++s_mouseReportCount;

                const bool buttonsChanged = (report.buttons != s_prevButtons);
                s_prevButtons = report.buttons;

                const bool moved = (dx != 0) || (dy != 0);
                bool logThis = false;
                if (buttonsChanged)
                {
                    logThis = true;
                }
                else
                {
                    // Slow heartbeat so we know input is still flowing.
                    // Avoid logging movement by default; serial I/O here directly impacts UI latency.
                    logThis = ((s_mouseReportCount % 5000u) == 0u);
                }

                if (logThis)
                {
                    g_Log("Mouse report (");
                    g_Log(report.isAbsolute ? "abs" : "rel");
                    g_Log(") pos(");
                    logInt(curX);
                    g_Log(",");
                    logInt(curY);
                    g_Log(") d(");
                    logInt(dx);
                    g_Log(",");
                    logInt(dy);
                    g_Log(") buttons=");
                    logInt(report.buttons);
                    g_Log("\r\n");
                }

                auto logClick = [&](const char *label)
                {
                    // NOTE: A click packet often has dx/dy = 0; that's normal for relative mice.
                    g_Log(label);
                    g_Log(" at (");
                    logInt(curX);
                    g_Log(", ");
                    logInt(curY);
                    g_Log(") ");
                    g_Log(report.isAbsolute ? "abs" : "rel");
                    g_Log("\r\n");
                };

                bool leftBtn = report.buttons & 0x01;
                bool rightBtn = report.buttons & 0x02;

                // Post mouse move event first so hover state is up-to-date.
                // Always post the current cursor position.
                // NOTE: For our USB mouse path, the driver already maintains a clamped
                // absolute cursor position even for relative devices (curX/curY). Windowing
                // hit-testing relies on x/y being meaningful.
                bool postedMove = false;
                if (moved || buttonsChanged)
                {
                    eventMgr.postMouseMove(curX, curY, dx, dy);
                    postedMove = true;
                }

                // Scroll wheel (mouse wheel) -> mouse scroll event.
                // Ensure hover is current even if the wheel moved without pointer movement.
                if (report.wheel != 0)
                {
                    if (!postedMove)
                    {
                        eventMgr.postMouseMove(curX, curY, 0, 0);
                        postedMove = true;
                    }
                    eventMgr.postMouseScroll(report.wheel, curX, curY);
                }

                // Check for button state changes.
                if (leftBtn && !g_prevLeftBtn)
                {
                    logClick("Left click");
                    eventMgr.postMouseButton(QK::Event::Type::MouseButtonDown,
                                             QK::Event::MouseButton::Left,
                                             curX, curY, QK::Event::Modifiers::None);
                }
                if (!leftBtn && g_prevLeftBtn)
                {
                    eventMgr.postMouseButton(QK::Event::Type::MouseButtonUp,
                                             QK::Event::MouseButton::Left,
                                             curX, curY, QK::Event::Modifiers::None);
                }
                if (rightBtn && !g_prevRightBtn)
                {
                    logClick("Right click");
                    eventMgr.postMouseButton(QK::Event::Type::MouseButtonDown,
                                             QK::Event::MouseButton::Right,
                                             curX, curY, QK::Event::Modifiers::None);
                }
                if (!rightBtn && g_prevRightBtn)
                {
                    eventMgr.postMouseButton(QK::Event::Type::MouseButtonUp,
                                             QK::Event::MouseButton::Right,
                                             curX, curY, QK::Event::Modifiers::None);
                }

                g_prevLeftBtn = leftBtn;
                g_prevRightBtn = rightBtn; });
        }
        g_Log("Mouse configured\r\n");

        // Seed initial cursor position immediately so the hardware cursor isn't stuck at (0,0)
        // until the first mouse movement packet arrives.
        if (auto *drv = QKDrv::Manager::instance().mouseDriver())
        {
            const QC::i32 seedX = drv->x();
            const QC::i32 seedY = drv->y();
            QK::Event::EventManager::instance().postMouseMove(seedX, seedY, 0, 0);
            QK::Event::EventManager::instance().processEvents();
        }

        // Create desktop using QDDesktop module.
        g_State.WindowSystemInitialized = true;
    }

    [[noreturn]] void InitializeDesktopAndRunLoop()
    {
        if (!g_State.Prepared)
        {
            if (g_Log)
                g_Log("Desktop: InitializeDesktop called before Prepare\r\n");
            for (;;)
                asm volatile("hlt");
        }
        if (!g_State.WindowSystemInitialized)
        {
            if (g_Log)
                g_Log("Desktop: InitializeDesktop called before WindowSystem init\r\n");
            for (;;)
                asm volatile("hlt");
        }

        if (!g_State.DesktopInitialized)
        {
            const QC::u32 width = g_State.Width;
            const QC::u32 height = g_State.Height;

            g_Log("Creating desktop...\r\n");
            g_Desktop.initialize(width, height);
            g_Log("Desktop initialized\r\n");

            // Trigger an initial paint via the normal window invalidation path.
            // Avoid repainting the entire desktop every loop (can add input latency).
            if (g_Desktop.window())
            {
                g_Desktop.window()->invalidate();
            }

            // Initial render.
            QW::WindowManager::instance().render();
            g_Log("Initial render complete!\r\n");

            // Register keyboard listener for Ctrl+Q shutdown.
            g_CtrlQListener.categoryMask = QK::Event::Category::Input;
            g_CtrlQListener.eventType = QK::Event::Type::KeyDown;
            g_CtrlQListener.handler = [](const QK::Event::Event &event, void *) -> bool
            {
                const auto &key = event.asKey();

                // Check for Q key with Ctrl modifier.
                if (key.keycode == static_cast<QC::u8>(QKDrv::PS2::Key::Q) &&
                    QK::Event::hasModifier(key.modifiers, QK::Event::Modifiers::Ctrl))
                {
                    g_Log("Ctrl+Q pressed - requesting shutdown!\r\n");
                    QK::Event::EventManager::instance().postShutdownEvent(
                        QK::Event::Type::ShutdownRequest,
                        static_cast<QC::u32>(QK::Shutdown::Reason::KeyboardShortcut));
                    return true;
                }
                return false;
            };
            g_CtrlQListener.userData = nullptr;
            g_CtrlQId = QK::Event::EventManager::instance().addListener(g_CtrlQListener);
            if (g_CtrlQId == QK::Event::InvalidListenerId)
            {
                g_Log("ERROR: Failed to register Ctrl+Q listener!\r\n");
            }
            else
            {
                g_Log("Ctrl+Q shutdown listener registered\r\n");
            }

            g_State.DesktopInitialized = true;
        }

        // Main loop - process events and render.
        g_Log("Entering main loop...\r\n");

        while (true)
        {
            // Poll all active drivers.
            QKDrv::Manager::instance().poll();

            auto &eventMgr = QK::Event::EventManager::instance();
            auto &wm = QW::WindowManager::instance();

            // Process a bounded batch, then give rendering a chance.
            // This keeps pointer hover/press visuals responsive even under
            // high-frequency mouse reports.
            for (;;)
            {
                const QC::usize processed = eventMgr.processEvents(128);

                if (wm.needsRender())
                {
                    wm.render();
                }

                // No more immediate work to do; exit the inner loop and let the
                // idle pacing logic decide how long to wait.
                if (processed == 0 && !wm.needsRender())
                {
                    break;
                }
            }

            // Idle pacing:
            // Previously this loop used `hlt` to wait for the next interrupt.
            // In practice, when the next interrupt is delayed (e.g., sparse input,
            // timer issues, or interrupt masking in drivers), this can stall UI
            // presentation for multi-second intervals.
            //
            // Instead, run a small periodic tick that:
            // - keeps driver polling/network alive,
            // - processes events,
            // - forces a render at a predictable cadence when idle.
            //
            // Minimal UX: target ~60Hz, but keep CPU usage bounded with short sleeps.
            static constexpr QC::u64 kFrameIntervalMs = 16;
            const QC::u64 nowMs = QDrv::Timer::instance().milliseconds();
            static QC::u64 s_nextFrameMs = 0;
            if (s_nextFrameMs == 0)
                s_nextFrameMs = nowMs;

            if (nowMs >= s_nextFrameMs)
            {
                // Force a compositor tick so hover/cursor/pointer feedback stays snappy.
                wm.render();
                s_nextFrameMs = nowMs + kFrameIntervalMs;
            }
            else
            {
                // Sleep in short slices so input interrupts can still wake us quickly.
                const QC::u64 remainingMs = s_nextFrameMs - nowMs;
                const QC::u64 sleepMs = (remainingMs > 2) ? 2 : remainingMs;
                if (sleepMs > 0)
                    QDrv::Timer::instance().sleep(static_cast<QC::u32>(sleepMs));
            }
        }
    }

    bool RunFromLimineRequests(QC::u64 FramebufferRequest[], QC::u64 ModuleRequest[], const EarlyHeap &Heap, FLogFn Log)
    {
        if (!PrepareFromLimineRequests(FramebufferRequest, ModuleRequest, Heap, Log))
            return false;

        InitializeInput();
        InitializeWindowSystem();
        InitializeDesktopAndRunLoop();
    }

} // namespace QK::Boot::Desktop
