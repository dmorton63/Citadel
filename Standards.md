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

### Directories
- Use `PascalCase` for directory names.
- Use `ALLCAPS` for common initialisms/acronyms.
	- Examples: `ACPI`, `TPM`, `UI`, `PCI`.
- Avoid mixed conventions in the same layer (e.g., don’t mix `Acpi` and `ACPI`).

### Kernel Source Layout
- The `kernel/` executable target should follow the same naming rules as the libraries where practical.
- Kernel C/C++ source files should generally use the `QK*` prefix (matching the `QK` namespace/module) unless the file is a bootloader/linker artifact.
	- OK exceptions: `linker.ld`, `limine_boot.h`, early boot `.asm` stubs.
- Boot subdirectories under `kernel/Boot/` must follow the directory rules above.
	- Examples (target convention): `kernel/Boot/ACPI/`, `kernel/Boot/TPM/`.

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