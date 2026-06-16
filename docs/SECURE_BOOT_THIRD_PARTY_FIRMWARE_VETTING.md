# Secure Boot Third-Party Firmware Vetting Checklist

## Scope
All third-party firmware payloads considered for Citadel distribution.

## Required Inputs
- Vendor package and checksum manifest
- Signature certificate chain
- Provenance metadata (build source, date, release notes)
- Rollback/recovery guidance

## Acceptance Checklist
- Signature verifies against approved trust anchors.
- Signature algorithm is strong (minimum SHA-256 + RSA-2048 or equivalent).
- Package hash matches published checksum.
- Provenance metadata is complete and internally consistent.
- Vendor disclosure includes known vulnerabilities and mitigations.
- Rollback guidance exists and has been rehearsed in lab.

## Automatic Reject Criteria
- Unsigned payload.
- Signature uses SHA-1 or weaker hashing.
- Key length below approved policy threshold.
- Missing provenance metadata.
- Checksum mismatch.
- Vendor cannot provide revocation/remediation process.

## Approval
Requires Security + Release Engineering approval with recorded decision evidence.
