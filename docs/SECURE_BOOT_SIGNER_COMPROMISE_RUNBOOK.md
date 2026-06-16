# Secure Boot Signer Compromise Containment Runbook

## Purpose
Contain and remediate any suspected compromise of production signing service identities.

## Trigger Conditions
- Unauthorized signing event detected.
- Signer host baseline attestation fails in production.
- HSM audit reports anomalous key usage.
- Provenance-chain mismatch for release-bound artifacts.

## Immediate Actions (0-30 minutes)
1. Declare incident severity and appoint incident commander.
2. Disable affected signer identities in HSM policy.
3. Freeze release promotion and block production signing jobs.
4. Snapshot current audit logs and preserve forensic evidence.

## Short-Term Containment (30-120 minutes)
1. Identify compromise window and quarantine all artifacts signed in window.
2. Publish deny-list/invalid signer list to release gate controls.
3. Validate clean standby signer path with dual-control approval.
4. Notify Engineering, Ops, Security stakeholders.

## Recovery (2-24 hours)
1. Rotate signer keys and update trusted signer manifests.
2. Re-sign verified clean artifacts.
3. Re-run provenance-chain validation and release-gate checks.
4. Resume production signing only after Security signoff.

## Communications
- Internal status updates: every 30 minutes during active containment.
- External statement: as required by policy and incident classification.

## Exit Criteria
- Compromised signer disabled and replaced.
- Quarantined artifacts resolved (re-signed or revoked).
- Release gate green with updated signer identity.
- Post-incident review scheduled and tracked.
