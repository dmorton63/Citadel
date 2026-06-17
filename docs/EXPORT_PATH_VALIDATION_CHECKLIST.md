# Export Path Validation Checklist

Date: 2026-06-16

## Purpose

Validate end-to-end export behavior on real hardware for `/system`, `/shared`, and USB targets.

This checklist confirms:
- target selection behavior (`auto`, `system`, `shared`, `usb`)
- persistence intent messaging
- ephemeral guardrails (`ephemeral-ok` override)
- metadata sidecar generation

## Preconditions

- System booted on real hardware to terminal session.
- Owner session unlocked for `sys_audit_export` paths requiring `present`.
- `/system` mounted for persistent path checks.
- `/shared` mounted for ephemeral path checks.
- At least one removable USB storage volume mounted for USB path checks.

## Capture Setup

- [ ] Start serial capture and console transcript capture for this run.
- [ ] Record machine ID, build commit, and test date in the test log.
- [ ] Create a run folder under `/system/logs/` for captured artifacts.

## Section A: System Target (Persistent)

1. Run: `bootlog export system`
- [ ] Expected: success line includes `target=system` and `persistence=persistent`.
- [ ] Expected: output file exists at `/system/logs/bootlog.txt`.
- [ ] Expected: sidecar exists at `/system/logs/bootlog.txt.meta.json`.

2. Run: `sys_audit_export system present`
- [ ] Expected: success line includes `target=system` and `persistence=persistent`.
- [ ] Expected: output file exists at `/system/logs/audit_events.log`.
- [ ] Expected: sidecar exists at `/system/logs/audit_events.log.meta.json`.

3. Verify sidecar fields for both artifacts.
- [ ] Expected JSON fields present: `timestamp_ms`, `source`, `target`, `persistence_class`, `artifact_path`, `artifact_sha256`.
- [ ] Expected `persistence_class` value is `persistent`.

## Section B: Shared Target (Ephemeral)

1. Run without override: `bootlog export shared`
- [ ] Expected: command refuses with message that target is ephemeral and requires `ephemeral-ok`.

2. Run with override: `bootlog export shared ephemeral-ok`
- [ ] Expected: success line includes `target=shared` and `persistence=ephemeral`.
- [ ] Expected: output file exists at `/shared/logs/bootlog.txt`.
- [ ] Expected: sidecar exists at `/shared/logs/bootlog.txt.meta.json`.

3. Run without override: `sys_audit_export shared present`
- [ ] Expected: command refuses with message that target is ephemeral and requires `ephemeral-ok`.

4. Run with override: `sys_audit_export shared present ephemeral-ok`
- [ ] Expected: success line includes `target=shared` and `persistence=ephemeral`.
- [ ] Expected: output file exists at `/shared/logs/audit_events.log`.
- [ ] Expected: sidecar exists at `/shared/logs/audit_events.log.meta.json`.

5. Verify sidecar fields for shared artifacts.
- [ ] Expected `persistence_class` value is `ephemeral`.

## Section C: USB Target (Removable)

1. Run: `bootlog export usb`
- [ ] Expected: success line includes `target=usb` and `persistence=removable`.
- [ ] Expected: output file exists under the mounted USB path `.../logs/bootlog.txt`.
- [ ] Expected: sidecar exists under the mounted USB path `.../logs/bootlog.txt.meta.json`.

2. Run: `sys_audit_export usb present`
- [ ] Expected: success line includes `target=usb` and `persistence=removable`.
- [ ] Expected: output file exists under the mounted USB path `.../logs/audit_events.log`.
- [ ] Expected: sidecar exists under the mounted USB path `.../logs/audit_events.log.meta.json`.

3. Verify sidecar fields for USB artifacts.
- [ ] Expected `persistence_class` value is `removable`.

## Section D: Auto Target Resolution

1. With `/system` mounted, run: `bootlog export auto`
- [ ] Expected: resolves to `target=system` and `persistence=persistent`.

2. With `/system` unavailable and `/shared` mounted, run: `bootlog export auto`
- [ ] Expected: resolves to `target=shared` and refuses without `ephemeral-ok`.

3. With `/system` and `/shared` unavailable but USB mounted, run: `bootlog export auto`
- [ ] Expected: resolves to `target=usb` and `persistence=removable`.

4. With no eligible mounted target, run: `bootlog export auto`
- [ ] Expected: explicit refusal about no mounted `/system`, `/shared`, or removable USB volume.

## Section E: Preflight Failure Clarity

1. Force a non-writable or missing parent path and run `sys_audit_export` to that path.
- [ ] Expected: preflight reports exact blocker (`target parent missing`, `not writable`, or `insufficient space`).

2. Run exports to all supported target modes and inspect failures.
- [ ] Expected: every refusal or failure includes a specific reason and no ambiguous durability messaging.

## Completion Criteria

- [ ] All Section A checks pass.
- [ ] All Section B checks pass.
- [ ] All Section C checks pass.
- [ ] All Section D checks pass.
- [ ] All Section E checks pass.
- [ ] Artifacts and console/serial logs archived under `/system/logs/` for regression baseline.
