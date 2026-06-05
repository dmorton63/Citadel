# CQL Repository Boundary

Date: 2026-06-05

## Purpose

This note fixes the architectural boundary between the external CQL project and Citadel so the engine, service, and workbench do not drift back into a single blended design.

## Boundary

### External CQL repository

- Owns the database engine implementation.
- Owns schema definitions and migrations.
- Owns the workbench or admin UI.
- Evolves independently of Citadel.
- Publishes versioned engine and schema releases that Citadel can adopt deliberately.

### Citadel repository

- Consumes a pinned engine version as an upstream dependency.
- Consumes pinned schema definitions as an upstream contract.
- Owns the Citadel-side database service boundary.
- Owns IPC, PAL, lifecycle, security integration, persistence policy, and boot/runtime integration.
- Does not treat the external workbench as part of the OS runtime.

## Change flow

### Schema or engine changes

1. Change originates in the external CQL repository.
2. The external project publishes a versioned update.
3. Citadel intentionally adopts that version.
4. Citadel updates its service integration only as needed for that adopted version.

### Citadel service changes

- Citadel service implementation changes do not flow back upstream automatically.
- Citadel may add adapters or service-side behavior without changing the external workbench.

### Workbench changes

- Workbench or admin UI changes do not flow into Citadel unless Citadel intentionally adopts a newer upstream engine or schema version.

## Rule

Citadel owns database service integration.
The external CQL project owns the engine, schema, and workbench.
Citadel only consumes versioned upstream artifacts.

## Practical implication for this repo

- `QCQL/` is the current Citadel-native engine surface in use here.
- `CQL_Database_Engine/` remains source material, service scaffolding, and a bridge to the broader external CQL lineage.
- The embedded `imgui` workbench lineage should be treated as an external project boundary, not as part of Citadel's core OS architecture.