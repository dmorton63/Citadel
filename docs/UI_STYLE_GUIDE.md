# Citadel UI Style Guide

This guide defines shared visual rules for desktop widgets and boot flows.
It is intended to keep `desktop.theme` configuration, boot screens, and widget rendering consistent.

## Scope

- Applies to desktop shell widgets, setup/login flows, boot status UI, and modal overlays.
- Applies whether styles come from builtin presets or JSON theme files.
- Uses existing theme structures documented in `README.md` (`palette`, `metrics`, `button`, `font`, `effects`).

## Color System

All colors use `#RRGGBB` or `#AARRGGBB`.

### Core roles

- `accent`: primary action and focus ring color.
- `accentLight`: hover/preview accent.
- `accentDark`: pressed/active accent.
- `panel`: default surface color for shells, panels, and cards.
- `panelBorder`: edge contrast for panel/window boundaries.
- `text`: primary readable text on panel backgrounds.
- `textSecondary`: supporting text, metadata, and hints.

### Contrast rules

- Body text should target at least a 4.5:1 contrast ratio against its background.
- Large text (18px+ equivalent) should target at least a 3:1 contrast ratio.
- Interactive focus indicators must remain visible on both normal and active states.

### State mapping

- Normal: base palette role.
- Hover: blend toward `accentLight` or raise brightness by ~8-12%.
- Pressed/Active: blend toward `accentDark` or lower brightness by ~10-15%.
- Disabled: reduce alpha and remove glow emphasis.
- Error/Destructive: use destructive role styling, never ambiguous accent-only coloring.

## Typography

Current renderer is fixed, but all typography settings should still be expressed through theme metadata for future font switching.

### Semantic sizes

- Display (boot title/major section): 20-24
- Heading (panel titles/dialog titles): 15-18
- Body (default content): 12-14
- Caption/Meta (timestamps, hints): 10-11

### Text behavior

- Prefer sentence case for UI labels.
- Keep primary action labels short and verb-first (`Open`, `Apply`, `Unlock`).
- Avoid all-caps except short status markers where space is constrained.

## Shape, Borders, and Depth

- `cornerRadius`: use as the base rounding language for windows/cards.
- `buttonCornerRadius`: may be smaller than window radius, but not larger.
- `borderWidth`: keep subtle (typically 1-2).
- Shadow should support depth separation, not visual noise.
- Glow is reserved for accent roles, active states, and critical attention points.

## Animation Beats

Animation should communicate state transitions, not decorate static screens.

### Timing guidance

- Hover: 100-160 ms
- Press: 40-90 ms
- Panel/dialog enter: 160-240 ms
- Boot status step transition: 120-220 ms
- Error pulse/shake: 220-320 ms total sequence

### Motion principles

- Use ease-out for enter transitions and ease-in for exits.
- Stagger groups (lists/cards/log rows) by 20-40 ms when revealing.
- Avoid multiple competing motion sources in the same region.
- Respect reduced-motion mode when introduced: fade-only fallback, no shakes/pulses.

## Boot Flow Visual Language

Boot and pre-desktop screens should feel like the same product family as desktop UI.

### Structure

- Top: product mark/status headline.
- Middle: progress/state narrative (what is happening now).
- Bottom: compact diagnostics and actionable errors.

### Status colors

- Info/progress: accent.
- Success: green-tinted accent variant.
- Warning: amber/orange variant.
- Error/blocking: red/destructive variant.

### Messaging

- Keep status lines explicit (`Verifying boot trust`, `Loading desktop services`).
- For failures, include brief cause + next action (`Retry`, `Open recovery`).

## Widget Consistency Checklist

When introducing or updating a widget/boot panel:

- Map colors only through semantic roles, not hardcoded constants.
- Use semantic text sizes instead of ad-hoc scaling.
- Apply hover/press/active states with the timing guidance above.
- Validate readability against both light and dark seasonal themes.
- Ensure error/warning states are distinguishable without color alone.

## Theme Authoring Notes

- Prefer starting from a `base` theme and layering `overrides`.
- Keep override payloads minimal and intentional; avoid redefining every key.
- Validate that missing optional keys gracefully fall back to base defaults.
- Keep animation values bounded to avoid sluggish UI or flicker.

## Adoption

- Desktop widgets should reference this guide when adding new role colors/sizes/states.
- Boot/setup flows should reference this guide when introducing new progress/error surfaces.
- If a new visual pattern is needed, update this document first, then implement.