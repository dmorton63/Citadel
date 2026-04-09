# Security Center Audit Event Taxonomy

Status: Draft v1 (MVP)

This taxonomy defines canonical audit event classes and event IDs for Security Center logging.
It covers provisioning, boot trust, update verification, execution allow/deny, SST rotation, and owner unlock/lock.

## Event Record Shape

Each audit record should include at minimum:

- `event_id`: stable numeric identifier from this document
- `class`: event family
- `severity`: `info|warn|error|critical`
- `ts`: monotonic + wallclock timestamp pair when available
- `actor_role`: `everyone|user|admin|su|system|sc`
- `result`: `success|denied|failed|partial`
- `subject`: target object (`module`, `command`, `vault`, `update`, etc.)
- `subject_id`: optional identifier/hash/path
- `reason_code`: short machine-readable reason
- `detail`: optional short human-readable context

## Severity Guidelines

- `info`: expected success transitions.
- `warn`: policy-denied or degraded but recoverable.
- `error`: operation failed unexpectedly or integrity uncertainty.
- `critical`: trust boundary break, tamper detection, or compromised state.

## Class A: Provisioning (`0x1000-0x10FF`)

- `0x1001 SC_PROVISION_START` (`info`)
- `0x1002 SC_PROVISION_COMPLETE` (`info`)
- `0x1003 SC_PROVISION_FAIL` (`error`)
- `0x1004 SC_RECOVERY_CODE_SET` (`warn`)
- `0x1005 SC_RECOVERY_CODE_VERIFY_FAIL` (`warn`)

## Class B: Boot Trust (`0x1100-0x11FF`)

- `0x1101 BOOT_TRUST_CHECK_START` (`info`)
- `0x1102 BOOT_TRUST_CHECK_PASS` (`info`)
- `0x1103 BOOT_TRUST_CHECK_FAIL` (`critical`)
- `0x1104 TAS_UNSEAL_PASS` (`info`)
- `0x1105 TAS_UNSEAL_FAIL` (`critical`)
- `0x1106 SAFE_MODE_ENTERED` (`critical`)

## Class C: Update Verify (`0x1200-0x12FF`)

- `0x1201 UPDATE_VERIFY_START` (`info`)
- `0x1202 UPDATE_VERIFY_PASS` (`info`)
- `0x1203 UPDATE_VERIFY_SIG_FAIL` (`error`)
- `0x1204 UPDATE_VERIFY_HASH_FAIL` (`error`)
- `0x1205 UPDATE_APPLY_BLOCKED` (`warn`)

## Class D: Execution Policy (`0x1300-0x13FF`)

- `0x1301 EXEC_REQUEST` (`info`)
- `0x1302 EXEC_APPROVE` (`info`)
- `0x1303 EXEC_DENY_POLICY` (`warn`)
- `0x1304 EXEC_DENY_TRUST` (`error`)
- `0x1305 EXEC_RUNTIME_FAIL` (`error`)

## Class E: SST Rotation (`0x1400-0x14FF`)

- `0x1401 SST_ROTATE_START` (`info`)
- `0x1402 SST_ROTATE_COMPLETE` (`info`)
- `0x1403 SST_ROTATE_FAIL` (`error`)
- `0x1404 SST_REWRAP_START` (`info`)
- `0x1405 SST_REWRAP_FAIL` (`error`)

## Class F: Owner Session (`0x1500-0x15FF`)

- `0x1501 USER_ENROLL` (`info`)
- `0x1502 USER_UNLOCK_SUCCESS` (`info`)
- `0x1503 USER_UNLOCK_DENIED` (`warn`)
- `0x1504 USER_LOCK` (`info`)
- `0x1505 USER_BACKOFF_APPLIED` (`warn`)

## Reason Codes (initial set)

- `policy_denied`
- `invalid_signature`
- `hash_mismatch`
- `kdf_failure`
- `anchor_unseal_failure`
- `integrity_check_failed`
- `dependency_missing`
- `invalid_state_transition`

## Correlation and Traceability

- Commands/services should propagate a correlation id into audit records when available.
- Multi-step operations (update verify/apply, rotation) should share an operation id.
- `*_START` and terminal `*_COMPLETE|*_FAIL` events must pair by operation id.

## MVP Rules

- Event IDs are append-only; never reuse or repurpose IDs.
- Unknown event IDs must still be stored and displayed as unknown.
- `critical` events should trigger elevated visibility and optional SAFE_MODE policy hooks.
- Audit emission must be best-effort but non-blocking for non-critical operations.
