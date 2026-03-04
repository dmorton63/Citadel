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
15. [ ] Compatibility registry system (JSON-backed per-app virtual registries) + merge semantics
16. [ ] Unified sandbox “personality profiles” (Windows/Linux/macOS/Citadel) + app launch flow integration
17. [x] Apply desktop overrides at runtime (optional `DESKOVR.JSN` merged onto golden desktop)

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
- #15 JSON layering + merge engine + validation can be implemented before full compat execution
- #16 Sandbox personality selection + staging/commit config build can be implemented before app shims exist

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

Citadel OS – Cross‑Platform Compatibility Registry System
Virtual Registry + JSON‑Backed Configuration Layer
Overview
Citadel aims to support execution of applications originally designed for other operating systems (Windows, Linux, macOS). To achieve this safely and cleanly, Citadel implements a Compatibility Registry System — a virtualized configuration environment that emulates the expected registry/config behavior of the target OS without exposing or contaminating Citadel’s native configuration.
This system ensures:
- Isolation between Citadel’s core registry and compatibility layers
- Per‑application configuration stored in JSON
- Runtime in‑memory virtual registries that mimic foreign OS behavior
- API/syscall interception to redirect registry/config requests
- No direct access to Citadel’s real system internals

1. Architecture Summary
A. Citadel Native Registry
- Protected, kernel‑owned
- Used only by Citadel services and processes
- Not visible to compatibility layers
- Not modifiable by foreign applications
B. Compatibility Registries (Virtual)
Citadel creates separate virtual registries for each supported platform:
- Windows Compatibility Registry
- Linux Compatibility Config Layer
- macOS Compatibility Config Layer
Each virtual registry is:
- Backed by JSON files on disk
- Loaded into in‑memory structures at runtime
- Exposed only to applications running inside that compatibility layer
- Fully sandboxed

2. JSON‑Backed Virtual Registry Files
Each foreign application receives its own JSON configuration file(s), stored under:
/compat/<platform>/<appname>/


Examples:
- /compat/windows/Photoshop/registry.json
- /compat/linux/Blender/config.json
- /compat/macos/LogicPro/plist.json
Windows Example
{
  "HKLM\\Software\\Vendor\\App": {
    "InstallPath": "C:\\Citadel\\Apps\\Vendor\\App",
    "Settings": {
      "EnableFeatureX": true,
      "CacheSize": 128
    }
  }
}


Linux Example
{
  "/etc/app/config": {
    "threads": 4,
    "use_gpu": true
  }
}


macOS Example
{
  "com.vendor.app": {
    "FirstRun": false,
    "WindowState": "maximized"
  }
}


These JSON files act as the persistent configuration for the compatibility layer.

2.1 Layering and Merge Semantics (MVP)
Citadel composes a foreign app’s effective virtual registry/config view from multiple layers (lowest → highest precedence):
- Base platform personality (e.g., generic Windows registry layout and defaults)
- Optional persona/profile (e.g., Win10‑like vs Win11‑like tweaks)
- Per‑app delta (app‑specific keys, overrides, compatibility quirks)

MVP merge rule: override‑only
- Higher layers may add keys and override values.
- Higher layers do not delete keys from lower layers (no tombstones in MVP).
- Tombstone/delete support is a later extension once schema and tests are stable.

### MVP implementation notes (Roadmap item #15)

On-disk layout (MVP)
- Base personalities (read-only):
  - `/compat/<platform>/base/registry.json`
  - `/compat/<platform>/base/filesystem.json`
  - `/compat/<platform>/base/env.json`
  - `/compat/<platform>/base/api_shims.json`
- Optional persona (read-only):
  - `/compat/<platform>/persona/<persona>/registry.json` (and peers)
- Per-app delta (writable by system tools only):
  - `/compat/<platform>/<appname>/registry.json` (and peers)

Deterministic merge algorithm (override-only)
- Merge order: base → persona (optional) → app.
- Objects: deep-merge by key.
- Scalars (string/number/bool/null): higher layer overwrites lower.
- Arrays: higher layer replaces lower (no element-wise merging in MVP).
- Unknown keys are allowed (future-proof), but type mismatches are validation failures.

Validation (MVP)
- Each file is optional; missing files are treated as empty objects.
- Each top-level document must be a JSON object.
- Size limits apply (prevent pathological memory use).

Acceptance (MVP)
- A key defined in base and overridden in app yields the app value.
- Persona overrides apply only when persona is selected.
- No merge operation can delete keys from a lower layer.

3. In‑Memory Virtual Registry (Runtime)
At application launch:
- Citadel loads the JSON file(s) for that app
- Builds an in‑memory registry/config map
- Exposes it through the compatibility layer’s API shims
- Intercepts all registry/config calls from the foreign executable
- Returns values from the virtual registry instead of Citadel’s real system
This gives the illusion of:
- a Windows registry
- Linux config files
- macOS plist system
…but all of it is fake, controlled, and safe.

4. API / Syscall Interception Layer
Citadel implements a shim layer for each platform:
Windows
Intercepts calls such as:
- RegOpenKeyEx
- RegQueryValueEx
- RegSetValueEx
- RegEnumKeyEx
These calls are redirected to the virtual registry.
Linux
Intercepts:
- config file reads
- environment variable lookups
- /proc and /etc queries (where applicable)
macOS
Intercepts:
- plist reads
- NSUserDefaults queries
- application preference lookups
The foreign application never touches Citadel’s real filesystem or registry.

5. Why This Approach Works
Isolation
Foreign apps cannot corrupt Citadel’s native configuration.
Transparency
Apps believe they are interacting with their native OS.
Modularity
Each app has its own JSON config — no global hive.
Debuggability
JSON is easy to inspect, edit, version, and restore.
Security
Citadel controls exactly what the app sees and can modify.
Flexibility
Different apps can have different virtual registry environments.

6. Spoofing vs. Emulation (Important Distinction)
Citadel does not “spoof” the system.
It emulates the expected behavior through:
- controlled API interception
- virtual registry/config maps
- JSON‑backed persistence
This is clean, legal, and architecturally sound.

7. Summary
Citadel’s Compatibility Registry System provides:
- A safe, isolated environment for foreign executables
- JSON‑based persistent configuration
- In‑memory virtual registries
- API interception to emulate native OS behavior
- Zero risk to Citadel’s core registry or system integrity
This design allows Citadel to run Windows/Linux/macOS‑style applications without inheriting their baggage.

## Section IV - addendums.

Citadel OS – Unified Sandbox Architecture for Native & Compatibility Apps
One Sandbox Model, Multiple Personalities
Overview
Citadel already uses per‑application sandboxes for security, isolation, and containment.
Now we extend that same sandbox model to foreign applications (Windows, Linux, macOS) by configuring the sandbox to emulate the environment that app expects.
This means:
- We don’t build three OSes inside Citadel.
- We don’t bolt on Wine, Proton, Rosetta, or WSL clones.
- We don’t contaminate Citadel’s core registry or filesystem.
Instead:
Every application runs in its own Citadel sandbox.
The sandbox is configured to behave like the OS that app expects.

This is clean.
This is modular.
This is Citadel.

1. The Sandbox Is the Foundation
Every app — native or foreign — gets:
- its own virtual filesystem
- its own virtual registry/config
- its own API shim table
- its own environment variables
- its own resource limits
- its own process namespace
- its own memory space
- its own security policy
This was already part of your design.
Now we simply add:
- Windows personality
- Linux personality
- macOS personality
…as configurations of the sandbox, not separate subsystems.

2. How It Works (High‑Level)
Step 1 — App is launched
Citadel checks the binary type:
- PE → Windows personality
- ELF → Linux personality
- Mach‑O → macOS personality
- Citadel native → Citadel personality
Step 2 — Citadel creates a sandbox
Sandbox is empty at first.
Step 3 — Citadel loads the personality profile
Each personality profile defines:
- virtual filesystem layout
- virtual registry/config system
- API shim table
- environment variables
- compatibility services
- security rules
Step 4 — Citadel loads JSON config for that app
Example:
/compat/windows/Photoshop/registry.json
/compat/windows/Photoshop/filesystem.json
/compat/windows/Photoshop/env.json


Step 5 — Sandbox becomes the app’s “world”
The app believes it is running on:
- Windows
- Linux
- macOS
…but it is actually running inside a Citadel sandbox with:
- virtual registry
- virtual filesystem
- virtual APIs
- virtual environment
All backed by JSON and in‑memory structures.

3. Why This Is Brilliant
Because you don’t need:
- a global Windows registry
- a global Linux /etc
- a global macOS plist system
You don’t need to “spoof” anything globally.
You simply configure the sandbox to present the environment the app expects.
This means:
- No contamination
- No cross‑app interference
- No global compatibility hacks
- No legacy baggage
- No risk to Citadel’s core systems
Each app gets its own self‑contained compatibility bubble.

4. The Philosophy in One Line
Citadel doesn’t emulate operating systems — it emulates expectations inside isolated sandboxes.

That’s the cleanest, safest, most modular approach possible.

5. Summary for Your Spec

Here’s the short version you can paste directly:
Citadel uses a unified sandbox model for all applications.
Foreign applications (Windows/Linux/macOS) run inside sandboxes configured with the appropriate “personality profile,” including virtual filesystem, virtual registry/config, API shims, and environment variables.
Each sandbox is isolated, JSON‑backed, and ephemeral, ensuring no contamination of Citadel’s native systems.


Citadel OS – JSON‑Driven Sandbox Personality Profiles
Unified Sandbox Architecture for Native & Compatibility Applications
Overview
Citadel uses a single, universal sandbox model for all applications.
Every app — whether native, Windows, Linux, or macOS — runs inside its own isolated sandbox.
The key insight:
Each sandbox is configured entirely through JSON files.

This allows Citadel to present different “personalities” to different applications without ever exposing or compromising its core systems.

1. JSON as the Source of Truth for Sandbox Configuration
Each sandbox is created using a set of JSON files that define:
- virtual filesystem layout
- virtual registry/config entries
- API shim tables
- environment variables
- compatibility services
- security rules
- resource limits
- execution behavior
These JSON files live under:
/compat/<platform>/<appname>/


Examples:
- /compat/windows/Photoshop/registry.json
- /compat/linux/Blender/filesystem.json
- /compat/macos/LogicPro/env.json

2. Personality Profiles (JSON‑Defined)
Citadel defines a personality profile for each supported platform:
- Windows Personality
- Linux Personality
- macOS Personality
- Citadel Native Personality
Each personality is a JSON file that describes:
A. Virtual Filesystem
{
  "filesystem": {
    "C:\\Windows": "/compat/windows/base/Windows",
    "C:\\Program Files": "/compat/windows/base/ProgramFiles",
    "C:\\Users": "/compat/windows/users"
  }
}


B. Virtual Registry / Config
{
  "registry": {
    "HKLM\\Software\\Vendor\\App": {
      "InstallPath": "C:\\Citadel\\Apps\\Vendor\\App",
      "EnableFeatureX": true
    }
  }
}


C. API Shim Table
{
  "api_shims": {
    "RegOpenKeyEx": "shim_reg_open",
    "CreateFileW": "shim_file_create"
  }
}


D. Environment Variables
{
  "env": {
    "APPDATA": "C:\\Users\\CitadelUser\\AppData\\Roaming",
    "TEMP": "C:\\Temp"
  }
}


E. Security Rules
{
  "security": {
    "allow_network": false,
    "allow_raw_disk": false,
    "max_memory_mb": 1024
  }
}



3. Sandbox Creation Flow
Step 1 — Detect binary type
- PE → Windows personality
- ELF → Linux personality
- Mach‑O → macOS personality
- Citadel native → Citadel personality
Step 2 — Create empty sandbox
Step 3 — Load personality JSON
This defines the base environment.
Step 4 — Load app‑specific JSON
This defines the app’s environment.
Step 5 — Build in‑memory virtual environment
- virtual filesystem
- virtual registry/config
- API shim table
- environment variables
Step 6 — Launch app inside sandbox
The app believes it is running on its native OS.
Citadel knows it is running inside a controlled, isolated bubble.

### MVP implementation notes (Roadmap item #16)

Goal: one sandbox engine; personalities are just data-driven configurations.

Personality selection (MVP)
- Default mapping: `PE→windows`, `ELF→linux`, `Mach-O→macos`, else `citadel`.
- Persona selection is explicit (per-app config or launcher argument). If unspecified, persona is `default`.

Sandbox build (staging + commit)
- Stage: load+merge the personality layers (see item #15) into an in-memory `SandboxConfig`.
- Commit: create the sandbox namespace (vfs map, env, shim table, limits) from the staged config.
- Only after commit does the process start executing.

Security posture (MVP)
- Sandbox config cannot grant privileges beyond kernel policy.
- Deny-by-default for dangerous capabilities (raw disk, unrestricted network) unless explicitly allowed by policy.

Acceptance (MVP)
- A Windows app sees Windows paths/keys from its merged compatibility config.
- Two apps can have different personas without affecting each other.
- A malformed personality JSON fails sandbox creation safely (no partial sandbox, clear log).

4. Why JSON Makes This Perfect
Modular
Each app has its own config.
Readable
Admins and developers can inspect and edit JSON easily.
Versionable
JSON works beautifully with Git or any VCS.
Safe
Foreign apps never touch Citadel’s real registry or filesystem.
Extensible
New platforms can be added by creating new personality JSON files.
Debuggable
You can see exactly what the app “thinks” its world looks like.

5. Summary
Citadel uses JSON files to define sandbox personalities for all applications.
Each sandbox is configured with virtual filesystems, virtual registries, API shims, and environment variables — all defined in JSON and isolated from Citadel’s core systems.
This allows Citadel to run foreign applications safely, cleanly, and without legacy contamination.
