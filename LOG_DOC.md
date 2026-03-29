## Security document...  Please read and understand that this is just a guideline when developing our security model.  The exact workings will never be discussed openly.

LOG_DOC.md — CITADEL Security Architecture (Brainstorm Draft)
Overview
This document captures the initial architectural vision for CITADEL’s internal security model. It outlines the design of the Security Center, provisioning, the runtime trust/token model, user security (identity + vault), and the update verification pipeline.
This is a foundational document intended to guide subsystem development.
##
1. Security Center (SC) — Concept
The Security Center is a core subsystem of CITADEL responsible for all trust, identity, encryption, and verification tasks.
It is:
- Invisible (no public API names, no logs, no visible hooks)
- Isolated (runs in its own address space)
- Privileged (controls all sensitive operations)
- Message‑driven (communicates only through the event/messaging system)
- Boot‑critical (initialized silently during early kernel bring‑up)
The SC is not referenced by name anywhere in user‑facing code or logs.
##
2. Initialization Model
2.1 Boot Flow
- boot_manager → kernel_main()
- kernel_main() performs normal subsystem initialization.
- A single disguised call triggers SC initialization, e.g.:

  function_name("I", "Hello from Citadel");

Internally, this call:
- spawns the SC task/process
- registers it with the message/event bus
- begins provisioning steps if needed
- loads protected storage and keys
No logs, no banners, no visible indicators.
##
3. Install / Provisioning (Machine Anchor + “No Login” Bootstrap)
Out of the box, CITADEL and the Security Center share no secrets. Provisioning establishes a **machine-bound anchor** once, then the system can operate without a daily login.

Note on TPM “random numbers”:
- The TPM can generate high-quality randomness.
- The security anchor should be a **TPM-protected persistent secret/key** (sealed object / NV / storage key hierarchy), not a one-off RNG output.

3.1 Provisioning State
Suggested high-level states:
- UNPROVISIONED: no machine anchor exists
- PROVISIONED: anchor exists, SC storage initialized
- OPERATIONAL: runtime token active + rotation enabled
- SAFE_MODE / RECOVERY: provisioning or trust checks failed

3.2 Machine Anchor Secret (TAS)
During install / first boot:
- Create a **TPM Anchor Secret (TAS)** if TPM is present
- Seal TAS to the machine (optionally to measurements/PCRs in a future enhancement)
- Persist only wrapped metadata in SC protected storage

3.3 Fallback (No TPM)
If TPM sealing is unavailable:
- Generate a one-time recovery/setup code (display once)
- Derive a recovery key using a memory-hard KDF
- Wrap (encrypt + integrity-protect) the TAS under the recovery-derived key

3.4 Provisioning Failure Policy
If provisioning fails:
- Do not proceed to normal operation
- Enter SAFE_MODE / RECOVERY
- Allow recovery/unlock only via explicit owner + physical presence policy

##
4. Runtime Trust Model (SST + Rotation)
This model replaces the earlier “three-pass pairing ritual” with a simpler token approach.

4.1 System Security Token (SST)
- Define a **System Security Token (SST)** as a rotatable, high-entropy secret used to derive runtime keys.
- SST is never persisted in plaintext; SC persists only a **wrapped SST** blob in protected storage.

Goals:
- Provide a single, machine-bound root for runtime key derivation.
- Allow scheduled and forced secret rotation without reinstall.
- Keep the kernel surface minimal (SST should not be broadly accessible).

Non-goals (v1):
- Defining TPM PCR policies and measured boot binding in detail.
- Defining user vault storage formats beyond the key-derivation relationship.

Terminology:
- TAS: TPM Anchor Secret (machine-bound, sealed/wrapped at rest).
- SC Root Key (SRK): key material derived from TAS, used only to wrap/unwrap SST.
- Wrapped SST: authenticated-encrypted blob containing SST material and metadata.

Key hierarchy sketch:
- TAS → derives SRK
- SRK unwraps current SST
- SST derives per-subsystem runtime keys

Wrapped SST blob format (conceptual, v1):
- Header: magic, version, algorithm ids
- Metadata: generation counter, created time, optional rotation reason
- Nonce/IV
- Ciphertext: SST bytes
- Auth tag / MAC

Security invariants:
- Only SC may unwrap SST.
- SST plaintext exists only in volatile memory and must be zeroized on retire.
- Rotation must be atomic from the perspective of dependent key derivation (no partial states).

4.2 Startup (Boot Trust Gate)
At every boot:
- SC is silently initialized
- SC unseals/unwraps TAS
- If TAS access fails → SAFE_MODE / RECOVERY
- Derive/load current SST and expose only the minimum needed to kernel/SC internals.

Startup rules:
- If wrapped SST is missing/invalid, SC may enter provisioning flow to create a new SST (policy-gated).
- If wrapped SST exists, SC must validate integrity/authenticity before unwrapping.
- If SRK derivation fails, do not attempt fallback derivations using weaker sources.

4.3 Runtime Token Rotation (Scheduled + Forced)
During normal operation:
- Rotation schedule configurable (hourly, 4h, 12h, daily, never)
- Random/policy-driven **forced SST replacement** may occur
Cutover principles:
- Generate new SST’
- Rewrap dependent headers/keys to SST’
- Allow in-flight tasks to finish under old SST (retiring state)
- After task completion boundary: securely destroy old SST

Rotation protocol (v1):
- Rotation request creates SST’ and produces new wrapped SST’ (under SRK).
- Persist wrapped SST’ first, then switch “current generation” pointer/record.
- Keep old SST in-memory only while needed for retirement window.
- Retirement window ends at a defined quiescence boundary (e.g., no active secure sessions / tasks).

Suggested API boundaries:
- Kernel-facing: request rotation, query status (generation, next due), derive scoped keys by label.
- SC-internal: unwrap current SST, generate new SST, wrap SST, zeroize retired SST.

##
5. User Security (Identity + Vault)
Machine security protects the device; user security protects private data and identity.

5.1 User Model (v1)
- Single Owner user
- System may boot without daily login, but **vault operations require unlock**
- Unlock states: LOCKED → UNLOCKED → TIMED_LOCK

5.2 Enrollment
At install / first run:
- Owner sets a password and/or passkey (either/both)
- Store only salted verifier/metadata (never store raw secrets)
- Create a per-user vault header (salt, KDF params, wrapped keys)

5.3 Vault Key Derivation
- Derive a **User Master Key (UMK)** from the user secret via memory-hard KDF
- Derive a **Vault Root Key (VRK)** that binds the vault to the machine’s security posture:
  - VRK = KDF(UMK, SST, user_id, vault_version)
- Encrypt entries with per-entry data keys wrapped by VRK
- Prefer re-wrapping keys on SST rotation without decrypting all vault content

5.4 Authentication Hardening
- Rate-limit unlock attempts and enforce backoff
- Securely wipe in-memory keys on lock/timeout

##
6. Update Verification Pipeline
All updates must be verified before staging.
##
6.1 Vendor Updates
Vendor update files contain:
- Magic number
- Subsystem identifier
- Daily rotating vendor ID
- Cryptographic signature block
Verification steps:
- Check magic number
- Validate vendor ID for today
- Verify signature
- Scan payload
- Only then stage the update
If any step fails → discard.
##
6.2 Third‑Party Software
All third‑party downloads:
- are scanned
- cannot self‑execute
- cannot bypass SC
- have macros disabled
- have extensions inspected
- are sandboxed until approved
If malicious → purge and block.
##
7. Execution Policy
No downloaded file becomes executable without:
- SC approval
- tagging
- scanning
- optional sandbox first‑run
Browsers and download apps cannot execute anything directly.
##
8. Internal Audit Logging (Owner-Visible On Demand)
Security decisions must be observable for debugging and incident response, but not exposed to normal applications or public/user-facing logs.

8.1 Goals
- Logs are internal-only by default
- Logs are tamper-evident
- The owner can request a view from the internal terminal
- No secrets are printed (redaction enforced)
- Nothing leaves the system unless explicitly required by an application policy

8.2 Audit Log Storage Model
- SC maintains an append-only audit log
- Each record is chained (each entry includes a hash of the previous entry) to detect modification or deletion
- Log records include: event type, timestamp/boot-id, requesting principal, decision, minimal metadata

8.3 Owner “View Logs” Flow
- Owner issues a privileged system message to SC requesting an audit view
- SC authenticates and authorizes the request (owner/physical-presence policy)
- SC renders a filtered, human-readable view and returns it to the internal terminal
- The rendered view is not the authoritative source; the chained log is

8.4 Redaction + Rate Limiting
- Never output key material, bootstrap seed details, or precise secret locations
- Support coarse filtering by category/time-range where safe
- Rate-limit and/or paginate to avoid flooding the terminal and to reduce side-channel leakage

8.5 Export Policy
- Default: audit logs never leave the system
- Any outbound telemetry/log export must be a separate, explicit, SC-mediated policy decision

If the audit chain integrity fails, the system should treat it as a security event (enter SAFE_MODE/RECOVERY or clearly mark audit as compromised).
##
9. Philosophy
CITADEL’s security model is built on:
- constant scrutiny
- zero blind trust
- isolation of authority
- rolling verification
- invisible enforcement
- fortress‑grade containment
The Security Center is the unseen guardian of the OS.
