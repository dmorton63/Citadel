Citadel OS – Architecture Brainstorm Log 
Date: 2026‑02‑25
Time: 20:38 CST
Author: David & Copilot
Session: Core System Architecture – Registry, Config, Recovery

---

## Roadmap status (as of 2026-02-28)

1. [x] Boot minimum spec gate (RAM + CPU features) with Refusal Mode messaging
2. [x] Reduced Memory Mode threshold + log message (recommended vs minimum)
3. [x] Load runtime boot policy from Limine ramdisk module (`BOOT.JSN`)
4. [x] Boot policy signature verification (TPM-backed RSA-2048 RSASSA(SHA256))
5. [x] Early measurement (PCR extends): cmdline, kernel image, module list, `BOOT.JSN`, `BOOT.SIG`
6. [x] Development vs production enforcement posture (build-time `CITADEL_PRODUCTION`)
7. [x] Production build fail-fast if `BOOT.JSN` / `BOOT.SIG` cannot be packaged correctly
8. [x] Define + implement `sysconfig.json` as a signed/measured index of early config modules (drives early module load/validation)
9. [x] Implement two-tier config selection + fallback (current: both tiers live on the Limine ramdisk as 8.3 FAT32 roots `/PROD` and `/GOLDEN`; policy: `/GOLDEN` stays on ramdisk; `/PROD` stays on ramdisk for now)
10. [ ] Implement recovery logic beyond selection: validate production → fallback to golden → rebuild runtime registries
11. [ ] Define/implement runtime registries (Process/Service/Window/Resource/Security) data model + ownership rules
12. [ ] TPM sealing for golden-config hashes (and unseal policy expectations)
13. [ ] Measurement/event log format (structured boot measurement log beyond “PCR extended” strings)
14. [ ] Reduced-memory notices integrated with splash/UX + persistent logging target (e.g. `/system/logs/boot/memory_profile.log`)
15. [ ] Per-app registry/config system (JSON-backed; Citadel-native) + merge semantics
16. [ ] Unified sandbox profiles (Citadel) + app launch flow integration
17. [x] Apply desktop overrides at runtime (optional `DESKOVR.JSN` merged onto golden desktop)
18. [x] Boot log capture + `bootlog` terminal command (text ring buffer; viewable during boot + callable from QDTerminal)
19. [ ] Windowing UX rules: desktop always-bottom + non-focusable; add proper window chrome (Terminal first) with minimize/maximize

### Implementation readiness (note: checkboxes above are implementation status)

As of 2026-03-02, items #10–#16 have MVP implementation notes drafted in this document, but are not implemented yet.

Ready to implement now (works with current ramdisk-only `/PROD` + `/GOLDEN`)
- #10 Recovery validation + deterministic rebuild of runtime registries
- #11 Runtime registry structs + ownership/mutation rules (kernel-only)
- #13 Structured boot event log (ring buffer + serial one-line form)
- #14 Reduced-memory splash notice + emit structured event (persistence is optional)

Blocked/partially blocked on “writable persistent storage exists”
- #12 TPM sealing persistence (you can compute/log/measure the digest now; sealing needs somewhere to store the blob)
- #14 Persistent log target path (can buffer + flush later)

Incremental/parallelizable (core merge logic can land early)
- #15 JSON layering + merge engine + validation can be implemented before full app-launch plumbing exists
- #16 Sandbox profile selection + staging/commit config build can be implemented before all per-app policies exist

---

## Desktop windowing UX notes (2026-03-05)

Goal: make desktop clicks not “hide” windows, and align terminal behavior with standard window systems.

### Desktop window rules

Desired semantics (conceptual; maps to WindowRegistry/WindowManager flags):
- `window->isDesktop = true;`
- `window->acceptsFocus = false;`
- `window->zIndex = 0; // always bottom`

Effect: clicking the desktop should NOT reorder the desktop above other windows. It should remain the background surface.

### QDTerminal chrome + minimize/maximize

- Add a top window label bar (title bar) for QDTerminal.
- Title bar contains: window title + Close + Minimize + Maximize buttons.
- Maximize: grows window to occupy the entire screen.
- Minimize: hides window and places a button/icon on the bottom task bar.
- Restore: clicking the taskbar entry restores the window to its default size or last-known size.

Note: once the desktop is always-bottom/non-focusable, the "Terminal" taskbar button should only be needed for minimized state (not as a permanent duplicate of the sidebar launcher).

---
## Sample sysconfig.json (info only; contents TBD)
Note: stored on the ramdisk FAT32 image as `SYSCFG.JSN` (8.3) with `SYSCFG.SIG`, verified and measured in early boot (no LFN yet).
Current implementation uses ramdisk roots (`production.root` = `/PROD`, `golden.root` = `/GOLDEN`) and resolves relative module paths under the selected tier; the sample below illustrates the longer-term persistent-storage layout.
```
json
{
  "version": 1,
  "profile": "desktop",
  "golden": {
    "root": "/system/config/golden",
    "hash": "sha256:abcd1234..."
  },
  "production": {
    "root": "/system/config/production",
    "hash": "sha256:ef567890..."
  },
  "modules": [
    {
      "id": "services",
      "path": "/system/config/production/services.json",
      "type": "service-index",
      "required": true
    },
    {
      "id": "desktop",
      "path": "/system/config/production/desktop.json",
      "type": "desktop-profile",
      "required": false
    },
    {
      "id": "security",
      "path": "/system/config/production/security.json",
      "type": "security-policy",
      "required": true
    },
    {
      "id": "apps",
      "path": "/system/config/production/apps.json",
      "type": "app-index",
      "required": false
    }
  ],
  "boot": {
    "early": ["security", "services"]
  }
}
```

Sysconfig definition checklist (turning item #8 into concrete steps):

- [x] Decide how `sysconfig.json` is located at boot (ramdisk file/module: `SYSCFG.JSN`)
- [x] Decide what is measured/signed: signed+measured `SYSCFG.JSN`/`SYSCFG.SIG` (modules are validated; per-module signing is a later enhancement)
- [x] Define module type contracts (early module IDs/types validated via allowlist)
- [x] Implement a minimal loader: read `sysconfig.json` → load ordered early modules → JSON-parse + allowlist-validate

---

1. Citadel’s Registry Model (Breakthrough)
We established that Citadel does not use a monolithic registry like Windows.
Instead, Citadel uses multiple focused registries, each with a clear purpose:
Runtime Registries (in protected memory)
- Process Registry – running processes, IDs, states, roles
- Service Registry – system services, capabilities, dependencies
- Window Registry – windows, handles, focus, z‑order
- Resource Registry – textures, models, fonts, assets
- Security Registry – TPM state, attestation, trust levels
These are in‑memory, expandable, and owned by the kernel.
They are not files.
They are not user‑accessible.
They are not monolithic.
This is the architectural leap Windows never made.

### MVP data model + ownership rules (Roadmap item #11)

Design intent: registries are kernel-owned runtime truth. JSON modules seed policy/defaults, but registries are not “a reflection of config files”.

Common rules (all registries)
- Ownership: kernel owns all registry memory.
- Mutation: only privileged kernel subsystems (or a trusted system service via narrow syscalls) may propose changes.
- Atomicity: boot-time config applies via staging + commit; no partial state in live registries.
- Identity: every object has a stable ID within a boot session (opaque handle or monotonic integer).
- Lifetime: explicit create/update/destroy; stale handles fail safely.

Registry definitions (MVP)
- Process Registry: keyed by `pid`; tracks state, parent, image identity, sandbox id, capabilities.
- Service Registry: keyed by `service_id`; tracks desired state, deps, capabilities, entrypoint.
- Window Registry: keyed by `window_id`; tracks owner pid, z-order/focus, bounds, flags.
- Resource Registry: keyed by `resource_id`; tracks type, owner, refcount, residency.
- Security Registry: global posture + policy objects; TPM availability, enforcement mode, measured artifacts summary.

2. Persistent Configuration (JSON‑based)
Citadel’s persistent configuration is modular, human‑readable, and versionable.
Key principles
- No single “hive” file
- No monolithic blob
- No regedit‑style exposure
- Everything is split into purpose‑specific JSON files
Examples
- sysconfig.json – top‑level index
- boot.json – boot sequence
- services/*.json – one file per service
- desktop/*.json – layout, themes, shell
- security/*.json – policies, TPM config
- apps/*.json – app manifests
sysconfig.json is not the whole registry — it’s an index pointing to the rest.

3. Two‑Tier Config System (Production + Golden Copy)
We defined a dual‑config model for resilience:
A. Production Config
- Active configuration
- Writable only by system tools
- Validated and signed
- Lives in a protected folder
B. Golden Config (Immutable)
- Stored in a hidden, read‑only “file safe”
- Only installer or trusted updater can modify
- TPM‑measured for integrity
- Used as fallback if production config is corrupted

Current policy (now that two-tier selection exists):
- `/GOLDEN` remains in the ramdisk as the immutable baseline.
- `/PROD` remains in the ramdisk for now; a future disk-backed `/PROD` is an option once persistent storage + validation/mount semantics exist.
- Where `/PROD` lives determines recovery semantics (ramdisk-only updates vs disk validation + fallback).

Recovery Logic
- Load production config
- If corrupted → load golden config
- Rebuild runtime registry
- Continue booting
This gives Citadel self‑healing, something Windows never achieved.

### MVP implementation notes (Roadmap item #10)

Goal: make `/PROD` safe to use without risking partial/invalid state.

Recovery algorithm (high level)
- Attempt `/PROD` first; if it fails validation, fall back to `/GOLDEN`.
- Validate selected tier in a staging area (do not mutate live registries during validation):
  - open/read succeeds for all required modules
  - JSON parses successfully
  - module `type` allowlist + module `id` allowlist
  - required fields exist with correct types
  - optional: signature/measurement checks (when enabled)
- On `/PROD` failure: emit one clear log line with the first failure reason; discard staging; retry with `/GOLDEN`.
- On success: commit by rebuilding runtime registries from staged config (deterministic reset + repopulate).

Definition of “corrupted” (MVP)
- missing file, read error, invalid size bounds
- JSON parse failure
- invalid schema/type for any required module

Acceptance (MVP)
- Malformed or missing required `/PROD/*` module triggers fallback to `/GOLDEN`.
- No registry contains partially-applied `/PROD` state after fallback.
- Serial log reports selected tier and fallback reason.

4. Boot Sequence and Minimum Spec Gate
Citadel OS – Minimum Hardware Gate Behavior
Boot‑Time Hardware Validation & Refusal Mode
Overview
During early boot, Citadel performs a minimum hardware capability check before loading any high‑level services, registries, or desktop components. This ensures the system only runs on hardware that meets the baseline requirements for stability, performance, and security.
If the hardware does not meet minimum requirements, Citadel enters Refusal Mode — a graceful, intentional fallback that informs the user and halts the boot process safely.

## SEE SECTION II addenims - Below

Boot‑Time Hardware Validation Sequence
- Kernel Initialization
- CPU mode setup
- Memory map acquisition
- Basic device enumeration
- Hardware Probe
- Total RAM
- CPU feature flags
- GPU capability (if required)
- Optional: TPM presence
- Minimum Spec Comparison
- Compare detected hardware against Citadel’s minimum requirements
- Requirements are defined in boot.json and validated at runtime
- Important: boot.json can tighten requirements, but must never loosen the compiled‑in safety/security floors (e.g., absolute RAM minimum, required CPU mode/features, and any build‑flavor security minimums)
- Decision Point
- If hardware meets or exceeds requirements:
Continue normal boot → load runtime registry → load services → load desktop
- If hardware fails requirements:
Enter Refusal Mode
## SEE SECTION II addenims - Below
Refusal Mode (Graceful Failure Path)
When Citadel determines the hardware is insufficient, it switches to a minimal graphics path and displays a clear, friendly, and informative message.
Displayed Information
- Required RAM:
The minimum RAM Citadel needs to operate safely
- Detected RAM:
The actual RAM found during hardware probe
- Tolerance Gap:
How much additional RAM is needed to meet minimum spec
User‑Facing Message
After reporting the above values, Citadel displays:
“Will not run in this configuration!  This machine does not meet the hardware requirements as listed above.”

This message is intentionally light‑hearted but firm — Citadel knows its limits and refuses to run in an unstable environment.

Design Goals of Refusal Mode
- Clarity: Users understand why Citadel won’t run
- Safety: Prevents undefined behavior on underpowered hardware
- Professionalism: No crashes, no cryptic errors, no stack traces
- Identity: The refusal message reinforces Citadel’s personality and standards
- Predictability: The system halts cleanly and safely

Notes
- Refusal Mode uses the simplest possible graphics path to ensure it can run even on minimal hardware.
- No services, registries, or desktop components are loaded during this mode.
- This behavior is part of Citadel’s commitment to intentional failure, not chaotic failure


5. Runtime Registry Expandability
The runtime registry must be:
- dynamic (services can register/unregister)
- extensible (new fields/types can be added)
- version‑tolerant
- protected (kernel‑only write access)
Expansion of the runtime registry does not mean a single config file grows uncontrollably.
Instead, new components simply add new JSON modules.

6. TPM Integration (High‑Level)
TPM is used for:
- attestation
- integrity checks
- sealing hashes of golden config
- verifying system state at boot
TPM is not used to store the entire registry — only critical trust anchors.

TPM policy model (boot + SKU)
- The kernel contains a compiled‑in minimum security posture (the floor).
- boot.json / boot profile may tighten (e.g., “Secure Mode requires TPM”), but must never weaken the compiled floor.
- TPM‑dependent features should activate only when TPM is present and the selected profile requires/permits them.

### MVP sealing plan (Roadmap item #12)

Goal: make `/GOLDEN` integrity tamper-evident across boots by sealing a known-good digest set to the TPM.

What is sealed (MVP)
- A single `golden_manifest_digest = sha256(SYSCFG.JSN + required module digests + ordering)`.
- The manifest digest is derived deterministically (stable module ordering; canonical serialization rules if needed).

Where it lives
- A TPM-sealed blob stored on disk/firmware-backed storage when available.
- Until persistent storage exists, treat this as “design-ready”: you can still compute and log the manifest digest and extend PCRs, but sealing cannot persist across reboots without somewhere to store the blob.

Unseal policy expectations
- The sealed blob unseals only when:
  - boot measurements match expected PCR policy (kernel + cmdline + module list + boot policy)
  - the runtime recomputed `golden_manifest_digest` matches what was sealed
- If unseal fails:
  - production build: fail closed (Refusal Mode or controlled halt)
  - development build: log loudly and continue (policy choice), but never “silently trust”

Acceptance (MVP)
- If any `/GOLDEN/*` required config changes, unseal fails (or digest mismatch is logged before seal exists).
- If kernel/boot policy measurement changes, unseal fails per PCR policy.

### Structured measurement/event log (Roadmap item #13)

Goal: replace ad-hoc “PCR extended …” strings with machine-parseable boot events.

Event record (MVP fields)
- `seq`: monotonic event index
- `t_ms`: milliseconds since boot (or since logger init)
- `stage`: `early|bootpolicy|sysconfig|services|desktop`
- `type`: e.g. `pcr_extend`, `config_select`, `config_validate_fail`, `fallback_to_golden`, `seal_attempt`, `unseal_ok`, `unseal_fail`
- `details`: small object with relevant keys (e.g. `pcr`, `algo`, `digest`, `path`, `reason`)

Encoding/storage (MVP)
- Keep an in-memory ring buffer of events.
- Always emit a compact one-line text form to serial.
- When a writable filesystem exists, flush as JSONL to a stable path (later: `/system/logs/boot/measurements.jsonl`).

Acceptance (MVP)
- A boot with `/PROD` failure produces: `config_select(PROD)` → `config_validate_fail(...)` → `fallback_to_golden` → `config_select(GOLDEN)`.
- Every PCR extend event includes `pcr`, `algo`, and `digest`.

7. Citadel’s Philosophy (Emerging)
Citadel is shaping into a system that is:
- modular
- resilient
- self‑healing
- declarative
- secure
- modern
- free of legacy baggage

## Section II - addendums.  Installing on systesm with reduced memory configurations.

Citadel OS – Administrator Warnings for Reduced Memory Operation
Installation‑Time and Boot‑Time Notices
Citadel is designed to adapt to a wide range of hardware configurations. However, running in Reduced Memory Mode carries operational limitations that administrators must understand before deployment.
This document defines the warnings Citadel presents during installation and boot when the system detects insufficient RAM for full functionality.

1. Installation‑Time Warning (Mandatory Acknowledgment)
If the installer detects that the system does not meet the recommended RAM for Full Mode, it displays the following message:
“This system does not meet the recommended memory requirements for full Citadel functionality.
Citadel can operate in Reduced Memory Mode, but certain services and features may be limited or unavailable.
Proceeding may impact performance, stability, and multitasking capability.”

The installer then requires explicit confirmation:
- [Continue in Reduced Memory Mode]
- [Cancel Installation]
This ensures administrators understand the consequences before committing.

2. Boot‑Time Reduced Memory Notice (If Applicable)
If Citadel detects reduced memory at boot but still meets the minimum threshold for operation, it displays a brief notice:
“Citadel is operating in Reduced Memory Mode.
Some services and features may be limited.
Increase system memory for full functionality.”

This message appears during the boot splash and is also logged for system administrators.

3. Full Refusal Mode (Below Minimum Requirements)
If the system does not meet the minimum RAM required for safe operation, Citadel enters Refusal Mode.
After reporting:
- Required RAM
- Detected RAM
- Tolerance Gap
Citadel displays:
“Will not run in this configuration!
This machine does not meet the hardware requirements as listed above.”

Boot halts safely at this point.

4. Why These Warnings Exist
Running Citadel in Reduced Memory Mode may result in:
- fewer background services
- reduced visual features
- smaller caches
- more aggressive unloading of subsystems
- limited multitasking
- slower performance under load
These are not failures — they are intentional adaptations.
But administrators must be aware of them.
Citadel’s warnings ensure:
- transparency
- predictability
- professional communication
- no surprises during deployment

5. Logging and Documentation
Citadel logs all memory‑mode decisions to:
/system/logs/boot/memory_profile.log


This allows sysadmins to audit:
- when Reduced Mode was triggered
- why it was triggered
- what thresholds were involved

### MVP implementation notes (Roadmap item #14)

Goal: unify the memory decision across UX + logs so the system is transparent and predictable.

Runtime memory profile (single source of truth)
- Define a `MemoryProfile` value early in boot: `full|reduced|refusal`.
- Record: `detected_mb`, `minimum_mb`, `recommended_mb`, and selected profile.
- The decision must be made before loading services/desktop.

Splash/UX behavior (MVP)
- If `reduced`: show the existing Reduced Memory notice text during splash.
  - Include the numbers (detected vs recommended) if available.
  - Keep it non-blocking (no prompts) and time-bound (e.g., shown briefly while boot continues).
- If `refusal`: show Refusal Mode messaging and halt (already defined).
- If `full`: no notice.

Logging behavior (MVP)
- Emit a structured boot event (see item #13) with: profile + thresholds + detected_mb.
- Always log one compact serial line for humans.
- Persistent target:
  - If a writable filesystem is available: append a single JSONL record to `/system/logs/boot/memory_profile.log`.
  - If not available yet: buffer the record and flush when storage comes online.

Acceptance (MVP)
- A reduced-memory boot shows the splash notice and produces exactly one log record.
- The log record contains the thresholds + detected amount and matches the UX decision.


## Section III - addendums.

Citadel OS – Citadel-Native Sandboxes (near-term focus)

At this stage, the sandbox model is used to isolate Citadel-native applications and services:
- Per-app isolation (namespaces, capabilities, limits)
- Deterministic, JSON-driven configuration and recovery

Execution of non-Citadel application ecosystems is explicitly deferred until the core OS, persistent storage, and the native app model are mature.

---

## Executable format note

Planned Citadel native executable file extension: `.cpe` (Citadel Program Executable). Format details TBD.

---

## CUI-ML (Citadel UI Markup Language) note

Current state: JSON drives much of Citadel’s runtime configuration, including UI.

Proposal: introduce a Citadel-native markup language for UI layout called **CUI-ML** with extension `.cuiml`.

Sample (concept)
```xml
<CUI xmlns="citadel.ui" Title="Help" Width="600" Height="500">
  <Window>
    <Column Padding="12" Spacing="8">

      <Text Style="Heading" Value="Citadel Help" />

      <WebView Width="100%" Height="100%">
        <html>
          <body>
            <h1>Welcome to Citadel</h1>
            <p>This is your system help page.</p>
            <p>You can embed any HTML content here.</p>
          </body>
        </html>
      </WebView>

    </Column>
  </Window>
</CUI>
```

Design direction: single markup parser, multiple dialects
- Build one tokenizer + tree builder that can parse both `.cuiml` and `.html` into a markup tree.
- Treat the file extension as a dialect selector:
  - `.cuiml` → Citadel UI dialect (CUI-ML)
  - `.html` → Web dialect (HTML)
- Semantic interpretation stays separate per dialect (different tag sets, layout rules, event model).

Why this is attractive (high-level)
- One parser codebase instead of separate HTML + UI parsers.
- Enables embedded documentation/help content via HTML (e.g., `<WebView>` host).
- Future-friendly for additional dialects (Markdown/XML/SVG) by reusing the same parsing pipeline.

Key caution: keep semantics isolated
- Do not allow HTML semantics to leak into CUI-ML.
- CUI-ML remains Citadel-native: strongly typed components, Citadel layout engine, Citadel bindings/events.
- HTML remains HTML (DOM/events/CSS are separate concerns; optional/limited support as policy permits).
