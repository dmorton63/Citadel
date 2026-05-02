# Security Runtime Developer Notes

This note captures the current Security Center runtime path as implemented in kernel code.

## Lifecycle

- `UNPROVISIONED`: Security Center initialized, SST not yet available.
- `PROVISIONED`: SST is available and TAS/SRK derivation path succeeds.
- `OPERATIONAL`: Owner session unlocked and UMK/VRK are present, with SST available.
- `RECOVERY` / `SAFE_MODE`: entered when provisioning or TAS/SST trust gates fail.

## Boot and Trust

- `ensureSst()` now verifies SST availability and explicitly touches TAS unseal/unwrap + SRK derivation through `deriveInitialKeyHierarchyFromTas(...)`.
- `checkBootTrustGate()` enforces SST availability and TAS-anchored derivation success.

## Rotation Cutover

- Rotation path waits for a boundary, marks SST retiring, performs cutover, and records retire markers.
- On successful rotation, Security Center re-wraps dependent vault header material (`VAULTHDR`) when owner key material is available.
- If owner key material is not available during rotation, a pending rewrap marker is persisted and applied on next owner unlock.

## Recovery-Wrapped TAS Material

- Install recovery flow now also stores wrapped TAS material (`TASRCOV`) using a recovery-derived key.
- Only wrapped material and verification metadata are persisted.

## Vault Authorization Path

- Vault requests are owner-gated and now require `OPERATIONAL` lifecycle state.
- Effective path is: owner unlock -> UMK/VRK derivation -> vault request authorization.
