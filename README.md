# CITADEL Kernel

<img width="975" height="645" alt="qaiosDesktop" src="https://github.com/user-attachments/assets/31138a6a-9323-4621-8ea2-d8f82a41f69c" />

A modular x86-64 kernel with unified naming conventions, built with the Limine bootloader.

## A Snapshot of our desktop design as it is at the moment.

The screenshot above reflects the current desktop shell with the latest console-first flow.

## Project Structure

```
qaiosplus-ue-naming/
├── kernel/                  # Kernel executable target
│   ├── QKMain.cpp          # Entry point (kernel_main)
│   ├── QKBoot.asm          # Assembly bootstrap
│   ├── linker.ld           # Linker script
│   └── drivers/            # Hardware drivers (new structure)
│       ├── QKDrvBase.h     # Base driver interfaces
│       ├── QKDrvManager.*  # Driver probing/selection
│       ├── PS2/            # PS/2 keyboard/mouse drivers
│       ├── UHCI/           # USB 1.1 controller driver
│       └── XHCI/           # USB 3.0 controller driver
│
├── QCommon/                 # Common utilities library
│   ├── QCTypes.h           # Type definitions (u8, i32, PhysAddr, etc.)
│   ├── QCString.*          # String/memory operations
│   ├── QCLogger.*          # Logging system
│   └── QCVector.h          # Dynamic array template
│
├── QKernel/                 # Kernel services library
│   ├── QKInterrupts.*      # IDT, IRQ handlers, PIC
│   ├── QKScheduler.*       # Task scheduling
│   └── QKTaskManager.*     # Process management
│
├── QKMemory/                # Memory management
│   ├── QKMemPMM.*          # Physical memory manager
│   ├── QKMemVMM.*          # Virtual memory manager
│   ├── QKMemHeap.*         # Kernel heap allocator
│   └── QKMemTranslator.*   # Address translation/MMIO mapping
│
├── QArch/                   # Architecture-specific code
│   ├── QArchGDT.*          # Global Descriptor Table
│   ├── QArchIDT.*          # Interrupt Descriptor Table
│   ├── QArchPCI.*          # PCI bus enumeration
│   └── QArchPort.*         # I/O port operations
│
├── QEvent/                  # Event system
│   ├── QKEventManager.*    # Event dispatch
│   ├── QKEventQueue.*      # Event queue
│   └── QKEventListener.*   # Event handlers
│
├── QDrivers/                # Hardware drivers (Timer, BGA)
│   ├── QDrvTimer.*         # PIT/APIC timer
│   └── QDrvBGA.*           # Bochs Graphics Adapter
│
├── QWindowing/              # Windowing system
│   ├── QWWindowManager.*   # Window management
│   ├── QWWindow.*          # Window class
│   ├── QWFramebuffer.*     # Framebuffer access
│   └── QWCompositor.*      # Compositing/cursor
│
├── QWControls/              # UI controls
│   ├── Base/               # ControlBase plumbing
│   ├── Containers/         # Panel, Frame, Container
│   ├── Leaf/               # Button, Label, TextBox, ScrollBar
│   └── Composite/          # ComboBox, ListView, etc.
│
├── QFileSystem/             # File system (VFS, FAT32)
├── QNetwork/                # Networking (stub)
├── QPR/                     # Process runtime (stub)
└── QQuantum/                # Quantum computing (stub)
```

## Naming Conventions

### Modules
- `QC*` - **Q**AIOS **C**ommon (shared utilities)
- `QK*` - **Q**AIOS **K**ernel (kernel services)
- `QArch*` - **Q**AIOS **Arch**itecture (x86-64 specific)
- `QDrv*` - **Q**AIOS **Drv**ivers (hardware drivers)
- `QKDrv*` - **Q**AIOS **K**ernel **Drv**ivers (kernel-level input drivers)
- `QW*` - **Q**AIOS **W**indowing
- `QWControls/*` - **Q**AIOS **W**indowing control library (hierarchical folders)
- `QFS*` - **Q**AIOS **F**ile **S**ystem

### Files
- Headers: `Q<Module><Class>.h`
- Implementation: `Q<Module><Class>.cpp`
- Example: `QKDrvPS2Mouse.h`, `QKDrvPS2Mouse.cpp`

### Namespaces
```cpp
namespace QC { }       // Common
namespace QK { }       // Kernel
namespace QArch { }    // Architecture
namespace QDrv { }     // Drivers
namespace QKDrv { }    // Kernel drivers
namespace QW { }       // Windowing
```

## Driver Architecture

### Input Driver Hierarchy
```
QKDrv::DriverBase           # Base interface
├── QKDrv::MouseDriver      # Mouse interface (relative/absolute)
└── QKDrv::KeyboardDriver   # Keyboard interface

QKDrv::PS2::Mouse           # PS/2 mouse implementation
QKDrv::PS2::Keyboard        # PS/2 keyboard implementation
QKDrv::UHCI::Controller     # USB 1.1 controller
QKDrv::XHCI::XHCIController # USB 3.0 controller
```

### Driver Manager
The `QKDrv::Manager` probes for available controllers and selects the best driver:
1. **USB** (xHCI, then UHCI) - preferred for tablet/absolute positioning
2. **PS/2** - fallback for keyboard/mouse

## Building

First-time setup (required):

```bash
git submodule update --init --recursive
```

```bash
# Configure
cmake -B build -S .

# Build
cmake --build build

# Output: build/citadel.elf, build/citadel.bin
```

### Build Tasks (VS Code)
- **Build Kernel**: `cmake --build build`
- **Configure CMake**: `cmake -B build -S .`
- **Clean**: `rm -rf build`

## Running (QEMU)

```bash
# Basic run
qemu-system-x86_64 -kernel build/citadel.elf -serial stdio

# With USB mouse (recommended, matches real hardware)
qemu-system-x86_64 -kernel build/citadel.elf -serial stdio -usb -device usb-mouse

# Optional: USB tablet (absolute positioning)
qemu-system-x86_64 -kernel build/citadel.elf -serial stdio -usb -device usb-tablet

# Create ISO with Limine
./build.sh
qemu-system-x86_64 -cdrom build/citadel-limine.iso -serial stdio
```

### Shared host folder (QEMU)

- Create a directory named `shared/` at the repo root. The build script detects it automatically.
- `./build.sh -r` now passes `-drive file=fat:rw:shared,...` to QEMU, exposing the folder as an IDE FAT volume so host↔guest file drops stay in sync.
- Until the kernel grows an ATA/IDE block driver the extra disk remains invisible inside QAIOS++, but the QEMU plumbing is ready—once the driver lands we can mount it (e.g., `/share`) without touching the tooling again.

## Desktop JSON & Theming

The desktop shell loads [desktop.json](desktop.json) from the ramdisk (build.sh copies it to `/DESKTOP.JSN`). Everything under `desktop.theme` controls runtime styling.

### Theme loader

`theme` accepts either a string (path to a `.json` theme) or an object:

```json
"theme": {
	"base": "Vista",
	"file": "/system/themes/vista.json",
	"definition": { "colors": { /* inline Theme */ } },
	"overrides": { /* see below */ }
}
```

- `base`: builtin preset name understood by `QDTheme` (e.g., `Vista`, `DarkGlass`).
- `file` / `path`: filesystem location of a full theme definition; first one that loads wins.
- `definition`: inline `QDTheme` payload; alternatively, placing `colors`/`effects`/`animations` (or `base`) at the root of the `theme` object is also treated as a full definition.
- `overrides`: optional partial tweaks merged on top of the loaded theme.

Colors everywhere follow `#RRGGBB` or `#AARRGGBB`.

### Overrides schema

#### `palette`

| Key             | Description                                                            |
| --------------- | ---------------------------------------------------------------------- |
| `accent`        | Primary highlight color used for selection rings and accent buttons.   |
| `accentLight`   | Lighter hover state for accent/selection affordances.                  |
| `accentDark`    | Pressed/active accent fill and window title background.                |
| `text`          | Default control text color.                                            |
| `textSecondary` | Secondary text (sidebar, supporting labels).                           |
| `panel`         | Base panel fill (top bar, sidebar, taskbar, window body).              |
| `panelBorder`   | Panel/window border color; also seeds button borders if unset.         |

#### `metrics`

| Key                   | Type  | Description                                               |
| --------------------- | ----- | --------------------------------------------------------- |
| `cornerRadius`        | u32   | Window corner radius; doubles as button radius if specific button radius is not supplied. |
| `buttonCornerRadius`  | u32   | Button corner radius only.                                |
| `borderWidth`         | u32   | Outline width applied to windows and buttons.             |

#### `button`

Currently recognized roles are `sidebar`, `accent`, and `destructive`, which map to the same `QW::ButtonRole` entries used in code. Each role block supports:

| Field            | Type    | Description                                               |
| ---------------- | ------- | --------------------------------------------------------- |
| `fillNormal`     | color   | Idle fill.                                                |
| `fillHover`      | color   | Hover fill.                                               |
| `fillPressed`    | color   | Pressed fill.                                             |
| `text`           | color   | Foreground text/icon color.                               |
| `border`         | color   | Outline color.                                            |
| `glass`          | bool    | Enables glass highlight treatment for that role.          |
| `shineIntensity` | float   | 0.0–1.0 intensity controlling simulated specular highlights/overlays. |

#### `font`

| Key       | Type   | Description                                                                 |
| --------- | ------ | --------------------------------------------------------------------------- |
| `size`    | u32    | Base UI font size. `12` maps to 1.0× scaling; values are clamped to 1–255.  |
| `family`  | string | Optional hint recorded for future font switching (current renderer is fixed). |

#### `effects`

- **`border`**: `color`, `width`, `radius`. Updates both palette borders and metrics.
- **`shadow`**: `offsetX`, `offsetY` (signed), `blur` (u32), and `color`. Drives control/window drop shadows.
- **`glow`**: `radius`, `intensity` (0–255, converted to alpha), `color`. Applied to accent/destructive/sidebar-selected/taskbar-active roles.
- **`transparency`**: `windowOpacity` and `panelOpacity` (0–255). Governs background alpha for windows vs. chrome panels.

Any override flag activates `m_themeOverrides`, so a minimal palette tweak is enough to opt-in. Combine this with the layout controls in [desktop.json](desktop.json) to ship seasonal or branded desktops without rebuilding the kernel.

## Recent Changes (Feb 19, 2026)

See [backups/2026-02-19_backup_summary.md](backups/2026-02-19_backup_summary.md) for the detailed log.

- Restructured QC libraries into `QCCore/`, `QCMath/`, `QCSerialization/`, `QUICommon/`, `QCommand/` with `QCommon` kept as a compatibility umbrella target.
- Added a permanent `cpu_relax()` (x86 `pause`) routine in NASM and replaced deprecated `volatile` delay loops in UHCI/XHCI.
- Added `startup.cfg` support (packed into ramdisk) and optional QEMU host share plumbing.
- Continued desktop/theme work: transparency + font scaling overrides, painter text scaling, and a minimal terminal `ls` command.

## Recent Changes (Feb 9, 2026)

### Driver Refactoring
- Created unified driver structure under `kernel/drivers/`
- Implemented `QKDrv::Manager` for driver probing and selection
- Separated drivers by controller type: PS2, UHCI, XHCI
- Removed old `QUSB/` module (replaced by kernel/drivers/UHCI, XHCI)
- Removed old `drivers/usb/` C code
- Cleaned up `QDrivers/` to only keep Timer and BGA

### Input System
- PS/2 mouse/keyboard now use new `QKDrv::PS2::*` classes
- Mouse callback uses `QKDrv::MouseReport` (supports both relative and absolute)
- Keyboard uses `QKDrv::PS2::KeyEvent` with full modifier support
- Mouse cursor starts at screen center (not corner)

### Boot Fixes (earlier session)
- Fixed IDT selector (0x28 for Limine's 64-bit CS)
- Fixed IRQ stubs (dummy error code for consistency)
- Enabled IRQ2 cascade for slave PIC (mouse IRQ12)
- Fixed mouse axis inversion for VM compatibility

## Key Components

### Memory
- **Early heap**: 32MB static buffer before PMM
- **Early DMA**: 1MB identity-mapped buffer for USB
- **PMM**: Page frame allocator (4KB pages)
- **VMM**: Virtual memory with paging

### Events
- Event queue with type-based filtering
- Listener registration with category masks
- Mouse/keyboard events routed through `QK::Event::EventManager`

### Windowing
- Compositor with software cursor (12x16 arrow)
- Window manager with focus tracking
- Desktop window with shutdown button (Ctrl+Q or button click)

## License

MIT License

## Author

QAIOS Project
# qaiosplus

# Math library updates:
  Column-major and right-handed.
⭐ 1. Column‑major vs row‑major
You already chose column‑major Mat4f, which is perfect for:
• 	OpenGL‑style transforms
• 	GPU‑friendly memory layout
• 	Composing transforms as 
• 	Treating vectors as column vectors
This also matches the mental model of your compositor, where transforms flow down the scene graph.
If you stick with column‑major everywhere, you get:
• 	predictable multiplication order
• 	clean interoperability with future GPU APIs
• 	no mental gymnastics when composing transforms
I’d absolutely keep column‑major as the global convention.

⭐ 2. Right‑handed vs left‑handed
This is the big one.
Here’s the clean breakdown:
Right‑handed (RH)
• 	+Z comes out of the screen
• 	Used by OpenGL, Vulkan, most math texts
• 	Cross products behave intuitively
• 	Camera “lookAt” feels natural
Left‑handed (LH)
• 	+Z goes into the screen
• 	Used by DirectX, D3D11/12, Windows math libraries
• 	Some people prefer it for UI or 2D‑centric engines
Since Citadel is:
• 	column‑major
• 	compositor‑driven
• 	2D‑first with future 3D potential
• 	GPU‑accelerated
• 	architecturally closer to OpenGL/Vulkan than DirectX
…I strongly recommend right‑handed.
It keeps your math consistent with:
• 	cross products
• 	camera transforms
• 	rotation direction
• 	the majority of graphics literature
And it avoids the “why is my camera backwards” problem that LH systems often create