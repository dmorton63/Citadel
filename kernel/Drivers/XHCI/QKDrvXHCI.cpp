// QKDrvXHCI - USB 3.0 xHCI controller implementation
// Namespace: QKDrv::XHCI

#include "xhci_internal.h"
#include "QArchCPU.h"
#include "QKMemTranslator.h"
#include "QCLogger.h"
#include "QCString.h"
#include "PS2/QKDrvPS2Keyboard.h"

// Early page allocator (defined in QKMain.cpp, returns physical address)
extern "C" QC::PhysAddr earlyAllocatePage();
// Convert physical to virtual via HHDM
extern "C" QC::VirtAddr physToVirt(QC::PhysAddr phys);
// Convert kernel virtual to physical
extern "C" QC::PhysAddr kernelVirtToPhys(QC::VirtAddr virt);

namespace QKDrv
{
    namespace XHCI
    {

        // DMA allocation result with both physical and virtual addresses
        struct DMAPage
        {
            QC::PhysAddr phys;
            void *virt;
        };

        // Helper: allocate a DMA page, returning both physical and virtual addresses
        static DMAPage allocateDMAPage()
        {
            QC::PhysAddr phys = earlyAllocatePage();
            return {phys, reinterpret_cast<void *>(physToVirt(phys))};
        }

        // USB request types
        constexpr QC::u8 USB_REQ_GET_DESCRIPTOR = 0x06;
        constexpr QC::u8 USB_REQ_SET_CONFIGURATION = 0x09;
        constexpr QC::u8 USB_REQ_SET_PROTOCOL = 0x0B;

        // USB descriptor types
        constexpr QC::u8 USB_DESC_DEVICE = 1;
        constexpr QC::u8 USB_DESC_CONFIG = 2;
        constexpr QC::u8 USB_DESC_INTERFACE = 4;
        constexpr QC::u8 USB_DESC_ENDPOINT = 5;
        constexpr QC::u8 USB_DESC_HID = 0x21;
        constexpr QC::u8 USB_DESC_HID_REPORT = 0x22;

        // USB class codes
        constexpr QC::u8 USB_CLASS_HID = 0x03;
        constexpr QC::u8 USB_SUBCLASS_BOOT = 0x01;
        constexpr QC::u8 USB_PROTOCOL_KEYBOARD = 0x01;
        constexpr QC::u8 USB_PROTOCOL_MOUSE = 0x02;

        // Extended capability IDs
        constexpr QC::u8 XCAP_LEGACY = 1;

        // Static singleton instance to avoid heap allocation issues
        static XHCIControllerImpl *s_instance = nullptr;
        static QC::u8 s_controllerBuffer[sizeof(XHCIControllerImpl)] __attribute__((aligned(16)));

        XHCIController *xhci_init(QArch::PCIDevice *device)
        {
            return XHCIControllerImpl::create(device);
        }

        void xhci_reset(XHCIController *controller)
        {
            if (controller)
            {
                controller->hardwareReset();
            }
        }

        bool xhci_submit_transfer(XHCIController *controller,
                                  QC::u8 slotId,
                                  QC::u8 endpointId,
                                  QC::PhysAddr buffer,
                                  QC::u32 length,
                                  QC::u32 trbFlags)
        {
            if (!controller)
            {
                return false;
            }
            return controller->submitTransfer(slotId, endpointId, buffer, length, trbFlags);
        }

        void xhci_poll_events(XHCIController *controller)
        {
            if (controller)
            {
                controller->poll();
            }
        }

        XHCIControllerImpl *XHCIControllerImpl::create(QArch::PCIDevice *pciDevice)
        {
            if (!pciDevice)
                return nullptr;

            // Check for xHCI: class 0x0C, subclass 0x03, progIF 0x30
            if (pciDevice->classCode == QArch::PCIClass::SerialBus &&
                pciDevice->subclass == 0x03 &&
                pciDevice->progIF == 0x30)
            {
                QC_LOG_INFO("xHCI", "Found xHCI controller");

                if (s_instance)
                {
                    QC_LOG_WARN("xHCI", "XHCIControllerImpl already exists, skipping");
                    return s_instance;
                }

                QC_LOG_INFO("xHCI", "Using placement new with buffer at %p", s_controllerBuffer);
                s_instance = new (s_controllerBuffer) XHCIControllerImpl(pciDevice);
                QC_LOG_INFO("xHCI", "XHCIControllerImpl created at %p", s_instance);
                return s_instance;
            }
            return nullptr;
        }

        // TabletDriver implementation
        TabletDriver::TabletDriver(XHCIControllerImpl *controller)
            : m_controller(controller), m_callback(nullptr), m_x(0), m_y(0), m_minX(0), m_minY(0), m_maxX(1023), m_maxY(767), m_buttons(0)
        {
        }

        void TabletDriver::poll()
        {
            if (m_controller)
            {
                m_controller->poll();
            }
        }

        void TabletDriver::setBounds(QC::i32 minX, QC::i32 minY, QC::i32 maxX, QC::i32 maxY)
        {
            m_minX = minX;
            m_minY = minY;
            m_maxX = maxX;
            m_maxY = maxY;
        }

        void TabletDriver::updatePosition(QC::i32 x, QC::i32 y, QC::u8 buttons)
        {
            // Clamp to bounds
            if (x < m_minX)
                x = m_minX;
            if (x > m_maxX)
                x = m_maxX;
            if (y < m_minY)
                y = m_minY;
            if (y > m_maxY)
                y = m_maxY;

            m_x = x;
            m_y = y;
            m_buttons = buttons;

            if (m_callback)
            {
                MouseReport report;
                report.x = m_x;
                report.y = m_y;
                report.wheel = 0;
                report.buttons = m_buttons;
                report.isAbsolute = true;
                m_callback(report);
            }
        }

        // HIDMouseDriver implementation
        HIDMouseDriver::HIDMouseDriver(XHCIControllerImpl *controller)
            : m_controller(controller), m_callback(nullptr), m_x(0), m_y(0), m_fx(0.0f), m_fy(0.0f), m_minX(0), m_minY(0), m_maxX(1023), m_maxY(767), m_buttons(0)
        {
        }

        void HIDMouseDriver::poll()
        {
            if (m_controller)
            {
                m_controller->poll();
            }
        }

        void HIDMouseDriver::setBounds(QC::i32 minX, QC::i32 minY, QC::i32 maxX, QC::i32 maxY)
        {
            m_minX = minX;
            m_minY = minY;
            m_maxX = maxX;
            m_maxY = maxY;

            // Center cursor when bounds are (re)applied.
            m_x = (minX + maxX) / 2;
            m_y = (minY + maxY) / 2;
            m_fx = static_cast<float>(m_x);
            m_fy = static_cast<float>(m_y);
        }

        void HIDMouseDriver::updateDelta(QC::i32 dx, QC::i32 dy, QC::i32 wheel, QC::u8 buttons)
        {
            // Relative mice in QEMU can be very fast/jittery. Apply a modest sensitivity
            // factor so UI hit-testing feels controllable.
            // (Historically this scaling lived in WindowManager; keeping it here ensures
            // cursor rendering and event routing remain 1:1 in absolute coordinates.)
            static constexpr float kSensitivity = 0.12f;

            const QC::i32 oldX = m_x;
            const QC::i32 oldY = m_y;

            // Accumulate with fractional precision; do not snap the accumulator to integer
            // each packet or small motions become hard to land (quantization).
            m_fx += static_cast<float>(dx) * kSensitivity;
            m_fy += static_cast<float>(dy) * kSensitivity;

            // Clamp the accumulator (not just the integer) so fractions are preserved.
            if (m_fx < static_cast<float>(m_minX))
                m_fx = static_cast<float>(m_minX);
            if (m_fx > static_cast<float>(m_maxX))
                m_fx = static_cast<float>(m_maxX);
            if (m_fy < static_cast<float>(m_minY))
                m_fy = static_cast<float>(m_minY);
            if (m_fy > static_cast<float>(m_maxY))
                m_fy = static_cast<float>(m_maxY);

            // Derive the integer cursor position from the accumulator.
            // (Accumulator is already clamped to bounds above.)
            // Use rounding (not truncation) so subpixel motion feels less "sticky".
            m_x = static_cast<QC::i32>(m_fx + 0.5f);
            m_y = static_cast<QC::i32>(m_fy + 0.5f);

            m_buttons = buttons;

            if (m_callback)
            {
                MouseReport report;
                report.x = m_x;               // absolute position
                report.y = m_y;               // absolute position
                report.deltaX = (m_x - oldX); // scaled movement
                report.deltaY = (m_y - oldY); // scaled movement
                report.wheel = wheel;
                report.buttons = m_buttons;
                report.isAbsolute = false;
                m_callback(report);
            }
        }

        // HIDKeyboardDriver implementation
        HIDKeyboardDriver::HIDKeyboardDriver(XHCIControllerImpl *controller)
            : m_controller(controller), m_callback(nullptr), m_prevBootMods(0), m_modifiers(0), m_capsLock(false)
        {
            QC::String::memset(m_keyStates, 0, sizeof(m_keyStates));
            QC::String::memset(m_prevKeys, 0, sizeof(m_prevKeys));
        }

        void HIDKeyboardDriver::poll()
        {
            if (m_controller)
            {
                m_controller->poll();
            }
        }

        bool HIDKeyboardDriver::isKeyPressed(QC::u8 scancode) const
        {
            return m_keyStates[scancode];
        }

        static QC::u8 usbUsageToPs2Keycode(QC::u8 usage)
        {
            using Key = QKDrv::PS2::Key;
            switch (usage)
            {
            // Letters
            case 0x04:
                return static_cast<QC::u8>(Key::A);
            case 0x05:
                return static_cast<QC::u8>(Key::B);
            case 0x06:
                return static_cast<QC::u8>(Key::C);
            case 0x07:
                return static_cast<QC::u8>(Key::D);
            case 0x08:
                return static_cast<QC::u8>(Key::E);
            case 0x09:
                return static_cast<QC::u8>(Key::F);
            case 0x0A:
                return static_cast<QC::u8>(Key::G);
            case 0x0B:
                return static_cast<QC::u8>(Key::H);
            case 0x0C:
                return static_cast<QC::u8>(Key::I);
            case 0x0D:
                return static_cast<QC::u8>(Key::J);
            case 0x0E:
                return static_cast<QC::u8>(Key::K);
            case 0x0F:
                return static_cast<QC::u8>(Key::L);
            case 0x10:
                return static_cast<QC::u8>(Key::M);
            case 0x11:
                return static_cast<QC::u8>(Key::N);
            case 0x12:
                return static_cast<QC::u8>(Key::O);
            case 0x13:
                return static_cast<QC::u8>(Key::P);
            case 0x14:
                return static_cast<QC::u8>(Key::Q);
            case 0x15:
                return static_cast<QC::u8>(Key::R);
            case 0x16:
                return static_cast<QC::u8>(Key::S);
            case 0x17:
                return static_cast<QC::u8>(Key::T);
            case 0x18:
                return static_cast<QC::u8>(Key::U);
            case 0x19:
                return static_cast<QC::u8>(Key::V);
            case 0x1A:
                return static_cast<QC::u8>(Key::W);
            case 0x1B:
                return static_cast<QC::u8>(Key::X);
            case 0x1C:
                return static_cast<QC::u8>(Key::Y);
            case 0x1D:
                return static_cast<QC::u8>(Key::Z);

            // Digits
            case 0x1E:
                return static_cast<QC::u8>(Key::Num1);
            case 0x1F:
                return static_cast<QC::u8>(Key::Num2);
            case 0x20:
                return static_cast<QC::u8>(Key::Num3);
            case 0x21:
                return static_cast<QC::u8>(Key::Num4);
            case 0x22:
                return static_cast<QC::u8>(Key::Num5);
            case 0x23:
                return static_cast<QC::u8>(Key::Num6);
            case 0x24:
                return static_cast<QC::u8>(Key::Num7);
            case 0x25:
                return static_cast<QC::u8>(Key::Num8);
            case 0x26:
                return static_cast<QC::u8>(Key::Num9);
            case 0x27:
                return static_cast<QC::u8>(Key::Num0);

            // Control + punctuation
            case 0x28:
                return static_cast<QC::u8>(Key::Enter);
            case 0x29:
                return static_cast<QC::u8>(Key::Escape);
            case 0x2A:
                return static_cast<QC::u8>(Key::Backspace);
            case 0x2B:
                return static_cast<QC::u8>(Key::Tab);
            case 0x2C:
                return static_cast<QC::u8>(Key::Space);
            case 0x2D:
                return static_cast<QC::u8>(Key::Minus);
            case 0x2E:
                return static_cast<QC::u8>(Key::Equals);
            case 0x2F:
                return static_cast<QC::u8>(Key::LeftBracket);
            case 0x30:
                return static_cast<QC::u8>(Key::RightBracket);
            case 0x31:
                return static_cast<QC::u8>(Key::Backslash);
            case 0x33:
                return static_cast<QC::u8>(Key::Semicolon);
            case 0x34:
                return static_cast<QC::u8>(Key::Apostrophe);
            case 0x35:
                return static_cast<QC::u8>(Key::Backtick);
            case 0x36:
                return static_cast<QC::u8>(Key::Comma);
            case 0x37:
                return static_cast<QC::u8>(Key::Period);
            case 0x38:
                return static_cast<QC::u8>(Key::Slash);
            case 0x39:
                return static_cast<QC::u8>(Key::CapsLock);

            // Function keys
            case 0x3A:
                return static_cast<QC::u8>(Key::F1);
            case 0x3B:
                return static_cast<QC::u8>(Key::F2);
            case 0x3C:
                return static_cast<QC::u8>(Key::F3);
            case 0x3D:
                return static_cast<QC::u8>(Key::F4);
            case 0x3E:
                return static_cast<QC::u8>(Key::F5);
            case 0x3F:
                return static_cast<QC::u8>(Key::F6);
            case 0x40:
                return static_cast<QC::u8>(Key::F7);
            case 0x41:
                return static_cast<QC::u8>(Key::F8);
            case 0x42:
                return static_cast<QC::u8>(Key::F9);
            case 0x43:
                return static_cast<QC::u8>(Key::F10);
            case 0x44:
                return static_cast<QC::u8>(Key::F11);
            case 0x45:
                return static_cast<QC::u8>(Key::F12);

            // Navigation
            case 0x46:
                return static_cast<QC::u8>(Key::PrintScreen);
            case 0x47:
                return static_cast<QC::u8>(Key::ScrollLock);
            case 0x48:
                return static_cast<QC::u8>(Key::Pause);
            case 0x49:
                return static_cast<QC::u8>(Key::Insert);
            case 0x4A:
                return static_cast<QC::u8>(Key::Home);
            case 0x4B:
                return static_cast<QC::u8>(Key::PageUp);
            case 0x4C:
                return static_cast<QC::u8>(Key::Delete);
            case 0x4D:
                return static_cast<QC::u8>(Key::End);
            case 0x4E:
                return static_cast<QC::u8>(Key::PageDown);
            case 0x4F:
                return static_cast<QC::u8>(Key::Right);
            case 0x50:
                return static_cast<QC::u8>(Key::Left);
            case 0x51:
                return static_cast<QC::u8>(Key::Down);
            case 0x52:
                return static_cast<QC::u8>(Key::Up);

            // Keypad
            case 0x53:
                return static_cast<QC::u8>(Key::NumLock);
            case 0x54:
                return static_cast<QC::u8>(Key::KpDivide);
            case 0x55:
                return static_cast<QC::u8>(Key::KpMultiply);
            case 0x56:
                return static_cast<QC::u8>(Key::KpMinus);
            case 0x57:
                return static_cast<QC::u8>(Key::KpPlus);
            case 0x58:
                return static_cast<QC::u8>(Key::KpEnter);
            case 0x59:
                return static_cast<QC::u8>(Key::Kp1);
            case 0x5A:
                return static_cast<QC::u8>(Key::Kp2);
            case 0x5B:
                return static_cast<QC::u8>(Key::Kp3);
            case 0x5C:
                return static_cast<QC::u8>(Key::Kp4);
            case 0x5D:
                return static_cast<QC::u8>(Key::Kp5);
            case 0x5E:
                return static_cast<QC::u8>(Key::Kp6);
            case 0x5F:
                return static_cast<QC::u8>(Key::Kp7);
            case 0x60:
                return static_cast<QC::u8>(Key::Kp8);
            case 0x61:
                return static_cast<QC::u8>(Key::Kp9);
            case 0x62:
                return static_cast<QC::u8>(Key::Kp0);
            case 0x63:
                return static_cast<QC::u8>(Key::KpDot);

            default:
                return static_cast<QC::u8>(Key::None);
            }
        }

        void HIDKeyboardDriver::emit(QC::u8 scancode, bool pressed)
        {
            if (scancode == 0)
                return;

            m_keyStates[scancode] = pressed;

            if (m_callback)
            {
                KeyboardReport report;
                report.scancode = scancode;
                report.pressed = pressed;
                report.modifiers = m_modifiers;
                m_callback(report);
            }
        }

        void HIDKeyboardDriver::handleBootReport(const QC::u8 *data, QC::u32 length)
        {
            if (!data || length < 8)
                return;

            const QC::u8 newBootMods = data[0];
            QC::u8 newKeys[6];
            for (int i = 0; i < 6; ++i)
                newKeys[i] = data[2 + i];

            // Update effective modifiers (shift/ctrl/alt) from the boot modifier byte.
            const bool shift = (newBootMods & (0x02 | 0x20)) != 0;
            const bool ctrl = (newBootMods & (0x01 | 0x10)) != 0;
            const bool alt = (newBootMods & (0x04 | 0x40)) != 0;
            m_modifiers = (shift ? 0x01 : 0) | (ctrl ? 0x02 : 0) | (alt ? 0x04 : 0) | (m_capsLock ? 0x08 : 0);

            // Emit modifier key transitions (as real key events too).
            const QC::u8 diffBootMods = static_cast<QC::u8>(m_prevBootMods ^ newBootMods);
            m_prevBootMods = newBootMods;

            auto emitMod = [&](QC::u8 mask, QC::u8 keycode)
            {
                if (diffBootMods & mask)
                {
                    const bool pressed = (newBootMods & mask) != 0;
                    emit(keycode, pressed);
                }
            };

            emitMod(0x01, static_cast<QC::u8>(QKDrv::PS2::Key::LeftCtrl));
            emitMod(0x02, static_cast<QC::u8>(QKDrv::PS2::Key::LeftShift));
            emitMod(0x04, static_cast<QC::u8>(QKDrv::PS2::Key::LeftAlt));
            emitMod(0x10, static_cast<QC::u8>(QKDrv::PS2::Key::RightCtrl));
            emitMod(0x20, static_cast<QC::u8>(QKDrv::PS2::Key::RightShift));
            emitMod(0x40, static_cast<QC::u8>(QKDrv::PS2::Key::RightAlt));

            auto contains = [](const QC::u8 keys[6], QC::u8 usage) -> bool
            {
                for (int i = 0; i < 6; ++i)
                {
                    if (keys[i] == usage)
                        return true;
                }
                return false;
            };

            // Key releases
            for (int i = 0; i < 6; ++i)
            {
                const QC::u8 oldUsage = m_prevKeys[i];
                if (oldUsage == 0)
                    continue;
                if (!contains(newKeys, oldUsage))
                {
                    const QC::u8 keycode = usbUsageToPs2Keycode(oldUsage);
                    emit(keycode, false);
                }
            }

            // CapsLock press toggles the lock state.
            const QC::u8 capsUsage = 0x39;
            const bool capsWasDown = contains(m_prevKeys, capsUsage);
            const bool capsIsDown = contains(newKeys, capsUsage);
            const bool capsPressed = (!capsWasDown && capsIsDown);
            if (capsPressed)
            {
                m_capsLock = !m_capsLock;
                m_modifiers = (shift ? 0x01 : 0) | (ctrl ? 0x02 : 0) | (alt ? 0x04 : 0) | (m_capsLock ? 0x08 : 0);
                emit(static_cast<QC::u8>(QKDrv::PS2::Key::CapsLock), true);
            }

            // Key presses (excluding CapsLock handled above)
            for (int i = 0; i < 6; ++i)
            {
                const QC::u8 newUsage = newKeys[i];
                if (newUsage == 0 || newUsage == 0x01 || newUsage == 0x02 || newUsage == 0x03)
                    continue;

                if (!contains(m_prevKeys, newUsage) && newUsage != capsUsage)
                {
                    const QC::u8 keycode = usbUsageToPs2Keycode(newUsage);
                    emit(keycode, true);
                }
            }

            for (int i = 0; i < 6; ++i)
                m_prevKeys[i] = newKeys[i];
        }

        XHCIControllerImpl::XHCIControllerImpl(QArch::PCIDevice *pciDevice)
            : m_pciDevice(pciDevice), m_mmioBase(0), m_capRegs(nullptr), m_opRegs(nullptr), m_doorbells(nullptr), m_portRegs(nullptr), m_interrupter(nullptr), m_portCount(0), m_maxSlots(0), m_maxIntrs(0), m_pageSize(4096), m_contextSize(32), m_dcbaa(nullptr), m_commandRing(nullptr), m_commandEnqueue(0), m_commandCycle(true), m_eventRing(nullptr), m_erst(nullptr), m_eventDequeue(0), m_eventCycle(true), m_inputContext(nullptr), m_deviceCount(0), m_tabletSlot(0), m_mouseSlot(0), m_keyboardSlot(0), m_commandPending(false), m_lastCompletionCode(CompletionCode::Invalid), m_lastSlotId(0), m_transferPending(false), m_transferCompletionCode(CompletionCode::Invalid), m_mouseCallback(nullptr), m_screenWidth(1024), m_screenHeight(768), m_tabletDriver(nullptr), m_mouseDriver(nullptr), m_keyboardDriver(nullptr)
        {
            QC_LOG_INFO("xHCI", "Constructor body entered");

            // Use memset to zero the arrays to avoid any issues with brace initialization
            QC_LOG_INFO("xHCI", "Zeroing deviceContexts array");
            QC::String::memset(m_deviceContexts, 0, sizeof(m_deviceContexts));

            QC_LOG_INFO("xHCI", "Zeroing devices array");
            QC::String::memset(m_devices, 0, sizeof(m_devices));

            QC::String::memset(m_portEnumerating, 0, sizeof(m_portEnumerating));

            QC::String::memset(m_ep0Rings, 0, sizeof(m_ep0Rings));
            QC::String::memset(m_ep0RingPhys, 0, sizeof(m_ep0RingPhys));
            QC::String::memset(m_ep0Enqueue, 0, sizeof(m_ep0Enqueue));
            for (QC::usize i = 0; i <= MAX_DEVICES; ++i)
                m_ep0Cycle[i] = true;

            QC_LOG_INFO("xHCI", "Constructor complete");
        }

        XHCIControllerImpl::~XHCIControllerImpl()
        {
            shutdown();
        }

        QC::Status XHCIControllerImpl::initialize()
        {
            QC_LOG_INFO("xHCI", "Initializing xHCI controller");

            // Enable bus mastering and memory space
            QArch::PCI::instance().enableBusMastering(m_pciDevice->address);
            QArch::PCI::instance().enableMemorySpace(m_pciDevice->address);

            // Map MMIO
            QC::PhysAddr barAddr = m_pciDevice->bar[0];
            QC_LOG_INFO("xHCI", "BAR0 at 0x%lx", barAddr);

            if (barAddr == 0)
            {
                QC_LOG_ERROR("xHCI", "BAR0 is zero");
                return QC::Status::Error;
            }

            m_mmioBase = QK::Memory::Translator::instance().mapMMIO(barAddr, 0x10000);

            if (!m_mmioBase)
            {
                QC_LOG_ERROR("xHCI", "Failed to map MMIO");
                return QC::Status::OutOfMemory;
            }

            QC_LOG_INFO("xHCI", "MMIO mapped at 0x%lx", m_mmioBase);

            // Set up register pointers
            QC_LOG_INFO("xHCI", "Setting up capRegs pointer");
            m_capRegs = reinterpret_cast<volatile CapRegs *>(m_mmioBase);

            QC_LOG_INFO("xHCI", "Reading capLength from 0x%lx", reinterpret_cast<QC::VirtAddr>(m_capRegs));
            QC::u8 capLen = m_capRegs->capLength;
            QC_LOG_INFO("xHCI", "capLength = %u", capLen);

            m_opRegs = reinterpret_cast<volatile OpRegs *>(m_mmioBase + capLen);
            m_doorbells = reinterpret_cast<volatile QC::u32 *>(m_mmioBase + m_capRegs->dbOffset);
            m_portRegs = reinterpret_cast<volatile PortRegs *>(
                m_mmioBase + m_capRegs->capLength + 0x400);

            // Runtime registers (interrupter 0)
            QC::VirtAddr runtimeBase = m_mmioBase + m_capRegs->rtsOffset;
            m_interrupter = reinterpret_cast<volatile InterrupterRegs *>(runtimeBase + 0x20);

            // Get controller parameters
            m_maxSlots = m_capRegs->hcsParams1 & 0xFF;
            m_portCount = (m_capRegs->hcsParams1 >> 24) & 0xFF;
            m_maxIntrs = (m_capRegs->hcsParams1 >> 8) & 0x7FF;

            // Check context size (64-byte contexts if AC64 bit set)
            m_contextSize = (m_capRegs->hccParams1 & (1 << 2)) ? 64 : 32;

            // Get page size
            m_pageSize = (m_opRegs->pageSize & 0xFFFF) << 12;
            if (m_pageSize == 0)
                m_pageSize = 4096;

            QC_LOG_INFO("xHCI", "xHCI v%x.%x: %u ports, %u slots, ctx=%u",
                        (m_capRegs->hciVersion >> 8) & 0xFF,
                        m_capRegs->hciVersion & 0xFF,
                        m_portCount, m_maxSlots, m_contextSize);

            // Take ownership from BIOS
            takeOwnership();

            // Halt and reset controller
            reset();

            // Initialize data structures
            initializeDCBAA();
            initializeCommandRing();
            initializeEventRing();
            initializeScratchpad();

            // Allocate input context
            DMAPage inputPage = allocateDMAPage();
            m_inputContext = reinterpret_cast<QC::u8 *>(inputPage.virt);
            m_inputContextPhys = inputPage.phys;
            QC::String::memset(m_inputContext, 0, 4096);

            // Configure max slots
            m_opRegs->config = m_maxSlots;

            // Enable interrupter
            m_interrupter->iman = 0x3; // Enable + pending clear

            // Start controller
            m_opRegs->usbcmd |= CMD_RUN | CMD_INTE;

            // Wait for controller to start
            for (int i = 0; i < 100 && (m_opRegs->usbsts & STS_HCH); ++i)
            {
                for (int j = 0; j < 10000; ++j)
                    cpu_relax();
            }

            if (m_opRegs->usbsts & STS_HCH)
            {
                QC_LOG_ERROR("xHCI", "XHCIControllerImpl failed to start");
                return QC::Status::Error;
            }

            QC_LOG_INFO("xHCI", "xHCI controller running");

            // Probe for devices
            probeDevices();

            return QC::Status::Success;
        }

        void XHCIControllerImpl::shutdown()
        {
            if (m_opRegs)
            {
                m_opRegs->usbcmd &= ~CMD_RUN;
                for (int i = 0; i < 100 && !(m_opRegs->usbsts & STS_HCH); ++i)
                {
                    for (int j = 0; j < 10000; ++j)
                        cpu_relax();
                }
            }
            QC_LOG_INFO("xHCI", "xHCI controller shutdown");
        }

        void XHCIControllerImpl::takeOwnership()
        {
            // Find legacy support extended capability
            QC::u32 xecp = (m_capRegs->hccParams1 >> 16) & 0xFFFF;
            if (xecp == 0)
                return;

            volatile QC::u32 *cap = reinterpret_cast<volatile QC::u32 *>(m_mmioBase + (xecp << 2));

            while (true)
            {
                QC::u32 capVal = *cap;
                QC::u8 capId = capVal & 0xFF;

                if (capId == XCAP_LEGACY)
                {
                    // Request ownership from BIOS
                    if (capVal & (1 << 16))
                    {                              // BIOS owns
                        *cap = capVal | (1 << 24); // Request OS ownership

                        // Wait for BIOS to release
                        for (int i = 0; i < 1000; ++i)
                        {
                            if (!(*cap & (1 << 16)))
                                break;
                            for (int j = 0; j < 10000; ++j)
                                cpu_relax();
                        }

                        if (*cap & (1 << 16))
                        {
                            QC_LOG_WARN("xHCI", "BIOS did not release ownership");
                        }
                        else
                        {
                            QC_LOG_INFO("xHCI", "Took ownership from BIOS");
                        }
                    }
                    break;
                }

                // Move to next capability
                QC::u8 next = (capVal >> 8) & 0xFF;
                if (next == 0)
                    break;
                cap = reinterpret_cast<volatile QC::u32 *>(
                    reinterpret_cast<QC::VirtAddr>(cap) + (next << 2));
            }
        }

        void XHCIControllerImpl::hardwareReset()
        {
            reset();
        }

        void XHCIControllerImpl::reset()
        {
            // Halt first
            m_opRegs->usbcmd &= ~CMD_RUN;
            for (int i = 0; i < 100 && !(m_opRegs->usbsts & STS_HCH); ++i)
            {
                for (int j = 0; j < 10000; ++j)
                    cpu_relax();
            }

            // Reset
            m_opRegs->usbcmd |= CMD_HCRST;
            for (int i = 0; i < 1000 && (m_opRegs->usbcmd & CMD_HCRST); ++i)
            {
                for (int j = 0; j < 10000; ++j)
                    cpu_relax();
            }

            // Wait for CNR to clear
            for (int i = 0; i < 1000 && (m_opRegs->usbsts & STS_CNR); ++i)
            {
                for (int j = 0; j < 10000; ++j)
                    cpu_relax();
            }

            QC_LOG_INFO("xHCI", "XHCIControllerImpl reset complete");
        }

        void XHCIControllerImpl::initializeDCBAA()
        {
            // Allocate DCBAA (Device Context Base Address Array)
            DMAPage dcbaaPage = allocateDMAPage();
            m_dcbaa = reinterpret_cast<QC::u64 *>(dcbaaPage.virt);
            QC::String::memset(m_dcbaa, 0, 4096);

            // Set DCBAAP
            m_opRegs->dcbaap = dcbaaPage.phys;

            QC_LOG_INFO("xHCI", "DCBAA at 0x%lx", dcbaaPage.phys);
        }

        void XHCIControllerImpl::initializeCommandRing()
        {
            DMAPage cmdRingPage = allocateDMAPage();
            m_commandRing = reinterpret_cast<TRB *>(cmdRingPage.virt);
            QC::String::memset(m_commandRing, 0, 4096);

            // Set up Link TRB at end of ring
            TRB &linkTrb = m_commandRing[RING_SIZE - 1];
            linkTrb.parameter = cmdRingPage.phys;
            linkTrb.status = 0;
            linkTrb.control = makeTRBControl(TRBType::Link, TRB_TC);

            m_commandEnqueue = 0;
            m_commandCycle = true;

            // Set CRCR (with cycle bit)
            m_opRegs->crcr = cmdRingPage.phys | 1;

            QC_LOG_INFO("xHCI", "Command ring at 0x%lx", cmdRingPage.phys);
        }

        void XHCIControllerImpl::initializeEventRing()
        {
            // Allocate event ring segment
            DMAPage eventRingPage = allocateDMAPage();
            m_eventRing = reinterpret_cast<TRB *>(eventRingPage.virt);
            QC::String::memset(m_eventRing, 0, 4096);

            // Allocate ERST (Event Ring Segment Table)
            DMAPage erstPage = allocateDMAPage();
            m_erst = reinterpret_cast<ERSTEntry *>(erstPage.virt);
            QC::String::memset(m_erst, 0, 4096);

            // Set up single segment entry
            m_erst[0].ringSegmentBase = eventRingPage.phys;
            m_erst[0].ringSegmentSize = RING_SIZE;

            m_eventDequeue = 0;
            m_eventCycle = true;

            // Configure interrupter 0
            m_interrupter->erstsz = 1;
            m_interrupter->erstba = erstPage.phys;
            m_interrupter->erdp = eventRingPage.phys;

            // Store event ring physical base for ERDP update
            m_eventRingPhys = eventRingPage.phys;

            QC_LOG_INFO("xHCI", "Event ring at 0x%lx", eventRingPage.phys);
        }

        void XHCIControllerImpl::initializeScratchpad()
        {
            // Check if scratchpad buffers are needed
            QC::u32 hcsParams2 = m_capRegs->hcsParams2;
            QC::u32 scratchHi = (hcsParams2 >> 21) & 0x1F;
            QC::u32 scratchLo = (hcsParams2 >> 27) & 0x1F;
            QC::u32 scratchCount = (scratchHi << 5) | scratchLo;

            if (scratchCount == 0)
                return;

            QC_LOG_INFO("xHCI", "Allocating %u scratchpad buffers", scratchCount);

            // Allocate scratchpad buffer array
            DMAPage arrayPage = allocateDMAPage();
            QC::u64 *array = reinterpret_cast<QC::u64 *>(arrayPage.virt);

            // Allocate each scratchpad buffer
            for (QC::u32 i = 0; i < scratchCount && i < 64; ++i)
            {
                DMAPage scratchPage = allocateDMAPage();
                array[i] = scratchPage.phys;
            }

            // Point DCBAA[0] to scratchpad array
            m_dcbaa[0] = arrayPage.phys;
        }

        void XHCIControllerImpl::ringCommandDoorbell()
        {
            m_doorbells[0] = 0; // Slot 0, target 0 = command ring
        }

        TRB *XHCIControllerImpl::enqueueCommand(const TRB &trb)
        {
            TRB *slot = &m_commandRing[m_commandEnqueue];

            // Copy TRB with correct cycle bit
            slot->parameter = trb.parameter;
            slot->status = trb.status;
            slot->control = trb.control | (m_commandCycle ? TRB_CYCLE : 0);

            // Advance enqueue pointer
            m_commandEnqueue++;
            if (m_commandEnqueue >= RING_SIZE - 1)
            {
                // Update link TRB cycle bit
                m_commandRing[RING_SIZE - 1].control =
                    (m_commandRing[RING_SIZE - 1].control & ~TRB_CYCLE) |
                    (m_commandCycle ? TRB_CYCLE : 0);
                m_commandEnqueue = 0;
                m_commandCycle = !m_commandCycle;
            }

            return slot;
        }

        bool XHCIControllerImpl::waitForCommand(QC::u32 timeoutMs)
        {
            m_commandPending = true;

            for (QC::u32 i = 0; i < timeoutMs * 10; ++i)
            {
                processEvents();
                if (!m_commandPending)
                {
                    return m_lastCompletionCode == CompletionCode::Success;
                }
                for (int j = 0; j < 1000; ++j)
                    cpu_relax();
            }

            QC_LOG_WARN("xHCI", "Command timeout");
            m_commandPending = false;
            return false;
        }

        bool XHCIControllerImpl::fetchHIDLogicalRanges(QC::u8 slotId, QC::u8 interfaceNumber,
                                                       QC::u16 reportLength, QC::u32 &logicalMaxX,
                                                       QC::u32 &logicalMaxY)
        {
            if (reportLength == 0)
            {
                return false;
            }

            static constexpr QC::u16 MaxReportDescriptor = 512;
            QC::u16 readLength = reportLength;
            if (readLength > MaxReportDescriptor)
            {
                QC_LOG_WARN("xHCI", "Truncating HID report descriptor from %u to %u bytes",
                            reportLength, MaxReportDescriptor);
                readLength = MaxReportDescriptor;
            }

            alignas(64) static QC::u8 reportBuffer[MaxReportDescriptor];
            QC::String::memset(reportBuffer, 0, readLength);

            if (!controlTransfer(slotId,
                                 0x81, // IN | Class | Interface
                                 USB_REQ_GET_DESCRIPTOR,
                                 static_cast<QC::u16>(USB_DESC_HID_REPORT << 8),
                                 interfaceNumber,
                                 reportBuffer,
                                 readLength))
            {
                QC_LOG_WARN("xHCI", "Failed to read HID report descriptor for slot %u", slotId);
                return false;
            }

            parseHIDLogicalRanges(reportBuffer, readLength, logicalMaxX, logicalMaxY);
            return true;
        }

        void XHCIControllerImpl::parseHIDLogicalRanges(const QC::u8 *descriptor, QC::u16 length,
                                                       QC::u32 &logicalMaxX, QC::u32 &logicalMaxY)
        {
            if (!descriptor || length == 0)
            {
                return;
            }

            QC::u32 usagePage = 0;
            QC::u32 currentLogicalMax = 0;

            for (QC::u16 idx = 0; idx < length;)
            {
                QC::u8 prefix = descriptor[idx++];

                if (prefix == 0xFE)
                {
                    if (idx + 1 >= length)
                        break;
                    QC::u8 size = descriptor[idx];
                    idx += static_cast<QC::u16>(size) + 2;
                    continue;
                }

                QC::u8 sizeCode = prefix & 0x3;
                QC::u8 dataSize = (sizeCode == 3) ? 4 : sizeCode;
                QC::u8 type = (prefix >> 2) & 0x3;
                QC::u8 tag = (prefix >> 4) & 0xF;

                if (idx + dataSize > length)
                {
                    break;
                }

                QC::u32 value = 0;
                for (QC::u8 i = 0; i < dataSize; ++i)
                {
                    value |= static_cast<QC::u32>(descriptor[idx + i]) << (8 * i);
                }
                idx += dataSize;

                if (type == 1)
                {
                    switch (tag)
                    {
                    case 0x0: // Usage Page
                        usagePage = value;
                        break;
                    case 0x2: // Logical Maximum
                        if (dataSize == 4)
                        {
                            currentLogicalMax = value;
                        }
                        else
                        {
                            QC::u32 mask = (dataSize == 0) ? 0 : ((1u << (dataSize * 8)) - 1);
                            currentLogicalMax = value & mask;
                        }
                        break;
                    default:
                        break;
                    }
                }
                else if (type == 2 && tag == 0x0)
                {
                    if (usagePage == 0x01)
                    {
                        if (value == 0x30 && currentLogicalMax)
                        {
                            logicalMaxX = currentLogicalMax;
                        }
                        else if (value == 0x31 && currentLogicalMax)
                        {
                            logicalMaxY = currentLogicalMax;
                        }
                    }

                    if (logicalMaxX && logicalMaxY)
                    {
                        break;
                    }
                }
            }
        }

        void XHCIControllerImpl::processEvents()
        {
            static QC::u32 evtCount = 0;
            while (true)
            {
                TRB &event = m_eventRing[m_eventDequeue];

                // Check cycle bit
                bool eventCycle = (event.control & TRB_CYCLE) != 0;
                if (eventCycle != m_eventCycle)
                    break; // No more events

                ++evtCount;
                TRBType type = getTRBType(event);
                if (evtCount <= 10 || evtCount % 1000 == 0)
                {
                    QC_LOG_INFO("xHCI", "Event #%u: type=%u", evtCount, static_cast<QC::u8>(type));
                }

                switch (type)
                {
                case TRBType::CommandComplete:
                    handleCommandComplete(event);
                    break;
                case TRBType::TransferEvent:
                    handleTransferEvent(event);
                    break;
                case TRBType::PortStatusChange:
                    handlePortStatusChange(event);
                    break;
                default:
                    QC_LOG_DEBUG("xHCI", "Unhandled event type %u", static_cast<QC::u8>(type));
                    break;
                }

                // Advance dequeue pointer
                m_eventDequeue++;
                if (m_eventDequeue >= RING_SIZE)
                {
                    m_eventDequeue = 0;
                    m_eventCycle = !m_eventCycle;
                }

                // Update ERDP (with EHB bit to clear busy)
                m_interrupter->erdp =
                    (m_eventRingPhys + m_eventDequeue * sizeof(TRB)) | (1 << 3);
            }
        }

        void XHCIControllerImpl::handleCommandComplete(const TRB &event)
        {
            m_lastCompletionCode = getCompletionCode(event);
            m_lastSlotId = getSlotId(event);
            m_commandPending = false;

            if (m_lastCompletionCode != CompletionCode::Success)
            {
                QC_LOG_WARN("xHCI", "Command completed with code %u",
                            static_cast<QC::u8>(m_lastCompletionCode));
            }
        }

        void XHCIControllerImpl::handleTransferEvent(const TRB &event)
        {
            QC::u8 slotId = getSlotId(event);
            QC::u8 epId = (event.control >> 16) & 0x1F;
            CompletionCode code = getCompletionCode(event);

            QC_LOG_DEBUG("xHCI", "Transfer event: slot=%u ep=%u code=%u", slotId, epId, static_cast<QC::u8>(code));

            // EP1 (DCI 1) is control endpoint 0 - signal completion for control transfers
            if (epId == 1 && m_transferPending)
            {
                m_transferCompletionCode = code;
                m_transferPending = false;
                return;
            }

            // Find device for interrupt transfers
            for (QC::usize i = 0; i < m_deviceCount; ++i)
            {
                DeviceInfo &dev = m_devices[i];
                if (dev.slotId == slotId && (dev.isTablet || dev.isMouse || dev.isKeyboard))
                {
                    if (code == CompletionCode::Success || code == CompletionCode::ShortPacket)
                    {
                        QC::u8 *data = dev.hidBuffer;

                        if (dev.isTablet)
                        {
                            // For QEMU tablet: buttons(1) + x(2) + y(2) + wheel(1)
                            if (data && m_tabletDriver)
                            {
                                QC::u8 buttons = data[0];
                                QC::u16 absX = data[1] | (data[2] << 8);
                                QC::u16 absY = data[3] | (data[4] << 8);

                                QC::u32 logicalMaxX = dev.logicalMaxX ? dev.logicalMaxX : 0x7FFF;
                                QC::u32 logicalMaxY = dev.logicalMaxY ? dev.logicalMaxY : 0x7FFF;

                                if (logicalMaxX == 0)
                                    logicalMaxX = 0x7FFF;
                                if (logicalMaxY == 0)
                                    logicalMaxY = 0x7FFF;

                                if (absX > logicalMaxX)
                                    absX = static_cast<QC::u16>(logicalMaxX);
                                if (absY > logicalMaxY)
                                    absY = static_cast<QC::u16>(logicalMaxY);

                                QC::i32 x = 0;
                                QC::i32 y = 0;
                                QC::u32 widthRange = (m_screenWidth > 0) ? (m_screenWidth - 1) : 0;
                                QC::u32 heightRange = (m_screenHeight > 0) ? (m_screenHeight - 1) : 0;

                                if (logicalMaxX && widthRange)
                                {
                                    x = static_cast<QC::i32>((static_cast<QC::u64>(absX) * widthRange) /
                                                             logicalMaxX);
                                }
                                if (logicalMaxY && heightRange)
                                {
                                    y = static_cast<QC::i32>((static_cast<QC::u64>(absY) * heightRange) /
                                                             logicalMaxY);
                                }

                                m_tabletDriver->updatePosition(x, y, buttons);
                            }
                        }
                        else if (dev.isMouse)
                        {
                            if (data && m_mouseDriver)
                            {
                                // Boot-protocol mouse: buttons + dx + dy (+ optional wheel)
                                const QC::u8 buttons = data[0] & 0x07;
                                const QC::i32 dx = static_cast<QC::i8>(data[1]);
                                const QC::i32 dy = static_cast<QC::i8>(data[2]);
                                QC::i32 wheel = 0;
                                if (dev.hidMaxPacket >= 4)
                                {
                                    wheel = static_cast<QC::i8>(data[3]);
                                }
                                m_mouseDriver->updateDelta(dx, dy, wheel, buttons);
                            }
                        }
                        else if (dev.isKeyboard)
                        {
                            if (data && m_keyboardDriver && slotId == m_keyboardSlot)
                            {
                                m_keyboardDriver->handleBootReport(data, dev.hidMaxPacket);
                            }
                        }
                    }

                    // Re-schedule interrupt transfer
                    scheduleInterruptIn(dev);
                    break;
                }
            }
        }

        void XHCIControllerImpl::handlePortStatusChange(const TRB &event)
        {
            QC::u8 port = ((event.parameter >> 24) & 0xFF) - 1;

            // Clear status change bits FIRST before doing anything else
            if (port < m_portCount)
            {
                QC::u32 portsc = m_portRegs[port].portsc;
                // Write 1 to clear CSC, PEC, WRC, OCC, PRC, PLC, CEC
                m_portRegs[port].portsc = portsc | 0x00FE0000;

                // Skip if already enumerating this port (prevents re-entrancy)
                if (m_portEnumerating[port])
                {
                    return;
                }

                // Check if we already have a device on this port
                bool alreadyEnumerated = false;
                for (QC::usize i = 0; i < m_deviceCount; ++i)
                {
                    if (m_devices[i].port == port)
                    {
                        alreadyEnumerated = true;
                        break;
                    }
                }

                // Only enumerate if device connected AND not already enumerated
                if ((portsc & PORTSC_CCS) && !alreadyEnumerated)
                {
                    QC_LOG_INFO("xHCI", "Port %u: new device connected", port);
                    // Don't enumerate here - probeDevices already handles initial enumeration
                }
            }
        }

        QC::u8 XHCIControllerImpl::enableSlot()
        {
            TRB cmd = {};
            cmd.control = makeTRBControl(TRBType::EnableSlot, 0);

            enqueueCommand(cmd);
            ringCommandDoorbell();

            if (waitForCommand())
            {
                QC_LOG_INFO("xHCI", "Slot %u enabled", m_lastSlotId);
                return m_lastSlotId;
            }
            return 0;
        }

        bool XHCIControllerImpl::addressDevice(QC::u8 slotId, QC::u8 port, Speed speed)
        {
            // Allocate device context
            DMAPage devCtxPage = allocateDMAPage();
            m_deviceContexts[slotId] = reinterpret_cast<QC::u8 *>(devCtxPage.virt);
            QC::String::memset(m_deviceContexts[slotId], 0, 4096);

            // Store in DCBAA
            m_dcbaa[slotId] = devCtxPage.phys;

            // Set up input context
            QC::String::memset(m_inputContext, 0, 4096);

            // Input Control Context
            InputControlContext *icc = reinterpret_cast<InputControlContext *>(m_inputContext);
            icc->addFlags = 0x3; // Add slot context (bit 0) and EP0 context (bit 1)

            // Slot Context
            SlotContext *slot = reinterpret_cast<SlotContext *>(m_inputContext + m_contextSize);
            slot->routeString = 0;
            slot->speed = static_cast<QC::u8>(speed);
            slot->contextEntries = 1; // Only EP0 for now
            slot->rootHubPort = port + 1;

            // Endpoint 0 Context (Control endpoint)
            EndpointContext *ep0 = reinterpret_cast<EndpointContext *>(
                m_inputContext + 2 * m_contextSize);
            ep0->epType = static_cast<QC::u8>(EndpointType::Control);
            ep0->maxPacketSize = (speed == Speed::Super) ? 512 : ((speed == Speed::Low) ? 8 : 64);
            ep0->maxBurstSize = 0;
            ep0->errorCount = 3;

            // Allocate transfer ring for EP0
            DMAPage ep0RingPage = allocateDMAPage();
            TRB *ep0Ring = reinterpret_cast<TRB *>(ep0RingPage.virt);
            QC::String::memset(ep0Ring, 0, 4096);

            // Store EP0 ring for this slot
            m_ep0Rings[slotId] = ep0Ring;
            m_ep0RingPhys[slotId] = ep0RingPage.phys;

            // Link TRB
            ep0Ring[RING_SIZE - 1].parameter = ep0RingPage.phys;
            ep0Ring[RING_SIZE - 1].control = makeTRBControl(TRBType::Link, TRB_TC);

            ep0->trDequeuePtr = ep0RingPage.phys | 1; // DCS = 1

            // Address Device command
            TRB cmd = {};
            cmd.parameter = m_inputContextPhys;
            cmd.control = makeTRBControl(TRBType::AddressDevice, 0) | (slotId << 24);

            enqueueCommand(cmd);
            ringCommandDoorbell();

            if (waitForCommand(500))
            {
                QC_LOG_INFO("xHCI", "Device addressed on slot %u", slotId);
                return true;
            }

            QC_LOG_ERROR("xHCI", "Failed to address device");
            return false;
        }

        bool XHCIControllerImpl::controlTransfer(QC::u8 slotId, QC::u8 reqType, QC::u8 request,
                                                 QC::u16 value, QC::u16 index, void *data, QC::u16 length)
        {
            TRB *ring = m_ep0Rings[slotId];
            if (!ring)
            {
                QC_LOG_ERROR("xHCI", "No EP0 ring for slot %u", slotId);
                return false;
            }

            QC::usize idx = m_ep0Enqueue[slotId];
            bool cycle = m_ep0Cycle[slotId];

            DMAPage dmaBuffer = allocateDMAPage();
            if (!dmaBuffer.virt)
            {
                QC_LOG_ERROR("xHCI", "Failed to allocate DMA buffer for control transfer");
                return false;
            }

            QC::String::memset(dmaBuffer.virt, 0, 4096);
            if (!(reqType & 0x80) && length > 0 && data)
            {
                QC::String::memcpy(dmaBuffer.virt, data, length);
            }

            auto advance = [&]()
            {
                idx++;
                if (idx >= RING_SIZE - 1)
                {
                    QC::u32 linkFlags = (ring[RING_SIZE - 1].control & ~TRB_CYCLE) |
                                        (cycle ? TRB_CYCLE : 0);
                    ring[RING_SIZE - 1].control = linkFlags;
                    idx = 0;
                    cycle = !cycle;
                }
            };

            QC::u64 setupData = 0;
            setupData |= static_cast<QC::u64>(reqType);
            setupData |= static_cast<QC::u64>(request) << 8;
            setupData |= static_cast<QC::u64>(value) << 16;
            setupData |= static_cast<QC::u64>(index) << 32;
            setupData |= static_cast<QC::u64>(length) << 48;

            ring[idx].parameter = setupData;
            ring[idx].status = 8;
            QC::u32 setupCtrl = makeTRBControl(TRBType::SetupStage, TRB_IDT | (cycle ? TRB_CYCLE : 0));
            if (length > 0)
            {
                setupCtrl |= (reqType & 0x80) ? (3u << 16) : (2u << 16);
            }
            ring[idx].control = setupCtrl;
            advance();

            if (length > 0)
            {
                ring[idx].parameter = dmaBuffer.phys;
                ring[idx].status = length;
                QC::u32 dataCtrl = makeTRBControl(TRBType::DataStage, (cycle ? TRB_CYCLE : 0));
                if (reqType & 0x80)
                {
                    dataCtrl |= TRB_DIR_IN;
                }
                ring[idx].control = dataCtrl;
                advance();
            }

            ring[idx].parameter = 0;
            ring[idx].status = 0;
            QC::u32 statusCtrl = makeTRBControl(TRBType::StatusStage, TRB_IOC | (cycle ? TRB_CYCLE : 0));
            if (length > 0 && !(reqType & 0x80))
            {
                statusCtrl |= TRB_DIR_IN;
            }
            ring[idx].control = statusCtrl;
            advance();

            m_ep0Enqueue[slotId] = idx;
            m_ep0Cycle[slotId] = cycle;

            m_transferPending = true;
            m_transferCompletionCode = CompletionCode::Invalid;

            ringDoorbell(slotId, 1);

            for (int i = 0; i < 5000 && m_transferPending; ++i)
            {
                processEvents();
                for (int j = 0; j < 1000; ++j)
                    cpu_relax();
            }

            if (m_transferPending)
            {
                QC_LOG_WARN("xHCI", "Control transfer timeout");
                m_transferPending = false;
                return false;
            }

            if (length > 0 && data && (reqType & 0x80))
            {
                QC::String::memcpy(data, dmaBuffer.virt, length);
            }

            bool success = (m_transferCompletionCode == CompletionCode::Success ||
                            m_transferCompletionCode == CompletionCode::ShortPacket);
            if (!success)
            {
                QC_LOG_WARN("xHCI", "Control transfer failed: %u",
                            static_cast<QC::u8>(m_transferCompletionCode));
            }
            return success;
        }

        bool XHCIControllerImpl::submitTransfer(DeviceInfo &dev, QC::u8 endpointId,
                                                QC::PhysAddr buffer, QC::u32 length,
                                                QC::u32 trbFlags)
        {
            if (!dev.transferRing)
            {
                QC_LOG_WARN("xHCI", "submitTransfer: no transfer ring for slot %u", dev.slotId);
                return false;
            }

            TRB *ring = dev.transferRing;
            QC::usize idx = dev.transferEnqueue;

            ring[idx].parameter = buffer;
            ring[idx].status = length;

            QC::u32 controlFlags = trbFlags & ~TRB_CYCLE;
            if (dev.transferCycle)
            {
                controlFlags |= TRB_CYCLE;
            }
            ring[idx].control = makeTRBControl(TRBType::Normal, controlFlags);

            dev.transferEnqueue++;
            if (dev.transferEnqueue >= RING_SIZE - 1)
            {
                QC::u32 linkFlags = ring[RING_SIZE - 1].control & ~TRB_CYCLE;
                if (dev.transferCycle)
                {
                    linkFlags |= TRB_CYCLE;
                }
                ring[RING_SIZE - 1].control = linkFlags;
                dev.transferEnqueue = 0;
                dev.transferCycle = !dev.transferCycle;
            }

            ringDoorbell(dev.slotId, endpointId);
            return true;
        }

        bool XHCIControllerImpl::setConfiguration(QC::u8 slotId, QC::u8 configValue)
        {
            return controlTransfer(slotId, 0x00, USB_REQ_SET_CONFIGURATION, configValue, 0, nullptr, 0);
        }

        bool XHCIControllerImpl::setHIDProtocol(QC::u8 slotId, QC::u8 interfaceNumber, QC::u16 protocol)
        {
            // SET_PROTOCOL request: bmRequestType=0x21, bRequest=0x0B
            // wValue: 0 = boot protocol, 1 = report protocol
            // wIndex: interface number
            return controlTransfer(slotId, 0x21, USB_REQ_SET_PROTOCOL, protocol, interfaceNumber, nullptr, 0);
        }

        bool XHCIControllerImpl::configureEndpoint(QC::u8 slotId, const USBEndpointDescriptor &ep)
        {
            // Determine endpoint index (DCI)
            QC::u8 epNum = ep.bEndpointAddress & 0x0F;
            bool isIn = (ep.bEndpointAddress & 0x80) != 0;
            QC::u8 dci = epNum * 2 + (isIn ? 1 : 0);

            // Set up input context
            QC::String::memset(m_inputContext, 0, 4096);

            InputControlContext *icc = reinterpret_cast<InputControlContext *>(m_inputContext);
            icc->addFlags = 1 | (1 << dci); // Add slot + endpoint

            // Copy slot context from device context
            SlotContext *slotIn = reinterpret_cast<SlotContext *>(m_inputContext + m_contextSize);
            SlotContext *slotDev = reinterpret_cast<SlotContext *>(m_deviceContexts[slotId]);
            *slotIn = *slotDev;
            slotIn->contextEntries = dci;

            // Set up endpoint context
            EndpointContext *epCtx = reinterpret_cast<EndpointContext *>(
                m_inputContext + (dci + 1) * m_contextSize);

            QC::u8 epType = (ep.bmAttributes & 0x03);
            if (epType == 3)
            { // Interrupt
                epCtx->epType = isIn ? static_cast<QC::u8>(EndpointType::InterruptIn)
                                     : static_cast<QC::u8>(EndpointType::InterruptOut);
            }
            else if (epType == 2)
            { // Bulk
                epCtx->epType = isIn ? static_cast<QC::u8>(EndpointType::BulkIn)
                                     : static_cast<QC::u8>(EndpointType::BulkOut);
            }

            epCtx->maxPacketSize = ep.wMaxPacketSize & 0x7FF;
            epCtx->interval = ep.bInterval;
            epCtx->errorCount = 3;
            epCtx->avgTRBLength = epCtx->maxPacketSize;

            // Allocate transfer ring
            DMAPage ringPage = allocateDMAPage();
            TRB *ring = reinterpret_cast<TRB *>(ringPage.virt);
            QC::String::memset(ring, 0, 4096);

            ring[RING_SIZE - 1].parameter = ringPage.phys;
            ring[RING_SIZE - 1].control = makeTRBControl(TRBType::Link, TRB_TC);

            epCtx->trDequeuePtr = ringPage.phys | 1;

            // Configure Endpoint command
            TRB cmd = {};
            cmd.parameter = m_inputContextPhys;
            cmd.control = makeTRBControl(TRBType::ConfigureEndpoint, 0) | (slotId << 24);

            enqueueCommand(cmd);
            ringCommandDoorbell();

            if (waitForCommand(500))
            {
                QC_LOG_INFO("xHCI", "Endpoint %u configured for slot %u", dci, slotId);

                // Store transfer ring info in device
                for (QC::usize i = 0; i < m_deviceCount; ++i)
                {
                    if (m_devices[i].slotId == slotId)
                    {
                        m_devices[i].transferRing = ring;
                        m_devices[i].transferRingPhys = ringPage.phys;
                        m_devices[i].transferEnqueue = 0;
                        m_devices[i].transferCycle = true;
                        m_devices[i].hidEndpoint = dci;
                        m_devices[i].hidMaxPacket = epCtx->maxPacketSize;
                        break;
                    }
                }
                return true;
            }

            return false;
        }

        bool XHCIControllerImpl::scheduleInterruptIn(DeviceInfo &dev)
        {
            if (!dev.transferRing)
            {
                QC_LOG_WARN("xHCI", "scheduleInterruptIn: no transfer ring for slot %u", dev.slotId);
                return false;
            }

            // Allocate DMA buffer for HID data if not already done
            if (!dev.hidBuffer)
            {
                DMAPage hidBufferPage = allocateDMAPage();
                if (!hidBufferPage.virt)
                    return false;
                QC::String::memset(hidBufferPage.virt, 0, 4096);
                dev.hidBuffer = reinterpret_cast<QC::u8 *>(hidBufferPage.virt);
                dev.hidBufferPhys = hidBufferPage.phys;
                QC_LOG_INFO("xHCI", "HID buffer: phys=0x%lx virt=%p", dev.hidBufferPhys, dev.hidBuffer);
            }

            // Clear the report buffer before each submission so short/partial writes can't
            // leave stale dx/dy data and cause phantom motion.
            if (dev.hidBuffer && dev.hidMaxPacket)
            {
                const QC::u32 clearLen = (dev.hidMaxPacket > 4096u) ? 4096u : dev.hidMaxPacket;
                QC::String::memset(dev.hidBuffer, 0, clearLen);
            }

            TRB *ring = dev.transferRing;
            QC::usize idx = dev.transferEnqueue;

            // Normal TRB for interrupt IN - use physical address for DMA
            ring[idx].parameter = dev.hidBufferPhys;
            ring[idx].status = dev.hidMaxPacket;
            ring[idx].control = makeTRBControl(TRBType::Normal, TRB_IOC | (dev.transferCycle ? TRB_CYCLE : 0));

            QC_LOG_DEBUG("xHCI", "Scheduled interrupt IN: slot=%u ep=%u idx=%lu", dev.slotId, dev.hidEndpoint, idx);

            // Advance
            dev.transferEnqueue++;
            if (dev.transferEnqueue >= RING_SIZE - 1)
            {
                ring[RING_SIZE - 1].control =
                    (ring[RING_SIZE - 1].control & ~TRB_CYCLE) |
                    (dev.transferCycle ? TRB_CYCLE : 0);
                dev.transferEnqueue = 0;
                dev.transferCycle = !dev.transferCycle;
            }

            // Ring doorbell
            ringDoorbell(dev.slotId, dev.hidEndpoint);

            return true;
        }

        void XHCIControllerImpl::probeDevices()
        {
            QC_LOG_INFO("xHCI", "Probing %u ports for devices", m_portCount);

            for (QC::u8 port = 0; port < m_portCount; ++port)
            {
                if (isPortConnected(port))
                {
                    enumerateDevice(port);
                }
            }
        }

        bool XHCIControllerImpl::enumerateDevice(QC::u8 port)
        {
            // Prevent re-entrancy - if already enumerating this port, skip
            if (port < 32 && m_portEnumerating[port])
            {
                return false;
            }

            // Mark port as being enumerated
            if (port < 32)
            {
                m_portEnumerating[port] = true;
            }

            QC_LOG_INFO("xHCI", "Enumerating device on port %u", port);

            // Reset port
            resetPort(port);

            // Get speed
            Speed speed = getPortSpeed(port);
            QC_LOG_INFO("xHCI", "Port %u speed: %u", port, static_cast<QC::u8>(speed));

            // Enable slot
            QC::u8 slotId = enableSlot();
            if (slotId == 0)
            {
                QC_LOG_ERROR("xHCI", "Failed to enable slot");
                if (port < 32)
                    m_portEnumerating[port] = false;
                return false;
            }

            // Address device
            if (!addressDevice(slotId, port, speed))
            {
                if (port < 32)
                    m_portEnumerating[port] = false;
                return false;
            }

            // Get device descriptor
            static USBDeviceDescriptor devDesc __attribute__((aligned(64)));
            if (!controlTransfer(slotId, 0x80, USB_REQ_GET_DESCRIPTOR,
                                 USB_DESC_DEVICE << 8, 0, &devDesc, sizeof(devDesc)))
            {
                QC_LOG_WARN("xHCI", "Failed to get device descriptor");
            }

            QC_LOG_INFO("xHCI", "Device: VID=%04x PID=%04x Class=%02x",
                        devDesc.idVendor, devDesc.idProduct, devDesc.bDeviceClass);

            // Get configuration descriptor
            static QC::u8 configData[256] __attribute__((aligned(64)));
            if (!controlTransfer(slotId, 0x80, USB_REQ_GET_DESCRIPTOR,
                                 USB_DESC_CONFIG << 8, 0, configData, sizeof(configData)))
            {
                QC_LOG_WARN("xHCI", "Failed to get config descriptor");
            }

            // Create device entry FIRST so identifyHID/configureEndpoint can find it
            if (m_deviceCount >= MAX_DEVICES)
            {
                if (port < 32)
                    m_portEnumerating[port] = false;
                return false;
            }

            DeviceInfo &dev = m_devices[m_deviceCount++];
            dev.slotId = slotId;
            dev.port = port;
            dev.speed = speed;
            dev.isHID = false;
            dev.isTablet = false;
            dev.isMouse = false;
            dev.isKeyboard = false;
            dev.transferRing = nullptr;
            dev.hidBuffer = nullptr;
            dev.hidBufferPhys = 0;
            dev.logicalMaxX = 0x7FFF;
            dev.logicalMaxY = 0x7FFF;

            // Look for HID interface (this will call configureEndpoint which updates dev)
            HIDDeviceKind hidKind = identifyHID(slotId, dev, configData, 256);
            dev.isHID = hidKind != HIDDeviceKind::None;
            dev.isTablet = hidKind == HIDDeviceKind::Tablet;
            dev.isMouse = hidKind == HIDDeviceKind::Mouse;
            dev.isKeyboard = hidKind == HIDDeviceKind::Keyboard;

            if (dev.isTablet)
            {
                m_tabletSlot = slotId;
                // Create tablet driver
                m_tabletDriver = new TabletDriver(this);
                m_tabletDriver->setBounds(0, 0, m_screenWidth - 1, m_screenHeight - 1);
                QC_LOG_INFO("xHCI", "USB tablet detected on slot %u", slotId);

                // Schedule first interrupt transfer (device is now in array with transferRing set)
                scheduleInterruptIn(dev);
            }
            else if (dev.isMouse)
            {
                m_mouseSlot = slotId;
                m_mouseDriver = new HIDMouseDriver(this);
                m_mouseDriver->setBounds(0, 0, m_screenWidth - 1, m_screenHeight - 1);
                QC_LOG_INFO("xHCI", "USB mouse detected on slot %u", slotId);

                scheduleInterruptIn(dev);
            }
            else if (dev.isKeyboard)
            {
                if (m_keyboardSlot == 0)
                {
                    m_keyboardSlot = slotId;
                    m_keyboardDriver = new HIDKeyboardDriver(this);
                    QC_LOG_INFO("xHCI", "USB boot keyboard detected on slot %u", slotId);
                    scheduleInterruptIn(dev);
                }
                else
                {
                    QC_LOG_WARN("xHCI", "Additional USB keyboard on slot %u ignored", slotId);
                }
            }

            // Clear enumeration flag
            if (port < 32)
                m_portEnumerating[port] = false;
            return true;
        }

        HIDDeviceKind XHCIControllerImpl::identifyHID(QC::u8 slotId, DeviceInfo &dev, const QC::u8 *configData, QC::u16 length)
        {
            USBConfigDescriptor *config = reinterpret_cast<USBConfigDescriptor *>(
                const_cast<QC::u8 *>(configData));

            if (config->bDescriptorType != USB_DESC_CONFIG)
                return HIDDeviceKind::None;

            QC::u16 totalLen = config->wTotalLength;
            if (totalLen > length)
                totalLen = length;

            // Parse descriptors
            QC::u16 offset = config->bLength;
            USBInterfaceDescriptor *hidIface = nullptr;
            USBEndpointDescriptor *hidEp = nullptr;
            USBHIDDescriptor *hidDesc = nullptr;

            while (offset + 2 <= totalLen)
            {
                QC::u8 descLen = configData[offset];
                QC::u8 descType = configData[offset + 1];

                if (descLen == 0)
                    break;

                if (descType == USB_DESC_INTERFACE)
                {
                    USBInterfaceDescriptor *iface = reinterpret_cast<USBInterfaceDescriptor *>(
                        const_cast<QC::u8 *>(&configData[offset]));

                    if (iface->bInterfaceClass == USB_CLASS_HID)
                    {
                        QC_LOG_INFO("xHCI", "Found HID interface: subclass=%u protocol=%u",
                                    iface->bInterfaceSubClass, iface->bInterfaceProtocol);
                        hidIface = iface;
                    }
                }
                else if (descType == USB_DESC_ENDPOINT && hidIface)
                {
                    USBEndpointDescriptor *ep = reinterpret_cast<USBEndpointDescriptor *>(
                        const_cast<QC::u8 *>(&configData[offset]));

                    // Look for interrupt IN endpoint
                    if ((ep->bmAttributes & 0x03) == 0x03 && (ep->bEndpointAddress & 0x80))
                    {
                        hidEp = ep;
                        QC_LOG_INFO("xHCI", "Found interrupt IN endpoint: addr=%02x maxPacket=%u",
                                    ep->bEndpointAddress, ep->wMaxPacketSize);
                    }
                }
                else if (descType == USB_DESC_HID && hidIface)
                {
                    hidDesc = reinterpret_cast<USBHIDDescriptor *>(
                        const_cast<QC::u8 *>(&configData[offset]));
                    QC_LOG_INFO("xHCI", "Found HID descriptor: reportLen=%u",
                                hidDesc->wDescriptorLength);
                }

                offset += descLen;
            }

            if (hidIface && hidEp)
            {
                const bool isBootMouse = (hidIface->bInterfaceSubClass == USB_SUBCLASS_BOOT) &&
                                         (hidIface->bInterfaceProtocol == USB_PROTOCOL_MOUSE);
                const bool isBootKeyboard = (hidIface->bInterfaceSubClass == USB_SUBCLASS_BOOT) &&
                                            (hidIface->bInterfaceProtocol == USB_PROTOCOL_KEYBOARD);

                if (hidDesc)
                {
                    QC::u32 logicalMaxX = dev.logicalMaxX;
                    QC::u32 logicalMaxY = dev.logicalMaxY;

                    // Only tablet-style absolute devices need X/Y logical ranges.
                    if (!isBootMouse && !isBootKeyboard)
                    {
                        if (fetchHIDLogicalRanges(slotId,
                                                  hidIface->bInterfaceNumber,
                                                  hidDesc->wDescriptorLength,
                                                  logicalMaxX,
                                                  logicalMaxY))
                        {
                            dev.logicalMaxX = logicalMaxX;
                            dev.logicalMaxY = logicalMaxY;
                            QC_LOG_INFO("xHCI", "HID logical range: X=%u Y=%u",
                                        dev.logicalMaxX, dev.logicalMaxY);
                        }
                        else
                        {
                            QC_LOG_WARN("xHCI", "Using default HID logical range");
                        }
                    }
                }

                // Set configuration
                setConfiguration(slotId, config->bConfigurationValue);

                // Ensure the device is in an expected protocol mode.
                // Boot protocol is only valid for boot mouse/keyboard; tablets/other HID pointers
                // should remain in report protocol.
                if (isBootMouse || isBootKeyboard)
                {
                    setHIDProtocol(slotId, hidIface->bInterfaceNumber, 0);
                }
                else
                {
                    setHIDProtocol(slotId, hidIface->bInterfaceNumber, 1);
                }

                // Configure the interrupt endpoint
                configureEndpoint(slotId, *hidEp);

                // Note: scheduleInterruptIn is called from enumerateDevice after device is added

                if (isBootKeyboard)
                {
                    return HIDDeviceKind::Keyboard;
                }

                return isBootMouse ? HIDDeviceKind::Mouse : HIDDeviceKind::Tablet;
            }

            return HIDDeviceKind::None;
        }

        void XHCIControllerImpl::poll()
        {
            static QC::u32 pollCount = 0;
            if (++pollCount % 1000 == 0)
            {
                QC_LOG_DEBUG("xHCI", "poll called %u times", pollCount);
            }
            processEvents();
        }

        void XHCIControllerImpl::setScreenSize(QC::u32 width, QC::u32 height)
        {
            m_screenWidth = width;
            m_screenHeight = height;
        }

        bool XHCIControllerImpl::submitTransfer(QC::u8 slotId,
                                                QC::u8 endpointId,
                                                QC::PhysAddr buffer,
                                                QC::u32 length,
                                                QC::u32 trbFlags)
        {
            for (QC::usize i = 0; i < m_deviceCount; ++i)
            {
                if (m_devices[i].slotId == slotId)
                {
                    return submitTransfer(m_devices[i], endpointId, buffer, length, trbFlags);
                }
            }
            return false;
        }

        bool XHCIControllerImpl::isPortConnected(QC::u8 port) const
        {
            if (port >= m_portCount)
                return false;
            return (m_portRegs[port].portsc & PORTSC_CCS) != 0;
        }

        Speed XHCIControllerImpl::getPortSpeed(QC::u8 port) const
        {
            if (port >= m_portCount)
                return Speed::Full;

            QC::u32 speed = (m_portRegs[port].portsc >> 10) & 0xF;
            switch (speed)
            {
            case 1:
                return Speed::Full;
            case 2:
                return Speed::Low;
            case 3:
                return Speed::High;
            case 4:
                return Speed::Super;
            case 5:
                return Speed::SuperPlus;
            default:
                return Speed::Full;
            }
        }

        void XHCIControllerImpl::resetPort(QC::u8 port)
        {
            if (port >= m_portCount)
                return;

            // Assert reset
            m_portRegs[port].portsc |= PORTSC_PR;

            // Wait for reset to complete
            for (int i = 0; i < 100; ++i)
            {
                if (!(m_portRegs[port].portsc & PORTSC_PR))
                    break;
                for (int j = 0; j < 10000; ++j)
                    cpu_relax();
            }

            // Clear status change bits
            m_portRegs[port].portsc |= PORTSC_PRC;
        }

        void XHCIControllerImpl::ringDoorbell(QC::u8 slot, QC::u8 target)
        {
            m_doorbells[slot] = target;
        }

    } // namespace XHCI
} // namespace QKDrv
