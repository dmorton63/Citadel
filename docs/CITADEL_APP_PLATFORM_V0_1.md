# Citadel App Platform v0.1

## 1. Purpose

This document defines the minimum application platform contract required to run CiteLang programs as native Citadel applications using a custom executable format.

Goals:
- Stable app launch contract for custom Citadel binaries.
- Stable syscall/API surface for file, process, UI, network, and time.
- Clear separation between language runtime APIs and kernel-facing ABI.
- Security model based on manifest capabilities.

## 2. Artifact Types

Citadel defines three primary app artifacts:
- Program image: `.cap` (Citadel Application Package)
- Manifest: `app.manifest.json`
- Optional symbol file: `.capsym`

A `.cap` may be a single binary image or a signed container with one primary image and resources.

## 3. CAP Binary Layout (MVP)

Byte order: little-endian.
Pointer width: 64-bit.

Header fields:
- Magic: `CAP0`
- Format version: `0x0001`
- ABI version: `0x0001`
- Flags: bitfield (PIE, signed, debug)
- Entry RVA: relative virtual address for app entry stub
- Segment table offset
- Import table offset
- Export table offset
- Resource table offset
- Signature block offset
- Image checksum

Required segments:
- `.text` executable code
- `.rodata` immutable data
- `.data` writable initialized data
- `.bss` writable zero-init data
- `.capmeta` metadata used by loader/runtime

## 4. Loader Contract

At launch, the Citadel loader must:
1. Verify magic, version, checksum, and signature policy.
2. Map segments with correct page permissions.
3. Resolve imports against kernel service table and shared runtime modules.
4. Build process context (argv, env, working dir, capability token).
5. Transfer control to entry stub.

Exit behavior:
- App returns `Int32` status code.
- Loader reports termination reason: normal, signal, fault, policy-denied.

## 5. Process and Thread ABI

Calling convention for v0.1:
- Architecture: x86_64
- Integer/pointer args: SysV register order
- Return: `rax`
- Stack alignment: 16 bytes before call

Runtime entry ABI:
- Entry symbol: `_citadel_app_entry`
- Signature:
  `Int32 _citadel_app_entry(AppContext* ctx)`

AppContext includes:
- `argc: Int32`
- `argv: Ptr<Ptr<Char>>`
- `envc: Int32`
- `envv: Ptr<Ptr<Char>>`
- `cwd: Ptr<Char>`
- `caps_handle: UInt64`
- `std_in`, `std_out`, `std_err` handles

## 6. System Call Surface (Kernel ABI)

Apps should call stdlib APIs first. Syscalls are for runtime and advanced native apps.

Normative syscall catalog:
- `docs/CITADEL_SYSCALL_ABI_V0_1.md` defines stable syscall IDs, signatures, and error contracts for v0.1.
- `QKernel/Include/QKSyscallABI.h` provides kernel-side C++ constants and request structs aligned to the syscall catalog.
- `QJFunctions/Include/QJFCitadelSyscalls.h` provides runtime wrapper declarations aligned to the syscall catalog.

Syscall families (MVP):
- `0x01` Process: spawn, exit, wait, sleep
- `0x02` Thread: create, join, yield
- `0x03` Memory: map, unmap, protect, shared map
- `0x04` FileSystem: open, close, read, write, seek, stat, list
- `0x05` Time: monotonic, realtime, timer create
- `0x06` IPC: channel create, send, recv
- `0x07` Net: socket, connect, bind, send, recv
- `0x08` UI: window create, destroy, show, event poll
- `0x09` Graphics: surface create, present, upload buffer
- `0x0A` Security: capability query, token introspection

Syscall return convention:
- Success: non-negative result
- Failure: negative error code (mapped to language/runtime diagnostics)

## 7. Capability and Security Model

Every app must declare permissions in `app.manifest.json`.

Manifest core fields:
- `appId`
- `version`
- `entry`
- `minPlatformVersion`
- `permissions`
- `signing`

Permission examples:
- `fs.read`
- `fs.write`
- `net.client`
- `net.server`
- `ui.window`
- `device.input`
- `process.spawn`

Policy rules:
- Deny-by-default for undeclared permissions.
- Loader grants runtime capability token from manifest + signature trust.
- Runtime and kernel enforce token on each protected operation.

Permission enforcement matrix:
- `docs/CITADEL_PERMISSION_ENFORCEMENT_MATRIX_V0_1.md` defines required permission checks by syscall and validation checklist coverage.

## 8. Language Runtime API Mapping

CiteLang standard library maps to kernel ABI through runtime adapters:

- `core.fs.*` -> FileSystem family
- `core.time.*` -> Time family
- `core.net.*` -> Net family
- `core.process.*` -> Process/Thread families
- `core.ui.*` -> UI/Graphics families
- `core.security.*` -> Security family
- `core.registry.*` -> planned Registry family (see Registry v0.1 spec)

Registry service specification:
- `docs/CITADEL_REGISTRY_V0_1.md` defines DB-backed registry model, key hierarchy, permissions, and SecureStore boundary.

Rule:
- CiteLang source does not use raw syscall IDs in normal application code.
- Raw syscalls may be allowed under an explicit unsafe FFI mode.

## 9. FFI Contract (MVP)

Purpose:
- Call native Citadel shared modules and low-level kernel shims.

Draft syntax:

```citelang
ffi mod citadel.kernel {
    public func cap_open(path: String, mode: Int32) -> Int32;
}
```

FFI constraints:
- FFI signatures must use ABI-stable primitive and pointer-compatible types.
- Nullable and collection types are not passed directly across FFI boundary in v0.1.
- Runtime performs signature validation at load time.

## 10. Error and Fault Model

Platform-level error classes:
- `PLT_LOAD_*` for loader failures
- `PLT_ABI_*` for ABI mismatch failures
- `PLT_CAP_*` for permission denials
- `PLT_IO_*` for filesystem/device errors

Critical faults:
- Invalid memory access: process terminated with fault status.
- Illegal syscall family/id: `PLT_ABI_BAD_SYSCALL`.
- Signature policy violation: launch denied.

## 11. Toolchain Contract

Compiler pipeline for CiteLang target `citadel-x64`:
1. Parse + type check `.cl`
2. Lower to IR
3. Generate object code
4. Link against Citadel runtime import library
5. Emit `.cap` + `app.manifest.json`
6. Sign package (optional in dev, required in secure mode)

Suggested CLI shape:
- `citlc build app.cl --target citadel-x64`
- `citlc run app.cl`
- `citlc package app.cl --manifest app.manifest.json`

## 12. MVP Compatibility Policy

- Platform v0.1 ABI is frozen for patch releases.
- New syscalls may be added in minor versions, never renumbered.
- CAP header major change requires new magic or incompatible version bump.

## 13. MVP Implementation Order

1. CAP loader validation + segment mapping.
2. Process start ABI and `AppContext` handoff.
3. FileSystem and Time syscall families.
4. Runtime adapters for `core.fs` and `core.time`.
5. Manifest permission checks.
6. UI family bring-up for windowed applications.
7. Network and IPC families.

## 14. Open Questions

- Should `.cap` be a raw mapped image or a compressed signed container by default?
- Should UI be a separate permission from graphics surface access?
- Should FFI require explicit per-symbol capability annotations?
- Do we need a wasm-like sandbox target in parallel with native ABI?
