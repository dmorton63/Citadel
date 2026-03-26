# **TODO_S_LIST.md — CITADEL Security Architecture (vNext) Implementation Tasks**

This TODO list is organized around **three pillars**:
- **Hardware security** (TPM when present)
- **Software security** (policy, scanning, execution gates)
- **User security** (identity, unlock, vault protection)

It replaces the earlier “3‑pass pairing ritual” with a simpler model:
- A **TPM-sealed persistent secret** becomes the machine’s long‑term anchor.
- A **rotatable System Security Token** (SST) is derived from that anchor and can be forced to rotate safely.
- **User vault keys** are derived from (user secret) + (SST), so protecting the machine also strengthens user privacy.

Important note on TPM “random numbers”:
- The TPM can generate strong randomness, but the *security anchor* should be a **TPM-protected secret/key** (sealed object / NV index / storage key hierarchy), not a one-off RNG output.

---

## **0. Foundation Setup**
- [ ] Create project structure for Security Center (SC) subsystem
- [ ] Reserve an internal-only namespace/module for SC (not exposed)
- [ ] Add kernel init hook for silent SC bring‑up
- [ ] Add event bus channels for SC communication
- [ ] Define SC threat model “in scope” (offline disk theft, casual tamper, rollback attempts, malicious downloads)

---

## **1. Security Center Initialization**
### **1.1 Boot Integration**
- [ ] Implement disguised SC initialization call inside `kernel_main()`
- [ ] Ensure SC starts as a background system task/process
- [ ] Ensure SC has no user-facing logs/identifiers
- [ ] Register SC with the message/event system

### **1.2 Protected Storage + Memory**
- [ ] Create hidden encrypted storage area for SC
- [ ] Implement secure file I/O routines (no direct access from userland)
- [ ] Add protections for SC runtime memory (no swapping/dumping; minimal exposure surfaces)

---

## **2. Provisioning (Install / First Boot)**
Goal: establish a **machine-bound anchor** and initial keys with a clean recovery story.

### **2.1 State Machine**
- [ ] Define provisioning state machine: `UNPROVISIONED → PROVISIONED → OPERATIONAL` (+ `SAFE_MODE` / `RECOVERY`)
- [ ] Define wipe points (what is destroyed, when, and why)
- [ ] Define rollback/anti-rollback policy (monotonic counter if available; otherwise hash-chained metadata + warnings)

### **2.2 TPM Abstraction (when available)**
- [ ] Implement `tpm_present()` probe
- [ ] Implement `seal_secret()` / `unseal_secret()` abstraction (TPM-backed; stubbed fallback)
- [ ] Define “TPM Anchor Secret” (TAS): persistent secret sealed to the machine (optionally to measurements/PCRs later)

### **2.3 Fallback (no TPM)**
- [ ] Define non-TPM secure-bootstrapping path (still encrypted-at-rest, but weaker): recovery code → KDF → wraps the anchor secret
- [ ] Generate one-time recovery code during install (display once)
- [ ] Derive recovery key using memory-hard KDF
- [ ] Wrap TAS under recovery-derived key; store only wrapped material on disk

### **2.4 First Boot**
- [ ] Unseal/unwrap TAS
- [ ] Initialize SC protected storage
- [ ] Derive initial key hierarchy from TAS
- [ ] Transition to `OPERATIONAL`
- [ ] Emit internal audit event: provisioning completed

---

## **3. System Security Token (SST) + Forced Rotation**
Goal: a single “system token” that can be **rotated safely** without complex multi-pass handshakes.

### **3.1 Token Model**
- [ ] Define **SST (System Security Token)** as a rotatable secret used to derive runtime keys
- [ ] Keep SST out of normal filesystem storage; persist only **wrapped SST** (encrypted and integrity-protected) in SC storage
- [ ] Define key hierarchy (example):
  - TAS → derives `SC_Root_Key`
  - `SC_Root_Key` wraps/unwraps the current SST
  - SST derives per-subsystem keys (vault, update verify, execution policy)

### **3.2 Rotation (scheduled + forced)**
- [ ] Implement rotation scheduler (configurable)
- [ ] Implement **forced rotation** trigger (random or policy-driven)
- [ ] Implement safe “cutover”:
  - [ ] Generate new SST’
  - [ ] Rewrap all dependent keys/headers to SST’ (or re-encrypt data keys)
  - [ ] Mark SST as “retiring” while in-flight operations complete
  - [ ] Once no tasks depend on old SST: securely destroy old SST
- [ ] Ensure rotation does not interrupt active tasks (define “task completion” boundary and timeouts)
- [ ] Emit internal audit events: rotation start, rotation complete, rotation failure

### **3.3 Measurements / Attestation (de-scoped for now)**
- [ ] Keep “PCR-based measured boot attestation” as a later enhancement
- [ ] For v1: use TAS unseal success + SC integrity checks as the boot trust gate

---

## **4. User Security (Identity + Vault)**
Goal: protect *people* and their private data, not just the machine.

### **4.1 User Model (v1 scope)**
- [ ] Define initial scope: **single Owner user** (multi-user later)
- [ ] Define “unlock states”: `LOCKED` (vault sealed), `UNLOCKED` (vault usable), `TIMED_LOCK`
- [ ] Define “no daily login” stance:
  - OS may boot to desktop without login,
  - but **sensitive vault operations require unlock** (password/passkey)

### **4.x UI Positioning (planned)**
- [ ] After feature design is complete: position **User Registration / Login** UI *before* the Desktop is created (pre-desktop gate), so enrollment/unlock can happen prior to any desktop initialization.

### **4.2 Enrollment (install / first run)**
- [ ] Implement Owner enrollment flow:
  - [ ] Password and/or passkey (allow either/both)
  - [ ] Store only salted verifier/metadata; never store raw secrets
  - [ ] Generate per-user vault header (salt, KDF params, wrapped keys)
- [ ] Define recovery policy for user vault (recovery code + physical presence gate)

### **4.3 Key Derivation for User Vault**
- [ ] Define **User Master Key (UMK)** derived from user secret using a memory-hard KDF
- [ ] Define **Vault Root Key (VRK)** derived from: `VRK = KDF(UMK, SST, user_id, vault_version)`
- [ ] Store vault contents encrypted with per-entry data keys, wrapped by VRK
- [ ] Support re-wrapping keys on SST rotation without decrypting all vault contents (preferred)

### **4.4 Authentication + Rate Limiting**
- [ ] Implement unlock attempt tracking + backoff
- [ ] Implement lockout policy (time-based) after repeated failures
- [ ] Implement secure wipe of in-memory keys on lock / timeout

### **4.5 What belongs in the “Critical Vault”**
- [ ] Define categories: saved passwords, private notes, encryption keys, app secrets, “hidden files” folder
- [ ] Define which subsystems can request vault items (principle of least privilege)

---

## **5. Software Security (Execution + Updates)**
### **5.1 Update Verification Pipeline**
- [ ] Implement update download staging area
- [ ] Implement signature verification (design + format)
- [ ] Implement payload scanning
- [ ] Implement safe parking of verified updates
- [ ] Implement update application scheduling

### **5.2 Third‑Party Software Handling**
- [ ] Implement mandatory scan for all downloads
- [ ] Implement “never execute directly” rule in browser/downloader
- [ ] Implement sandbox-only first run for unknown binaries
- [ ] Implement purge/quarantine logic for malicious files

### **5.3 Execution Policy Enforcement**
- [ ] Implement SC‑mediated execution approval
- [ ] Implement tagging system for downloaded files
- [ ] Implement audit logging for approve/deny decisions

---

## **6. Audit Logging (Internal, Owner-Visible On Demand)**
- [ ] Define audit event taxonomy (provisioning, boot trust, update verify, exec approve/deny, SST rotation, user unlock/lock)
- [ ] Implement tamper-evident append-only log chain (hash-linked records)
- [ ] Implement redaction rules (no secrets, no key material, no precise secret locations)
- [ ] Implement owner “view logs” flow (requires owner unlock + physical presence policy)
- [ ] Implement rate limiting/pagination for log viewing
- [ ] Define export policy gate (default deny)

---

## **7. Message/Event System Integration**
- [ ] Define SC message types:
  - [ ] `SYS_TRUST_CHECK`
  - [ ] `SYS_ROTATE_SST`
  - [ ] `SYS_UPDATE_VERIFY`
  - [ ] `SYS_EXEC_REQUEST`
  - [ ] `SYS_USER_ENROLL`
  - [ ] `SYS_USER_UNLOCK`
  - [ ] `SYS_USER_LOCK`
  - [ ] `SYS_VAULT_REQUEST`
  - [ ] `SYS_AUDIT_VIEW`
  - [ ] `SYS_AUDIT_EXPORT`
- [ ] Implement handlers in SC
- [ ] Implement kernel-side dispatchers

---

## **8. Failure Modes & Recovery**
- [ ] Define behavior if TAS unseal/unwrap fails (enter `SAFE_MODE` / `RECOVERY`)
- [ ] Define behavior if SST rotation fails mid-cutover (rollback safely; mark degraded state; do not brick)
- [ ] Define behavior for corrupted vault header/content (recovery flow; never silently discard)
- [ ] Define behavior for corrupted/invalid audit log chain (mark audit compromised; SAFE_MODE policy)

---

## **9. Documentation**
- [ ] Update LOG_DOC.md to match the SST + user-vault model (remove 3-pass pairing references)
- [ ] Add diagrams:
  - [ ] Provisioning + TAS/SST lifecycle
  - [ ] SST rotation cutover flow
  - [ ] User unlock → vault key derivation → request authorization

---

## **10. Future Enhancements (Not Required for v1)**
- [ ] PCR-based measured boot policy for TAS unseal
- [ ] Passkey (FIDO2/WebAuthn-style) support if/when the stack exists
- [ ] Multi-user accounts and per-app vault permissions
- [ ] Hardware-backed rate limiting (TPM dictionary-attack protections) where applicable



