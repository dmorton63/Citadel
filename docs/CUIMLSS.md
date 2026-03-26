# CUIMLSS (CUI‑ML Style Subset)

CUIMLSS is a deliberately small, deterministic, *CSS-like* stylesheet language for CUI‑ML.
It is **not** a browser CSS engine: there is no layout, no cascade/inheritance, no combinators, and unknown properties are ignored.

## File Extensions

- `.cxs` is reserved for **CUIMLSS stylesheets**.
- Other Citadel UI formats (e.g. the XML-like component/icon/role library) should use a distinct extension (currently `.cui`) and a different parser.

## Selectors

Exactly one selector per rule:

- `Element` — matches a control tag name (e.g. `Panel`, `Label`, `Button`). Element matching is ASCII case-insensitive.
- `.class` — matches when the control has `class="..."` containing that token (space-separated). Class matching is case-sensitive.
- `#id` — matches when the control has `id="..."`. Id matching is case-sensitive.

Example:

```css
Panel { background: #111; }
.sidebar { background: #222; }
#taskbar { border: #444; border-width: 1; }
```

## Rule Application Order (Deterministic)

For each control, CUIMLSS computes an *effective* property set in this order:

1. All matching `Element` rules (in file order)
2. All matching `.class` rules (in file order; class token order does not matter)
3. All matching `#id` rules (in file order)

Later rules overwrite earlier ones.

Finally, **inline CUI‑ML attributes override stylesheet values** (e.g. `color="..."` beats `color:` from CUIMLSS).

## Supported Properties

Properties are ASCII case-insensitive; values are trimmed. Comments are supported via `/* ... */` and `// ...`.

### Common

- `enabled: true|false|1|0|yes|no|on|off`
- `visibility: visible|hidden`
- `opacity: 0%..100% | 0.0..1.0 | 0..255`
  - Applied by multiplying the alpha of any specified `color`, `background`, and/or `border` colors.

### Colors

- `color: <color>`
  - Applies to: `Label` text
- `background:` / `background-color: <color>`
  - Applies to: `Panel` background; `Label` background (also forces the label to be non-transparent)
- `border:` / `border-color: <color>`
  - Applies to: `Panel` border color (flat border)

`<color>` accepts the same formats as CUI‑ML today (e.g. `#RRGGBB`, `#RRGGBBAA`, `rgb(...)`, `rgba(...)`).

### Border / Padding

- `border-width: <int>`
  - Applies to: `Panel` border width (enables a flat frame)
- `padding: <int>`
- `padding-left|padding-top|padding-right|padding-bottom: <int>`
  - Applies to: `Panel` padding

### Text

- `font: <fontSpec>`
  - Applies to: `Label` and `Button`
  - Font spec is the same as the current inline attribute: `<family>-<style>-<sizePx>` (pixel size), e.g. `roboto-regular-14`
- `font-family: <fontFamily>`
  - Applies to: `Label` and `Button`
  - Sets the *base* font token (no size). Typically matches the `font:` value without the trailing `-NN`.
  - Intended to be paired with `font-size:` so you can override size without repeating the full spec.
- `font-size: <int>`
  - Applies to: `Label` and `Button`
  - Pixel size used with `font-family:` to form the effective `font:`.
  - If both `font-family` and `font-size` are present after rule merging, CUIMLSS synthesizes `font` as: `<font-family>-<font-size>`.
  - This also allows “partial overrides”: e.g. set `Label { font: roboto-regular-14; }` and later override only `font-size: 18;`.
- `text-align: left|center|right`
  - Applies to: `Label`

### Button

- `role: destructive|...`
  - Applies to: `Button` (currently only `destructive` has special meaning; other values are treated as default)

## Importing Styles

Stylesheets can be referenced from CUI‑ML using either of these tags (anywhere in the file; common placement is under `<head>` when using the HTML wrapper):

```xml
<ImportStyle src="/THEME.CXS" />
<link rel="stylesheet" type="text/cuimlss" href="/THEME.CXS">
```

Notes:
- `http://` and `https://` sources are ignored.
- If multiple stylesheets are imported, rules are applied in the order they are discovered/loaded.

## Optional HTML Wrapper

A CUI‑ML document may be wrapped as:

```html
<html lang="cuiml">
  <head>
    <link rel="stylesheet" type="text/cuimlss" href="/THEME.CXS">
  </head>
  <body>
    <!-- CUI‑ML tags here (Panel/Label/Button/etc) -->
  </body>
</html>
```

When `lang="cuiml"` is present, parsing only happens inside `<body> ... </body>`.

## 8.3 Filename Guidance (Early Boot)

Citadel’s early FAT32 reader does **not** implement Long File Names (LFN). For files you need available at early boot (ramdisk), use **8.3** names.

- The build already copies the desktop definition into the boot image as `DESKTOP.CML`.
- For stylesheets, prefer an 8.3 name like `THEME.CXS` (or similar) and reference that exact path in `src=` / `href=`.
- Ensure the stylesheet file is actually present in the boot image (e.g. copied into the ramdisk during build).

## Production / STRONG Signing Notes

If your production threat model includes *tampering with boot-time UI assets*, treat **stylesheets as part of the trusted desktop surface**, not “just cosmetics”. CUIMLSS can currently change `visibility`, `enabled`, `opacity`, and `role`, which can materially change what the user sees/clicks.

Recommended options (from simplest to strongest):

- **Sign every imported `.cxs`** (e.g. `THEME.CXS` + `THEME.SIG`) and refuse boot/load if missing/invalid in production.
- **Sign a manifest** (one signature covers the desktop `.cml` plus every referenced `.cxs` and asset hash), then verify the manifest.
- **Only sign the desktop `.cml`**, but have it *cryptographically bind* every referenced `.cxs` (e.g. embedded SHA‑256 hashes) and verify at load time.

Dev builds can keep “warn-only” behavior to avoid trapping iteration.
