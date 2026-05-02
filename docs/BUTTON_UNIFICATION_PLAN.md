# Citadel Button Unification Plan

Date: 2026-05-01

## Purpose

This note defines the next control-layer step after agreeing that Citadel should not keep growing separate button implementations for text buttons, icon buttons, and borderline style variants.

The goal is one shared button behavior and rendering model with small convenience wrappers where they improve readability.

## Current State

Today the control split is mostly this:

- `QW::Controls::Button`
  - owns text, optional icon, borderless flag, role, click handling, and shared press/hover state
- legacy `QW::Controls::IconButton`
  - used to own icon, tooltip text, role, click handling, a narrower icon-centered hit-test, and nearly the same press/hover state machine

The current call sites already show the intended split:

- desktop launchers and app icons use icon-only buttons with tooltip semantics
- QCMS navigation uses text buttons with borderless styling and role-based state

That means the problem is not styling alone. The problem is that structure, content, and semantics are spread across two controls plus ad-hoc booleans.

## Design Rule

Keep `ButtonRole` as the semantic meaning of the action.

Examples:

- `Default`
- `Accent`
- `Sidebar`
- `Taskbar`
- `Destructive`

Do not overload `ButtonRole` to also mean layout shape, content arrangement, or hit-test policy.

Those are separate concerns.

## Proposed Unified Surface

Unify on one core `Button` implementation with these additional concepts:

```cpp
enum class ButtonContentMode : QC::u8
{
    Auto = 0,
    Text,
    Icon,
    TextAndIcon
};

enum class ButtonVariant : QC::u8
{
    Standard = 0,
    Borderless,
    Icon,
    Toolbar,
    Ghost,
    Compact
};
```

And extend `QW::Controls::Button` toward this shape:

```cpp
class Button : public ControlBase
{
public:
    const char *text() const;
    void setText(const char *text);

    const QG::ImageSurface *icon() const;
    void setIcon(const QG::ImageSurface *icon);

    const char *tooltipText() const;
    void setTooltipText(const char *text);

    ButtonContentMode contentMode() const;
    void setContentMode(ButtonContentMode mode);

    ButtonVariant variant() const;
    void setVariant(ButtonVariant variant);

    ButtonRole role() const;
    void setRole(ButtonRole role);
};
```

## Responsibility Split

The unified button should own these concerns:

- shared input state
  - hover
  - pressed
  - focused
  - disabled
- shared click behavior
  - capture on press
  - slop-tolerant release
- shared content model
  - text
  - icon
  - optional tooltip text
- shared layout policy
  - icon slot
  - label slot
  - text/icon spacing
  - content alignment
- shared rendering dispatch
  - standard framed button
  - borderless or ghost treatment
  - icon-only treatment
- variant-driven hit-test policy
  - default full-bounds hit-test
  - icon-centered hit-test when explicitly requested by variant

## Mapping From Today

Current `Button` maps like this:

- `borderless = false` -> `variant = Standard`
- `borderless = true` -> `variant = Borderless`
- text-only usage -> `contentMode = Text`
- text + icon usage -> `contentMode = TextAndIcon`

`setBorderless(...)` should now be treated as a compatibility bridge only.

New code should prefer:

- `setVariant(ButtonVariant::Borderless)`
- `setVariant(ButtonVariant::Icon)`
- `setVariant(ButtonVariant::Compact)`
- `setContentMode(...)`

The old `IconButton` shape mapped like this:

- `contentMode = Icon`
- `variant = Icon`
- `tooltipText = current tooltip`
- `role = current role`
- icon-centered hit-test retained through variant policy

## Removal Outcome

`IconButton` no longer needs to exist as a separate control type.

The old constructor and behavior map directly to:

- set `contentMode = Icon`
- set `variant = Icon`
- use `tooltipText` on `Button` when icon-only hover text is needed

To preserve old authored markup, the desktop parser may still accept names like `IconButton`, but that should resolve to a plain `Button` configured for icon-only behavior.

## Style System Impact

The style/render layer should continue to receive semantic intent through `ButtonRole`.

The smallest safe renderer evolution is:

1. keep `ButtonRole` unchanged
2. add `ButtonVariant` and `ButtonContentMode` to `ButtonPaintArgs`
3. let the renderer choose padding, framing, content arrangement, and icon sizing from those fields
4. gradually retire special-case logic that currently depends on `borderless` or implicit icon-only assumptions

This avoids collapsing styling and behavior into one enum.

## Recommended Migration Order

### Phase 1

Add the new enums and fields to the public button API while preserving current behavior.

### Phase 2

Move tooltip support and icon-only hit-test policy into `Button`.

### Phase 3

Collapse old `IconButton` behavior into `Button` and remove the separate control type once call sites no longer depend on it.

### Phase 4

Convert call sites incrementally:

- QCMS navigation buttons -> `Button` with `variant = Borderless`
- desktop launcher icons -> `Button` with `contentMode = Icon`, `variant = Icon`, tooltip enabled

### Phase 5

Remove old boolean-only styling knobs once call sites stop depending on them.

In practice, that means `setBorderless(...)` should remain temporarily for compatibility, but it should stop being the preferred API in new code.

## Non-Goals For The First Refactor

Do not combine all role, variant, and content concepts into one enum.

Do not rewrite all button call sites in one pass.

Do not change desktop visual behavior and event behavior at the same time as the API introduction.

## Definition Of Done

The button family is in a good place when:

- one control owns the real button state machine
- icon-only and text buttons differ by configuration, not implementation fork
- `ButtonRole` remains semantic rather than structural
- old `IconButton` semantics survive only as `Button` configuration or parser aliasing
- existing desktop and QCMS call sites can migrate without visual regressions
 
## Current Status

- `Button` now owns the shared state machine, tooltip support, icon-mode hit testing, and icon/text rendering configuration.
- Main desktop and QCMS call sites now use `Button` directly with explicit `contentMode` and `variant` settings.
- The separate `IconButton` control type has been removed to avoid programmer confusion.