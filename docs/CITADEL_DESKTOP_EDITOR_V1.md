# Citadel Desktop Editor V1

## Purpose

Define a practical first version of a desktop editor for Citadel that can:

- create and modify desktop layouts
- preview theme application against those layouts
- persist editable desktop data into CMMS
- export or regenerate bootstrap artifacts when needed

This document is intentionally grounded in the current runtime.

Today, Citadel already has:

- a DB-first desktop load path in `QDesktop`
- CUI-ML as the active production desktop definition
- JSON as an existing layout format and fallback surface
- theme loading through `ThemeService`
- CMMS document storage with chunked payload reconstruction

The editor must converge those surfaces. It should not invent a second long-term model that drifts away from runtime reality.

## V1 Goal

V1 should ship a layout editor first, with theme-aware preview but not full theme authoring.

That means:

- edit desktop structure
- edit control geometry and control properties
- choose a theme for preview
- save desktop documents into CMMS
- optionally export CUI-ML and JSON bootstrap artifacts

V1 should not attempt all of the following at once:

- full theme package authoring
- arbitrary visual scripting
- live multi-user collaboration
- signing workflow management
- direct editing of low-level renderer internals

## Design Principles

- One canonical editor document model.
- One persistence target for runtime state: CMMS.
- Import/export adapters for existing file formats.
- Deterministic round-tripping where practical.
- Validation before publish, not after boot failure.
- Keep layout editing separate from theme package editing.

## Module Boundary

V1 editor document code should live in `QDesktop`.

Reason:

- `QDesktop` already owns the active desktop load path
- `QDesktop` already contains the CUI-ML and JSON desktop import surfaces
- `QDesktop` already initializes CMMS-backed desktop documents
- the canonical `DesktopDocument` model should stay adjacent to the runtime code that consumes and adapts desktop layouts

Concrete v1 boundary:

- `QDDesktopDocument.*`: canonical document structs and enums
- `QDDesktopDocumentIO.*`: CMMS, CUI-ML, and JSON import/export adapters
- runtime rendering/widget classes remain in C++ and keep living where they are today

This keeps the first editor slice close to the runtime truth and avoids creating a second, drifting desktop model in a new module too early.

## Canonical Model

The editor should use one internal document shape regardless of whether the source came from CUI-ML, JSON, or CMMS.

### DesktopDocument

Represents one editable desktop layout.

Core fields:

- `documentId`: stable logical id such as `production`, `golden`, or user-defined ids
- `displayName`: human-readable name
- `sourceKind`: `cmms`, `cuiml-import`, `json-import`, `new`
- `version`: document schema version
- `canvas`: root desktop surface definition
- `controls`: flattened or hierarchical control list
- `bindings`: event and action bindings
- `background`: wallpaper, gradient, or fill definition
- `themeRef`: selected preview or default runtime theme id
- `metadata`: author, timestamps, notes, publish state

### CanvasModel

Represents the root desktop surface.

Fields:

- `widthPolicy`: fixed, scalable, or runtime-native
- `heightPolicy`: fixed, scalable, or runtime-native
- `safeInsets`
- `backgroundMode`
- `rootStyleClass`

### ControlModel

Represents one desktop control or container.

Fields:

- `id`: stable editor id
- `type`: button, panel, label, icon, clock, launcher, container, slider, custom
- `name`: optional human label
- `parentId`: optional parent relationship
- `layout`: absolute rect in v1
- `zIndex`
- `visible`
- `enabled`
- `text`
- `iconRef`
- `styleClass`
- `properties`: control-specific property bag
- `bindings`: click, hover, open, command, function, navigation

### ThemePreviewRef

V1 should not embed full theme payloads into the desktop editor document by default.

Fields:

- `themeId`
- `variant`: optional
- `previewOverrides`: optional transient overrides used only inside the editor session

### AssetRef

All asset references should be normalized.

Fields:

- `path`: preferably runtime-safe alias paths such as `/WALL/...`, `/ICONS/...`, `/UI/...`, `/FONTS/...`
- `kind`: wallpaper, icon, illustration, font, import
- `exists`: derived validation state

## Why This Model

The runtime currently consumes multiple authoring surfaces, but the editor should not expose that complexity as the primary mental model.

The editor should operate on:

- desktop structure
- control properties
- asset references
- action bindings
- theme selection for preview

Then adapters handle:

- CUI-ML import/export
- JSON import/export
- CMMS storage and retrieval

That preserves the current runtime while reducing future drift.

## Persistence Model

### Runtime Truth

CMMS should be the runtime-authoritative persistence layer.

For desktop layouts, the editor should save:

- metadata row
- chunked document rows

This matches the current chunked document strategy already used for desktop documents in `QDesktop`.

### Recommended CMMS Tables

V1 can build on the existing desktop document tables instead of replacing them immediately.

Logical storage shape:

- `DesktopLayouts`: document metadata for JSON-compatible layout documents
- `DesktopLayoutChunks`: chunk rows for layout document payloads
- `DesktopCuiml`: document metadata for CUI-ML documents
- `DesktopCuimlChunks`: chunk rows for CUI-ML payloads

Later, if the editor’s canonical model becomes distinct from either format, add a dedicated canonical document table instead of overloading runtime-specific formats forever.

### Drafts vs Published

V1 should distinguish drafts from published documents even if that starts as metadata only.

Recommended states:

- `draft`
- `validated`
- `published`

Published means safe for boot/runtime selection. Draft means editable and previewable, but not yet chosen as a primary boot layout.

## Import and Export Strategy

### Import

The editor should be able to open:

- CMMS-backed production desktop
- CMMS-backed golden desktop
- raw `.cml` desktop files
- raw `.json` desktop files

Each import path should normalize into `DesktopDocument`.

### Export

V1 should support export to:

- CMMS draft document
- CMMS published document
- CUI-ML file
- JSON file

Export is important even if CMMS is the runtime source of truth, because bootstrap and recovery flows still rely on file-backed artifacts.

## Editing Scope for V1

V1 should support these layout editing capabilities:

- create a new desktop document
- open an existing desktop from CMMS or file
- add, remove, duplicate, and reorder controls
- drag and resize controls on a canvas
- edit core properties in an inspector
- edit basic action bindings
- choose wallpaper and icon assets from runtime-safe paths
- preview the layout under an existing theme id
- validate and save

V1 should not support these advanced features yet:

- arbitrary constraint-based layout solving
- animation timeline authoring
- full stylesheet authoring for CUIMLSS
- direct theme token editing beyond preview overrides
- signed publish pipeline automation

## Editor UX

V1 should be a workspace-style editor with five primary surfaces.

### 1. Document Explorer

Shows:

- production desktop
- golden desktop
- drafts
- imported files

Actions:

- new
- duplicate
- open
- rename
- publish
- export

### 2. Canvas

The main desktop preview surface.

Capabilities:

- drag/drop placement
- resize handles
- selection outline
- z-order controls
- snap to grid in v1 if needed

V1 should prefer a predictable absolute layout canvas because that maps most directly onto current desktop definitions.

### 3. Control Palette

Initial control set should be small and runtime-backed:

- panel
- button
- label
- image/icon
- launcher tile
- clock
- slider
- container

Do not add control types the runtime cannot render yet.

### 4. Property Inspector

Shows editable properties for the current selection.

Core groups:

- identity
- geometry
- text and label
- asset references
- style class
- visibility and enabled state
- action bindings

### 5. Theme Preview Panel

Lets the user:

- pick a `ThemeID`
- compare current layout against installed themes
- optionally apply temporary preview overrides

This panel is for preview in v1, not for creating a new theme package from scratch.

## Validation Rules

The editor should validate before save and before publish.

Validation categories:

- duplicate control ids
- invalid parent relationships
- out-of-bounds geometry
- missing required text or asset references
- unresolved theme ids
- invalid action bindings
- unsupported control types
- non-runtime-safe asset paths
- export round-trip failures

Publish validation should be stricter than draft validation.

## Runtime Integration

The editor should integrate with runtime in this order:

### Phase A

- open CMMS desktop documents
- save draft documents back into CMMS
- export bootstrap file artifacts manually

### Phase B

- publish a selected document id as active production or golden
- trigger runtime reload in a controlled way if supported

### Phase C

- add dedicated theme package editor
- add validation and signing workflow support

## Recommended Architecture

Separate the editor into four layers.

### 1. Canonical Document Layer

Responsible for:

- `DesktopDocument`
- editor-side validation
- change tracking
- undo/redo

### 2. Adapter Layer

Responsible for:

- CUI-ML import/export
- JSON import/export
- CMMS document load/save

### 3. Editor UI Layer

Responsible for:

- canvas
- palette
- inspector
- preview
- document management

### 4. Publish Layer

Responsible for:

- publish validation
- CMMS promotion from draft to published
- optional file artifact regeneration

## Suggested V1 Deliverables

Build V1 in this order.

### Step 1

Define the canonical `DesktopDocument` schema in code.

### Step 2

Implement import adapters for:

- current CMMS desktop documents
- current CUI-ML file format
- current JSON layout format

### Step 3

Implement a simple editor shell with:

- canvas
- control palette
- property inspector
- save/load

### Step 4

Implement draft save into CMMS.

### Step 5

Implement export back to CUI-ML and JSON.

### Step 6

Implement publish validation and active-layout promotion.

## Theme Editing After V1

Full theme editing should be a separate milestone after the layout editor is stable.

That future editor can manage:

- semantic palette tokens
- typography tokens
- asset packs
- style rules
- validation against `ThemeService`

For now, layout authoring and theme authoring should stay loosely coupled.

## Key Decision

The most important architectural decision is this:

The desktop editor should not treat raw `.cml` or `.json` as the primary long-term editing model.

They are compatibility and bootstrap formats.

The editor should operate on one canonical document model, persist runtime state through CMMS, and import/export the legacy authoring surfaces only as adapters.

That keeps the system aligned with the current DB-first runtime direction without breaking recovery and production artifact flows.