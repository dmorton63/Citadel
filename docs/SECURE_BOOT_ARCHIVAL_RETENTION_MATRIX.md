# Secure Boot Archival and Retention Compliance Matrix

| Artifact Class | Example Artifacts | Minimum Retention | Integrity Requirement | Restore Test Cadence | Owner |
|---|---|---|---|---|---|
| Signature manifests | build manifests, artifact hashes | 7 years | SHA-256 manifest checksum + immutable store | Quarterly | Engineering |
| Approval records | dual-control approvals, promotion approvals | 7 years | Signed audit record + immutable log | Quarterly | Security |
| Incident artifacts | containment timeline, forensic snapshots | 7 years | Chain-of-custody metadata | Semi-annual | Security |
| Recovery evidence | drill reports, recovery timelines | 5 years | Signed drill report + checksum | Quarterly | Operations |
| Governance records | decision logs, exceptions, post-mortems | 7 years | Immutable append-only record | Quarterly | Security Governance |
| Revocation history | revocation metadata, propagation reports | 7 years | Signed provenance and timestamp chain | Monthly verify + quarterly restore | Operations |
