# CSSMS and QCSQL Architecture

Date: 2026-04-28

## Purpose

This note captures the intended Citadel shape for the database stack so the standalone Windows-era CQL work and the in-repo QCQL work do not drift into two different architectures.

## Intended Shape

- `CQL_Database_Engine/` is source material and service scaffolding, not the final runtime application shape inside Citadel.
- The intended runtime boundary is `QCSQLService`.
- The database engine lives behind the service boundary.
- CSSMS is the management client on top of the service, not a separate engine host.

In short:

`engine implementation -> QCSQL service -> CSSMS client`

## What Exists Today

### Service-side

- `CQL_Database_Engine/QCSQLServiceProtocol.h` defines the request/response protocol.
- `CQL_Database_Engine/QCSQLService.h/.cpp` define a service wrapper with `CreateDatabase`, `OpenDatabase`, `CloseDatabase`, `ExecuteSQL`, `GetStatus`, and `GetDatabaseInfo` handlers.
- Root CMake exposes compile switches:
  - `CITADEL_QCSQL_USE_QCQL_ENGINE`
  - `CITADEL_QCSQL_USE_CQL_ENGINE`
- The service is not yet registered into the main Citadel runtime path.

### Engine-side

- `QCQL/` is the current Citadel-native engine path.
- `CQL_Database_Engine/` still contains parser and executor logic from the standalone design path.

### UI-side

- `QCMS/` is the current Citadel Management Studio surface.
- The first CSSMS query workspace should target the `QCSQL` protocol shape even before the real service transport is complete.

## Current Rule

- New UI work should talk to a service-shaped adapter.
- Direct engine calls are acceptable only inside a temporary local adapter that mirrors `QCSQLServiceProtocol` request/response behavior.
- Once `QCSQLService` is registered on `QK::Svc::Registry`, the UI transport should switch without requiring a panel redesign.

## First-Step Implementation Pattern

For the initial Citadel-native CSSMS query panel:

- Use `ExecuteSQLRequest` / `ExecuteSQLResponse` semantics.
- Back the implementation with a local adapter over `QCQL::Database`.
- Support a small but useful SQL subset first:
  - `SHOW TABLES`
  - `DESCRIBE <table>`
  - `SHOW COLUMNS FROM <table>`
  - `SELECT * FROM <table> [LIMIT N]`
  - `CREATE TABLE ...`
- Treat this as a transport stub, not the final service integration.

## Definition of Done for the Service Transition

- `QCSQLService` is registered with `QK::Svc::Registry`.
- CSSMS sends named-service messages to `QCSQL`.
- The local adapter path can be removed or kept only as an offline fallback.
- The UI contract remains unchanged because it already matches `QCSQLServiceProtocol`.
