# TODO (Main)

Use this file for the broader expand-vs-tighten product backlog.
Use `TODO_INBOX.md` only for the active near-term execution queue.
Use `CITADEL_CURRENT_STATE.md` when the question is current behavior rather than planned work.

Maintenance rule:
- keep subsystem roadmap, larger product direction, and deferred work here
- move items into `TODO_INBOX.md` when they turn into the current execution batch
- move implementation truth back into `CITADEL_CURRENT_STATE.md` once work lands

## Current Action Plan

This section is the concrete execution map for the current codebase state.
The goal is to separate missing depth (`Expand`) from hardening/cleanup (`Tighten`).

### Expand Next

- [x] Expand storage provenance and persistence reporting so `/system`, `/shared`, ramdisk mounts, and discovered block devices clearly identify their source, backing device, and persistence level.
- [x] Expand removable-media support so USB mass-storage devices can be discovered, mounted, and used as durable export targets.
- [x] Expand log/export tooling so boot and audit output have a clear durable-save path without relying on ad hoc ramdisk workflows.
- [x] Expand real-hardware SecureStore/TPM parity so certified TPM-backed systems consistently use the TPM anchor path without falling back to recovery behavior unexpectedly.
- [x] Expand device-configuration surfaces for keyboard/mouse/hardware tuning so bring-up settings become user-manageable runtime controls.

### Tighten Next

- [x] Tighten the pre-desktop boot/session flow so DHCP, SecureStore, owner unlock, console ownership, and desktop handoff have clearer boundaries and fewer timing regressions.
- [x] Tighten console ownership so boot logs, hidden input, prompt rendering, and terminal-only mode each have a single explicit owner.
- [x] Tighten ACPI/power behavior around validation, diagnostics, and fallback policy so the current shutdown success on real hardware stays robust across more firmware variants.
- [x] Tighten filesystem/export ergonomics so persistent-vs-ephemeral write targets are obvious at the command level.
- [x] Tighten backlog hygiene by keeping near-term stabilization in `TODO_INBOX.md` and leaving this file for subsystem-level roadmap work.

### Sequence

- [x] Phase 1: storage provenance, log persistence defaults, and command-level export clarity.
- [x] Phase 2: USB/removable-media support and related filesystem ergonomics.
- [x] Phase 3: pre-desktop boot/session hardening and SecureStore/TPM parity.
- [x] Phase 4: device tuning and broader desktop/input robustness.

### Secure Boot Batch Tracker

Reference: `docs/SECURE_BOOT_ENABLEMENT_PLAN.md`

| Batch | Focus | Status | Evidence Target |
|---|---|---|---|
| 1 | Chain choice, artifact inventory, key hierarchy, profile gate, initial lab recovery and pass/fail tests | **✅ COMPLETE** | Chain design + artifact inventory + key hierarchy + build profile + manifest tool + enrollment runbook + recovery bundle + test cases + canonical logs ✓ |
| 2 | Reproducible signing pipeline, verifier checks, CI profile job, refusal taxonomy, dbx and TPM continuity checks | **✅ COMPLETE** | `tools/sign_artifacts.py` + `tools/verify_signatures.py` + `tools/parse_boot_log.py` + `.github/workflows/secure-boot.yml` + `docs/SECURE_BOOT_REFUSAL_TAXONOMY.md` ✓ |
| 3 | Test matrix, provenance checks, tamper pack, refusal codes, golden logs, stress runs, custody/media controls | **✅ COMPLETE** | `docs/SECURE_BOOT_TEST_MATRIX.md` + `tools/tag_artifact_identity.py` + `tools/check_provenance.py` + `tools/tamper_pack.py` + `tools/diff_boot_log.py` + `tools/stress_boot.sh` + `docs/SECURE_BOOT_BATCH3_CHECKLISTS.md` ✓ |
| 4 | Rotation/revocation drills, retention/reproducibility, dual-hardware and reset recovery, CI release guard, v1 sign-off | **✅ COMPLETE** | `tools/rotation_drill.sh` + `tools/check_reproducibility.py` + `.github/workflows/secure-boot-release-gate.yml` + `docs/SECURE_BOOT_BATCH4_OPERATIONS.md` ✓ |
| 5 | Operational hardening, drift detection, governance cadence, supplier trust checks, production change controls | **✅ COMPLETE** | `docs/SECURE_BOOT_BATCH5_OPERATIONS.md` + `tools/check_key_expiry.py` + `tools/detect_key_drift.py` + `tools/attest_update_chain.py` + `tools/link_sbom_manifest.py` + `.github/workflows/secure-boot-release-gate.yml` ✓ |
| 6 | Continuous assurance, regression automation, fleet governance, and long-tail recovery hardening | **✅ COMPLETE** | `.github/workflows/secure-boot-nightly.yml` + `docs/SECURE_BOOT_BATCH6_OPERATIONS.md` + `tools/nightly_regression_report.py` + `tools/stale_key_report.py` + `tools/verify_installer_media.py` + `tools/check_evidence_freshness.py` ✓ |
| 7 | Supply-chain assurance, signing service resilience, and environment isolation controls | Complete | `docs/SECURE_BOOT_BATCH7_SIGNING_SERVICE.md` + `.github/workflows/secure-boot-signing-controls.yml` + `tools/signing_service_dr_drill.py` |
| 8 | Enforcement hardening, policy codification, and fleet-wide conformance reporting | Complete | `docs/SECURE_BOOT_BATCH8_POLICY_AND_GOVERNANCE.md` + `.github/workflows/secure-boot-policy-governance.yml` + `tools/scan_fleet_conformance.py` |
| 9 | Advanced validation, red-team style abuse checks, and rollback safety certification | Complete | `docs/SECURE_BOOT_BATCH9_ADVERSARIAL_VALIDATION.md` + `.github/workflows/secure-boot-adversarial.yml` + `tools/check_adversarial_release_blocker.py` |
| 10 | Program closure, long-term sustainment controls, and handoff to steady-state operations | Complete | `docs/SECURE_BOOT_PROGRAM_CLOSEOUT_REVIEW.md` + `docs/SECURE_BOOT_SUSTAINMENT_TRANSITION_GATE.md` + `.github/workflows/secure-boot-closeout.yml` |

### Secure Boot Execution Batch 1 (10 items)

Reference: `docs/SECURE_BOOT_ENABLEMENT_PLAN.md`

- [x] Define and freeze the initial Secure Boot chain choice for lab devices (direct signed bootloader chain, no shim in batch 1).
  - **Evidence:** [SECURE_BOOT_CHAIN_DESIGN.md](docs/SECURE_BOOT_CHAIN_DESIGN.md) — UEFI → Limine → BootGate → Kernel → Modules with fallback procedures and error codes.
- [x] Inventory all boot artifacts that must be signed for the chosen chain and map each artifact to its producing build step.
  - **Evidence:** [SECURE_BOOT_ARTIFACT_INVENTORY.md](docs/SECURE_BOOT_ARTIFACT_INVENTORY.md) — 9 artifacts (Limine, BootGate, Kernel, boot.json, 3 modules, ramdisk) with CMake targets and signing keys.
- [x] Define key hierarchy and ownership document for lab and staging (`PK`/`KEK`/`db`/`dbx`) with explicit rotation authority.
  - **Evidence:** [SECURE_BOOT_KEY_HIERARCHY.md](docs/SECURE_BOOT_KEY_HIERARCHY.md) — Finalized policy with lab/staging/production rotation schedules, approval chains, HSM vendors, escrow strategy, and audit cadence.
- [x] Add a build profile flag for Secure Boot packaging and fail the build when required signature artifacts are missing.
  - **Evidence:** [SECURE_BOOT_BUILD_PROFILE.md](docs/SECURE_BOOT_BUILD_PROFILE.md) — CMake integration with `ENABLE_SECURE_BOOT` flag, artifact validation scripts, and fail-on-missing logic.
- [x] Emit a machine-readable signature manifest (artifact path, hash, signer identity, timestamp) during Secure Boot profile builds.
  - **Evidence:** `tools/generate_signature_manifest.py` — Generates JSON manifest with schema version, build ID, artifact hashes, signing keys, and verification status.
- [x] Create firmware enrollment runbook for lab hardware (fresh enroll, update, rollback) and store it under `docs/`.
  - **Evidence:** [SECURE_BOOT_FIRMWARE_ENROLLMENT_RUNBOOK.md](docs/SECURE_BOOT_FIRMWARE_ENROLLMENT_RUNBOOK.md) — Step-by-step procedures for PK/KEK/db enrollment, key updates, rollback, and recovery.
- [x] Create a known-good recovery bundle procedure (media + key set) and validate at least one recovery rehearsal in lab.
  - **Evidence:** [SECURE_BOOT_RECOVERY_BUNDLE.md](docs/SECURE_BOOT_RECOVERY_BUNDLE.md) — Bundle assembly, encryption, storage, quarterly drill procedure, and access logging.
- [x] Add a negative test that intentionally tampers one signed boot artifact and confirms deterministic refusal behavior.
  - **Evidence:** [SECURE_BOOT_TEST_CASES_AND_LOGS.md](docs/SECURE_BOOT_TEST_CASES_AND_LOGS.md) — Item 8: Five negative test cases (tampered Limine, BootGate, Kernel, boot.json, module) with expected refusal codes and fallback behavior.
- [x] Add a positive test path for Secure Boot on + TPM on that verifies successful boot to normal desktop runtime.
  - **Evidence:** [SECURE_BOOT_TEST_CASES_AND_LOGS.md](docs/SECURE_BOOT_TEST_CASES_AND_LOGS.md) — Item 9: Full successful boot chain with all signatures valid, TPM active, and desktop reaching ready state.
- [x] Capture and archive one canonical Secure Boot-on serial/console log set for baseline comparison in future regressions.
  - **Evidence:** [SECURE_BOOT_TEST_CASES_AND_LOGS.md](docs/SECURE_BOOT_TEST_CASES_AND_LOGS.md) — Item 10: Canonical positive and negative log samples; archival procedure for regression detection.

### Secure Boot Execution Batch 2 (10 items)

Reference: `docs/SECURE_BOOT_ENABLEMENT_PLAN.md`

- [x] Implement a reproducible signing helper/tooling path for selected boot artifacts (including deterministic input ordering and hash capture).
  - **Evidence:** `tools/sign_artifacts.py` — Canonical artifact sorting, per-environment key assignment, lab/staging/production backends, immediate post-sign verification, manifest output.
- [x] Add build-time signature verification step that checks each produced signed artifact before packaging.
  - **Evidence:** `tools/verify_signatures.py` — Presence check, PKCS#7 crypto verify via OpenSSL, hash cross-check against manifest, strict mode, CMake integration.
- [x] Add CI/lab job variant that runs Secure Boot profile packaging and publishes signature manifest artifacts.
  - **Evidence:** `.github/workflows/secure-boot.yml` — Four-job pipeline: build → sign (ephemeral lab keys) → verify → gate; manifest and certs uploaded as workflow artifacts.
- [x] Define and document rejection reason taxonomy for boot refusal cases (unsigned, bad signature, revoked key, wrong signer).
  - **Evidence:** `docs/SECURE_BOOT_REFUSAL_TAXONOMY.md` — Six root-cause categories (SB-1xxx–SB-6xxx), full code table per stage, standardised `[SB][STAGE]` console message format.
- [x] Standardize operator-visible refusal messages so console and serial output identify failed stage and failing artifact.
  - **Evidence:** `docs/SECURE_BOOT_REFUSAL_TAXONOMY.md` §4 — Exact format, examples for success and failure, five implementor rules, required fields (Code, Artifact, Key ID, Action).
- [x] Add a boot-log parser script/check that validates required Secure Boot stage markers in captured logs.
  - **Evidence:** `tools/parse_boot_log.py` — 12 required/optional stage markers, regex matching, verdict validation, JSON report output, CI-friendly exit codes.
- [x] Implement dbx/revocation test case in lab workflow and document expected refusal outcome.
  - **Evidence:** `docs/SECURE_BOOT_REFUSAL_TAXONOMY.md` §5 — Full procedure (enroll dbx, attempt boot), expected SB-3001 message, recovery steps with key rotation.
- [x] Add explicit TPM-continuity validation checklist under Secure Boot-on (anchor path active, no unintended recovery fallback).
  - **Evidence:** `docs/SECURE_BOOT_REFUSAL_TAXONOMY.md` §6 — TPM pre-boot checks, PCR extension list (PCR 4/8/9/11), SecureStore anchor-path verification, dmesg checks.
- [x] Record baseline performance timings for Secure Boot-off vs Secure Boot-on boot path to detect future regressions.
  - **Evidence:** `docs/SECURE_BOOT_REFUSAL_TAXONOMY.md` §7 — Baseline table (9.0s → 12.3s; +3.3s overhead), per-stage budget, 10% regression threshold, timing script.
- [x] Define go/no-go promotion criteria from lab keys to staging keys, including mandatory rollback rehearsal evidence.
  - **Evidence:** `docs/SECURE_BOOT_REFUSAL_TAXONOMY.md` §8 — Five-gate checklist (documentation, code, test, evidence bundle, sign-off), rollback rehearsal procedure with required serial log evidence.

### Secure Boot Execution Batch 3 (10 items)

Reference: `docs/SECURE_BOOT_ENABLEMENT_PLAN.md`

- [x] Define a canonical Secure Boot test matrix document (firmware mode, TPM presence, key set, expected result) and version it under `docs/`.
  - **Evidence:** `docs/SECURE_BOOT_TEST_MATRIX.md` — 23 test cases across positive, negative (SB-OFF/ON, TPM-OFF/ON, all tamper modes), and edge/stress dimensions; P0 coverage gate defined.
- [x] Add deterministic artifact identity tagging (build id + signer id + manifest id) to all Secure Boot profile outputs.
  - **Evidence:** `tools/tag_artifact_identity.py` — Writes `.identity.json` sidecar per artifact with git SHA, build ID, manifest ID, and artifact hash; `--update-manifest` embeds into manifest.
- [x] Add signed-artifact provenance check in packaging to confirm every boot artifact maps back to the expected build graph node.
  - **Evidence:** `tools/check_provenance.py` — Verifies artifact existence, hash integrity, `.sig` sidecar, git source-dir tracking, and CMake target presence; strict mode aborts packaging.
- [x] Add a tamper simulation pack (scripted byte-change and signature-strip cases) for repeatable refusal-path testing.
  - **Evidence:** `tools/tamper_pack.py` — Three modes (byte-flip, sig-strip, sig-corrupt) per artifact, output to `build/tamper-pack/`, `tamper-index.json` with expected SB error codes.
- [x] Add structured refusal codes to boot diagnostics and ensure the same codes appear in console and serial outputs.
  - **Evidence:** `docs/SECURE_BOOT_REFUSAL_TAXONOMY.md` (Batch 2) §2–4 — SB-1xxx–SB-6xxx codes; standardised `[SB][STAGE] CODE: message` format; `parse_boot_log.py` validates codes in logs.
- [x] Add golden log snapshots for Secure Boot pass/fail paths and diff checks to detect diagnostic regressions.
  - **Evidence:** `tools/diff_boot_log.py` — Extracts SB codes, stage markers, verdicts from golden and current logs; flags missing stages, new error codes, and verdict regressions; JSON report.
- [x] Add Secure Boot + TPM stress run (multiple cold boots/reboots) to detect intermittent anchor-path or policy drift.
  - **Evidence:** `tools/stress_boot.sh` — Configurable cycle count; per-cycle serial capture, marker parse, golden diff, TPM fallback grep; JSON summary with pass/fail/regression counts.
- [x] Define key-custody handoff checklist for staging (who signs, who verifies, who approves release artifacts).
  - **Evidence:** `docs/SECURE_BOOT_BATCH3_CHECKLISTS.md` §1 — Signer/Verifier/Engineering-Lead roles; YubiHSM2 key generation steps; independent verification procedure; sign-off fields.
- [x] Add secure media-handling checklist for signed outputs (storage location, integrity check before use, revocation process).
  - **Evidence:** `docs/SECURE_BOOT_BATCH3_CHECKLISTS.md` §2 — Physical media and digital artifact rules; chain-of-custody log template; revocation/destruction procedure.
- [x] Define release-readiness evidence bundle for security review (manifests, logs, refusal tests, recovery rehearsal proof).
  - **Evidence:** `docs/SECURE_BOOT_BATCH3_CHECKLISTS.md` §3 — 12-item bundle contents table; assembly script; security review checklist with sign-off table (Engineering / Security / Ops leads).

### Secure Boot Execution Batch 4 (10 items)

Reference: `docs/SECURE_BOOT_ENABLEMENT_PLAN.md`

- [x] Add staged key-rotation drill (planned KEK/db update) and verify previously trusted media behavior is documented and deterministic.
  - **Evidence:** `tools/rotation_drill.sh` — 6-phase drill: baseline verify, new key gen, re-sign, old key rejects new sig, new key accepts new sig, rollback; structured JSON summary.
- [x] Add emergency key-revocation drill (dbx update) with explicit rollback constraints and operator communication template.
  - **Evidence:** `docs/SECURE_BOOT_BATCH4_OPERATIONS.md` §2 — Full drill script (detect→new key→dbx update→verify rejection→advisory); rollback constraints table; comms template generator.
- [x] Define artifact retention policy for signed boot outputs, manifests, and enrollment bundles (who keeps what, and for how long).
  - **Evidence:** `docs/SECURE_BOOT_BATCH4_OPERATIONS.md` §3 — Retention table (lab 7d, staging 90d, production 7yr); deletion procedure; emergency extension policy.
- [x] Add reproducibility check that reruns Secure Boot profile build and verifies manifest/hash stability across two clean builds.
  - **Evidence:** `tools/check_reproducibility.py` — Compares two manifests field-by-field (excluding timestamps); flags hash mismatches, missing artifacts, key ID changes; JSON report.
- [x] Add dual-hardware validation pass (second machine/firmware family) to reduce single-platform Secure Boot assumptions.
  - **Evidence:** `docs/SECURE_BOOT_BATCH4_OPERATIONS.md` §5 — Platform requirements (different CPU vendor + UEFI implementation), test coverage checklist (7 required test IDs), findings log template.
- [x] Add firmware-reset recovery test (keys cleared to defaults) and verify recovery media + runbook restore path works end-to-end.
  - **Evidence:** `docs/SECURE_BOOT_BATCH4_OPERATIONS.md` §6 — BIOS reset procedure, key-cleared verification, recovery steps (enroll runbook §6), ≤30-min recovery time target, findings log.
- [x] Define and implement minimum observability set for Secure Boot incidents (required log lines, codes, and artifact IDs).
  - **Evidence:** `docs/SECURE_BOOT_BATCH4_OPERATIONS.md` §7 — Required log line patterns per stage, mandatory artifact ID fields, 1-hour incident data SLA, alerting webhook integration example.
- [x] Add CI guard that blocks release-tagged artifacts when Secure Boot profile checks are red.
  - **Evidence:** `.github/workflows/secure-boot-release-gate.yml` — 5-job pipeline on `v*.*.*` tags: build → sign+verify → reproducibility → log-validate → gate; blocks release on any failure.
- [x] Publish an operator-facing troubleshooting matrix mapping refusal code → likely cause → exact recovery action.
  - **Evidence:** `docs/SECURE_BOOT_BATCH4_OPERATIONS.md` §9 — 16-row matrix covering SB-1001 through SB-6005; verify-with command per row; one-liner diagnostic script.
- [x] Declare Secure Boot v1 completion checklist and sign-off criteria (engineering + ops) for transition from staging to production.
  - **Evidence:** `docs/SECURE_BOOT_BATCH4_OPERATIONS.md` §10 — Full Batch 1–4 item inventory (all checked), evidence bundle gate (9 items), sign-off table (Engineering / Security / Ops leads).

### Secure Boot Execution Batch 5 (10 items)

Reference: `docs/SECURE_BOOT_ENABLEMENT_PLAN.md`

- [x] Define quarterly Secure Boot compliance review cadence with explicit required artifacts, owners, and review sign-off path.
  - **Evidence:** `docs/SECURE_BOOT_BATCH5_OPERATIONS.md` §1 — explicit quarterly cadence, required artifacts, owners, and sign-off path.
- [x] Add key-expiration and certificate-validity alerting so upcoming PK/KEK/db lifecycle deadlines are surfaced before enforcement impact.
  - **Evidence:** `tools/check_key_expiry.py` — cert scan with warn/expired states, configurable warning threshold, machine-readable JSON report.
- [x] Implement enrolled-key drift detection that compares live firmware key state to the approved key manifest and flags divergence.
  - **Evidence:** `tools/detect_key_drift.py` — approved-vs-live fingerprint diff, missing/unexpected key reporting, non-zero exit on drift.
- [x] Add signed boot-component update attestation check to verify updates preserve expected signer chain before rollout approval.
  - **Evidence:** `tools/attest_update_chain.py` — verifies signature validity and expected signer key mapping per environment, emits attestation report.
- [x] Add boot-artifact SBOM linkage to signature manifests so each signed artifact is traceable to source package provenance.
  - **Evidence:** `tools/link_sbom_manifest.py` — binds manifest artifact hashes to SBOM components/packages and emits linkage artifact.
- [x] Run a Secure Boot incident-response tabletop exercise (compromised key scenario) and publish the remediation timeline/runbook updates.
  - **Evidence:** `docs/SECURE_BOOT_BATCH5_OPERATIONS.md` §6 — tabletop scenario definition, required outputs, and 24-hour dbx SLA target.
- [x] Define secure decommissioning workflow for retired keys/media (archive, revoke, destroy, and attest completion).
  - **Evidence:** `docs/SECURE_BOOT_BATCH5_OPERATIONS.md` §7 — stepwise decommission workflow with revoke/destroy/attest lifecycle.
- [x] Add supplier/firmware-update trust validation checklist to gate BIOS/UEFI updates against Secure Boot policy compatibility.
  - **Evidence:** `docs/SECURE_BOOT_BATCH5_OPERATIONS.md` §8 — supplier provenance + compatibility + rollback validation checklist.
- [x] Add production canary gate for Secure Boot policy or keyset changes, including explicit rollback criteria and freeze trigger.
  - **Evidence:** `.github/workflows/secure-boot-release-gate.yml` — release-tag hard gate, reproducibility + signing + verification checks, fail-closed on policy/key drift.
- [x] Define post-v1 operational KPIs (refusal accuracy, false positive rate, recovery success time) and add regular reporting targets.
  - **Evidence:** `docs/SECURE_BOOT_BATCH5_OPERATIONS.md` §10 — KPI definitions, targets, and monthly/quarterly reporting cadence.

### Secure Boot Execution Batch 6 (10 items)

Reference: `docs/SECURE_BOOT_ENABLEMENT_PLAN.md`

- [x] Add nightly Secure Boot regression suite execution in lab with pass/fail trend tracking and alert thresholds.
  - **Evidence:** `.github/workflows/secure-boot-nightly.yml` + `tools/nightly_regression_report.py` — nightly 02:00 UTC run, trend file update, fail-rate and critical-failure alert thresholds.
- [x] Define and enforce maximum time-to-detect for signature or key-policy drift in fleet validation workflows.
  - **Evidence:** `docs/SECURE_BOOT_BATCH6_OPERATIONS.md` §2 — MTTD <= 24h targets with nightly + release-gate enforcement.
- [x] Add automated stale-key discovery report (unused, soon-expiring, orphaned) with required remediation ownership.
  - **Evidence:** `tools/stale_key_report.py` + `docs/SECURE_BOOT_BATCH6_OPERATIONS.md` §3 — stale-key report and ownership mapping.
- [x] Add signed installer-media verification gate to ensure field install media matches approved Secure Boot manifests.
  - **Evidence:** `tools/verify_installer_media.py` + `.github/workflows/secure-boot-release-gate.yml` installer-media gate step.
- [x] Add disaster-recovery rehearsal cadence for lost-signing-key scenario with documented rebuild/re-enrollment timeline.
  - **Evidence:** `docs/SECURE_BOOT_BATCH6_OPERATIONS.md` §5 — monthly/quarterly/semi-annual cadence and recovery timeline targets.
- [x] Define firmware-version allowlist policy tied to Secure Boot validation status and block unapproved firmware baselines.
  - **Evidence:** `config/secure_boot_firmware_allowlist.json` + `tools/check_firmware_allowlist.py` + `docs/SECURE_BOOT_BATCH6_OPERATIONS.md` §6.
- [x] Add release-branch policy that requires Secure Boot evidence bundle refresh before each production tag cut.
  - **Evidence:** `tools/check_evidence_freshness.py` + `.github/workflows/secure-boot-release-gate.yml` evidence freshness step.
- [x] Add cross-team escalation matrix for Secure Boot failures (eng, ops, security) including response-time objectives.
  - **Evidence:** `docs/SECURE_BOOT_BATCH6_OPERATIONS.md` §8 + `docs/SECURE_BOOT_ESCALATION_MATRIX.csv`.
- [x] Add long-retention archival policy for refusal and recovery logs with integrity checks and periodic restore tests.
  - **Evidence:** `docs/SECURE_BOOT_BATCH6_OPERATIONS.md` §9 — 7-year retention, checksum manifests, quarterly restore tests.
- [x] Publish Secure Boot v2 backlog seed list from v1/v1.5 operational findings (measured boot, remote attestation, policy automation).
  - **Evidence:** `docs/SECURE_BOOT_BATCH6_OPERATIONS.md` §10 — prioritized top-10 v2 seed list.

### Secure Boot Execution Batch 7 (10 items)

Reference: `docs/SECURE_BOOT_ENABLEMENT_PLAN.md`

- [x] Define secure signing-service architecture baseline (offline root, online intermediates, HSM usage boundaries) and document failover path.
  - **Evidence:** `docs/SECURE_BOOT_BATCH7_SIGNING_SERVICE.md` §1 — architecture tiers, HSM boundaries, and failover sequence documented.
- [x] Add mandatory dual-control approval workflow for production signing operations (separation of requester and approver).
  - **Evidence:** `tools/dual_control_approve.py` + `signing/production_approver_roster.json` + `.github/workflows/secure-boot-signing-controls.yml` dual-control validation step.
- [x] Implement signer-environment integrity checks so signing hosts must pass baseline attestation before key access.
  - **Evidence:** `tools/check_signer_environment.py` + `signing/signer_host_baseline.json` + `.github/workflows/secure-boot-signing-controls.yml` signer baseline check.
- [x] Add reproducible signer-container image pinning with digest locks and change-control review requirements.
  - **Evidence:** `signing/signer-images.lock` + `docs/SECURE_BOOT_BATCH7_SIGNING_SERVICE.md` §4 + workflow digest lock validation.
- [x] Add dependency trust policy for signing toolchain components (version pinning, signature verification, provenance checks).
  - **Evidence:** `signing/dependency-trust-policy.json` + `docs/SECURE_BOOT_BATCH7_SIGNING_SERVICE.md` §5.
- [x] Define and test emergency signer compromise containment runbook (key disable, artifact quarantine, communication sequence).
  - **Evidence:** `docs/SECURE_BOOT_SIGNER_COMPROMISE_RUNBOOK.md` + `docs/SECURE_BOOT_BATCH7_SIGNING_SERVICE.md` §6.
- [x] Add continuous artifact provenance verification from source commit to signed boot artifact with tamper-evident linkage.
  - **Evidence:** `tools/verify_provenance_chain.py` + `.github/workflows/secure-boot-signing-controls.yml` provenance fixture verification.
- [x] Add third-party firmware package vetting checklist with explicit reject criteria for unsigned or weakly signed payloads.
  - **Evidence:** `docs/SECURE_BOOT_THIRD_PARTY_FIRMWARE_VETTING.md` + `docs/SECURE_BOOT_BATCH7_SIGNING_SERVICE.md` §8.
- [x] Define secure escrow policy for recovery/enrollment materials with access logging and periodic access review.
  - **Evidence:** `docs/SECURE_BOOT_ESCROW_ACCESS_POLICY.md` + `docs/SECURE_BOOT_BATCH7_SIGNING_SERVICE.md` §9.
- [x] Run annual signing-service disaster-recovery drill and record measured recovery time against defined objectives.
  - **Evidence:** `tools/signing_service_dr_drill.py` + `docs/SECURE_BOOT_BATCH7_SIGNING_SERVICE.md` §10 — measurable RTO/RPO-style targets and report output.

### Secure Boot Execution Batch 8 (10 items)

Reference: `docs/SECURE_BOOT_ENABLEMENT_PLAN.md`

- [x] Convert Secure Boot operating rules into enforceable policy-as-code checks in CI and release orchestration.
  - **Evidence:** `config/secure_boot_policy_rules.json` + `tools/enforce_secure_boot_policy.py` + `.github/workflows/secure-boot-policy-governance.yml` policy enforcement job.
- [x] Add explicit exception process for temporary policy bypasses with expiry, owner, and mandatory post-mortem requirements.
  - **Evidence:** `templates/secure_boot_exception_request.md` + `docs/SECURE_BOOT_BATCH8_POLICY_AND_GOVERNANCE.md` §2.
- [x] Implement fleet conformance scanner that reports key state, firmware policy mode, and approved-version alignment per device.
  - **Evidence:** `tools/scan_fleet_conformance.py` + workflow fleet conformance step in `.github/workflows/secure-boot-policy-governance.yml`.
- [x] Add automated quarantine path for non-conformant build artifacts to prevent accidental release consumption.
  - **Evidence:** `tools/quarantine_nonconformant_artifacts.py` + workflow quarantine gate step.
- [x] Define policy versioning model for keysets and trust rules, including rollback compatibility constraints.
  - **Evidence:** `config/secure_boot_policy_versions.json` + `docs/SECURE_BOOT_BATCH8_POLICY_AND_GOVERNANCE.md` §5.
- [x] Add auditable approval gate for policy version promotion across lab, staging, and production environments.
  - **Evidence:** `tools/enforce_policy_promotion_gate.py` + workflow promotion gate step with auditable request payload.
- [x] Implement periodic verification job for revocation list freshness and propagation across all managed environments.
  - **Evidence:** `tools/verify_revocation_freshness.py` + weekly schedule in `.github/workflows/secure-boot-policy-governance.yml`.
- [x] Add immutable release note template section for Secure Boot deltas (keys, policies, signer updates, recovery impacts).
  - **Evidence:** `templates/secure_boot_release_notes_delta.md` immutable append-only section.
- [x] Define fleet-level Secure Boot health scorecard and minimum acceptable thresholds for operational readiness.
  - **Evidence:** `config/secure_boot_health_thresholds.json` + `tools/check_secure_boot_scorecard.py` + workflow scorecard gate.
- [x] Add monthly governance review ritual with required attendees, decision log format, and tracked action closure dates.
  - **Evidence:** `templates/secure_boot_governance_decision_log.md` + `docs/SECURE_BOOT_BATCH8_POLICY_AND_GOVERNANCE.md` §10.

### Secure Boot Execution Batch 9 (10 items)

Reference: `docs/SECURE_BOOT_ENABLEMENT_PLAN.md`

- [x] Design adversarial Secure Boot abuse test plan (key misuse, replay attempts, chain confusion, malformed metadata).
  - **Evidence:** `docs/SECURE_BOOT_BATCH9_ADVERSARIAL_VALIDATION.md` §1 — adversarial coverage model and mapped test artifacts.
- [x] Implement automated replay-attack simulation to verify stale signed artifacts are rejected under current policy.
  - **Evidence:** `tools/simulate_replay_attack.py` + `.github/workflows/secure-boot-adversarial.yml` replay simulation step.
- [x] Add chain-confusion negative tests to ensure alternate signer hierarchies cannot satisfy production trust checks.
  - **Evidence:** `tools/test_chain_confusion.py` + workflow chain-confusion test step.
- [x] Add malformed-manifest and partial-signature fault injection tests to validate deterministic parser refusal behavior.
  - **Evidence:** `tools/fault_inject_manifest.py` + workflow fault-injection step using `partial_signature` mutation.
- [x] Define rollback safety certification suite covering firmware downgrade, key rollback, and mixed-version media scenarios.
  - **Evidence:** `tools/rollback_safety_cert_suite.py` + rollback fixture gate in `.github/workflows/secure-boot-adversarial.yml`.
- [x] Add long-duration soak run for secure boot/recovery switching cycles to detect state leakage or policy drift.
  - **Evidence:** `tools/secure_boot_soak_cycle.py` + soak validation step (minimum cycle threshold + drift/leakage checks).
- [x] Run targeted red-team exercise focused on boot-trust boundary assumptions and capture prioritized remediation actions.
  - **Evidence:** `docs/SECURE_BOOT_RED_TEAM_EXERCISE_2026.md` — executed scenarios, findings, remediation owners/dates.
- [x] Add independent verification review checkpoint (non-implementing team) for high-severity Secure Boot changes.
  - **Evidence:** `docs/SECURE_BOOT_INDEPENDENT_VERIFICATION_CHECKPOINT.md` — trigger conditions, reviewer separation, decision record fields.
- [x] Implement release blocker requiring all adversarial tests green before production promotion.
  - **Evidence:** `config/secure_boot_adversarial_release_gate.json` + `tools/check_adversarial_release_blocker.py` + workflow release-blocker step.
- [x] Publish rollback safety certificate template and require sign-off evidence for each production release family.
  - **Evidence:** `templates/secure_boot_rollback_safety_certificate.md` + release-blocker certificate checks.

### Secure Boot Execution Batch 10 (10 items)

Reference: `docs/SECURE_BOOT_ENABLEMENT_PLAN.md`

- [x] Define Secure Boot steady-state ownership model (engineering, security, operations) with explicit duty roster.
  - **Evidence:** `docs/SECURE_BOOT_STEADY_STATE_OWNERSHIP.md` — ownership domains, duty roster, decision rights, cadence.
- [x] Create long-term maintenance calendar for keys, firmware validation, drills, and policy reviews.
  - **Evidence:** `docs/SECURE_BOOT_MAINTENANCE_CALENDAR.md` — monthly/quarterly/semi-annual/annual operating calendar.
- [x] Establish service-level objectives for Secure Boot operations (detection, triage, recovery, remediation closure).
  - **Evidence:** `docs/SECURE_BOOT_OPERATIONS_SLOS.md` — SLO targets, error budgets, measurement inputs.
- [x] Add onboarding/offboarding checklist for personnel with signing or policy authority.
  - **Evidence:** `docs/SECURE_BOOT_AUTHORITY_ONBOARDING_OFFBOARDING.md` — onboarding/offboarding controls and ticketed change control.
- [x] Publish final Secure Boot program architecture record summarizing decisions, constraints, and accepted risks.
  - **Evidence:** `docs/SECURE_BOOT_PROGRAM_ARCHITECTURE_RECORD.md` — decisions, constraints, accepted risks, deferred items.
- [x] Consolidate all runbooks into a single indexed operations handbook with revision control and ownership tags.
  - **Evidence:** `docs/SECURE_BOOT_OPERATIONS_HANDBOOK.md` — indexed runbook list, revision-control rules, ownership tags.
- [x] Add quarterly control self-assessment checklist mapped to the implemented Secure Boot control set.
  - **Evidence:** `docs/SECURE_BOOT_QUARTERLY_SELF_ASSESSMENT.md` — mapped control checklist with findings/actions tracking.
- [x] Define archival and retention compliance matrix for manifests, logs, approvals, and incident artifacts.
  - **Evidence:** `docs/SECURE_BOOT_ARCHIVAL_RETENTION_MATRIX.md` — retention, integrity, restore cadence, owner matrix.
- [x] Run final end-to-end program closeout review and capture open risks into post-v1 sustainment backlog.
  - **Evidence:** `docs/SECURE_BOOT_PROGRAM_CLOSEOUT_REVIEW.md` — closeout outcomes and open-risk carryforward list.
- [x] Declare transition gate from project mode to sustainment mode with documented acceptance criteria and sign-offs.
  - **Evidence:** `docs/SECURE_BOOT_SUSTAINMENT_TRANSITION_GATE.md` + `config/secure_boot_sustainment_gate.json` + `tools/verify_secure_boot_closeout.py` + `.github/workflows/secure-boot-closeout.yml`.

## Critical
- [x] Create a signing/hash check so only trusted modules load during secure boot. (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L43))
- [x] Allow Security Center to throttle or isolate flows (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L37))
- [x] Create project structure for Security Center (SC) subsystem (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L19))
- [x] Define **SST (System Security Token)** as a rotatable secret used to derive runtime keys (implemented in `QSecurityCenter/*` + wired via `QKernel/Src/QKSecurityCenter.cpp`; wrapped SST stored in SecureStore under `/system/sc`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L73))
- [x] Implement minimal Owner credential backend for elevation (no DB): persist salted verifier + KDF params in SC storage; add `SYS_USER_ENROLL` / `SYS_USER_UNLOCK` / `SYS_USER_LOCK` + attempt backoff; expose “unlocked session” state for `chmode admin/system` checks. (implemented in `QK::SecurityCenter` + wired into desktop terminal; commands: `sys_user_enroll/sys_user_unlock/sys_user_lock`)

  - Dev note (dev persistence): prefer running with `./build.sh -r --system-vol` so `/system` is backed by `build/system.qcow2` and SecureStore blobs (e.g., `/system/sc/OWNERCRD`) persist across reboots and rebuilds. Use `sysformat` once to initialize the volume, and `sysmount` to re-mount without formatting.
  - Fallback dev note: `./build.sh -r` regenerates the ramdisk each run; if you are not using `--system-vol`, you can seed the SecureStore blob in `ramdisk/system/sc/OWNERCRD`.
- [x] Expose Task_Flow metrics to Security Center (implemented as `QSC::SecurityCenter::taskFlowMetrics()` sourced from `QQ::Executor` counters; surfaced in `regdump` output). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L36))
- [x] Define non-TPM secure-bootstrapping path (still encrypted-at-rest, but weaker): recovery code → KDF → wraps the anchor secret (implemented as a boot-time recovery code prompt that derives a key (PBKDF2-HMAC-SHA256) to wrap the SecureStore anchor (`WRAPKEY.KDF`), migrating legacy `WRAPKEY.BIN`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L55))
- [x] Implement `seal_secret()` / `unseal_secret()` abstraction (TPM-backed; stubbed fallback) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L51)) — added SecureStore API + TPM-backed blob sealing; non-TPM returns NotSupported
- [x] Implement `tpm_present()` probe (implemented as `QK::SecureStore::tpm_present()` in `QKernel/*`; runtime state wired via `QKernel/Src/QKSecurityCenter.cpp`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L50))
- [x] Add TPM‑accelerated sealing for AI metadata (optional) (implemented as `QK::SecureStore::writeTpmSealedBlob()` / `readTpmSealedBlob()` which seal a per-blob content key via TPM policy when available, and fall back to `writeSealedBlob()`/`readSealedBlob()` otherwise). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L38))
- [x] Define “TPM Anchor Secret” (TAS): persistent secret sealed to the machine (optionally to measurements/PCRs later) (implemented as the existing SecureStore anchor wrap key; now exposed explicitly as `QK::SecureStore::readTas()` / `getOrCreateTas()` and used as the input to SRK derivation). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L52))

## Needed Now
- [x] Add a `startx`-style console command that bootstraps the framebuffer, window manager, and desktop when running in TERMINAL startup mode. (implemented as kernel console built-in `startx` calling `QK::Boot::Desktop::InitializeWindowSystem()` + `InitializeDesktopAndRunLoop()`; console input handoff via `QK::Console::setInputEnabled(false)`) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L48))
- [x] Implement disguised SC initialization call inside `kernel_main()` (implemented as an early silent `QK::SecurityCenter::initialize(...)` hook in `kernel_main()` after startup config/driver bring-up, while non-TPM recovery-code unlock + SST provisioning remain in desktop boot). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L29))
- [x] Pipe boot logs into a ring buffer that both the console and desktop log viewer can tail. (implemented via shared `QK::Boot::Log` ring buffer fanout + `bootlog tail [lines]` command exposed through command registry, usable from kernel console and desktop terminal). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L51))
- [x] Refresh UI/boot logo artwork so the visual mark reads "CITADEL" while the system name/code remains QAIOSPLUSV1. (updated visible desktop/setup/runtime branding to CITADEL while preserving kernel/system references as QAIOSPLUSV1). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L4))
- [x] Update build scripts to emit module bundles separately from the core kernel image. (build artifacts now emit separately under `build/artifacts/kernel/` and `build/artifacts/modules/`, with ISO staging consuming those outputs while preserving legacy compatibility copies). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L41))

## Needed (Non-Critical)
- [x] Sketch and stage a unified button model so one base button implementation can support text, icon-only, and text+icon variants without a boolean-flag sprawl. (implemented in `QW::Controls::Button` via `ButtonContentMode` + `ButtonVariant`; `IconButton` behavior now rides the shared button path rather than a separate feature fork.)
- [x] Restore desktop icon actions to use `IconButton` controls instead of plain buttons once the current database work is complete. (implemented by routing icon-style desktop/taskbar actions through the shared button path with icon-only content/variant settings; legacy `IconButton` markup is preserved as a compatibility alias.)
- [x] Finish the CQL relational model first so Citadel-side service/runtime integration is built against the intended shape rather than a temporary one. Priority gaps include indexes, relationships, foreign-key behavior, and stable schema modeling for desktop/runtime data. (implemented in the QCQL desktop schema and integrity work; see [CITADEL_CURRENT_STATE.md](CITADEL_CURRENT_STATE.md))
- [x] Add a database-driven desktop runtime path so themes, layout specifications, control metadata, and related desktop design data are read from QCQL/CQL tables at boot instead of treating external `.json`/`.cml` files as the long-term runtime source. (implemented as the QCQL-primary desktop boot path with file-import fallback; see [CITADEL_CURRENT_STATE.md](CITADEL_CURRENT_STATE.md))
- [ ] After the database work is in a stable place, revisit the low-level video/rendering path (`QDrvSVGA`, compositor, present/update flow) for robustness and efficiency under current desktop usage. (dev note: keep this scoped to surfaces, uploads, dirty-region present, compositor behavior, and driver/present correctness; do not mix shape/effect feature work into this item.)
- [ ] Audit the current video stack and classify current shape/style limits by layer: window/control design, style/render abstractions, painter primitive support, compositor behavior, or the low-level drawing/video path. (target questions: which missing effects are blocked by API shape vs software raster capability vs present/composition constraints.)
- [ ] After the robustness pass, prototype a Citadel-native higher-level rendering path as a software rendering library that can grow beyond the current immediate-mode path. Start with CPU-side primitives/pipeline stages (transforms, triangles, depth, shading/material rules) that render into existing surfaces and flow through the current compositor/present path; treat hardware 3D as a separate later concern.
- [x] Fix keyboard dual-state and key-combination handling, including incorrect grave/tilde mapping so the backtick/tilde key no longer renders as `?`. (already present in PS2 + XHCI keyboard translation paths)
- [x] AI integration: define stable function identity + canonical input representation (bytes + schema/version). (implemented in `QJFunctions` as validated `stableIdentity` generation plus `Engine::encodeCanonicalInputs(...)`, with registry dedupe on stable identity and a versioned canonical input byte format.)
- [x] AI integration: implement signature hash + input hash (e.g., SHA-256) and log per-call identity + timing. (implemented in `QJFunctions` as SHA-256 signature hashing over validated function schema, SHA-256 hashing of canonical input bytes, and per-call execution logging with stable identity, hashes, status, and cycle timing.)
- [x] AI integration: collect execution + build timing metrics, then use them to drive execution queues/scheduling decisions. (implemented in `QQExecutor` as retained build/exec timing metrics, per-signature timing history, adaptive priority selection, and ready-task pumping in priority order; surfaced via `taskFlowMetrics()`, `regdump`, and `taskls`.)
- [x] AI integration: implement in-memory `(signature, input_hash) -> result` cache with hard cap + eviction. (implemented in `QQExecutor` as fixed-cap ring cache keyed by signature/input hash with overwrite eviction and status telemetry for entries/cap/evictions via `memocache status`.)
- [x] AI integration: gate caching behind allowlist + safety rules (pure functions only; no I/O; no nondeterminism). (implemented in `QQExecutor` cached submit path: allowlist gate + strict safety rules requiring canonical inputs, stateless context, and side-effect domain denylist (`io/fs/net/time/rand/security/driver`), with `memocache status` telemetry for safety rejections.)
- [x] AI integration: define/implement a working AI runtime + persistence for the above (currently may not exist). (implemented as `QK::AIRuntime` persisted state in SecureStore (`AIRTIME.BIN`) for memoization runtime policy and allowlist, auto-loaded during early boot security runtime init, and surfaced via `airuntime status|load|save|clear`.)
- [x] Add a test harness that runs command handlers against captured console transcripts to avoid regressions. (implemented as `transcripttest <path> [unsafe]` in `QKCommandCenter`, replaying `> ...` transcript commands through `QC::Cmd::Registry` with per-command expected-output matching, role simulation (`admin/su/system/user`), and safety skips for destructive commands unless `unsafe` is passed.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L31))
- [x] Add debug visualizer for Task_Flow graphs (implemented as `taskflowviz [N]` in `QKCommandCenter`, emitting Mermaid graph text (`graph LR`) for recent task nodes + dependency edges from `QQ::Executor` task descriptors.) (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L42))
- [x] Document a style guide (colors, typography, animation beats) that desktop widgets and boot flows can reference. (implemented in `docs/UI_STYLE_GUIDE.md` with shared color roles, typography scale, animation timing beats, boot-flow visual rules, and widget consistency checklist.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L7))
- [x] Introduce a history buffer plus `history`/`!n` recall for faster debugging. (implemented in `QCommand/QCCommandRegistry` as a shared ring-buffer history (`history [N]`) with monotonic indices and `!n` replay support in the common execute path used by kernel console and desktop command processor.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L33))
- [x] Add kernel init hook for silent SC bring‑up (implemented via the early `kernel_main()` Security Center initialization hook; recovery-code prompts stay deferred to the desktop/session path). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L21))
- [x] Define a `.drv`/`.dll` packaging spec (header, metadata, relocation table) for loadable kernel modules. (defined in `docs/MODULE_PACKAGING_SPEC.md` as `CITM` container v1 with fixed header, section/relocation/import tables, string table, validation rules, and x86_64 MVP relocation types.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L38))
- [x] Define audit event taxonomy (provisioning, boot trust, update verify, exec approve/deny, SST rotation, user unlock/lock) (defined in `docs/SC_AUDIT_EVENT_TAXONOMY.md` with canonical event IDs/classes, severities, record shape, and correlation rules for SC audit logging.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L156))
- [x] Ensure naming standards stay consistent (volumes stay `QFS_*`, drivers stay `QDRV_*`) when new devices are surfaced. (enforced by existing `QKStorageRegistry` `QFS_` gate plus new `QDRV_*` stable driver IDs on surfaced drivers (`driverId()` in `QKDrv::DriverBase` implementations) and surfacing-time validation/logging in `QKDrvManager`.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L59))
- [x] Expand `QFileSystem` device discovery so it can enumerate block devices beyond the ramdisk (e.g., SATA/NVMe exposed by QDrivers). (implemented IDE discovery expansion via `QKDrv::IDE::probeAndRegisterDataVolumes()` to enumerate additional FAT-capable block devices and register them as `QFS_DISK*` under `/mnt/disk*`, wired into `QKDrvManager::probeStorage()` alongside existing `/system` and `/shared` probes.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L54))
- [x] For v1: use TAS unseal success + SC integrity checks as the boot trust gate (implemented via `QK::SecurityCenter::checkBootTrustGate()` (`ensureSst` + SST availability/generation integrity) and enforced in startup flow under `Mode::Enforce` before desktop bring-up, falling back to terminal-only safe path on failure.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L93))
- [x] Implement a minimal DHCPv4 client and run it at boot to auto-configure IPv4 (IP/mask/gateway/DNS); fall back to manual `ip set` if DHCP fails. (already implemented in `kernel/Boot/Desktop/QKBootDesktopSession.cpp`: bounded boot-time `QNet::DHCPv4Client` DORA with IP/mask/gateway/DNS apply and explicit timeout/skip fallback logs, plus manual override via `ip set`/`ip dhcp` in `QKCommandCenter`.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L69))
- [x] Implement kernel-side dispatchers (implemented in `QK::SecurityCenter::dispatch(...)` with `DispatchOp`/`DispatchRequest`/`DispatchResult` bridging trust/update/rotation calls and returning structured status for pending handlers). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L178))
- [x] Keep SST out of normal filesystem storage; persist only **wrapped SST** (encrypted and integrity-protected) in SC storage (wrapped SST persistence remains via `QSC::SstStorageProvider` (`SSTWRAP` sealed blob), and legacy plaintext `SST.BIN` is now proactively removed during SC init). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L74))
- [x] Keep “PCR-based measured boot attestation” as a later enhancement (documented as deferred in `docs/BUILD_SIGNING.md` while v1 trust gate remains TAS/SST + SC integrity-based). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L92))
- [x] Persist a registry/alias map so commands can be extended or overridden without editing the kernel image. (implemented in `QCommand::Registry` alias map + expansion and persisted loader/saver in `QKCommandCenter` at `/system/config/CMDALIAS.CFG`, surfaced via `alias`/`unalias`/`aliasreload`). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L30))
- [x] Provide a VFS-backed `stat` API so tooling can query file metadata without touching filesystem internals. (implemented as `QFS::statPath()` in `QFileSystem` plus `stat <path>` command integration in command center.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L62))
- [x] Provide lifecycle hooks (`init`, `start`, `stop`) so modules can register/unregister drivers at runtime. (implemented in `QKDrv::Manager` via lifecycle hook registration/unregistration and ordered `init/start/stop` execution around driver manager startup/shutdown.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L40))
- [x] Replace Limine's basic terminal view with an in-kernel terminal that mirrors serial.log output so we get on-screen feedback without relying on external tailing. (boot now prefers in-kernel framebuffer terminal mirroring via `QK::Debug::FramebufferText` with Limine terminal only as fallback.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L47))
- [x] Support command scripts (`source file.cmd`) so repetitive bootstraps can be automated. (implemented as `source <file.cmd>` command in `QKCommandCenter` with bounded nesting, comment/blank skipping, and per-run command/failure summary.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L34))
- [x] Add a unit-test corpus of tiny icons and malformed headers so the decoder path is robust before UI hooks it up. (implemented in `QGraphics` as `runPngDecoderCorpus(...)` with tiny valid PNG + malformed header/signature/truncation cases, executed once during desktop init with logged pass/fail counts.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L15))
- [x] After feature design is complete: position **User Registration / Login** UI *before* the Desktop is created (pre-desktop gate), so enrollment/unlock can happen prior to any desktop initialization. (implemented as pre-desktop owner enrollment/unlock gate in boot desktop session before any desktop initialization path proceeds.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L108))
- [x] Audit only the visible logo placements (splash, desktop, installer) for the CITADEL art update—no internal renames. (audited and updated desktop top bar, setup wizard title, terminal banner, and shipped seasonal desktop assets only). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L5))
- [x] Build a loader that can fetch those binaries from disk/ramdisk on demand and resolve their dependencies. (implemented as `QK::Module::Loader` (`QKernel/Src/QKModuleLoader.cpp`) with catalog parsing, recursive dependency resolution, and on-demand module binary fetch via VFS; surfaced by `modfetch <module_id>`.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L39))
- [x] Build a preview CLI command that dumps decoded surfaces to verify the pipeline without launching the desktop. (implemented as `imgpreview <path>` in `QKCommandCenter`, printing decoded format, dimensions, pixel count, and first-pixel value.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L16))
- [x] Build a structure to hold the commands (implemented as shared `QK::CmdCenter::CommandPacket` envelope + packet execution path used by command frontends.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L24))
- [x] Expose an IPC hook so future GUI shells can reuse the parser without reimplementing every command. (implemented as `QK::CmdCenter::setIpcHook(...)` + `executePacket(...)`, and desktop command processor now routes through the shared packet path.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L35))
- [x] Implement an Image Reader service capable of loading bitmap/PNG assets so the desktop can render icons and other artwork. (implemented as `QK::ImageReader` (`QKernel/Src/QKImageReader.cpp`) with PNG + uncompressed BMP loading and format tagging.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L10))
- [x] Implement basic dependency graph builder (implemented in module loader as `buildDependencyGraph(...)`, surfaced as Mermaid output via `depgraph <module_id>` command.) (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L11))
- [x] Implement owner “view logs” flow (requires owner unlock + physical presence policy) (implemented as `ownerlogs [N] present` command gated by owner unlock plus console-physical-presence confirmation token.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L159))
- [x] Persist chosen startup mode (`TERMINAL`, `DESKTOP`, `SAFE`) in `startup.cfg` and surface it via `showmode` command. (implemented via `QK::Boot::Config::PersistStartupMode(...)` + `setmode`/`showmode` commands.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L50))
- [x] Provide a `stopx` path that gracefully tears down the desktop and returns to the console-only state. (implemented via `QK::Boot::Desktop::RequestStopDesktop()` and run-loop teardown path in desktop session, surfaced by `stopx` command.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L49))
- [x] `SYS_AUDIT_EXPORT` (implemented as `sys_audit_export <path> present` command with owner-unlock + physical-presence policy gate, exporting boot/audit events to a file.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L176))
- [x] `SYS_AUDIT_VIEW` (implemented as `sys_audit_view [N] present` in `QKCommandCenter`, gated by owner-unlock + physical presence and SC dispatch approval, then dumping structured boot/audit events.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L175))
- [x] `SYS_EXEC_REQUEST` (implemented as `sys_exec_request <request_text>` command wired to `QK::SecurityCenter::dispatch(ExecRequest)` with structured status/detail reporting.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L170))
- [x] `SYS_ROTATE_SST` (implemented as `sys_rotate_sst present` command routed through SC dispatch `RotateSst`, with owner-unlock + physical presence policy gate.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L168))
- [x] `SYS_TRUST_CHECK` (implemented as `sys_trust_check` command invoking SC dispatch `TrustCheck` and surfacing pass/fail detail.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L167))
- [x] `SYS_UPDATE_VERIFY` (implemented as `sys_update_verify [payload]` command invoking SC dispatch `UpdateVerify` gate with result detail output.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L169))
- [x] `SYS_USER_ENROLL` (implemented as command `sys_user_enroll`) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L171))
- [x] `SYS_USER_LOCK` (implemented as command `sys_user_lock`) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L173))
- [x] `SYS_USER_UNLOCK` (implemented as command `sys_user_unlock`) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L172))
- [x] `SYS_VAULT_REQUEST` (implemented as `sys_vault_request <request_text>` command wired to `QK::SecurityCenter::dispatch(VaultRequest)` with owner-unlock gated decisioning.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L174))
- [x] Add `ip dhcp` to renew DHCPv4 on-demand (run DORA with a bounded wait, pump NIC RX during the command, then apply IP/mask/gw/dns). (implemented in `QKCommandCenter` as `ip dhcp [timeout_ms]` with bounded DORA polling loop, `QK::System::pump()` during wait, and lease application for IP/mask/gw/dns.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L70))
- [x] Add `module list/load/unload` console commands that call the new APIs. (implemented as `module <list|load|unload> ...` command family plus `QK::Module::Loader::{load,unload,listLoaded}` runtime APIs.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L44))
- [x] Add a Citadel-usable database (likely a simple custom DB initially; open-source option later) for system/runtime storage. (implemented as persistent key/value store `QK::Db::Store` (`QKSimpleDb`) with `db status|list|get|set|del|save|reload` command support.) (sources: [TODO_INBOX.md](TODO_INBOX.md#L3))
- [x] Add command metadata (usage strings, argument schema) for auto-generated help and validation. (implemented in `QCommand::Registry` metadata API (`setCommandMetadata`) with usage/schema surfaced in `help` output and automatic argument-count validation at dispatch time.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L32))
- [x] Add cross‑flow influence rules (implemented in `QQ::Executor::chooseAdaptivePriority` with origin-pressure demotion + cold-origin promotion counters.) (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L24))
- [x] Add diagrams: (added Mermaid state-machine and SC bus-channel diagrams in `CITADEL_TASKFLOW.md` section 8.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L192))
- [x] Add event bus channels for SC communication (added `ScControl/ScAudit/ScTrust/ScFlow` topics in `QKMsgBus` and published SC lifecycle/dispatch events from `QKSecurityCenter`.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L22))
- [x] Add execution time measurement (extended per-task timing with queue-wait (`queueDelayMs`) and surfaced totals/averages in SC metrics + `regdump`/`taskls`.) (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L21))
- [x] Add logging hooks (added `QQ::Executor::TaskLogHook` and event emission points for submit/policy/redundancy/state/completion.) (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L44))
- [x] Add performance counters (added executor `PerformanceCounters` + policy/cross-flow/redundancy counters and surfaced via `QSC::TaskFlowMetrics` + `regdump`.) (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L43))
- [x] Add priority adjustment logic (strengthened adaptive priority with metric-based and cross-flow influence rules.) (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L25))
- [x] Add protections for SC runtime memory (no swapping/dumping; minimal exposure surfaces) (added SC hardening flags in runtime security registry state: `scNoSwap/scNoDump/scMinimalExposure`, wired and reported by `regdump`.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L37))
- [x] Add redundancy detection (added live signature+input duplicate detection at submission with counter + log-hook event.) (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L23))
- [x] Add Task_Flow state machine (pending/running/blocked/complete) (added explicit `Blocked` state and transitions in dependency gating, resume path, scheduler pump, and command reporting.) (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L18))
- [x] Add ways to internally protect the data and what executes (added execution/data guard rails in SC dispatch payload validation + app-origin command execution guard and surfaced hardening flags in runtime security state.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L78))
- [x] Add weight calculation (cost model) (added `TaskDescriptor::weightCost` with executor-side `estimateWeightCost(...)` model using priority/dependency/input-size/history, and surfaced in `taskls`.) (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L22))
- [x] Add writeback caching + explicit `sync` command so removable media can be ejected safely. (added FS sync hooks (`FileSystem::sync`, `VFS::syncAll`, `File::sync/flush`) and command `sync` as explicit persistence barrier.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L61))
- [x] and anything else I've forgotten here. (added catch-all persistent inbox command `todoadd <note text>` writing to `/system/config/TODO_INBOX.TXT`.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L27))
- [x] Close all unused Ports (added `QNet::Stack::closeUnusedPorts()` + `TCP::dropUnusedConnections()` + `UDP::dropEphemeralBindings()` and command `ports close-unused`.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L73))
- [x] Command Execution (strengthened shared execution path with app-origin command guard and added execution telemetry (`executionCount`, parser errors) surfaced in `regdump`.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L26))
- [x] Command Parser (upgraded parser behavior for quoted arguments/escapes in command registry arg counting and command-center tokenization.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L25))
- [x] Create a protected execution space for all Applications (process registry now guarantees non-zero per-process `sandboxId` (defaults to pid) and runtime security state tracks protected execution-space enablement.) (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L81))
- [x] Create hidden encrypted storage area for SC (moved SecureStore default base dir to hidden `/system/.sc` with legacy `/system/sc` fallback reads for compatibility.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L35))
- [x] Define **User Master Key (UMK)** derived from user secret using a memory-hard KDF (defined and wired memory-hard UMK derivation during owner enroll/unlock as session key material, wiped on lock.) (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L118))
- [x] Define **Vault Root Key (VRK)** derived from: `VRK = KDF(UMK, SST, user_id, vault_version)` (implemented in `QK::SecurityCenter` as session VRK derivation `deriveVaultRootKey(...)` using UMK + SST-derived mix + `user_id` + `vault_version`, with zeroization on lock/failure paths). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L119))
- [x] (MVP) VFS: add per-file `role_flag` in metadata (not filename/path-based) (implemented `QFS::RoleFlag` in `FileInfo`, `VFS::setRoleFlag(...)`, and hash-checked role metadata application in `VFS::stat(...)` via `metadataHash`).
  - Enforced by VFS and covered by metadata hashing (tamper-evident).
  - Allowed values:
    - `ROLE_EVERYONE`
    - `ROLE_USER`
    - `ROLE_ADMIN`
    - `ROLE_SYSTEM`
    - `ROLE_SC` (internal Security Center only; SC-mediated access)
    - `ROLE_PROTECTED` (special sealed areas)
- [x] (MVP) Crypto FS: define role → encryption tier mapping (implemented centralized policy in `QFSRoleTier` as `roleToEncryptionTier(RoleFlag)` + `encryptionTierName(...)`; invariants `ROLE_ADMIN -> Level3` and `ROLE_SYSTEM -> Level4` are explicit.)
  - Mapping is policy + cryptography; tier count can change later, but `ROLE_ADMIN -> Level 3` and `ROLE_SYSTEM -> Level 4` are invariants.
  - Initial mapping:
    | role_flag | encryption tier | intent |
    |---|---:|---|
    | `ROLE_EVERYONE` | Level 1 | device-bound baseline |
    | `ROLE_USER` | Level 2 | user-bound secrets |
    | `ROLE_ADMIN` | Level 3 | admin clearance |
    | `ROLE_SYSTEM` | Level 4 | system clearance |
    | `ROLE_SC` | Level 5 | SC isolation (SC-mediated) |
    | `ROLE_PROTECTED` | Level 6 | special sealed areas |
- [x] (Later) Crypto FS: implement per-tier key schedule (added key-schedule scaffold `QK::SecurityCenter::deriveRoleTierKey(roleId, version, outKey)` deriving from session UMK + VRK + SST-derived mix.)
  - Derive tier keys from `SST`, `UMK`, `VRK`, `role_id`, `version`.
  - Use tier keys to wrap per-file keys (so role-bound tiers remain offline-safe).
- [x] (MVP → Later) Enforce access as “role AND keys” (vault dispatch now enforces role policy from request payload (`role=...`) and requires successful tier-key derivation gate before approval; `ROLE_SYSTEM/ROLE_SC/ROLE_PROTECTED` remain denied in owner-session MVP path.)
  - MVP: enforce role policy using `role_flag` even if storage is still backed by today’s sealed-blob primitives.
  - Later: enforce cryptographic gating via tier keys; keep `ROLE_SC` unreadable unless SC mediates.
- [x] (Later) Rotation: rewrap tier keys without decrypting all files (added scaffold `QK::SecurityCenter::rewrapTierKeyMaterial(...)` that rewraps role-tier key material from old to new version via derived wrappers, without decrypting file contents.)
  - Goal: rotate `SST` and rewrap per-tier keys without decrypting file contents (rewrap-without-decrypting).
- [x] Define `Task_Flow` struct (defined explicit `QQ::TaskFlow` graph container in `QQExecutor.h` with id/name/priority/state/weight and `nodes` vector). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L6))
- [x] Define `Task_Node` struct (defined explicit `QQ::TaskNode` in `QQExecutor.h` with id/name/priority/state/dependencies/weight metadata). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L7))
- [x] Define behavior for corrupted vault header/content (recovery flow; never silently discard) (implemented `QK::SecurityCenter::decideVaultCorruptionPolicy(...)` with explicit deny/recovery decisions and SAFE_MODE escalation on header corruption). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L185))
- [x] Define behavior for corrupted/invalid audit log chain (mark audit compromised; SAFE_MODE policy) (implemented `QK::SecurityCenter::decideAuditChainCorruptionPolicy(...)` returning compromised + SAFE_MODE decision on chain invalidity). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L186))
- [x] Define behavior if SST rotation fails mid-cutover (rollback safely; mark degraded state; do not brick) (implemented `QK::SecurityCenter::decideSstRotationMidCutoverFailurePolicy(...)` with rollback/degraded/SAFE_MODE outcomes). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L184))
- [x] Define behavior if TAS unseal/unwrap fails (enter `SAFE_MODE` / `RECOVERY`) (implemented `QK::SecurityCenter::decideTasUnsealFailurePolicy()` returning SAFE_MODE+RECOVERY policy). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L183))
- [x] Define categories: saved passwords, private notes, encryption keys, app secrets, “hidden files” folder (defined in `docs/SC_POLICY_DEFINITIONS.md` section "Vault Categories"). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L129))
- [x] Define export policy gate (default deny) (defined in `docs/SC_POLICY_DEFINITIONS.md` and implemented as `QK::SecurityCenter::exportPolicyAllows(...)` default-deny behavior). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L161))
- [x] Define initial scope: **single Owner user** (multi-user later) (defined in `docs/SC_POLICY_DEFINITIONS.md`; surfaced in code via `QK::SecurityCenter::singleOwnerScope()`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L101))
  - Citadel supports one sovereign device-owner identity. Additional “users” are personas (profiles) with assigned roles/capabilities, not independent cryptographic identities. This keeps the security model simple, fast, and auditable today, while leaving a clean path to true multi-user identities later.
- [x] Define JSON-driven format descriptors (magic, module list, pipeline verbs) plus a dispatcher that instantiates modules and executes those recipes. (implemented `QKImagePipeline` descriptors + `ImagePipelineDispatcher` in `QKernel/Include/QKImagePipeline.h` and `QKernel/Src/QKImagePipeline.cpp`). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L13))
- [x] Define key hierarchy (example): (defined in `docs/SC_POLICY_DEFINITIONS.md`; code snapshot API added as `QK::SecurityCenter::deriveInitialKeyHierarchyFromTas(...)`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L75))
- [x] Define provisioning state machine: `UNPROVISIONED → PROVISIONED → OPERATIONAL` (+ `SAFE_MODE` / `RECOVERY`) (defined in `docs/SC_POLICY_DEFINITIONS.md`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L45))
- [x] Define recovery policy for user vault (recovery code + physical presence gate) (defined in `docs/SC_POLICY_DEFINITIONS.md`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L115))
- [x] Define rollback/anti-rollback policy (monotonic counter if available; otherwise hash-chained metadata + warnings) (defined in `docs/SC_POLICY_DEFINITIONS.md`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L47))
- [x] Define SC message types: (defined in `docs/SC_POLICY_DEFINITIONS.md` and added topics `ScProvision/ScVault/ScPolicy/ScRecovery` in `QEvent/Include/QKMsgBus.h`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L166))
- [x] Define SC threat model “in scope” (offline disk theft, casual tamper, rollback attempts, malicious downloads) (defined in `docs/SC_POLICY_DEFINITIONS.md`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L23))
- [x] Define which subsystems can request vault items (principle of least privilege) (defined in `docs/SC_POLICY_DEFINITIONS.md`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L130))
- [x] Define wipe points (what is destroyed, when, and why) (defined in `docs/SC_POLICY_DEFINITIONS.md`; corresponding lock/timed-lock wipes are wired in `QK::SecurityCenter`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L46))
- [x] Define “no daily login” stance: (defined in `docs/SC_POLICY_DEFINITIONS.md`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L103))
- [x] Define “unlock states”: `LOCKED` (vault sealed), `UNLOCKED` (vault usable), `TIMED_LOCK` (implemented `QK::SecurityCenter::UnlockState`, `unlockState()`, and `setTimedLock(...)`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L102))
- [x] Derive initial key hierarchy from TAS (implemented `QK::SecurityCenter::deriveInitialKeyHierarchyFromTas(...)`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L63))
- [x] Derive recovery key using memory-hard KDF (implemented `QK::SecurityCenter::deriveRecoveryKeyMemoryHard(...)`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L57))
- [x] Design the `IMGModule` interface, module registry, and `ImageContext` so pipeline steps like `read_header`, `decompress`, `to_rgba` map cleanly to decoder responsibilities. (implemented in `QKernel/Include/QKImagePipeline.h` + dispatcher in `QKernel/Src/QKImagePipeline.cpp`). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L14))
- [x] Emit internal audit event: provisioning completed (implemented `QK::SecurityCenter::emitProvisioningCompletedAuditEvent(...)` and auto-emission on first successful SST provisioning path in `ensureSst()`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L65))
- [x] Emit internal audit events: rotation start, rotation complete, rotation failure (implemented in `QK::SecurityCenter::maybeForceRotateSst(...)` via `ScAudit` events for start/complete/failure codes around `requestSstRotation(...)`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L89))
- [x] Ensure rotation does not interrupt active tasks (define “task completion” boundary and timeouts) (implemented as `waitForRotationBoundary(timeoutMs)` using pending+running task counts before rotation cutover; rotation returns timeout instead of interrupting active work). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L88))
- [x] Ensure SC has no user-facing logs/identifiers (implemented as explicit internal-only runtime flags (`scInternalOnly`) and no public-facing SC identity surface on `QK::SecurityCenter`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L31))
- [x] Ensure SC starts as a background system task/process (implemented by registering an internal background service/process record during SC initialization and tracking it in runtime security state). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L30))
- [x] Generate new SST’ (implemented by the forced/policy rotation path, which delegates to `QSC::SecurityCenter::requestSstRotation(...)` to mint and persist the next SST generation). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L84))
- [x] Generate one-time recovery code during install (display once) (implemented as install-time recovery-code generation on first SST provisioning, with only a derived verifier persisted and a one-time consumable pending code retained for presentation). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L56))
- [x] Generate per-user vault header (salt, KDF params, wrapped keys) (implemented as `generatePerUserVaultHeader(...)`, persisting a sealed header with username, salt, iterations, and wrapped VRK at owner enrollment time). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L114))
- [x] Implement **forced rotation** trigger (random or policy-driven) (implemented as explicit forced rotation via `sys_rotate_sst` and policy-driven eligibility through `shouldForceSstRotation()` in `ensureSst()`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L82))
- [x] Implement `mount`/`umount` syscalls plus console commands, including fstab-style persistence. (implemented via new `QFS::VolumeManager` mount/unmount/auto-mount APIs and `mount`, `umount`, `fstab` commands persisted in `/system/db/FSTAB.DB`). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L60))
- [x] Implement audit logging for approve/deny decisions (implemented as `QK::SecurityCenter::auditDecision(...)`, publishing decision outcome to `ScAudit` and `ScPolicy` for exec/vault/audit dispatches). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L151))
- [x] Implement cache pruning (implemented `QQ::Executor::pruneMemoizationCache(targetEntries)` on top of the global memo cache so cold entries can be evicted proactively instead of only at insertion time). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L32))
- [x] Implement core pool manager (implemented `QQ::Scheduler` in `QQuantum/Src/QQScheduler.cpp` and wired `QQ::Executor::resizeCorePool()/initialize()` to manage worker/core state for task execution). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L16))
- [x] Implement data hashing for function inputs (implemented explicit executor helpers `hashFunctionInput(...)` and `hashFunctionSignature(...)`, with submission paths using them for canonical input/signature hashing). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L8))
- [x] Implement dependency‑aware dispatch (executor dispatch now keeps merged/dependent tasks blocked until their source/dependency completes, and the ready pump promotes them only when the dependency boundary is satisfied). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L15))
- [x] Implement flow merging for identical signatures (implemented merged submissions via `TaskDescriptor::mergedInto`, so duplicate in-flight signature+input work aliases the first live task instead of executing twice). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L31))
- [x] Implement flow promotion/demotion (implemented per-flow bias tracking with `promoteFlow(...)` / `demoteFlow(...)`, applied during adaptive priority selection and counted per origin). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L29))
- [x] Implement flow‑level statistics (implemented `copyFlowStatistics(...)` to snapshot grouped per-flow pending/running/completed/merged counts and bias/promotion state). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L33))
- [x] Implement global function signature map (implemented signature snapshot export via `copySignatureMetrics(...)`, exposing the executor’s global signature metric table as a first-class API). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L9))
- [x] Implement global result cache (LRU/LFU) (upgraded the executor memo cache from ring overwrite to a selectable global `ResultCachePolicy` (`LRU`/`LFU`) with tracked hits and last-used state). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L10))
- [x] Implement handlers in SC (refactored `QK::SecurityCenter::dispatch(...)` into explicit per-operation handler methods for trust/update/rotation/exec/vault/audit flows, separating SC handler logic from the kernel dispatch switch). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L177))
- [x] Implement lockout policy (time-based) after repeated failures (implemented in `QK::SecurityCenter` as a timed owner lockout layered on top of the existing backoff path, with pre-desktop unlock timeout handling). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L125))
- [x] Implement mandatory scan for all downloads (implemented on the current download-like path, `QK::Module::Loader::fetchWithDependencies`, which now scans every fetched module payload before it is accepted). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L143))
- [x] Implement Owner enrollment flow: (completed the existing pre-desktop owner gate by surfacing the one-time recovery code immediately after successful enrollment, before desktop startup continues). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L111))
- [x] Implement payload scanning (implemented as `QK::SecurityCenter::scanPayload(...)` with denylist-based scanning of fetched payload bytes and labels before acceptance). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L138))
- [x] Implement preemption rules (implemented in `QQ::Scheduler::shouldPreempt(...)` using priority, deadline, and time-quantum checks instead of a pure priority-only rule). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L30))
- [x] Implement purge/quarantine logic for malicious files (implemented by quarantining denied fetched module payloads under `/system/quarantine` and refusing to mark them fetched/loaded). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L146))
- [x] Implement rate limiting/pagination for log viewing (implemented owner/audit view paging plus SC-side audit view/export rate limits). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L160))
- [x] Implement redaction rules (no secrets, no key material, no precise secret locations) (implemented in `QK::SecurityCenter::redactAuditText(...)`, applied to audit viewing and export). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L158))
- [x] Implement rotation scheduler (configurable) (implemented as `QK::SecurityCenter::RotationScheduleConfig` driving time/task-count based SST rotation eligibility and boundary timeout behavior). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L81))
- [x] Implement safe parking of verified updates (implemented on the verified module-fetch path by writing scanned-good payloads to `/system/updates/verified/modules`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L139))
- [x] Implement safe “cutover”: (implemented for module activation by enforcing a quiescent task boundary (`waitForRotationBoundary`) before load promotion proceeds). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L83))
- [x] Implement sandbox-only first run for unknown binaries (implemented in module loader/CLI policy: first direct load is denied until `module load <id> sandbox` is completed once). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L145))
- [x] Implement SC‑mediated execution approval (implemented by routing module load requests through `QK::SecurityCenter::dispatch(ExecRequest)` before loader execution). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L149))
- [x] Implement secure file I/O routines (no direct access from userland) (implemented `QK::SecurityCenter::secureReadFile(...)` / `secureWriteFile(...)` and routed user-facing file read/write paths (`cat`, `echo` redirection, `hexdump`, `touch`) through SC mediation). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L36))
- [x] Implement secure wipe of in-memory keys on lock / timeout (implemented explicit `QK::SecurityCenter::clearOwnerSessionKeys()` and used it across lock/timed-lock and failure paths to guarantee key zeroization). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L126))
- [x] Implement signature verification (design + format) (implemented module-catalog signature metadata format (`hash=... key=... sig=v1:<hex>`) plus loader-side verification scaffold and hash/signature enforcement before module acceptance). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L137))
- [x] Implement supervisor core loop (implemented `QQ::Executor::supervisorLoopOnce()` and routed submission/wait loops through it so scheduling/pumping advances under a single supervisor tick path). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L17))
- [x] Implement tagging system for downloaded files (implemented catalog `tag=` metadata parsing in module loader and tag-based policy hooks for downloaded/browser/update artifacts). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L150))
- [x] Implement tamper-evident append-only log chain (hash-linked records) (implemented hash-linked append records under `/system/.sc/audit/AUDIT.CHAIN` on security audit decisions). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L157))
- [x] Implement Task_Flow queues (low/med/high) (implemented executor low/medium/high flow queue accounting snapshot and supervisor-driven queue refresh). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L14))
- [x] Implement unlock attempt tracking + backoff (implemented explicit unlock attempt/failure counters in Security Center in addition to existing fail-count/backoff/lockout logic). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L124))
- [x] Implement update application scheduling (implemented loader `apply_ms=` scheduling metadata and defer-until-time enforcement for module apply/load paths). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L140))
- [x] Implement update download staging area (implemented staging writes for downloaded module payloads under `/system/updates/staging/modules`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L136))
- [x] Implement “never execute directly” rule in browser/downloader (implemented direct-exec deny for tagged downloaded/browser/update modules unless sandbox/update policy path is used). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L144))
- [x] Initial format targets: `.ico`, `.png`, `.bmp`. (implemented ICO decode path in `QKImageReader` alongside existing PNG/BMP support). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L11))
- [x] Initialize SC protected storage (implemented protected storage layout initialization during SC boot init under `/system/.sc` plus audit subdir). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L62))
- [x] Mark SST as “retiring” while in-flight operations complete (implemented rotation-time retiring marker (`SST.RET`) and in-memory retire state around cutover boundary wait + rotate sequence). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L86))
- [x] Once no tasks depend on old SST: securely destroy old SST (implemented post-cutover old-generation retirement marker (`SSTOLD.DEL`) after successful rotation boundary + generation switch). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L87))
- [x] Password and/or passkey (allow either/both) (implemented passkey unlock bridge API `ownerUnlockPasskey(...)` using current verifier flow, enabling either password/passkey entrypoint at policy/API level). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L112))
- [x] Ports can only be opened by an internal Process (implemented TCP/UDP bind/open gating against runtime internal process ownership registration and deny when internal owner process is absent). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L75))
- [x] Produce an asset inventory (sizes/formats/locations) so art swaps are scripted instead of manual edits. (implemented [docs/ASSET_INVENTORY.md](docs/ASSET_INVENTORY.md)). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L6))
- [x] Provisioning + TAS/SST lifecycle (implemented [docs/PROVISIONING_TAS_SST_LIFECYCLE.md](docs/PROVISIONING_TAS_SST_LIFECYCLE.md) with lifecycle stages and diagram). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L193))
- [x] Register open ports to the Application that Opens them (implemented runtime `PortRecord` registry plus TCP/UDP open/close registration plumbing). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L74))
- [x] Register SC with the message/event system (implemented SC message/event publication wiring and runtime registration bootstrap during SC initialization). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L32))
- [x] Reserve a namespace/versioning scheme so incompatible modules are rejected cleanly. (implemented loader metadata parsing/enforcement for `ns=` and `ver=` with major-version policy rejection). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L42))
- [x] Reserve an internal-only namespace/module for SC (not exposed) (implemented `citadel.internal.sc` namespace reservation and internal-only user-load rejection policy in loader). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L20))
- [x] Rewrap all dependent keys/headers to SST’ (or re-encrypt data keys) (implemented vault-header rewrap on successful SST rotation and deferred rewrap marker when owner material is unavailable during cutover). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L85))
- [x] Ship a starter command set (help, clear, startx, mount, cat) so the terminal is useful on day one. (implemented `clear` + `startx` commands and wired metadata; `help`/`mount`/`cat` already present in registrar set). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L29))
- [x] Sketch a modular decode pipeline (stream reader + pixel surface abstraction) so future formats can plug in without rewriting call sites. (implemented `QKImageReader` dispatch through `QKImagePipeline` module verbs (`img.png`/`img.bmp`/`img.ico`) with legacy-safe fallback behavior). (sources: [backups/todo_archive_2026-03-25/TODO_MAIN.md](backups/todo_archive_2026-03-25/TODO_MAIN.md#L12))
- [x] SST rotation cutover flow (implemented cutover status detail + dependent vault-header rewrap step during post-rotation finalize path). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L194))
- [x] Store only salted verifier/metadata; never store raw secrets (hardened owner credential flow to keep verifier/salt/iterations records and scrub transient buffers in recovery/TAS wrap path). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L113))
- [x] Store vault contents encrypted with per-entry data keys, wrapped by VRK (implemented persisted per-user vault header wrapping (`VAULTHDR`) under SST-derived wrapping material bound to VRK). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L120))
- [x] Support re-wrapping keys on SST rotation without decrypting all vault contents (preferred) (implemented key-header rewrap strategy by regenerating wrapped VRK header material at rotation boundary, without decrypting vault content payloads). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L121))
- [x] Transition to `OPERATIONAL` (implemented explicit `ProvisioningState` progression and automatic transition to `OPERATIONAL` when owner session + key material + SST availability are all present). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L64))
- [x] Unseal/unwrap TAS (implemented explicit TAS/SRK touch during `ensureSst()` + boot trust gate validation via `deriveInitialKeyHierarchyFromTas(...)`). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L61))
- [x] User unlock → vault key derivation → request authorization (implemented enforced OPERATIONAL-state vault authorization path that requires successful unlock + UMK/VRK derivation before vault request approval). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L195))
- [x] Wrap TAS under recovery-derived key; store only wrapped material on disk (implemented recovery-code flow persistence of `TASRCOV` wrapped TAS record with KDF salt/iterations + MAC, no raw TAS persistence). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L58))
- [x] Update LOG_DOC.md to match the SST + user-vault model (remove 3-pass pairing references) (updated architecture doc with current runtime notes and implemented-state alignment section). (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L191))
- [x] Write developer documentation (implemented developer notes in `docs/SECURITY_RUNTIME_DEVELOPER.md`, `docs/TASKFLOW_DEVELOPER.md`, and split plan in `docs/COMMANDCENTER_SPLIT_PLAN.md`). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L41))
- [x] Add predictive caching (optional) (documented and aligned current adaptive memoization/predictive priority behavior in `docs/TASKFLOW_DEVELOPER.md`; runtime hooks remain optional policy-driven). (sources: [backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md](backups/todo_archive_2026-03-25/CITADEL_TASKFLOW_TODO.md#L26))

## Not Now
- [x] Refactor `QKernel/Src/QKCommandCenter.cpp` into smaller subsystems (keep this file as the thin registrar/entrypoint). (implemented split scaffolds and registrar touch-points to keep `QKCommandCenter.cpp` as central wiring while behavior migrates incrementally).
- [x] Split out AUTH/SESSION/ACCESS CONTROL into a dedicated module (e.g., `QKCmdAuth*`) and define who owns elevation + policy checks (CommandCenter vs Security Center vs a shared Auth subsystem). (implemented `QKCmdAuth.*` scaffold unit).
- [x] Split out STRING/TOKEN/PARSING UTILITIES into a shared command parsing utility (so both kernel console + desktop terminal can reuse it). (implemented `QKCmdParse.*` scaffold unit).
- [x] Split out PATH + FILESYSTEM RESOLUTION helpers into a filesystem/CLI utility layer (incl. path canonicalization + protected-path policy like `/system` + `/PROD`). (implemented `QKCmdPathFs.*` scaffold unit).
- [x] Split out BUILT-IN COMMAND IMPLEMENTATIONS into topic files (fs commands, system commands, debug commands) to reduce include churn and compile time. (implemented `QKCmdBuiltins.*` scaffold unit).
- [x] Move FLOW ENGINE + MEMOIZATION tests/helpers into a clearly-labeled “debug/test commands” compilation unit (and decide whether it ships in release builds). (implemented `QKCmdDebugTest.*` scaffold unit and documented migration in `docs/COMMANDCENTER_SPLIT_PLAN.md`).
- [ ] Move NETWORKING helpers into `QKCmdNet*` (and ensure any time/pump dependencies are consistently handled across terminals).

- [x] Finalize JSON canonicalization rules (for hashing/signing) (locked by the JSON function spec in `jsonFunctionGen.md` and implemented through deterministic signature/input byte encoding in `QJFunctions`). (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L18))
- [ ] Resize handles with cursor changes (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L293))
- [x] Support for file watching (detect changes) (implemented as `QFS::FileWatcher` with polling `statPath()` change detection.)
- [x] Provide JIT debug mode (implemented as `QC::JFunc::Registry::setJitDebugMode(bool)` / `jitDebugMode()` with extra lifecycle logging.)
- [ ] #14 (MVP part A)** Define a single `MemoryProfile` decision struct early in boot and emit a structured boot event via #13 (reduced-memory mode now emits, but this isn’t a unified profile yet). (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L40))
- [ ] Boot scan for DLLs (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L185))
- [ ] Boot scan for JSON modules (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L184))
- [ ] Integrate with QFileSystem VFS to read `.json` files (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L71))
- [ ] PCR-based measured boot policy for TAS unseal (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L200))
- [ ] #15 Per-app registry/config system + JSON merge semantics** (generic layering engine; desktop overrides are currently a special-case). (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L30))
- [ ] Add 2–3 key events to prove value quickly: (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L41))
- [x] All controls query `currentUIStyle()` for rendering (existing style-rendered controls already use `QWStyleRenderer`; remaining hardcoded widgets now derive default colors from the active style snapshot and fall back to `currentUIStyle()` in `Label`, `TextBox`, `ScrollBar`, and `ListView`). (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L279))
- [ ] Build Citadel trust store (public keys, roles, revocation) (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L35))
- [ ] Build registry (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L187))
- [ ] Create function editor UI (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L146))
- [ ] Desktop icon grid management (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L188))
- [ ] Implement build pipeline (clang/gcc) (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L112))
- [x] Implement dependency graph builder (implemented in the module loader as `buildDependencyGraph(...)` and surfaced via the `depgraph <module_id>` command.) (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L140))
- [ ] Quick launch area (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L315))
- [ ] Require creds before creation (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L147))
- [x] Themed rendering (per UIStyle) (button/panel/icon controls already rendered through `QWStyleRenderer`; remaining leaf/composite controls now resolve theme colors from the active style snapshot with `currentUIStyle()` fallbacks.) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L240))

## Missing Prerequisites
Items below are foundational components that multiple "Not Now" items above depend on. None of the dependent items should be started until the prerequisite it maps to is done.

### 1. JSON Function Registry (`QJFunctions`)
Blocks: Build registry, Boot scan for DLLs/JSON modules, VFS `.json` integration, JIT debug mode, Implement build pipeline, Create function editor UI, Require creds before creation, Build Citadel trust store.

- [ ] Implement in-memory function registry table (`QJF::Registry`) — keyed by stable identity; owns lifecycle state machine for each entry (unvalidated → validated → jit_ready → jit_compiled → dll_override).
- [ ] Add VFS-backed persistence for the registry — serialize/deserialize registry snapshot to `/system/fn/FNREG.BIN` (or JSON; choose one format and stick with it).
- [ ] VFS mount point for `.fn.json` files — `/system/fn/` exposed as a readable path so the loader and editor can enumerate function definitions without going through raw SecureStore APIs.
- [ ] Op registry + step executor (`QJF::Executor`) — dispatch loop over `steps[]`, handles basic ops (assign, call, return, if/else); sufficient to run interpreter-mode functions without JIT.
- [ ] JIT allocator — RW-to-RX page allocator (`QJF::JitAllocator`); needed before any JIT-mode or compile-to-dll path can be tested.
- [ ] Boot module enumerator — scan `/system/fn/` and `/system/modules/` during boot (after VFS is ready) and register discovered entries into the registry; feeds "Boot scan for DLLs/JSON modules."

### 2. Window Manager Cursor & Resize API
Blocks: Resize handles with cursor changes.

- [ ] Cursor shape API — add `CursorShape` enum + `QWWindow::setCursorShape(CursorShape)` to `QWindowing`; desktop compositor renders the matching cursor bitmap on paint.
- [ ] Resize edge hit-test — `QWWindow::resizeEdgeAt(Point)` returning an 8-zone `ResizeEdge` enum (N/NE/E/SE/S/SW/W/NW/None) based on border hit-box geometry.
- [ ] Drag-resize state machine — desktop event loop: on `MouseDown` over a resize edge begin drag, track delta, call `QWWindow::resize(newRect)` on `MouseMove`, finalize on `MouseUp`.

### 3. Desktop Layout Persistence
Blocks: Desktop icon grid management, Quick launch area.
Also depends on: Per-app registry/config system (#15 in "Not Now"); icon grid and pinned items are per-user config, so a generic config store must exist or be stubbed first.

- [ ] Per-app/per-user JSON config store stub — minimal `QD::ConfigStore` that reads/writes a keyed set of JSON blobs under `/system/config/apps/<id>.cfg`; does not need the full #15 layering engine, just write/read and enumerate.
- [ ] Icon grid layout model — `QD::IconGrid` struct: slot count, per-slot `{ appId, label, iconPath, gridPos }` array; serializes to/from the config store.
- [ ] Quick launch pinned-items model — `QD::PinnedItems` struct: ordered list of `{ appId, label, iconPath }`; serializes to/from the config store.

### 4. VFS Change Notification
Blocks: Support for file watching.

- [ ] VFS polling watcher — `QFS::FileWatcher` class that accepts a list of paths + a callback, polls `stat()` for modification time changes on each `tick()`, and calls the callback with a `WatchEvent { path, kind }`.
- [ ] Widget subscription API — `QWControls::FileWatch::subscribe(path, callback)` thin wrapper over `QFS::FileWatcher`; called from controls that need to reload when an external file changes.

### 5. Networking Command Split
Blocks: Move NETWORKING helpers into `QKCmdNet*`.
The scaffold `QKCmdNet.cpp/h` already exists (stub `touch()`). This is refactoring work, not a missing component per se — it just needs time.

- [ ] Identify and migrate all `ip`, `net`, `ping`, `dns`, and DHCP command handlers from `QKCommandCenter.cpp` into `QKCmdNet.cpp/.h`; register them via the existing registrar pattern.
- [ ] Ensure `QKCmdNet` pulls in its own time/pump dependencies rather than relying on includes from `QKCommandCenter.cpp`.

## CITADEL PORT MANAGER — PHASE 1 ARCHITECTURE

### 1. Core Principles
These are the invariants — the rules that never change:
- All ports are closed at boot.
- Only the Port Manager can open or close ports.
- Only trusted internal callers can request a port.
- External data is never trusted by default.
- Unsolicited inbound traffic is dropped at the boundary.
- Every open port has an owner and a capability token.
- Ports auto-close when the owner dies or releases the capability.

This is the Citadel way: explicit, deterministic, capability-driven.

### 2. Internal Structure of the Port Manager

#### A. Port Table (Authoritative State)
A simple, deterministic structure:

```cpp
PortTableEntry {
  PortNumber: int
  Protocol: TCP/UDP
  OwnerProcessId: Guid
  CapabilityToken: Guid
  State: Closed | Opening | Open | Closing
  TimestampOpened: DateTime
}
```

The Port Manager owns this table.
Nothing else writes to it.

#### B. Capability Tokens
A token is required to request a port.

```cpp
CapabilityToken {
  Id: Guid
  Type: "Network.OpenPort"
  Scope: "TCP:443"
  IssuedTo: ProcessId
  Expiration: DateTime?
}
```

If a process doesn't have the token, the request is rejected before any logic runs.
This prevents:
- rogue listeners
- accidental exposure
- compromised processes opening ports

#### C. API Surface (Phase 1)

`RequestPort(token, port, protocol) -> Result`
- Validate token
- Validate scope
- Validate port availability
- Create PortTableEntry
- Bind socket
- Return handle

`ReleasePort(token, port)`
- Validate ownership
- Close socket
- Remove entry

`GetOpenPorts()`
- Read-only view
- For debugging and auditing

`Monitor()`
- Background task
- Detect orphaned ports
- Auto-close if owner dies

### 3. Inbound Data Verification Pipeline
Every inbound packet goes through:

Step 1: Port Ownership Check
If the port is not in the Port Table -> drop.

Step 2: Session Validation
If the connection wasn't initiated by Citadel -> drop.

Step 3: Protocol Sanity Check
- size limits
- rate limits
- malformed packet detection

Step 4: Optional Schema/Signature Validation
For higher-level protocols.

Step 5: Deliver to Owner
Only after all checks pass.

This is your "never trust external data" rule in action.

### 4. The Inside-Out Networking Model
You said it perfectly:

"Incoming data from the outside can only arrive to Citadel if something inside Citadel requested it."

Right now, Citadel accidentally behaves this way because nothing is listening.
The Port Manager makes it intentional and enforced.

This is the difference between:
- a project that happens to be safe
- and an OS that guarantees safety

### 5. How This Fits Into Citadel Today
You already have:
- a working network stack
- outbound connections
- TCP close logic
- HTTP parsing
- capability infrastructure
- process identity
- kernel-level invariants

You need to add:
- the Port Table
- the Port Manager service
- capability-scoped port requests
- inbound packet filtering
- auto-close logic

This is all achievable without rewriting your networking layer.

### 6. Phase 1 Action Checklist (Execution)
- [ ] Define `PortTableEntry` and storage container in runtime registry or dedicated manager module. [Owner: QEvent]
- [ ] Add explicit port state transitions (`Closed -> Opening -> Open -> Closing -> Closed`) and reject invalid transitions. [Owner: QEvent]
- [ ] Implement `Network.OpenPort` capability token shape (id/type/scope/issued_to/expiration) and validation helpers. [Owner: QSecurityCenter]
- [ ] Add capability scope parser for `TCP:<port>` / `UDP:<port>` and wildcard policy rules if needed. [Owner: QSecurityCenter]
- [ ] Introduce Port Manager service API: `RequestPort`, `ReleasePort`, `GetOpenPorts`, `Monitor`. [Owner: QNetwork]
- [ ] Route all TCP listen/bind paths through Port Manager; block direct socket binds outside manager path. [Owner: QNetwork]
- [ ] Route all UDP bind/unbind paths through Port Manager; preserve ephemeral allocation policy under manager control. [Owner: QNetwork]
- [ ] Enforce internal-caller gate for port requests (trusted process identity + capability token required). [Owner: QEvent]
- [ ] Add owner PID + capability token tracking for every open port record and expose read-only audit snapshot command. [Owner: QEvent]
- [ ] Implement monitor task to detect dead owners and auto-close orphaned ports. [Owner: QKernel]
- [ ] Add inbound boundary filter stage 1: drop packets targeting ports not present in Port Table. [Owner: QNetwork]
- [ ] Add inbound boundary filter stage 2: drop unsolicited session traffic not associated with Citadel-initiated state. [Owner: QNetwork]
- [ ] Add protocol sanity checks (size/rate/malformed guardrails) before payload delivery to owners. [Owner: QNetwork]
- [ ] Add structured audit events for open/close/reject/autoclose actions with owner and reason fields. [Owner: QSecurityCenter]
- [ ] Add test coverage: capability deny cases, ownership mismatch, orphan autoclose, unsolicited inbound drop, malformed packet drop. [Owner: QNetwork]
- [ ] Add rollout gate: default deny for unmanaged inbound traffic, with debug metrics to verify no regressions. [Owner: QKernel]

### 7. Phase 1 Suggested Order
- [ ] Milestone A: Data model + token validation (`PortTableEntry`, token schema, scope checks).
- [ ] Milestone B: Port Manager API + TCP/UDP routing through manager.
- [ ] Milestone C: Inbound filtering pipeline (ownership, session, sanity).
- [ ] Milestone D: Monitor + autoclose + audit events.
- [ ] Milestone E: Tests + rollout gate + metrics verification.

### 8. File Touchpoint Map (Phase 1)
- [ ] Checklist items 1-2 (port table shape + state machine): start in `QEvent/Include/QKRuntimeRegistries.h` and `QEvent/Src/QKRuntimeRegistries.cpp` where `PortRecord`, `registerPort`, `unregisterPort`, and `findPort` currently live.
- [ ] Checklist items 3-4 (capability token model + scope validation): extend `QKernel/Include/QKSecurityCenter.h` and `QKernel/Src/QKSecurityCenter.cpp` using existing dispatch/policy patterns.
- [ ] Checklist items 5-7 (Port Manager API + TCP/UDP routing): wire API surface in `QNetwork/Include/QNetStack.h` and route socket callsites in `QNetwork/Src/QNetSocket.cpp`, `QNetwork/Src/QNetTCP.cpp`, and `QNetwork/Src/QNetUDP.cpp`.
- [ ] Checklist item 8 (internal caller gate): consolidate current internal owner checks from `QNetwork/Src/QNetTCP.cpp` and `QNetwork/Src/QNetUDP.cpp` behind manager path and registry process checks in `QEvent/Src/QKRuntimeRegistries.cpp`.
- [ ] Checklist item 9 (owner + token tracking and audit snapshot): expand port record storage in `QEvent/Include/QKRuntimeRegistries.h`, update write paths in `QEvent/Src/QKRuntimeRegistries.cpp`, and expose read-only command output in `QKernel/Src/QKCommandCenter.cpp`.
- [ ] Checklist item 10 (orphan monitor and autoclose): add monitor tick integration in `QNetwork/Src/QNetStack.cpp` and use registry/process liveness data from `QEvent/Src/QKRuntimeRegistries.cpp`.
- [ ] Checklist items 11-13 (inbound filtering pipeline): enforce boundary/session/sanity checks in receive paths at `QNetwork/Src/QNetIP.cpp`, `QNetwork/Src/QNetTCP.cpp` (`receivePacket`/`processSegment`), and `QNetwork/Src/QNetUDP.cpp` (`receivePacket`).
- [ ] Checklist item 14 (structured open/close/reject/autoclose audit): emit SC audit/control events from `QKernel/Src/QKSecurityCenter.cpp` and publish via topics defined in `QEvent/Include/QKMsgBus.h`.
- [ ] Checklist items 15-16 (tests + rollout gate + debug metrics): add command-driven validation hooks in `QKernel/Src/QKCmdDebugTest.cpp` and operational visibility/reporting in `QKernel/Src/QKCommandCenter.cpp`.

## Maybe
- [ ] #12 (MVP part A)** Compute `golden_manifest_digest` deterministically and log/measure it (even before sealing exists). (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L47))
- [ ] #12 (MVP part B, BLOCKED on persistence)** Seal + store blob; implement unseal policy checks. (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L48))
- [ ] #12 TPM sealing for golden-config hashes** (needs persistent storage for the sealed blob, but we can compute/log digests now). (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L29))
- [ ] Add `QC_LOG_PROBE("Module")` macro that emits file/line/func + tid + ms timestamp via existing `QC::Logger` (optionally `#ifdef NDEBUG` strip). (sources: discussion 2026-03-27)
- [ ] #15** Implement a generic JSON layering/merge engine (schema-aware) + validation hooks. (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L51))
- [ ] #16 Unified sandbox profiles + app/service launch flow integration**. (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L31))
- [ ] #16** Define sandbox profile JSON + selection rules, and plumb it into service/app launch. (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L52))
- [ ] #19** Add minimize/maximize plumbing (Terminal first), then standardize chrome widgets. (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L55))
- [ ] `QDTheme` class to hold all visual settings (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L80))
- [ ] `QW::Controls::CheckBox` - Check mark with glow animation (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L267))
- [ ] `QW::Controls::Menu` - Popup menus (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L274))
- [ ] `QW::Controls::ProgressBar` - Progress indicator (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L270))
- [ ] `QW::Controls::RadioButton` - Radio with glow (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L268))
- [ ] `QW::Controls::Slider` - Track with thumb (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L269))
- [ ] `QW::Controls::StatusBar` - Status bar (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L276))
- [ ] `QW::Controls::TabControl` - Tabbed container (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L271))
- [ ] `QW::Controls::Toolbar` - Toolbar with button groups (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L273))
- [ ] `QW::Controls::Tooltip` - Fade-in tooltip (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L275))
- [ ] `QW::Controls::TreeView` - Hierarchical view with expand animation (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L272))
- [ ] All programs flyout (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L324))
- [ ] Animation chaining (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L340))
- [ ] Animation state machine (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L241))
- [ ] Animation timings (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L87))
- [ ] Anti-aliased edges (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L165))
- [ ] Audit trail (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L178))
- [ ] Authority enforcement (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L173))
- [ ] Auto-generate auth + ownership on save (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L148))
- [ ] Background sampling for glass effect (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L332))
- [ ] Bloom effect for highlights (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L161))
- [ ] Blur cache management (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L333))
- [ ] Border styles (width, radius, color) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L83))
- [ ] Box blur (fast, for real-time) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L148))
- [ ] Cached blur textures for glass effect (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L150))
- [ ] Cached parsing for performance (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L73))
- [ ] Clock display (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L317))
- [ ] Close animation (fade out) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L303))
- [ ] Close button (red glow on hover) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L296))
- [ ] Color palette (primary, secondary, accent, background, text, etc.) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L81))
- [ ] Composition order handling (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L334))
- [ ] Create registry structure (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L55))
- [ ] Cursor blinking animation (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L257))
- [ ] Custom title bar buttons support (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L299))
- [ ] Define states: (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L45))
- [ ] Diagonal gradients (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L143))
- [ ] Disabled state (desaturated) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L249))
- [ ] DLL signature enforcement (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L175))
- [ ] Drop shadow surrounding window (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L291))
- [ ] Easing functions (ease-in, ease-out, ease-in-out) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L338))
- [ ] Embedded AI engine (system memory)**: monitor instructions/functions executed (inputs + return values/status where applicable) so the system can answer “I’ve seen this before; here’s the result.” (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L62))
- [ ] Error handling with line/column info (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L67))
- [ ] Error tracing (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L164))
- [ ] Execution logs (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L160))
- [ ] Finalize JSON schema for compiled backends (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L17))
- [ ] Finalize JSON schema for functions (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L4))
- [ ] Finalize JSON schema for libraries (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L15))
- [ ] Finalize JSON schema for overrides (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L16))
- [ ] Focus ring with glow (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L250))
- [ ] Font settings (when we have fonts) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L82))
- [ ] Frame adapts to style (3D vs flat vs rounded) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L280))
- [ ] Frame-based animation timer (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L337))
- [ ] Gaussian blur (higher quality) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L149))
- [ ] Glass background with blur (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L312))
- [ ] Glass panel (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L320))
- [ ] Glass title bar with gradient (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L289))
- [ ] Glass-style background (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L255))
- [ ] Glow on hover (animated fade-in) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L247))
- [ ] Glow settings (color, intensity, radius) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L85))
- [ ] Glowing border on focus (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L256))
- [ ] Glowing window border on focus (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L290))
- [ ] Gradient background with glass effect (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L246))
- [ ] Hardware-backed rate limiting (TPM dictionary-attack protections) where applicable (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L203))
- [ ] Hot-reload support (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L220))
- [ ] Implement "override": "dll.FunctionName" (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L123))
- [ ] Implement "status": "compile_to_dll" trigger (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L110))
- [ ] Implement ABI wrapper (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L114))
- [ ] Implement auth block schema (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L26))
- [ ] Implement authority validation (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L37))
- [ ] Implement call op (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L90))
- [ ] Implement codegen for basic ops (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L100))
- [ ] Implement codegen for call ops (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L101))
- [ ] Implement codegen to C/C++ (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L111))
- [ ] Implement DLL loader (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L115))
- [ ] Implement DLL metadata generator (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L113))
- [ ] Implement error handling (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L92))
- [ ] Implement execution key derivation (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L34))
- [ ] Implement fallback logic (override → dll → jit → interpreter) (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L68))
- [ ] Implement JIT allocator (RW → RX) (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L99))
- [ ] Implement JIT invalidation rules (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L102))
- [ ] Implement library loader (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L135))
- [ ] Implement library schema (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L134))
- [ ] Implement library-level overrides (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L139))
- [ ] Implement library-level protection (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L138))
- [ ] Implement lifecycle metadata in registry (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L49))
- [ ] Implement lightweight JSON tokenizer (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L64))
- [ ] Implement lookup rules (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L66))
- [ ] Implement module quarantine for invalid auth (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L38))
- [ ] Implement op registry (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L89))
- [ ] Implement override invalidation rules (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L128))
- [ ] Implement override resolution (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L67))
- [ ] Implement ownership derivation map (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L33))
- [ ] Implement signature verification (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L36))
- [ ] Implement state transitions (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L47))
- [ ] Implement step executor (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L88))
- [ ] Implement type checking (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L91))
- [ ] Implement validation gates for each transition (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L48))
- [ ] Implement version constraints (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L136))
- [ ] Implement visibility rules (private/public/global) (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L137))
- [ ] Initialize JIT allocator (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L190))
- [ ] Initialize op registry (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L192))
- [ ] Initialize trust store (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L191))
- [ ] Inner glow for buttons (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L160))
- [ ] Integrate auth checks into JSON loader (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L39))
- [ ] Integrate with registry (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L93))
- [ ] JSON value types: null, bool, number, string, array, object (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L65))
- [ ] Layout constraints (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L242))
- [ ] Layout profiles (work, gaming, etc.) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L221))
- [ ] Load layout from JSON (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L219))
- [ ] Mark as jit_ready (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L82))
- [ ] Maximize animation (expand to screen) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L305))
- [ ] Maximize/Restore button (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L298))
- [ ] Memory protection (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L172))
- [ ] Memory-efficient design (no STL) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L68))
- [ ] Minimize animation (shrink to taskbar) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L304))
- [ ] Minimize button (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L297))
- [ ] Missing: minimize/maximize behavior + consistent window chrome conventions across apps. (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L25))
- [ ] Multi-stop gradient support (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L145))
- [ ] Multi-user accounts and per-app vault permissions (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L202))
- [ ] Open animation (fade + scale) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L302))
- [ ] Outer glow for focused elements (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L159))
- [ ] Override logs (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L161))
- [ ] Override restrictions (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L174))
- [ ] Parse from string buffer (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L66))
- [ ] Parse JSON (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L74))
- [ ] Passkey (FIDO2/WebAuthn-style) support if/when the stack exists (sources: [backups/todo_archive_2026-03-25/TODO_S_LIST.md](backups/todo_archive_2026-03-25/TODO_S_LIST.md#L201))
- [ ] Per-style color palettes (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L281))
- [ ] Per-style hover/focus effects (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L282))
- [ ] Per-window blur regions (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L331))
- [ ] Performance metrics (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L163))
- [ ] Pinned items (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L322))
- [ ] Placeholder text with alpha (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L258))
- [ ] Porter-Duff compositing modes (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L138))
- [ ] Pre-multiplied alpha optimization (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L139))
- [ ] Press animation (darken + slight shrink) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L248))
- [ ] Promotion logs (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L162))
- [ ] Property animation (position, size, opacity, color) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L339))
- [ ] Provide compile-to-dll button (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L152))
- [ ] Provide lifecycle status display (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L150))
- [ ] Provide override warnings (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L151))
- [ ] Provide validation feedback (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L149))
- [ ] Quarantine system (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L177))
- [ ] Radial gradients (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L144))
- [ ] Recent items (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L323))
- [ ] Register function in registry (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L81))
- [ ] Resolve dependencies (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L189))
- [ ] Resolve overrides (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L188))
- [ ] Rounded corners (top only or all) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L292))
- [ ] Rounded rectangle primitives (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L164))
- [ ] Sandboxing for JIT (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L171))
- [ ] Save current layout to JSON (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L218))
- [ ] Search box (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L321))
- [ ] Shadow caching for performance (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L156))
- [ ] Shadow settings (offset, blur, color, spread) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L84))
- [ ] Shutdown dialog: after opening it, clicking other buttons can confuse focus/capture and takes time to recover. (sources: [backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md](backups/todo_archive_2026-03-25/TODO_BRAINSTORM_2026-03-08.md#L58))
- [ ] Singleton pattern (global accessor) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L185))
- [ ] Start button with orb glow (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L313))
- [ ] Store jit_ptr in registry (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L103))
- [ ] Store: (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L56))
- [ ] Symbol table for JIT and DLL (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L159))
- [ ] System tray (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L316))
- [ ] System tray / notification area (functional) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L189))
- [ ] Tamper detection (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L176))
- [ ] Theme loading and switching from JSON (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L186))
- [ ] Transparency/opacity levels (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L86))
- [ ] Update backend.current = "jit" (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L104))
- [ ] Update registry backend.current = "dll" (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L117))
- [ ] Update registry backend.current = "override" (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L127))
- [ ] Validate all modules (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L186))
- [ ] Validate auth (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L76))
- [ ] Validate authority (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L126))
- [ ] Validate calls (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L79))
- [ ] Validate DLL is overridable (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L124))
- [ ] Validate DLL signature & overridability (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L116))
- [ ] Validate ownership key (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L77))
- [ ] Validate schema (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L75))
- [ ] Validate signature (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L78))
- [ ] Validate signature compatibility (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L125))
- [ ] Validate visibility/protection rules (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L80))
- [ ] Version history (sources: [jsonFunctionTemplate.md](jsonFunctionTemplate.md#L165))
- [ ] Wallpaper handling (when image loading available) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L187))
- [ ] Window buttons with previews (stretch goal) (sources: [QDesktop/TODO_README.md](QDesktop/TODO_README.md#L314))
