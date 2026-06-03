# CITADEL — Current Working State (Single Source of Truth)

Maintained as the authoritative status snapshot for the repository.

This document is intended to be the one place to answer:
- What is working right now (and at what maturity level)
- What commands exist and what they do
- What hardware/devices are supported in practice
- What the video/desktop stack is
- What the parallel execution / queues / AI pieces are (current vs planned)
- What the security/access model is today
- What naming conventions are used
- Project timeline + authorship notes

Use this file when the question is "what is true today?"

Root documentation roles:
- `README.md` is the short entry point for build/run/use.
- `TODO_MAIN.md` is the subsystem roadmap and broader product backlog.
- `TODO_INBOX.md` is the active near-term stabilization queue.
- `docs/` contains subsystem specifications, plans, and reference material.

---

## 1. Authorship + Timeline
### Authorship
Citadel is the direct creation of its author.

Preferred attribution (stable):
- Primary Author / Owner: David K. Morton

### Timeline (Grounded)
- ~April 10, 2025 — Initial brainstorming begins under the early working name CopilotOS.
- ~April 2025 — Early prototypes and exploratory operating‑system work begin.
- 2026‑02‑09 — First verifiable commit in the public Git repository.
- 2026‑04‑01 — Latest commit observed during documentation generation.

**Note**
The April 2025 dates refer to private prototypes, scratch work, and early design experiments that predate the public repository.

### Origin Motivation
Citadel began as a research project born from fiction.
While writing a novel about a teenager who builds an operating system with a built‑in AI, the author realized that to portray such a system convincingly, he needed to understand operating‑system internals at a practical, implementable level. That curiosity evolved into a full engineering effort.
In the novel, the AI can:
- Modify its own code
- Monitor function calls and the data they use
- Return cached results when identical inputs reappear
- Avoid unnecessary CPU cycles by short‑circuiting known computations

**The challenge became:**
How close could a real operating system come to the fictional one?
Citadel is the answer.
And remarkably, many of the capabilities imagined for the fictional OS have now been achieved in the real implementation.

### Parallel Processing Philosophy (Design Goal)
Citadel adopts a parallel‑processing model inspired by GPU architecture — not just multitasking, but purpose‑assigned multicore orchestration.

This section describes the target architecture; implementation exists in parts today (Task_Flow concepts, queueing primitives, and memoization controls), but full “role assigned core orchestration” is still evolving.

**Core Concepts**
- CPU cores are assigned dedicated subsystem roles, such as:
  - Communications
  - Video
  - Interrupt processing
  - Message handling
  - Math/compute
  - High‑traffic I/O
- Additional cores (typically 3–6) are dedicated to task‑queue execution.

**Execution Model**
- Tasks are delivered as function pointers + data pointers.
- Each core processes tasks in timed execution slots categorized as:
  - Short
  - Medium
  - Long
- The scheduler and AI determine:
  - Which core receives which task
  - When execution occurs
  - When dependent processes require synchronized data availability

**Scalability**
More cores → more queues → more parallel throughput.

**Rationale**
By dedicating cores to specific high‑usage subsystems, Citadel achieves:
- Faster execution
- Reduced contention
- Higher determinism
- Greater trust in system behavior
This transforms a multicore CPU into a coordinated parallel engine, rather than a pool of general‑purpose workers.

---

## 1a. Minimum Hardware Validation (Pre‑Boot Gate)
Citadel performs a mandatory boot gate before the OS proceeds into the full runtime. This ensures Citadel only continues on systems that meet minimum security and stability requirements.

This is implemented in the boot path (see [kernel/Boot/QKBoot.cpp](kernel/Boot/QKBoot.cpp): `initializeBootPolicyAndGate()`).

### Purpose
The hardware gate exists to:
- Guarantee system stability
- Prevent undefined behavior on under‑powered machines
- Protect the integrity of Citadel’s parallel‑processing model
- Ensure the AI and scheduler have the resources they require
- Maintain predictable performance across all supported hardware
Citadel does not “attempt to run anyway.”
If the machine cannot support Citadel, the OS refuses to boot — intentionally and clearly.

### Validation Criteria
During the pre‑boot probe, Citadel evaluates (today):
- Total system RAM
- Minimum RAM threshold
- Gap (how far below the requirement the system is)
- CPU feature requirements (e.g., SSE2, NX, Long Mode)
- Policy/signature checks (e.g., boot config signature; desktop signature)

Additional checks may be added as Citadel evolves.
RAM is the primary gating factor today.

### Refusal Mode
If the system does not meet the minimum requirements, Citadel enters Refusal Mode, a controlled fallback environment designed to communicate the failure clearly and professionally.
Citadel displays:
- Required RAM
- Detected RAM
- Gap
- Followed by refusal messaging consistent with the boot path (e.g. "Will not run in this configuration!")
No stack traces.
No partial boot.
No undefined behavior.
Just a clean, intentional refusal.

### Boot Flow Integration
The hardware gate sits at a critical point in the boot pipeline:
- Kernel initialization
- Hardware probe
- Minimum hardware validation ← Gate occurs here
- If fail → Refusal Mode
- If pass → Continue boot (runtime registry, sysconfig, services, AI, etc.)
This ensures that every subsequent subsystem runs on a known‑good foundation.

---

## 2. Security + Access Control (What exists today)

### Roles (Access Levels)
Citadel currently uses role-based access gating for commands:
- `Everyone` (guest/kiosk style)
- `User`
- `Admin`
- `SysAdmin` (aka `su`)
- `System`

These roles are enforced by the command registry and (in the desktop terminal) UI context.

### Protected locations (filesystem policy)
To reduce foot-guns, write/delete operations are blocked for non-elevated roles:
- **/system**: modifications require **Admin+**
- **/PROD**: modifications require **SysAdmin+ (su)**

This protection is applied to file-modifying CLI commands (`touch`, `echo >`, `mkdir`, `rm`, `del`).

### UI Runtime permissions model (design + partial implementation)
The UI runtime is designed to apply multiple layers:
- user role
- terminal/window usage permissions
- per-command required level
- filesystem policy

See: [CITADEL_UI_RUNTIME.md](CITADEL_UI_RUNTIME.md).

### SecureStore + bootstrapping (machine anchor)
Citadel has a kernel-provided SecureStore used for protected persistence under `/system/sc`.

Current anchor behavior:
- TPM present: the machine anchor is sealed via TPM policy and stored as `WRAPKEY.TPM`.
- No TPM: boot prompts for a recovery code; a KDF-derived key unwraps `WRAPKEY.KDF` (legacy plaintext `WRAPKEY.BIN` is migrated away).

Key abstractions that exist in code today:
- `seal_secret()` / `unseal_secret()` (TPM-backed when available; stubbed fallback)
- TAS (TPM Anchor Secret): explicitly surfaced as `readTas()` / `getOrCreateTas()` and used as the input to SRK derivation
- TPM-accelerated sealed blobs: `writeTpmSealedBlob()` / `readTpmSealedBlob()` seal a per-blob content key via TPM and AEAD-wrap the payload (fallback to software sealing when TPM isn’t available)

---

## 3. What Works Today (Maturity Snapshot)

Status labels used below:
- **Working**: used in real runs / current workflow
- **Partial**: present and demonstrably doing something, but not “product complete”
- **Design/Stub**: concept present, scaffolding exists, not fully implemented

### Boot + Core
- Boot via Limine: **Working** (build produces bootable image)
- Kernel console: **Working** (basic CLI commands + transcript support)

### Storage + Filesystems
- VFS layer (`QFS::VFS`) + FAT probing/mounting: **Working**
- System volume `/system` FAT32 mount: **Working** in the known-good QEMU configs
- Shared folder `/shared` mount (host <-> guest): **Working** in the known-good QEMU configs
- Persistent system disk (`--system-vol` → `build/system.qcow2`): **Working** (used for `/system` persistence in dev)
- System disk formatting command `sysformat`: **Working** (formats FAT32 + attempts mount)

### Desktop / UI
- Windowing + controls + desktop shell: **Working/Partial** (actively used; still evolving)
- Current desktop/theme source path: **Hybrid/Partial** (external `.json` and `.cml` assets still exist as source/import material, but built-in themes and seeded desktop documents are already materialized into QCQL tables during desktop bring-up)
- Database-backed theme loading: **Partial** (`ThemeService::loadThemeFromDatabase(...)` is real, and desktop boot validates/imports built-in theme rows into QCQL `Themes` and `ThemeTokens` tables before applying them)
- Database-backed desktop document storage: **Partial** (desktop layout and CUI-ML payloads are seeded into QCQL document/chunk tables such as `DesktopLayouts`, `DesktopLayoutChunks`, `DesktopCuiml`, and `DesktopCuimlChunks`, but they are not yet modeled as a normalized relational desktop schema)
- Target desktop/theme source path: **CQL-backed/Planned** (Citadel is moving toward storing desktop design specifications, control layout, theme tokens, and related UI metadata in CQL/QCQL tables rather than treating external files as the long-term runtime format)
- Parser/import path: **Required/Planned** (the existing parser work still matters, but primarily as the ingest/bootstrap path that reads initial theme/layout assets and materializes them into database tables; subsequent boots should read from the CQL data model first, with file import/fallback reserved for provisioning, repair, or migration scenarios)
- Dependency note: **Desktop-data integration depends on CQL maturity** (before the desktop can fully move to a database-first runtime, CQL still needs more complete relational/schema support and stable service/runtime integration so desktop metadata can be modeled cleanly and loaded predictably at boot)

### CQL / Data Runtime
- QCQL engine core: **Partial** (implemented pieces include database create/open/close, table creation, page allocation/I/O, row serialization, primary-key index rebuild/lookup, and primary-key-based insert/select/update/remove helpers)
- QCQL system tables: **Partial** (`initializeSystemTables()` currently bootstraps `Themes`, `ThemeTokens`, and `Capabilities`; desktop code additionally creates/seeds document-style tables for layouts and CUI-ML payloads)
- Desktop/theme integration with QCQL: **Partial** (desktop bring-up creates/opens `/system/CMMS.QDB`, imports built-in themes, validates theme rows, and can load theme state from QCQL during normal desktop startup)
- QCQL runtime/query layer: **Partial/Design** (the low-level engine exists, but the broader text-query/parser/runtime-service shape described in the design docs is not fully represented in the `QCQL` module itself yet)
- Relational modeling and foreign-key behavior: **Planned** (current schemas are still simple and PK-centric; relationship metadata, foreign-key enforcement, and normalized desktop object modeling remain future work)
- Separate simple database path: **Working** (`QKSimpleDb` still exists as the generic key/value store used by the `db` command path, separate from QCQL)

### Networking
- IPv4 bring-up grade stack (DHCP, ARP, ICMP ping, DNS, TCP connect, HTTP GET diagnostics): **Working**

### Security Center (SC)
- SC concepts + hooks + flow controls: **Partial** (actively being built; evolving)
- Task_Flow metrics exposure to SC: **Working** (counters surfaced for diagnostics/policy)
- SRK derivation from the machine anchor (TAS): **Working** (used for SST wrapping boundary)

### “AI” / Task_Flow / Memoization
- Task_Flow concept + related commands/helpers: **Partial/Design** (exists in codebase + docs; ongoing)

---

## 4. Terminal Commands (What they do)

There are two user-facing “terminal surfaces”:
- Kernel console built-ins (minimal, always available)
- Desktop terminal (runs most commands through the shared Command Registry)

**Tip:** `help` is the canonical way to list what your current role is allowed to run.

### Core CLI
- `help [cmd]` — list commands / show help for one command
- `whoami` — print current role
- `echo ...` — print text; supports redirection:
  - `echo "text" > file` (overwrite/create)
  - `echo "text" >> file` (append)
- `pwd` — print working directory
- `cd <path>` — change working directory
- `ls [path]` — list directory
- `cat <path>` — print file
- `hexdump <path> [max_bytes]` — hex dump file

### Filesystem write/delete
- `touch <path>` — create empty file (no truncate)
- `mkdir <path>` — create directory (no `-p` yet)
- `rm <path>` — remove file or **empty** directory
- `rm -r <path>` — recursively remove directory tree
- `del <pattern> [pattern2 ...]` — delete **files** by wildcard pattern (skips directories)
  - examples: `del *.ext`, `del name.*`, `del *.*`, `del /shared/*.txt`

Protected-path policy applies:
- `/system` requires Admin+
- `/PROD` requires SysAdmin+

### System volume
- `sysdisks` (User) — list legacy IDE disks Citadel can currently see, including size and basic layout state
- `sysformat [diskN|N]` (Admin) — partition+format the selected detected disk as FAT32 and mount `/system`; without an argument it falls back to the first eligible detected disk
- `sysmount` (Admin) — probe+mount `/system` without formatting (useful after reboot or for recovery)

Dev workflow note: when running with `--system-vol`, `/system` is backed by `build/system.qcow2` and persists across reboots. You generally only need `sysformat` once; use `sysmount` if `/system` is not mounted.

Boot fallback note: when Security Center enforcement is active and `/system` is unavailable, Citadel now switches into `INSTALLER` mode instead of continuing toward the desktop. From there, use `help`, inspect detected disks with `sysdisks`, elevate with `admin enable` twice, then run `sysmount` or `sysformat`.

### Boot/config visibility
- `tier` — show active config tier + staged early modules
- `bootlog` (Admin) — dump captured boot log
- `bootmodules` (Admin) — dump early module trust metadata

### Networking
- `ip` / `ip set ...` / `ip dhcp [timeout_ms]` — view/set IPv4
- `arp [ip]` — show ARP cache / resolve entry
- `ping <ip|host> [timeout_ms]` — ICMP echo
- `nslookup <name> [timeout_ms]` — DNS A record
- `tcpconnect <ip|host> <port> [timeout_ms]` — TCP connect test
- `httpget <host> <path> [timeout_ms]` — minimal HTTP GET
- `tcplog` — dump recent TCP events

### Power
- `shutdown` (Admin) — request shutdown

### Desktop terminal local-only commands (UI-side)
The desktop terminal also implements some UI-local commands (not necessarily in the shared registry), such as transcript save.

---

## 5. Hardware / Device Support (Practical)

Citadel is used primarily under QEMU for bring-up; these are the common devices exercised.

### Input
- PS/2 keyboard/mouse: supported
- USB:
  - UHCI (USB 1.1) controller: supported
  - XHCI (USB 3.x) controller: supported

### Storage
- Legacy IDE / ATA PIO (for QEMU classic IDE paths): supported
- FAT filesystems (FAT16/FAT32): supported for `/system` and `/shared` style volumes

### Networking
- Intel E1000 NIC: supported (commonly used in QEMU)

### Trusted Platform Module (TPM)
- TPM2 emulation is used in development (via QEMU + swtpm).
- TPM-backed sealing exists for:
  - SecureStore anchor wrap key (`WRAPKEY.TPM`)
  - Generic secret sealing (`seal_secret()` / `unseal_secret()`)
  - TPM-accelerated sealed blobs (`writeTpmSealedBlob()` / `readTpmSealedBlob()`)

---

## 6. Video / Desktop Stack (High-Level)

- Framebuffer-based rendering (Limine-provided framebuffer / device-backed framebuffer)
- Window manager + compositor + cursor
- Controls library for widgets
- Desktop shell and system dialogs

Related modules (entry points / organization):
- [QWindowing/](QWindowing/)
- [QWControls/](QWControls/)
- [QDesktop/](QDesktop/)

---

## 7. Parallel Execution, Queues, and “AI”

### Today
- There is an operational kernel with scheduling primitives.
- The networking stack, eventing, and UI subsystems run in a cooperative/managed loop suitable for bring-up.

### Under active development
- Task_Flow execution engine (dependency-aware task graphs)
- Memoization / caching scaffolding (allowlist, cache controls)

Canonical design doc: [CITADEL_TASKFLOW.md](CITADEL_TASKFLOW.md).

---

## 8. Naming Conventions (Canonical)

Naming is consistent and intentional across modules, files, folders, and namespaces.

- Canonical reference: [Standards.md](Standards.md)
- Quick reference also exists in: [README.md](README.md)

High-level rules:
- Module prefixes: `QC*`, `QK*`, `QArch*`, `QDrv*`, `QKDrv*`, `QW*`, `QFS*`
- Directories: PascalCase
- Initialisms: ALLCAPS (`TPM`, `PCI`, `UI`, ...)

---

## 9. Pointers to “Ground Truth” Code Locations

- Shared command registry + most terminal commands: [QKernel/Src/QKCommandCenter.cpp](QKernel/Src/QKCommandCenter.cpp)
- Kernel console implementation (built-in CLI): [kernel/QKConsole.cpp](kernel/QKConsole.cpp)
- System disk formatter command registration: [kernel/QKSystemVolumeCommands.cpp](kernel/QKSystemVolumeCommands.cpp)
- UI runtime architecture (design doc): [CITADEL_UI_RUNTIME.md](CITADEL_UI_RUNTIME.md)
- Networking overview + command list: [README.md](README.md)

---

## 10. Maintenance Rules for This Document

- Keep claims grounded:
  - If something is aspirational, label it **Design/Stub**.
  - If something is used in the current QEMU workflow, label it **Working**.
- Prefer “where to verify” over long prose:
  - list the command name and what output to expect
  - point to the registration site / implementation file
