# Citadel Window Design Strategy

## Why This Exists

Recent UI behavior shows architecture drift:
- Window-level chrome and app-level controls overlap.
- Message routing is split across multiple patterns.
- Hit-testing works in many places but lacks one shared contract.
- CMMS close button can be hidden by content layering.

This document defines a single strategy for window creation, structure, routing, and hit-testing, plus a practical database adoption plan for themes and credentialing.

## 1) Standard Window Creation

Every app window should be built with one canonical lifecycle:

1. Allocate window hidden.
2. Set flags.
3. Build root regions in fixed order.
4. Bind handlers.
5. Hydrate state/data.
6. Show and focus.

### API Pattern

Use a `WindowSpec` + `WindowBuilder` style (or equivalent helper functions):
- `create(spec)` returns hidden window with validated bounds/flags.
- `buildChrome(window)` creates titlebar/caption buttons.
- `buildContent(window)` creates app panels.
- `bindHandlers(window)` connects messages/events.
- `show(window)` sets visible and requests render.

### Ownership Rule

Do not place app controls (like content close buttons) in raw root coordinates unless they are in a dedicated chrome layer. Root is for regions; regions own controls.

## 2) Good Window Structure

Adopt one structure for all desktop apps:

- Root
- ChromeLayer (title bar, caption buttons, drag area)
- ClientLayer (app content)
- OverlayLayer (dialogs, menus, tooltips)

### Z-Order Contract

Top-to-bottom paint and input priority:
1. OverlayLayer
2. ChromeLayer
3. ClientLayer

### Layout Contract

- Chrome height is a shared constant (single source of truth).
- Client starts below chrome (`y = chromeHeight`).
- Client never draws into chrome bounds.
- Overlay is explicit and temporary.

## 3) Good Message Routing

Use two channels with clear boundaries:

- Input/Event channel: mouse, keyboard, focus, lifecycle.
- UI Command channel: semantic actions (`WindowClose`, `ThemeApply`, `CredentialsUnlock`).

### Routing Rules

- Raw input never directly mutates business state.
- Controls emit semantic commands.
- App controller handles commands and updates model/services.
- Model/service changes emit state updates to views.

### Recommended Message Types

- `UiCommand::WindowClose`
- `UiCommand::WindowMinimize`
- `UiCommand::OpenPanel(panelId)`
- `UiCommand::ApplyTheme(themeId)`
- `UiCommand::EnrollOwner(user)`
- `UiCommand::UnlockOwner(user)`

This reduces ad-hoc callback paths and keeps window behavior predictable.

## 4) Good Mouse Hit-Test

Use one shared hit-test contract with explicit regions:

- `Nowhere`
- `Client`
- `ChromeDrag`
- `CaptionClose`
- `CaptionMinimize`
- `ResizeEdge*` / `ResizeCorner*`

### Hit-Test Order

1. Overlay controls
2. Caption buttons
3. Resize edges/corners
4. Chrome drag zone
5. Client controls

### Behavior Rules

- Capture on press and release to the same target.
- Title-drag starts only when hit-test returns `ChromeDrag` and no caption button consumed the click.
- Hover and pressed visuals are driven by the same region result.
- Region geometry uses the same constants as painting to prevent visual/input mismatch.

## Immediate Problem Explained (CMMS Close Button)

Current CMMS close button is created in root coordinates near top-right, then large content panels are added after it and painted above it. Input dispatch also checks topmost child first, so the panel can both hide and intercept the button area.

Short-term fix:
- Move close button into `ChromeLayer` and keep that layer above client panels.

Long-term fix:
- Implement shared window-region layout with strict z-order contract for every app.

## Database Strategy (Themes + Credentialing)

## Current State

- QCQL system tables exist for `Themes`, `ThemeTokens`, and `Capabilities`.
- Builtin theme importer exists and is idempotent.
- Theme importer is not yet wired into boot initialization.

## Target Data Model

### Theme Tables

Keep and use existing tables:
- `Themes(id, name, payload)`
- `ThemeTokens(id, themeId, tokenKey, tokenValue)`

Add when needed:
- `ThemeSelections(id, userId, themeId, updatedAt)`

### Credentialing Tables (metadata only)

Do not store secrets in QCQL. Store only policy and metadata:
- `CredentialProfiles(id, userId, algo, iterations, saltRef, state, createdAt, updatedAt)`
- `CredentialAudit(id, userId, eventType, status, source, timestamp)`

Secret material remains in SecurityCenter/SecureStore only.

## Integration Plan

### Phase A (Windowing Foundation)

- Introduce shared `WindowRegionLayout` and `HitRegion` types.
- Update one app (CMMS) to new structure.
- Verify close, drag, focus, and hit-test with logs.

### Phase B (Message Routing)

- Introduce `UiCommand` enum and central dispatch per app.
- Convert direct callback side-effects to command handlers.

### Phase C (Database Wiring)

- Wire `initializeSystemTables()` + `ThemeImporter::importBuiltinThemes()` at boot.
- Pass live QCQL database pointer from desktop into CMMS `open()`.
- Add read path for active theme from database, with fallback to builtins.

### Phase D (Credential Metadata)

- Add QCQL metadata/audit tables for credential state.
- Keep SecurityCenter as source of truth for enrollment/unlock.
- Record events to QCQL for visibility and tooling.

## Definition of Done

- Every app window uses the same region structure.
- Hit-test behavior is deterministic and logged by region.
- No app relies on root-level ad-hoc control placement for chrome.
- Theme tables are populated at boot and queryable in CMMS.
- Credential metadata is queryable without storing secrets in QCQL.

## First Tactical Steps

1. Refactor CMMS window into `ChromeLayer` + `ClientLayer`.
2. Add shared `HitRegion` enum and region function for window chrome.
3. Wire theme importer during boot database initialization.
4. Pass a live database instance to CMMS from desktop.
5. Add one integration test for caption close + drag + db theme list visibility.
