# CQL Database Engine Porting Guide (Citadel-Aligned, V2)

Date: 2026-04-15
Scope: Replace the generic Windows-to-Citadel guidance with a plan that matches the current repository layout and naming.
Legacy reference: `CITADEL_PORTING_GUIDE.md` contains the original conceptual guide.

## 1) Executive Summary

The repository already contains the core scaffolding for a service-based CQL integration:
- `CitadelPAL.h` (stub platform layer)
- `QCSQLServiceProtocol.h` (request/response protocol)
- `QCSQLService.h/.cpp` (message-routed service wrapper)
- `QCSQLServiceTest.cpp` (host-side tests)

Because this scaffolding exists, porting should proceed by completing stubs and wiring the engine, not by introducing a parallel `CSQLService` architecture.

## 2) Ground Truth In This Repo

## Existing service model
- Namespace and shape already used: `QCQL::Svc`
- Service registration point already present: `RegisterQCSQLService()`
- Message protocol already defined with bounded buffers and message types

## Existing PAL model
- PAL is class-based (`FileHandle`, `Memory`, `Console`, `String`, `Error`)
- Most methods are explicit stubs and intentionally non-functional in current state

## Naming constraints
- Project standards use module-prefixed naming and consistent namespaces.
- Keep `QCSQL*` naming for this module family; do not introduce new top-level service naming unless a broader repo-wide refactor is approved.

## 3) Target Architecture (Repo-Aligned)

## Service boundary
- Keep in-process message-routed architecture for now.
- Expose database operations through `QCSQLService` handlers.
- Avoid direct shell-to-engine calls once service handlers are implemented.

## Data flow
1. Client builds request struct from `QCSQLServiceProtocol.h`.
2. Request routed to `QCSQLService::HandleMessage`.
3. Handler validates input/handle/state.
4. Handler calls engine-facing layer (initially direct, later adapter if needed).
5. Response struct is filled with success/error and bounded payload.

## Storage and handles
- Maintain fixed handle table (current approach) until dynamic resource manager exists.
- Handle 0xFFFFFFFF (or equivalent invalid) consistently as invalid across all handlers.

## 4) Porting Plan

## Phase A: Harden PAL (required before real DB I/O)

Primary files:
- `CitadelPAL.h`
- `CitadelPAL.cpp` (create if missing)

Tasks:
- Implement `FileHandle::Open/Read/Write/Seek/Close/Exists/Delete` against actual Citadel VFS APIs.
- Implement `Memory::Allocate/Reallocate/Free` against kernel heap APIs.
- Implement `Console::Write/WriteLine/WriteError/DebugPrint` against Citadel console path.
- Keep `String` helpers safe and null-terminating.
- Set and surface `Error::lastError` on PAL failures.

Exit criteria:
- PAL no longer returns placeholder failures for core I/O and memory paths.
- A small PAL harness can create, write, read, and delete a file under Citadel runtime.

## Phase B: Replace Service Stubs With Engine Calls

Primary files:
- `QCSQLService.h`
- `QCSQLService.cpp`
- `Database.h/.cpp`, `SQLParser.h/.cpp`, `QueryExecutor.h/.cpp` (as needed)

Tasks:
- Replace placeholder success responses in handlers with real engine invocation.
- Implement handle lifecycle:
  - Allocate on create/open
  - Validate on query/info/close
  - Release on close/failure
- Ensure all response buffers are bounded and null-terminated.
- Return meaningful error strings for validation failures and engine errors.

Exit criteria:
- `CreateDatabase`, `OpenDatabase`, `ExecuteSQL`, and `CloseDatabase` reflect real outcomes.
- `GetStatus` and `GetDatabaseInfo` reflect actual service state.

## Phase C: Boot and Service Registry Integration

Primary files:
- Citadel service init location (kernel/service bootstrap)
- `services.json`

Tasks:
- Register `QCSQL` service in boot initialization after filesystem availability.
- Ensure registration failure is non-fatal to system boot (log and continue).
- Add/confirm `services.json` entry for discoverability/metadata.

Exit criteria:
- Service is available after boot in expected lifecycle order.
- Registry routes messages to `QCSQLService` reliably.

## Phase D: Runtime Validation in QEMU

Tasks:
- Execute smoke sequence via shell or test client:
  - Create DB
  - Create table
  - Insert row(s)
  - Select row(s)
  - Close DB
- Validate behavior across reboot (persistence check).
- Validate failure paths (bad handle, oversized query, missing DB file).

Exit criteria:
- End-to-end operation succeeds under QEMU with stable logs.
- Failure paths return deterministic error messages and no kernel instability.

## 5) Implementation Rules

- Keep current naming (`QCSQL*`, `QCQL::Svc`) unless standards are intentionally revised.
- Do not add a second service abstraction in parallel to existing one.
- Prefer minimal diffs and bounded buffers in protocol-facing code.
- Avoid hidden global state for query results; responses should be explicitly caller-owned structs.
- Preserve service availability even when database operations fail.

## 6) Testing Strategy

## Host-side (fast iteration)
- Continue using `QCSQLServiceTest.cpp` for protocol/handler behavior.
- Add tests for:
  - Invalid handle rejection
  - Maximum query length handling
  - Double-close safety
  - Error propagation from engine/PAL

## Target-side (Citadel/QEMU)
- Add a compact runtime test command path for service smoke testing.
- Capture first distinct failure line in logs for triage.

## Suggested milestones
1. PAL file I/O validated on target
2. Service handlers wired to real engine
3. Boot registration validated
4. CRUD smoke tests pass in QEMU

## 7) Risk Register

- PAL APIs may not map 1:1 to host assumptions in current engine code.
- Fixed response buffer sizes can truncate large query outputs.
- Stub-era optimistic success paths may hide lifecycle bugs when real I/O is enabled.
- Boot-order mistakes (service before FS) can create intermittent startup failures.

## 8) Definition of Done

- No critical service path returns stub placeholders.
- Real DB create/open/execute/close works on Citadel runtime.
- Service remains stable under invalid input and bad-handle scenarios.
- Boot integration is deterministic and non-fatal on failure.
- Documentation references current file names and namespaces only.

## 9) Quick Start Execution Checklist

- [ ] Implement PAL core methods against Citadel runtime APIs
- [ ] Wire `QCSQLService` handlers to real engine calls
- [ ] Enforce strict handle validation and release paths
- [ ] Register service in boot sequence after filesystem init
- [ ] Add/verify `services.json` metadata entry
- [ ] Run host tests and extend for boundary cases
- [ ] Run QEMU smoke tests for create/insert/select/close
- [ ] Verify persistence and failure-path behavior

## 10) Notes On Previous Guide

The earlier `CITADEL_PORTING_GUIDE.md` remains useful as conceptual background, but this V2 guide is the execution source of truth for this repository state.
