# Citadel Secure Boot Key Hierarchy

**Status:** Finalized policy (ready for Batch 1 implementation)  
**Last Updated:** 2026-06-16  
**Owner:** Citadel Security Team

---

## 1. Overview

This document defines the key hierarchy, ownership model, and management procedures for Citadel Secure Boot across lab, staging, and production environments.

---

## 2. Key Hierarchy Structure

### 2.1 Platform Key (PK)

| Aspect | Value |
|--------|-------|
| **Purpose** | Root of trust; controls KEK updates and Secure Boot mode |
| **Generation** | Lab, Staging, Production (separate hierarchies) |
| **Holder** | Citadel team (lab/staging); Legal custody (production) |
| **Backup Location** | Escrow service + secondary offline safe |
| **Rotation Policy** | Lab: daily; Staging: per-release; Production: every 3 years |
| **Recovery Path** | Escrow retrieve (72-hour SLA); offline air-gapped signing machine |

### 2.2 Key Exchange Key (KEK)

| Aspect | Value |
|--------|-------|
| **Purpose** | Signs db and dbx updates |
| **Count** | 1 per environment |
| **Generation** | Lab, Staging, Production (separate hierarchies) |
| **Holder** | Citadel team (lab/staging); Operations lead (production) |
| **Backup Location** | HSM backup + escrow service |
| **Rotation Policy** | Lab: daily; Staging: per-release; Production: every 2 years |
| **Recovery Path** | Escrow retrieve (72-hour SLA) |

### 2.3 Allowed Signatures Database (db)

| Aspect | Value |
|--------|-------|
| **Purpose** | Lists allowed boot artifact signers |
| **Signer** | KEK holder |
| **Initial Entries** | Limine signing key, Kernel signing key |
| **Update Authority** | Lab: single; Staging: single engineer; Production: dual-control |
| **Backup Location** | HSM + encrypted firmware update capsule |
| **Audit Trail** | Immutable log in `/system/.sc/audit/KEYS.CHAIN` |

### 2.4 Revoked Signatures Database (dbx)

| Aspect | Value |
|--------|-------|
| **Purpose** | Blacklist compromised or expired signing keys |
| **Signer** | KEK holder |
| **Initial Entries** | (empty at start) |
| **Update Authority** | Lab: single; Staging: single engineer; Production: emergency path (24-hour SLA) |
| **Backup Location** | HSM + firmware update capsule |
| **Audit Trail** | Immutable log + security incident ticket |

---

## 3. Environment Separation

### 3.1 Lab Keys (Development / Testing)

**Scope:** Local hardware, CI/lab runners  
**Trust Model:** Team-internal; disposable; zero ceremony  
**Rotation Frequency:** Daily or per-build  

| Key | Holder | Access | Retention |
|-----|--------|--------|------------|
| PK | Citadel developer | Disk (unencrypted; lab-only) | Per-session |
| KEK | Citadel developer | Disk (unencrypted; lab-only) | Per-session |
| db signer | CI agent / developer | CI environment | Per-build |
| Backup | None (disposable) | N/A | N/A |

**Enrollment Procedure:**
- [ ] Generate keys or import from secure backup
- [ ] Enroll PK in lab firmware (setup mode)
- [ ] Enroll KEK in firmware
- [ ] Seed initial db with lab signing key
- [ ] Disable Secure Boot mode for iteration; re-enable for tests
- [ ] Document firmware serial number and enrollment date

**Rotation Trigger:** Automatic; new keys generated daily or per-build artifact. Old keys discarded.

### 3.2 Staging Keys (Pre-Production Testing)

**Scope:** Staging hardware, release candidate builds  
**Trust Model:** Team + stakeholders; per-release ceremony  
**Rotation Frequency:** Per-release (or quarterly if no releases)  
**HSM:** YubiHSM2 (on-prem, scriptable)  

| Key | Holder | Access | Retention |
|-----|--------|--------|-----------|
| PK | Citadel engineering team | YubiHSM2 (PIN-protected) | Long-term |
| KEK | Citadel engineering team | YubiHSM2 (PIN-protected) | Long-term |
| db signer | CI / signing service | YubiHSM2 automated unlock | Per-release |
| Backup | Encrypted PKCS#12 export | Offline vault + encrypted USB | Long-term |

**Enrollment Procedure:**
- [ ] Generate or import from secure backup
- [ ] Enroll PK in staging hardware (setup mode, restricted access)
- [ ] Enroll KEK in firmware
- [ ] Seed db with staging signing key + any additional trusted signers
- [ ] Enable Secure Boot and lock firmware setup mode
- [ ] Document each device serial number, enrollment date, and approver
- [ ] Create attestation certificate

**Promotion Criteria from Lab → Staging:**
- [ ] Artifact signing works end-to-end in lab
- [ ] Bootloader + kernel + modules all sign cleanly
- [ ] Security review sign-off (team lead)

**Rotation Trigger:** Per-release or quarterly (whichever is more frequent)

### 3.3 Production Keys

**Scope:** Customer-facing releases, fleet deployments  
**Trust Model:** Externally auditable; long-lived; dual-control on all operations  
**Rotation Frequency:** PK every 3 years; KEK every 2 years; db/dbx as needed (annual hygiene update)  
**HSM:** Azure Key Vault HSM (FIPS 140-2 Level 3) or YubiHSM2 cluster with quorum unlock  

| Key | Holder | Access | Retention |
|-----|--------|--------|-----------|
| PK | Legal custody (escrow service) + offline backup | HSM + air-gapped recovery machine | Indefinite |
| KEK | Operations lead + backup operations lead | Azure Key Vault HSM (dual-control unlock) | Indefinite |
| db signer | Automated signing service | HSM (mandatory two-person approval + audit log) | Per-release |
| Backup | Escrow service + secondary offline safe | Requires two officers to unlock | Indefinite |

**Enrollment Procedure:**
- [ ] Generate keys under controlled conditions (witnessed)
- [ ] Enroll PK in production hardware (setup mode disabled post-enrollment)
- [ ] Enroll KEK in firmware
- [ ] Seed db with production signing key(s)
- [ ] Lock firmware to prevent future key changes
- [ ] Create deployment certificate and ship to customer
- [ ] Document audit trail: dates, signers, witnesses, serial numbers

**Promotion Criteria from Staging → Production:**
- [ ] All Batch 4 compliance criteria met (dual-hardware validation, revocation drills, etc.)
- [ ] v1 sign-off checklist completed by engineering + operations + security
- [ ] Legal sign-off on key custody model and escrow agreement
- [ ] HSM provisioned and backed up
- [ ] Escrow agreement locked with law firm
- [ ] Recovery drill executed and documented

**Rotation Trigger:** PK every 3 years; KEK every 2 years; or on suspected compromise

---

## 4. Signing Key Assignments

### Chain Order: UEFI → Limine → BootGate → Kernel → Modules

**Verification Flow (Production Mode):**

1. **UEFI Secure Boot** verifies **Limine.efi** using PK/KEK/db allowed-signers list
2. **Limine** verifies **BootGate** using **Limine Signing Key (LSK)**
3. **BootGate** verifies **Kernel image** using **Kernel Signing Key (KSK)**
4. **Kernel** verifies at runtime:
   - **boot.json** (root-of-policy) using **Module Signing Key (MSK)**
   - **Kernel modules** using MSK
   - **Ramdisk** (optional now, required later) using MSK

**Key Hierarchy:**
- **LSK** (Limine Signing Key): Signs BootGate; verified by Limine during boot
- **KSK** (Kernel Signing Key): Signs kernel image; verified by BootGate during boot
- **MSK** (Module Signing Key): Signs boot.json, modules, ramdisk; verified by kernel at runtime

### 4.1 Limine Bootloader Signing

**Purpose:** Sign Limine.efi for UEFI Secure Boot verification  
**Verification:** UEFI firmware verifies using PK/KEK/db

| Environment | Key ID | Holder | Approval |
|-------------|--------|--------|----------|
| Lab | `CITADEL_BOOT_LAB_v1` | Developer | Single-person |
| Staging | `CITADEL_BOOT_STAGING_v1` | CI signing service (YubiHSM2) | Single engineer |
| Production | `CITADEL_BOOT_PROD_v1` | HSM signing service (Azure KV) | Dual control |

### 4.2 Limine Signing Key (LSK) – BootGate Signature

**Purpose:** Sign BootGate for Limine verification at boot  
**Verification:** Limine verifies BootGate signature using LSK public key

| Environment | Key ID | Holder | Approval |
|-------------|--------|--------|----------|
| Lab | `CITADEL_LSK_LAB_v1` | Developer | Single-person |
| Staging | `CITADEL_LSK_STAGING_v1` | CI signing service (YubiHSM2) | Single engineer |
| Production | `CITADEL_LSK_PROD_v1` | HSM signing service (Azure KV) | Dual control |

### 4.3 Kernel Signing Key (KSK) – Kernel Image Signature

**Purpose:** Sign kernel image for BootGate verification at boot  
**Verification:** BootGate verifies kernel signature using KSK public key

| Environment | Key ID | Holder | Approval |
|-------------|--------|--------|----------|
| Lab | `CITADEL_KSK_LAB_v1` | Developer | Single-person |
| Staging | `CITADEL_KSK_STAGING_v1` | CI signing service (YubiHSM2) | Single engineer |
| Production | `CITADEL_KSK_PROD_v1` | HSM signing service (Azure KV) | Dual control |

### 4.4 Module Signing Key (MSK) – Runtime Components

**Purpose:** Sign boot.json, kernel modules, and ramdisk for kernel verification at runtime  
**Verification:** Kernel verifies all boot-time artifacts using MSK public key

| Environment | Key ID | Holder | Approval |
|-------------|--------|--------|----------|
| Lab | `CITADEL_MSK_LAB_v1` | Developer | Single-person |
| Staging | `CITADEL_MSK_STAGING_v1` | CI signing service (YubiHSM2) | Single engineer |
| Production | `CITADEL_MSK_PROD_v1` | HSM signing service (Azure KV) | Dual control |

---

## 5. Rotation Authority & Procedures

### 5.1 Who Can Request Rotation?

| Key | Lab | Staging | Production |
|-----|-----|---------|------------|
| PK | Developer | Engineering lead | Director + Legal custody holder |
| KEK | Developer | Engineering lead | Operations lead + Security lead |
| db | Developer | Release manager | Operations lead + Security lead |
| dbx | Developer | Security lead | **Emergency:** Security lead + Operations lead (24h SLA) |

### 5.2 Rotation Approval Chain

**Lab:** 
- **Single-person:** Developer self-approves and documents in commit.
- **Frequency:** Daily or per-build (automatic).
- **Ceremony:** None (keys are disposable).

**Staging:** 
- **Single engineer:** Developer/release manager signs off via approved CI job.
- **Ceremony:** Minimal—automated unlock of YubiHSM2 with hardcoded PIN, then sign artifacts.
- **Verification:** Test team verifies artifacts boot on staging hardware.
- **Audit:** CI logs + YubiHSM2 HSM audit log.

**Production:**
- **Two-person approval:** Requester + independent approver (separation of duty).
- **Ceremony:** Full ceremony with physical witness.
- **Steps:**
  - [ ] Requester files ticket with rotation justification (scheduled or emergency)
  - [ ] Two approvers review and sign ticket
  - [ ] Operations initiates rotation ceremony at agreed time
  - [ ] First operator unlocks HSM with PIN + physical token
  - [ ] Second operator generates/activates new key material
  - [ ] Both operators verify key fingerprints match approved list
  - [ ] Test signing on canary artifact
  - [ ] Deploy to staging hardware for validation
  - [ ] Stage production update (firmware capsule or OS-level)
  - [ ] Both operators sign audit log entry
  - [ ] Archive ceremony transcript (video optional but recommended)

### 5.3 Rotation Ceremony Timeline (Production)

**Planned Rotation (Non-Emergency):**
- 2-week notice to stakeholders
- Verify HSM backup is current
- Schedule ceremony window (off-peak)
- Validate rollback procedure

**Emergency Rotation (Compromise Suspected):**
- Immediate notification to operations + security lead
- Page on-call director if during off-hours
- Target: ceremony within 4 hours; dbx deployed within 24 hours

**Ceremony Execution:**
- [ ] Both operators present + witness
- [ ] HSM verified healthy and backed up
- [ ] Old key material moved to offline escrow marker
- [ ] New key generated in HSM (or restored from escrow backup)
- [ ] db/dbx updated and signed by KEK
- [ ] Firmware capsule or OS update created
- [ ] Test artifact signed and verified on staging hardware
- [ ] All operators + witness sign ceremony transcript
- [ ] Archive transcript + HSM audit logs

**Post-Rotation (24-hour window):**
- [ ] Canary deployment to 1-2% of fleet
- [ ] Monitor logs for refusal/acceptance anomalies
- [ ] Gradual rollout to remaining fleet (75% by day 3)
- [ ] Customer security advisory published
- [ ] Audit report filed with compliance team

---

## 6. Emergency Revocation (dbx Updates)

### 6.1 Trigger Conditions

- Signing key suspected or confirmed compromised
- Artifact discovered to be malicious or tampered
- Mass deployment of vulnerable bootloader
- Third-party firmware package failed security review
- Unscheduled mass refusal detected in fleet logs

### 6.2 Emergency Procedure

**SLA: dbx deployed within 24 hours of detection**

**Lab:**
- [ ] Developer identifies compromised key ID
- [ ] Generate dbx update blacklisting the key
- [ ] Test rejection of old artifacts locally
- [ ] Commit with `[SECURITY]` tag

**Staging:**
- [ ] Notify operations + security lead immediately (Slack + email)
- [ ] Create incident ticket (link to security advisory draft)
- [ ] Stage dbx update to YubiHSM2
- [ ] Test update on staging hardware
- [ ] Verify old media is deterministically rejected

**Production:**
- **Hour 0:** Incident declared; page on-call director + operations + security lead
- **Hour 1:** Brief incident response team; draft security advisory
- **Hour 2:** Execute abbreviated dbx update ceremony (dual-control, no 2-week delay)
- **Hour 3:** Firmware capsule signed and staged for distribution
- **Hour 4-6:** Begin gradual rollout (start with canary: 1-5% of fleet)
- **Hour 8-12:** Ramp to 50% of fleet; monitor refusal logs
- **Hour 12-24:** Complete rollout; verify no false positives
- **Hour 24:** Publish security advisory; notify customers
- **Hour 24-48:** Begin forensics on any compromised systems
- **Hour 48:** Post-incident review scheduled

**Rollback (if dbx update is incorrect):**
- [ ] Declare remediation incident
- [ ] Immediate KEK rotation (generates new KEK, rolls back dbx to empty)
- [ ] Publish corrected dbx in next cycle
- [ ] Root cause analysis on approval + testing process
- [ ] Updated dbx validation procedure documented

---

## 7. Key Backup & Recovery

### 7.1 Lab Key Backup

**Strategy:** No formal backup (keys are disposable).  
**Rotation:** Daily or per-build; new keys generated automatically.  
**Recovery:** Not applicable (if keys are lost, regenerate and re-sign artifacts).

### 7.2 Staging Key Backup

**Medium:** YubiHSM2 encrypted backup (PKCS#11 export)  
**Location:** Offline encrypted USB in secure drawer + secondary encrypted copy in data safe  
**Frequency:** After key rotation (per-release) or quarterly minimum  
**Backup Content:**
- HSM wrapped key material
- Backup authentication code (stored separately)
- Attestation certificate
- Recovery instructions

**Recovery Procedure (YubiHSM2 failure):**
- **SLA: 4 hours**
- [ ] Declare HSM failure incident
- [ ] Retrieve encrypted backup USB + authentication code
- [ ] Boot air-gapped signing machine
- [ ] Load backup into replacement YubiHSM2
- [ ] Verify key fingerprints against audit trail
- [ ] Resume signing operations on replacement HSM
- [ ] Post-mortem on root cause
- [ ] Update HSM health monitoring

### 7.3 Production Key Backup & Escrow

**Strategy:** Multi-layer escrow with M-of-N threshold scheme  

**Layer 1: HSM Native Backup**
- **Medium:** Azure Key Vault HSM backup service
- **Location:** Microsoft Azure cloud (geo-redundant)
- **Managed by:** Azure Key Vault
- **Recovery:** Automatic failover or manual restore from Azure console

**Layer 2: Legal Escrow (M-of-N Split)**
- **Medium:** Encrypted PK shares split via Shamir's Secret Sharing (2-of-3)
- **Location:** 
  - Share 1: Law firm's secure vault
  - Share 2: Corporate secretary service
  - Share 3: Encrypted offline media in fireproof safe
- **Frequency:** Annual (or post-PK rotation)
- **Backup Content:**
  - PK private key (split into shares)
  - KEK encrypted under recovery password
  - Ceremony transcript + witness signatures
  - Escrow agreement + attestation certificates
  - Recovery instructions

**Layer 3: Secondary Offline Backup**
- **Medium:** Encrypted USB or WORM (Write-Once-Read-Many) media
- **Location:** Fireproof safe on-site
- **Access:** Requires dual unlock (two officers, two keys)
- **Frequency:** Annual

**Recovery Procedure (Production HSM Failure):**
- **SLA: 72 hours for full PK recovery**
- **Primary path (HSM still accessible but degraded):**
  - [ ] Azure Key Vault initiates failover to backup HSM
  - [ ] Verify key IDs match production audit trail
  - [ ] Resume signing operations (2-4 hours)
  - [ ] Comprehensive health check + monitoring

- **Emergency path (Complete HSM loss + compromise suspected):**
  - [ ] Declare major security incident
  - [ ] Page director + legal + operations leads
  - [ ] Notify all escrow holders
  - [ ] Initiate 2-of-3 recovery from escrow parties
  - [ ] Contact law firm (share 1) + corporate secretary (share 2)
  - [ ] Both parties verify identity + approvers before releasing shares
  - [ ] Retrieve shares + combine (requires cryptographic reconstruction)
  - [ ] Load into new HSM under witness + audit video
  - [ ] Verify key fingerprints against original attestation
  - [ ] **Immediate PK rotation to new key pair** (compromise recovery is visible to entire organization)
  - [ ] Replace signing infrastructure
  - [ ] Re-enroll all keys in new HSM
  - [ ] Publish security advisory: old key compromised, new key in effect
  - [ ] Full forensics + incident report
  - [ ] Update escrow procedure

---

## 8. Audit & Compliance

### 8.1 Audit Logging

**Every key operation must record:**
- Timestamp (UTC, with microsecond precision)
- Operator name + employee ID + role
- Action type (generate, sign, rotate, revoke, backup, recovery, unlock)
- Key ID and public key fingerprint (SHA-256)
- Artifact hash (if signing)
- Approver name + signature (if required)
- HSM audit log ID (cross-reference)
- Outcome (success / failure + error code)
- Witness name (if applicable)
- Security context (lab / staging / production)

**Storage:**
- **Lab:** Local development log (not formally retained)
- **Staging:** Immutable append-only log in YubiHSM2 audit trail
- **Production:** 
  - Azure Key Vault HSM audit logs (Microsoft-managed)
  - Citadel-side immutable log under `/system/.sc/audit/KEYS.CHAIN`
  - Monthly export + archive to legal/compliance storage
  - Retention: **7 years** (or per regulatory requirement)

### 8.2 Compliance Review Cadence

**Monthly (Staging + Production):**
- [ ] Verify no unauthorized key generation or deletion attempts
- [ ] Check HSM health status and error rates
- [ ] Review access logs for anomalies (unusual operators, off-hours access)
- [ ] Validate backup jobs completed successfully
- [ ] Report metrics to engineering lead

**Quarterly (Staging + Production):**
- [ ] Verify all keys are still secured in HSM/escrow
- [ ] Cross-reference key fingerprints against audit trail
- [ ] Review and approve any pending key rotations
- [ ] Validate escrow agreements are current
- [ ] Simulate recovery procedure (non-destructive test)
- [ ] Report to security review board

**Annual (Production):**
- [ ] Execute planned PK rotation (if age ≥ 3 years)
- [ ] Execute planned KEK rotation (if age ≥ 2 years)
- [ ] Generate fresh escrow backup with new ceremony transcript
- [ ] Update legal escrow agreements (renew with law firm)
- [ ] Execute full recovery drill (recover from escrow on air-gapped machine)
- [ ] Publish control self-assessment (SOC2, ISO27001 audit trail)
- [ ] Board-level sign-off on key management program

---

## 9. Decision Checklist (FINALIZED)

✅ **Chain order (detailed):** UEFI → Limine → BootGate → Kernel → Modules  
✅ **Who verifies what:**
- UEFI firmware verifies Limine.efi (via PK/KEK/db)
- Limine verifies BootGate (via LSK)
- BootGate verifies kernel image (via KSK)
- Kernel verifies boot.json + modules + ramdisk (via MSK)

✅ **Signing key names:**
- LSK (Limine Signing Key): Signs BootGate
- KSK (Kernel Signing Key): Signs kernel image
- MSK (Module Signing Key): Signs boot.json, modules, ramdisk

✅ **Lab key rotation:** Daily or per-build (disposable keys)  
✅ **Staging key rotation:** Per-release or quarterly  
✅ **Production key rotation:** PK every 3 years, KEK every 2 years, LSK/KSK/MSK annual, db/dbx as needed (annual hygiene)  

✅ **Lab approval chain:** Single-person (developer self-approval)  
✅ **Staging approval chain:** Single engineer (CI job with YubiHSM2)  
✅ **Production approval chain:** Two-person dual-control ceremony  

✅ **Staging HSM:** YubiHSM2 (on-prem, scriptable)  
✅ **Production HSM:** Azure Key Vault HSM (FIPS 140-2 Level 3) or YubiHSM2 cluster with quorum unlock  

✅ **Escrow partner:** Legal firm + secondary offline safe (requires 2-of-3 officer unlock)  
✅ **Emergency contact:** Security lead (authorized to declare dbx incident within 24h SLA)  

✅ **Recovery SLAs:**
- Lab: N/A (disposable)
- Staging: 4 hours
- Production: 72 hours for full PK recovery; 24 hours for dbx emergency revocation  

✅ **Disaster plan:** Multi-layer escrow (Azure HSM backup + legal escrow M-of-N + offline media)

---

## 10. References

- [Batch 1 Checklist](#) (link to TODO_MAIN.md Batch 1)
- [SECURE_BOOT_ENABLEMENT_PLAN.md](SECURE_BOOT_ENABLEMENT_PLAN.md)
- [PROVISIONING_TAS_SST_LIFECYCLE.md](PROVISIONING_TAS_SST_LIFECYCLE.md)
- [SC_POLICY_DEFINITIONS.md](SC_POLICY_DEFINITIONS.md)
- UEFI Secure Boot specification (external)

---

**Status:** ✅ Complete and finalized

**Next Steps (Batch 1, Items 1–3):**
1. **Item 1:** Create [SECURE_BOOT_CHAIN_DESIGN.md](SECURE_BOOT_CHAIN_DESIGN.md) — document the boot chain verification logic (UEFI → Limine → BootGate → Kernel → Modules) with fallback/recovery behavior.
2. **Item 2:** Create [SECURE_BOOT_ARTIFACT_INVENTORY.md](SECURE_BOOT_ARTIFACT_INVENTORY.md) — list all files that must be signed (Limine, BootGate, kernel, boot.json, modules, ramdisk) and map to build steps.
3. **Item 3:** (Chain design + artifact inventory complete; proceed to reproducible signing setup.)
