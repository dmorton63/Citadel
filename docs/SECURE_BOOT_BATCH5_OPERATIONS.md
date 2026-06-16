# Citadel Secure Boot Batch 5 Operations

Status: Implemented (Batch 5, Items 1-10)
Last Updated: 2026-06-16
Owner: Security + Operations

## 1. Quarterly Compliance Review Cadence

Cadence:
- Week 1 each quarter: collect manifests, logs, drift reports, expiry reports
- Week 2: engineering + security review
- Week 3: operations remediation actions
- Week 4: sign-off and archive

Required artifacts:
- secure-boot manifests
- boot log reports + regression diff reports
- key expiry report
- key drift report
- update chain attestation reports
- SBOM linkage report
- incident/training drill record

Owners:
- Engineering Lead: artifact correctness
- Security Lead: key trust / revocation posture
- Operations Lead: deployment and rollback readiness

Sign-off:
- Quarterly review ticket must include all three approvals.

## 2. Key Expiration and Certificate Alerting

Implemented by:
- tools/check_key_expiry.py

Policy:
- Warning threshold: 90 days
- Critical threshold: 30 days
- Expired certs: immediate block for release workflows

## 3. Enrolled Key Drift Detection

Implemented by:
- tools/detect_key_drift.py

Policy:
- Drift check runs before release tags and weekly in staging
- Any unexpected key fingerprint blocks promotion

## 4. Signed Update Attestation

Implemented by:
- tools/attest_update_chain.py

Policy:
- Every release candidate must produce a PASS attestation report
- Signer mismatch or bad signature is fail-closed

## 5. SBOM Linkage to Signature Manifest

Implemented by:
- tools/link_sbom_manifest.py

Policy:
- Every signed boot artifact should map to at least one SBOM component
- Unresolved entries require manual review before production rollout

## 6. Incident Response Tabletop Exercise

Scenario:
- Compromised boot signing key in active deployment window

Required outputs:
- Timeline from detection to containment
- dbx update action log
- advisory draft and publication checklist
- remediation and lessons learned

Target SLA:
- dbx release in 24 hours

## 7. Secure Decommission Workflow (Keys/Media)

Steps:
1. Archive required records and manifests
2. Revoke key if key was active
3. Destroy physical media (witnessed)
4. Capture destruction attestation
5. Update key registry state to RETIRED

## 8. Supplier/Firmware Trust Validation Checklist

Before BIOS/UEFI update approval:
- Verify vendor release provenance/signature
- Confirm no Secure Boot regression in release notes
- Validate on dual hardware test pass
- Validate canonical log parser still passes
- Validate key enrollment compatibility remains intact
- Ensure rollback firmware path exists

## 9. Production Canary Gate for Policy/Keyset Changes

Guard rails:
- Canary cohort starts at 1-5% of fleet
- Freeze trigger if failure rate exceeds baseline + 10%
- Rollback criteria:
  - any SB-3xxx spike
  - any boot failure cluster > 1%
  - any key drift report in canary

Gate integration:
- .github/workflows/secure-boot-release-gate.yml

## 10. Post-v1 Operational KPIs

KPIs:
- Refusal accuracy (correct refusal code for induced tamper): target >= 99%
- False positive refusal rate: target <= 0.1%
- Mean time to recovery from signing incident: target <= 4 hours (lab/staging), <= 24 hours (production)
- Key drift detection latency: target <= 24 hours
- Reproducibility pass rate on release candidates: target 100%

Reporting:
- Monthly ops dashboard
- Quarterly compliance review pack
