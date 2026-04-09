# Security Center Policy Definitions (Batch 2026-04-09)

This file captures policy definitions referenced by TODO items for SC/vault behavior.

## 1. SST Rotation Mid-Cutover Failure
- If new wrapped SST is not persisted: keep previous generation, mark degraded.
- If persisted but generation pointer not switched: rollback pointer state and continue degraded.
- If generation switched and old generation not retired: keep old generation in recovery window and continue degraded.
- If post-cutover integrity cannot be re-established: enter SAFE_MODE.

## 2. TAS Unseal/Unwrap Failure
- Default policy: enter SAFE_MODE and RECOVERY.
- Normal operation is denied until recovery succeeds.

## 3. Vault Categories
- `saved_passwords`
- `private_notes`
- `encryption_keys`
- `app_secrets`
- `hidden_files`

## 4. Export Policy Gate
- Default deny for outbound export.
- Internal owner-mediated audit view is not considered outbound export.
- Any outbound channel requires explicit future policy enablement.

## 5. Initial Identity Scope
- Single Owner identity only.
- Additional personas are role profiles, not independent cryptographic owners.

## 6. Key Hierarchy Example
- TAS -> SRK
- SRK -> unwrap/wrap SST
- SST + UMK -> VRK
- VRK + role/tier -> wrapped tier keys -> wrapped file keys

## 7. Provisioning State Machine
- `UNPROVISIONED` -> `PROVISIONED` -> `OPERATIONAL`
- Failure branches: `SAFE_MODE`, `RECOVERY`

## 8. Recovery Policy for Vault
- Recovery code + physical presence gate.
- Recovery denies silent data discard.
- Recovery flow produces explicit compromised/degraded status.

## 9. Rollback / Anti-Rollback
- Prefer monotonic counter when available.
- Fallback uses generation metadata + chain verification + warning escalation.

## 10. SC Message Types
- `ScControl`, `ScAudit`, `ScTrust`, `ScFlow`
- `ScProvision`, `ScVault`, `ScPolicy`, `ScRecovery`

## 11. SC Threat Model (In Scope)
- Offline disk theft.
- Casual storage tampering.
- Rollback attempts.
- Malicious downloads/content.

## 12. Vault Requesting Subsystems
- SC and owner-mediated command path.
- Explicitly approved kernel security services.
- Deny-by-default for other subsystems.

## 13. Wipe Points
- Owner lock.
- Timed lock.
- Failed unlock.
- Failed key derivation.
- Session teardown.

## 14. No Daily Login Stance
- Device may boot without daily login.
- Vault and privileged operations still require unlock.

## 15. Unlock States
- `LOCKED`: vault sealed.
- `UNLOCKED`: vault usable.
- `TIMED_LOCK`: auto-lock state requiring explicit unlock.
