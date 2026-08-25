# Citadel Permission Enforcement Matrix v0.1

## 1. Purpose

This matrix defines permission checks that must be enforced by loader, runtime wrappers, and kernel syscall handlers.

Normative inputs:
- docs/CITADEL_APP_PLATFORM_V0_1.md
- docs/CITADEL_SYSCALL_ABI_V0_1.md

## 2. Enforcement Points

Every protected operation should be checked at all three layers:
- Loader: validates requested permissions and issues capability token.
- Runtime: pre-checks calls for clear diagnostics.
- Kernel: authoritative enforcement on syscall execution.

## 3. Permission-to-Syscall Mapping

| Permission | Syscall IDs | Runtime Namespace |
|---|---|---|
| `fs.read` | `0x0401`, `0x0403`, `0x0406`, `0x0407` | `core.fs` |
| `fs.write` | `0x0401`, `0x0404`, `0x0405`, `0x0408`, `0x0409` | `core.fs` |
| `process.spawn` | `0x0103` | `core.process` |
| `net.client` | `0x0701`, `0x0702`, `0x0706`, `0x0707`, `0x0708` | `core.net` |
| `net.server` | `0x0701`, `0x0703`, `0x0704`, `0x0705`, `0x0708` | `core.net` |
| `ui.window` | `0x0801` to `0x0805`, `0x0901` to `0x0904` | `core.ui` |
| `device.input` | `0x0804` (input events) | `core.ui` |
| `security.inspect` | `0x0A01`, `0x0A02` | `core.security` |
| `security.attest` | `0x0A03` | `core.security` |

## 4. Default Policy Rules

- Undeclared permission: deny (`PLT_E_CAPABILITY_DENIED`).
- Declared but untrusted signature policy: deny launch.
- Unknown permission string: deny with manifest validation error.
- Runtime wrappers must never bypass kernel denial results.

## 5. Validation Checklist

### 5.1 Loader
- [ ] Manifest parser rejects malformed permission entries.
- [ ] Capability token only includes explicitly granted permissions.
- [ ] Launch is denied when signature policy requires trust and trust is missing.

### 5.2 Runtime
- [ ] Wrapper checks map operation to required permission.
- [ ] Wrapper returns consistent typed diagnostic on deny.
- [ ] Wrapper includes raw platform error code for logs.

### 5.3 Kernel
- [ ] Syscall dispatcher maps syscall ID to required permission.
- [ ] Dispatcher checks capability token before operation dispatch.
- [ ] Denied operations return `PLT_E_CAPABILITY_DENIED`.

## 6. Test Matrix

| Test ID | Scenario | Expected Result |
|---|---|---|
| `CAP-001` | App without `fs.read` calls `0x0403` File.Read | Denied with capability error |
| `CAP-002` | App with `fs.read` calls `0x0403` File.Read | Allowed |
| `CAP-003` | App without `process.spawn` calls `0x0103` Process.Spawn | Denied |
| `CAP-004` | App with `net.client` calls `0x0702` Net.Connect | Allowed |
| `CAP-005` | App without `ui.window` calls `0x0801` UI.WindowCreate | Denied |
| `CAP-006` | App with `ui.window` and `device.input` polls events | Allowed |
| `CAP-007` | App with malformed permission string in manifest | Launch rejected |
| `CAP-008` | Runtime wrapper pre-check deny path | Typed diagnostic + raw code |

## 7. Audit Logging Requirements

On every denial, log:
- appId
- syscall ID
- required permission
- decision (`deny`)
- source layer (`loader`, `runtime`, `kernel`)
- timestamp

## 8. Change Control

- Mapping changes require updates to both ABI docs and this matrix.
- New protected syscalls must define required permission before merge.
