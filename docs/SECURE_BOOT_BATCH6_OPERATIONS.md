# Citadel Secure Boot Batch 6 Operations

Status: Implemented (Batch 6, Items 1-10)
Last Updated: 2026-06-16
Owner: Engineering + Security + Operations

## 1. Nightly Regression Suite and Trend Alerts

Implemented by:
- .github/workflows/secure-boot-nightly.yml
- tools/nightly_regression_report.py

Policy:
- Nightly run at 02:00 UTC
- Alert thresholds:
  - fail_rate > 2%
  - critical_failed > 0
- Trend retention: rolling 90 days

## 2. Maximum Time-to-Detect (MTTD) for Drift

Enforcement targets:
- Signature drift MTTD: <= 24 hours
- Key-policy drift MTTD: <= 24 hours

Controls:
- Nightly workflow executes drift-related checks every 24h
- Release gate executes drift checks before tag acceptance

Escalation trigger:
- Any drift report with unexpected or missing key fingerprints.

## 3. Automated Stale-Key Discovery

Implemented by:
- tools/stale_key_report.py

Report sections:
- soon_expiring
- expired
- orphaned
- unused

Remediation ownership:
- Security Lead: soon_expiring / expired
- Engineering Lead: orphaned / unused

## 4. Signed Installer-Media Verification Gate

Implemented by:
- tools/verify_installer_media.py

Policy:
- Every field-install media build must pass manifest hash + signature presence checks.
- Missing artifact/signature or hash mismatch blocks release candidate.

## 5. Disaster-Recovery Rehearsal Cadence (Lost Signing Key)

Cadence:
- Lab: monthly rehearsal
- Staging: quarterly rehearsal
- Production: semi-annual rehearsal

Required outputs:
- rebuild timeline
- re-enrollment timeline
- blockers + mitigations

Recovery timeline targets:
- Lab <= 4h
- Staging <= 24h
- Production <= 72h

## 6. Firmware-Version Allowlist Policy

Implemented by:
- config/secure_boot_firmware_allowlist.json
- tools/check_firmware_allowlist.py

Policy:
- Unapproved firmware versions are blocked by default.
- Firmware rollout requires:
  1. vendor provenance check
  2. compatibility test pass
  3. rollback path confirmation

## 7. Release-Branch Evidence Refresh Policy

Policy:
- Before every production tag cut:
  - regenerate signature manifest
  - regenerate provenance report
  - regenerate stale key report
  - regenerate update attestation report
- No reuse of stale evidence bundles across release tags.

Gate:
- release workflow must fail if evidence timestamp predates release candidate build.

## 8. Cross-Team Escalation Matrix

Severities and RTO objectives:
- Sev-1 (boot outage / revoked active key):
  - Engineering ack <= 15 min
  - Security ack <= 15 min
  - Ops ack <= 15 min
  - Incident commander assigned <= 30 min
- Sev-2 (drift alert / canary failures):
  - ack <= 1 hour
- Sev-3 (non-critical warning):
  - ack <= 1 business day

Escalation path:
1. On-call engineer
2. Security lead
3. Operations lead
4. Director escalation

## 9. Long-Retention Archival Policy (Refusal + Recovery Logs)

Retention:
- refusal logs: 7 years
- recovery logs: 7 years
- incident postmortems: 7 years

Integrity controls:
- archive checksum manifest (SHA-256)
- quarterly restore test of random sample (>=5%)
- immutable storage class for production archives

## 10. Secure Boot v2 Backlog Seed List

Prioritized v2 seeds:
1. measured boot PCR policy enforcement
2. remote attestation for fleet compliance
3. signed policy engine for boot-time rules
4. automated db/dbx update pipeline with staged rollout
5. hardware-backed key ceremony tooling
6. policy-as-code checks in CI for key lifecycle
7. immutable transparency log for all signing events
8. stronger module provenance linkage (source->binary attestation)
9. fleet anomaly detection on refusal-code telemetry
10. fully automated recovery rehearsal validation
