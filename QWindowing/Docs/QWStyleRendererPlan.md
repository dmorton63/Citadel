# QW StyleRenderer Integration Plan

## 1. Objectives
- Centralize control/window drawing logic so behaviors stay inside `QWControls` while visuals live inside a dedicated renderer.
- Drive all visual decisions from stylesheet data (e.g. `desktop.json`, seasonal variants) supplied by a Style SubSystem.
- Keep the existing framebuffer path alive by targeting the new `QG::GraphicsBackend` (with `QW::FramebufferBackend` as the first implementation) so the desktop stays interactive throughout the refactor.

## 2. Layered Rendering Stack
```
Controls (events, layout, state machines)
    ↓ paint(ControlPaintContext)
StyleRenderer (interprets styles, maps state → primitives)
    ↓
GraphicsBackend (FramebufferBackend today, GPU later)
    ↓
Offscreen window surface (existing window buffer)
    ↓
Compositor (current blit path, future effects)
    ↓
Framebuffer (final flush)
```

## 3. Style Data Flow
1. **Style Assets**: JSON descriptors (`desktop*.json`) describe palettes, gradients, radii, shadows, accent colors, asset URIs.
2. **Style SubSystem** (`QWStyleSystem` new module):
   - Watches selected JSON + overrides (seasonal, user prefs).
   - Normalizes data into a `StyleTree` (hierarchical dictionary keyed by semantic roles like `window.chrome`, `button.primary`, etc.).
   - Produces immutable `StyleSnapshot` objects pushed to interested consumers.
3. **Distribution**:
   - `QW::WindowManager` subscribes once, stores the latest snapshot, and hands references to each window/control tree during paint.
   - Controls never parse JSON; they consume resolved tokens via their paint contexts.

## 4. StyleRenderer Responsibilities
`class StyleRenderer` (lives in QWindowing):
- Holds references to:
  - `QG::GraphicsBackend` (per-frame, usually `FramebufferBackend`).
  - `StyleSnapshot` providing resolved metrics/colors.
- Provides layup helpers:
  - `void begin(const FrameContext &frame);` / `void end();` to wrap backend `beginFrame`/`endFrame` calls.
  - `void drawWindowChrome(const WindowPaintArgs &args);`
  - `void drawPanel(const PanelPaintArgs &args);`
  - `void drawButton(const ButtonPaintArgs &args);`
  - `void drawScrollBar(...)`, etc. (grow as controls migrate).
- Converts semantic parameters (state enums: normal/hover/pressed, density, accent) into concrete primitives: gradients, rounded rects, shadows, glyph styles.
- Exposes primitive helpers (shadow, highlight, text) so complex controls can be composed without duplicating logic.

### Suggested Paint Contexts
Each control passes a small struct containing:
- Geometry (`QC::Rect bounds`, `Margins padding`).
- State (`bool hovered`, `bool pressed`, `bool focused`, `float animationT`).
- Data binding (text, icon ID, progress percentage).
- Optional references to child surfaces (e.g., icon atlas entries).

## 5. Integration Steps
1. **Backend plumbing**
   - Ensure every window obtains a `GraphicsBackend` through `WindowManager` (already possible via `FramebufferBackend`).
   - Add a per-frame `StyleRenderer` instance (could be shared across windows since it holds only references).
2. **Control paint refactor**
   - Update `Controls::Control::paint()` to receive a `ControlPaintContext` that includes a `StyleRenderer &` instead of raw painter access.
   - Controls call semantic draw methods instead of issuing primitives directly. Transitional phase: fallback path can still access `QG::IPainter` until each control migrates.
3. **Window/Compositor**
   - `QW::Window` uses StyleRenderer to draw chrome, background, and drop shadow before painting child controls.
   - Compositor keeps blitting window buffers; once offscreen textures land, StyleRenderer simply targets the same backend interface.
4. **Style SubSystem activation**
   - Introduce `QWStyleSystem::initialize(const char *stylePath);` to parse JSON and emit `StyleSnapshot`.
   - Expose `subscribe(IStyleListener *)` so WindowManager/desktop shell can react to runtime theme swaps.
5. **Validation hooks**
   - Add tracing/profiling flags inside StyleRenderer to capture draw timings and cache hits (ensures responsiveness goal).

## 6. Phased Delivery
1. **Phase 0** – Land backend abstraction (done) + skeleton StyleRenderer class returning no-op draws.
2. **Phase 1** – Implement Style SubSystem parser delivering hard-coded snapshot defaults; StyleRenderer consumes it for window chrome + panels.
3. **Phase 2** – Migrate primary controls (Panel, Button, Label) to StyleRenderer semantics.
4. **Phase 3** – Add compositor-ready surfaces (optional) and extend backend to support rounded corners, blur shadows, animations.
5. **Phase 4** – Wire seasonal/JSON themes for runtime switching and add persistence of user selections.

## 7. Risks & Mitigations
- **Performance regressions**: keep backend calls batched per control, cache gradient ramps, reuse temporary buffers.
- **Style drift between controls**: enforce all paint paths go through StyleRenderer, with lint/tests that forbid direct painter usage once migrated.
- **Thread safety**: `StyleSnapshot` objects are immutable; Style SubSystem swaps them atomically to avoid tearing mid-frame.
- **Future GPU work**: backend interface already abstracts pixel format/capabilities; add capability bits for blur/rounded rect once accelerated paths exist.
