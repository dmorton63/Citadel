
# **Citadel OS — Theme System Concept (Development Summary)**

## **1. Purpose**
The Citadel Theme System provides a **deterministic, declarative, single‑source‑of‑truth** model for all visual styling in the Citadel Desktop Environment. It eliminates scattered color definitions, ad‑hoc JSON files, legacy theme folders, and UI‑layer duplication by consolidating everything into **one ID, one load step, one style struct, one asset struct**.

The goal:  
A theme system that is **predictable**, **parallel‑friendly**, **artist‑friendly**, and **architecturally clean**.

---

## **2. Core Principles**
- **Deterministic** — No runtime guessing, no multi‑source overrides, no “theme drift.”  
- **Declarative** — Themes describe *what* they are, not *how* to render them.  
- **Unified** — All theme data flows through a single service.  
- **Modular** — Style, assets, and layout remain separate but coordinated.  
- **Parallel‑Native** — Loading a theme is a single atomic operation, safe for multithreaded UI.  
- **Semantic** — Colors and styles are named by *meaning*, not by hex values.

---

## **3. High‑Level Architecture**

### **3.1 ThemeService (central authority)**
The ThemeService is the **only** subsystem responsible for:

- Loading a theme by `ThemeID`
- Parsing the theme’s CML file
- Constructing `CitadelStyle`
- Constructing `CitadelThemeAssets`
- Broadcasting a theme‑changed event to UI components

It is a **service**, not a folder of files.

### **3.2 ThemeID**
A simple enum or string key that identifies a theme package.

Examples:
```
ThemeID::Default
ThemeID::Midnight
ThemeID::Solar
ThemeID::HighContrast
```

This is the *only* selector the rest of the OS ever touches.

### **3.3 CML Theme Definition**
Each theme has a single CML file describing:

- Semantic color palette  
- Typography  
- Corner radii  
- Shadows  
- Spacing tokens  
- Optional theme metadata  

CML is the structural definition — not the renderer.

### **3.4 CitadelStyle (the semantic style struct)**
This is the **heart** of the system.

It contains:
- Semantic colors (e.g., `Surface`, `SurfaceVariant`, `AccentPrimary`, `AccentSecondary`, `Error`, `Warning`, `Success`)
- Typography tokens
- Spacing tokens
- Border radii
- Shadow definitions
- UI state variants (hover, pressed, disabled)

**No hex values appear in UI code.**  
UI code only references semantic tokens.

### **3.5 CitadelThemeAssets**
A companion struct containing:

- Icons  
- Backgrounds  
- Illustrations  
- Optional theme‑specific textures  

All assets are grouped by theme ID and loaded in one atomic step.

### **3.6 UI Components**
UI components never load colors or assets directly.  
They only reference:

```
ThemeService.Style
ThemeService.Assets
```

This guarantees consistency and prevents drift.

---

## **4. Data Flow (Single Load Step)**

1. User selects a theme (or system loads default).  
2. ThemeService receives `ThemeID`.  
3. ThemeService loads the theme’s CML file.  
4. ThemeService constructs:
   - `CitadelStyle`
   - `CitadelThemeAssets`
5. ThemeService publishes a theme‑changed event.  
6. UI components rebind to the new style/asset structs.  

**No partial loads. No multi‑file merges. No overrides.**

---

## **5. Why This Design Works**
### **5.1 Eliminates legacy clutter**
No more:
- scattered JSON files  
- multiple theme folders  
- random color constants in UI code  
- duplicated asset lookups  

### **5.2 Perfect for parallel rendering**
Because the theme is a **single immutable struct**, it can be safely shared across threads.

### **5.3 Artist‑friendly**
Designers can modify a theme by editing **one CML file** and dropping assets into **one folder**.

### **5.4 Developer‑friendly**
UI code becomes semantic and self‑documenting:

```
Button.Background = Style.SurfaceVariant
Button.TextColor = Style.OnSurface
Icon = Assets.Icons.Settings
```

### **5.5 Deterministic**
No runtime surprises.  
No “why is this widget a different shade?”  
No “which file is overriding this color?”  

---

## **6. Development Roadmap (Execution Order)**

### **Phase 1 — Foundation**
1. Define `ThemeID` enum  
2. Implement ThemeService skeleton  
3. Define CML schema for themes  

### **Phase 2 — Style System**
4. Implement `CitadelStyle` struct  
5. Implement semantic color model  
6. Implement typography, spacing, radii, shadows  

### **Phase 3 — Asset System**
7. Implement `CitadelThemeAssets`  
8. Define asset folder structure per theme  
9. Add asset loading pipeline  

### **Phase 4 — Integration**
10. Bind UI components to semantic tokens  
11. Add theme‑changed event system  
12. Implement default theme  

### **Phase 5 — Tooling**
13. Create Theme Previewer (optional)  
14. Create Theme Validator (checks CML correctness)  
15. Document “How to Create a Theme” for developers  

---

## **7. Future Extensions**
- Dynamic theme switching animations  
- User‑generated theme packs  
- High‑contrast accessibility themes  
- Real‑time theme editing tools  
- Theme marketplace integration (if desired)  

---

## **8. Reality Check Against Current Citadel**

This concept aligns with the direction Citadel is already moving in, but the current implementation is **not starting from zero**.

What already exists in practice:
- A runtime style snapshot path through `QW::StyleSystem`
- A desktop-side theme object via `QDTheme`
- JSON theme loading and seasonal desktop variants
- Theme override merging in desktop configuration
- Style distribution from the desktop shell into window/control rendering

This means the right implementation strategy is **consolidation**, not replacement.

The goal should be:
- keep the existing renderer/style snapshot pipeline where it already works
- reduce the number of authoring paths
- reduce runtime merge ambiguity
- converge on one canonical internal theme representation

---

## **9. What To Adopt As-Is**

These parts of the concept are strong and should remain the target design.

### **9.1 Single selector model**
The rest of the OS should select a theme through **one stable identifier** only.

Target:
```
ThemeID::Default
ThemeID::Winter
ThemeID::HighContrast
```

Desktop configs, boot UI, widgets, and future settings UI should reference `ThemeID`, not raw color payloads.

### **9.2 Canonical runtime theme object**
Citadel should always normalize loaded theme data into:
- one immutable semantic style struct
- one immutable asset struct

This is the internal truth regardless of authoring format.

### **9.3 Semantic tokens only in UI code**
UI code should consume semantic roles only.

Examples:
- `Surface`
- `SurfaceRaised`
- `OnSurface`
- `AccentPrimary`
- `AccentHover`
- `Error`
- `Warning`
- `Success`

No direct hex colors in widgets, desktop shell paint logic, or boot/status UI.

### **9.4 Atomic distribution**
Theme changes should publish a complete snapshot in one operation so controls never observe a half-updated style state.

---

## **10. What Should Be Softened**

These parts of the concept are directionally good but too strict for the current Citadel workflow.

### **10.1 “No overrides” should become “one bounded overlay layer”**
Citadel already uses seasonal variants, desktop overrides, and user/runtime adjustments.

That should not remain an open-ended merge chain, but it also should not be banned outright.

Recommended rule:
- one canonical base theme selected by `ThemeID`
- optional single overlay layer for approved runtime adaptation
- overlay must be explicit, validated, and limited in scope

Examples of allowed overlay use:
- accessibility adjustments
- seasonal wallpaper swap tied to the selected theme
- user font size or contrast preference

Examples of disallowed overlay use:
- arbitrary per-widget color mutations
- multiple cascading override files
- layout JSON redefining theme palette values

### **10.2 CML should not be mandatory on day one**
The important win is **schema discipline and a canonical internal model**, not the file extension.

If Citadel can reach the target architecture faster by first normalizing existing JSON themes, that is the better engineering path.

Recommended rule:
- phase 1 may accept existing JSON theme sources
- phase 2 may add CML as the preferred authoring format
- runtime output must be identical regardless of source format

### **10.3 Theme and layout must stay separate**
Theme data should define visual language.
Layout data should define structure, geometry, and composition.

The desktop shell should stop carrying direct palette values inside layout definitions except during migration.

### **10.4 Assets may need preload plus lazy-load policy**
The theme should appear atomic to the UI, but large wallpapers or optional illustrations may still need staged loading under the hood.

The contract should be atomic presentation, not necessarily naive eager loading of every asset byte.

---

## **11. Recommended Implementation Strategy**

The safest implementation path is to treat this as a **migration to a canonical theme pipeline**.

### **Phase 0 — Freeze The Target Contract**
Before code changes, define the contract for:
- `ThemeID`
- `CitadelStyle`
- `CitadelThemeAssets`
- approved overlay schema
- semantic token list

Deliverables:
- authoritative token list
- canonical theme schema document
- decision on required vs optional theme fields

### **Phase 1 — Normalize Current Inputs**
Keep the current theme sources working, but route them through one normalization layer.

Input sources supported temporarily:
- current desktop JSON theme object
- current theme file path loading
- builtin presets
- seasonal presets

Output from all of them:
- `CitadelStyle`
- `CitadelThemeAssets`

This phase removes ambiguity without forcing a content rewrite.

### **Phase 2 — Introduce ThemeService Over Existing StyleSystem**
Do not replace the current style publication machinery immediately.

Instead:
- implement `ThemeService` as the authority for theme loading and normalization
- let it publish into the existing style snapshot distribution path
- keep `QW::StyleSystem` as the fan-out mechanism during migration

This minimizes churn in rendering code.

### **Phase 3 — Strip Theme Logic Out Of Layout JSON**
Desktop layouts should stop embedding palette values and ad-hoc surface styling.

Allowed in layout:
- geometry
- hierarchy
- control roles
- asset references by semantic key or ID

Not allowed in final state:
- raw colors
- duplicate border styles
- per-layout button fill definitions

### **Phase 4 — Migrate Widgets And Desktop Shell To Semantic Tokens Only**
Audit rendering code and remove direct color selection outside the canonical style object.

Priority order:
1. window chrome
2. desktop shell panels
3. buttons
4. labels and text surfaces
5. terminal chrome
6. boot/setup surfaces

This is where drift is actually eliminated.

### **Phase 5 — Add Validator And Preview Tooling**
Before expanding theme authoring, add tools that keep the system disciplined.

Required tooling:
- schema validator
- missing-token validator
- contrast validation for key text/surface pairs
- theme preview utility

Without this, authoring freedom will recreate the same drift under a different name.

### **Phase 6 — Decide Whether To Formalize CML**
Only after the canonical schema is stable should Citadel decide whether CML provides enough value to justify migration from JSON.

Decision criteria:
- readability
- parser complexity
- maintainability
- artist/developer workflow
- compatibility with existing tooling

---

## **12. Concrete Module Plan**

### **12.1 ThemeService responsibilities**
ThemeService should own:
- resolving `ThemeID`
- loading canonical theme definition data
- applying one approved overlay layer
- constructing `CitadelStyle`
- constructing `CitadelThemeAssets`
- publishing the resulting snapshot

ThemeService should not own:
- layout parsing
- widget rendering behavior
- geometry or composition decisions

### **12.2 CitadelStyle responsibilities**
`CitadelStyle` should become the semantic source for:
- surfaces
- text roles
- accent states
- borders
- shadows
- spacing
- corner radii
- typography scale
- state variants

It should map cleanly onto the existing style snapshot machinery.

### **12.3 CitadelThemeAssets responsibilities**
`CitadelThemeAssets` should index assets by semantic purpose, not file path assumptions.

Examples:
- `WallpaperPrimary`
- `IconSettings`
- `IconTerminal`
- `IllustrationBoot`
- `TextureGlass`

The rest of the UI should request assets by semantic key.

---

## **13. Migration Risks**

### **13.1 Biggest technical risk**
Trying to switch authoring format, runtime contract, and rendering behavior all at once.

That would create a large refactor with weak rollback options.

### **13.2 Biggest workflow risk**
Allowing old and new theme paths to coexist indefinitely.

If both paths remain first-class for too long, the system will keep its current ambiguity.

### **13.3 Biggest design risk**
Confusing theme data with layout data.

If layout files keep carrying visual definitions, ThemeService will exist in name only.

---

## **14. Implementation Order (Practical)**

1. Define canonical semantic tokens and required fields.
2. Define `ThemeID` and the canonical internal structs.
3. Build ThemeService as a normalization layer over current JSON/preset inputs.
4. Publish canonical data into the existing style snapshot path.
5. Remove raw theme payloads from desktop layout JSON.
6. Audit controls and shell code for direct color usage.
7. Add validator and preview tooling.
8. Decide whether to keep JSON or formalize CML.

This order gives Citadel the architectural win early without forcing a risky full rewrite.

---

## **15. Bottom-Line Recommendation**

This concept is worth implementing.

But the correct interpretation is:

**not** “replace the desktop theme system with a brand-new one”

It is:

**“converge the current desktop/theme pipeline into one canonical theme authority, one semantic style model, and one bounded authoring path.”**

If implemented this way, Citadel gets:
- less runtime ambiguity
- cleaner theme authoring
- easier debugging
- safer theme switching
- better long-term maintainability

while preserving the renderer and style distribution pieces that already exist.

---

## **16. File-by-File Engineering Plan**

This is the practical implementation map tied to the current Citadel tree.

### **16.1 Stage 1 — Freeze The Canonical Contract**

Primary files:
- `QWindowing/Include/QWStyleTypes.h`
- `QDesktop/Include/QDTheme.h`
- `docs/UI_STYLE_GUIDE.md`
- `README.md`

Work:
- define the final semantic token set Citadel will support
- decide which current `StyleSnapshot` fields remain, which get renamed, and which become compatibility aliases
- define the required and optional fields for a canonical theme definition
- document the difference between theme data, layout data, and overlay data

Expected result:
- one stable schema contract before touching loaders or renderer behavior

### **16.2 Stage 2 — Introduce ThemeID And ThemeService**

Primary files:
- `QDesktop/Include/QDTheme.h`
- `QDesktop/Src/QDTheme.cpp`
- `QDesktop/Include/QDDesktop.h`
- `QDesktop/Src/QDDesktop.cpp`

New files recommended:
- `QDesktop/Include/QDThemeService.h`
- `QDesktop/Src/QDThemeService.cpp`

Work:
- add `ThemeID` enum/string-key mapping
- implement ThemeService as the single loader/normalizer authority
- move theme source resolution out of `QDDesktop` and into ThemeService
- keep `QDDesktop` focused on applying the already-resolved theme output

Expected result:
- `QDDesktop` stops deciding how themes are discovered and merged
- one subsystem becomes responsible for theme selection and normalization

### **16.3 Stage 3 — Normalize Existing Theme Inputs**

Primary files:
- `QDesktop/Src/QDTheme.cpp`
- `QDesktop/Src/QDDesktop.cpp`
- `desktop.json`
- `desktop-overrides.json`
- `desktopWinter.json`
- `desktopStandard.json`
- `desktopspring.json`
- `desktopsummer.json`
- `desktopAutum.json`

Work:
- keep legacy JSON theme loading operational during migration
- convert current inputs into one canonical internal theme object
- support current sources only as adapters:
  - builtin preset
  - theme file path
  - inline theme definition
  - seasonal preset
- stop allowing open-ended merge behavior beyond the approved overlay layer

Expected result:
- existing desktops keep working
- runtime ambiguity drops immediately because all paths converge internally

### **16.4 Stage 4 — Publish Canonical Style Through Existing StyleSystem**

Primary files:
- `QWindowing/Include/QWStyleSystem.h`
- `QWindowing/Src/QWStyleSystem.cpp`
- `QWindowing/Include/QWStyleTypes.h`
- `QWindowing/Src/QWStyleRenderer.cpp`
- `QWindowing/Src/QWWindowManager.cpp`
- `QWindowing/Src/QWWindow.cpp`

Work:
- keep `QW::StyleSystem` as the distribution bus
- have ThemeService produce the full style snapshot it publishes
- ensure style updates remain atomic from the point of view of listeners
- remove renderer dependence on ad-hoc fallback theme logic where possible

Expected result:
- no large rendering rewrite
- current listener/update model remains intact

### **16.5 Stage 5 — Remove Theme Logic From Desktop Layout Data**

Primary files:
- `desktop.json`
- seasonal desktop JSON files
- `QDesktop/Src/QDDesktop.cpp`
- `README.md`

Work:
- move theme choice to `ThemeID`
- restrict layout files to:
  - control geometry
  - hierarchy
  - semantic control roles
  - semantic asset references
- remove raw palette values from layout JSON except temporary migration support

Expected result:
- desktop layout becomes structural instead of visual-authoring-heavy

### **16.6 Stage 6 — Eliminate Direct Color Usage In Paint Paths**

Primary files:
- `QWindowing/Src/QWStyleRenderer.cpp`
- `QWindowing/Src/QWWindow.cpp`
- `QDesktop/Src/QDDesktop.cpp`
- `QWControls/**`

Work:
- audit controls and shell rendering for direct color constants
- route those decisions through semantic style tokens only
- keep any temporary compatibility code isolated at the normalization layer

Priority order:
1. window chrome
2. desktop shell panels
3. buttons
4. labels/text surfaces
5. terminal chrome
6. boot/setup screens

Expected result:
- actual theme drift elimination

### **16.7 Stage 7 — Define Theme Assets As A First-Class Contract**

Primary files:
- `QDesktop/Src/QDDesktop.cpp`
- any existing image-loading helpers under desktop/windowing
- `README.md`

New files recommended:
- `QDesktop/Include/QDThemeAssets.h`
- `QDesktop/Src/QDThemeAssets.cpp`

Work:
- replace direct file-path assumptions with semantic asset keys
- define required vs optional assets per theme
- make wallpaper/icon/texture lookup a theme responsibility, not a layout responsibility

Expected result:
- theme package contents become predictable and easier to validate

### **16.8 Stage 8 — Tooling, Validation, And Content Migration**

Primary files:
- `README.md`
- `docs/UI_STYLE_GUIDE.md`
- theme files under desktop/system theme storage

New tooling recommended:
- theme validator
- theme previewer
- contrast checker

Work:
- validate token completeness
- validate overlay legality
- validate contrast for critical surface/text pairs
- generate preview output before runtime deployment

Expected result:
- safer authoring workflow
- fewer regressions when new themes are added

---

## **17. Draft Canonical Schema**

This schema is the proposed contract the implementation should converge on.

### **17.1 ThemeID**

`ThemeID` should be a stable runtime identifier, independent of file names.

Draft examples:
```cpp
enum class ThemeID : QC::u16
{
   Default = 0,
   Standard,
   Winter,
   Spring,
   Summer,
   Autumn,
   Midnight,
   HighContrast,
   Count
};
```

Rules:
- runtime chooses themes by `ThemeID`
- file paths remain an implementation detail
- user settings persist `ThemeID`, not raw paths

### **17.2 Canonical Theme Package**

Each theme package should normalize into:
```cpp
struct CitadelThemePackage
{
   ThemeID id;
   CitadelStyle style;
   CitadelThemeAssets assets;
   CitadelThemeMetadata metadata;
};
```

### **17.3 CitadelThemeMetadata**

Draft fields:
```cpp
struct CitadelThemeMetadata
{
   char name[64];
   char author[64];
   char version[32];
   char description[128];
   bool darkTheme;
   bool highContrast;
   bool supportsReducedMotion;
};
```

Metadata is descriptive only.
It must not change rendering semantics outside explicitly defined style fields.

### **17.4 CitadelStyle**

`CitadelStyle` should be the semantic visual contract.

Draft shape:
```cpp
struct CitadelStyle
{
   struct Colors
   {
      QC::Color SurfaceDesktop;
      QC::Color SurfaceWindow;
      QC::Color SurfacePanel;
      QC::Color SurfacePanelRaised;
      QC::Color SurfacePanelSunken;
      QC::Color SurfaceSidebar;
      QC::Color SurfaceTaskbar;

      QC::Color BorderSubtle;
      QC::Color BorderStrong;
      QC::Color Shadow;

      QC::Color TextPrimary;
      QC::Color TextSecondary;
      QC::Color TextDisabled;
      QC::Color TextOnAccent;

      QC::Color AccentPrimary;
      QC::Color AccentHover;
      QC::Color AccentPressed;
      QC::Color AccentSecondary;

      QC::Color Success;
      QC::Color Warning;
      QC::Color Error;
      QC::Color FocusRing;

      QC::Color Selection;
      QC::Color SelectionInactive;
   } colors;

   struct Metrics
   {
      QC::u32 WindowCornerRadius;
      QC::u32 PanelCornerRadius;
      QC::u32 ButtonCornerRadius;
      QC::u32 BorderWidth;
      QC::u32 FocusRingWidth;
      QC::u32 ShadowBlur;
      QC::i32 ShadowOffsetX;
      QC::i32 ShadowOffsetY;
      float TextScale;
   } metrics;

   struct Typography
   {
      char Family[48];
      QC::u32 DisplaySize;
      QC::u32 HeadingSize;
      QC::u32 BodySize;
      QC::u32 CaptionSize;
   } typography;

   struct Motion
   {
      QC::u32 HoverDurationMs;
      QC::u32 PressDurationMs;
      QC::u32 EnterDurationMs;
      bool ReducedMotion;
   } motion;

   struct ButtonRoleStyle
   {
      QC::Color FillNormal;
      QC::Color FillHover;
      QC::Color FillPressed;
      QC::Color FillDisabled;
      QC::Color Text;
      QC::Color Border;
      QC::Color Glow;
      bool Glass;
      bool CastsShadow;
   } buttons[static_cast<QC::u32>(QW::ButtonRole::Count)];
};
```

### **17.5 Minimum Semantic Token Set**

These tokens should be treated as the required baseline.

Required colors:
- `SurfaceDesktop`
- `SurfaceWindow`
- `SurfacePanel`
- `SurfaceSidebar`
- `SurfaceTaskbar`
- `BorderSubtle`
- `BorderStrong`
- `TextPrimary`
- `TextSecondary`
- `TextDisabled`
- `AccentPrimary`
- `AccentHover`
- `AccentPressed`
- `Success`
- `Warning`
- `Error`
- `FocusRing`

Required metrics:
- `WindowCornerRadius`
- `ButtonCornerRadius`
- `BorderWidth`
- `FocusRingWidth`
- `TextScale`

Required typography:
- `Family`
- `BodySize`
- `HeadingSize`

### **17.6 CitadelThemeAssets**

Assets should be addressed by semantic purpose.

Draft shape:
```cpp
struct CitadelThemeAssets
{
   struct Icons
   {
      const char *Settings;
      const char *Terminal;
      const char *Folder;
      const char *Start;
      const char *Shutdown;
   } icons;

   struct Backgrounds
   {
      const char *DesktopPrimary;
      const char *DesktopSecondary;
      const char *LockScreen;
   } backgrounds;

   struct Textures
   {
      const char *GlassNoise;
      const char *PanelOverlay;
   } textures;

   struct Illustrations
   {
      const char *Boot;
      const char *Setup;
      const char *Recovery;
   } illustrations;
};
```

Rules:
- asset keys are semantic
- theme packages may point those keys to files internally
- UI code requests `Assets.icons.Settings`, not a hardcoded path

### **17.7 Overlay Schema**

Citadel should allow exactly one bounded overlay layer.

Draft shape:
```cpp
struct CitadelThemeOverlay
{
   bool allowAccessibilityAdjustments;
   bool allowTypographyScaling;
   bool allowWallpaperSwap;

   struct ColorOverrides
   {
      bool AccentPrimarySet;
      QC::Color AccentPrimary;

      bool FocusRingSet;
      QC::Color FocusRing;
   } colors;

   struct MetricOverrides
   {
      bool TextScaleSet;
      float TextScale;
   } metrics;

   struct AssetOverrides
   {
      bool DesktopPrimarySet;
      const char *DesktopPrimary;
   } assets;
};
```

Allowed overlay domains:
- accessibility contrast
- text scaling
- reduced motion
- wallpaper/background swaps

Disallowed overlay domains:
- arbitrary per-widget color changes
- control-role schema changes
- layout geometry changes
- multi-layer cascading override chains

### **17.8 Compatibility Mapping To Current Citadel Types**

Current types can map forward approximately like this:

- `QDTheme` -> authoring/legacy input adapter
- `QW::StyleSnapshot` -> current runtime distribution object
- `CitadelStyle` -> future canonical semantic contract
- `ThemeService` -> normalization + publishing authority

Migration rule:
- do not force renderer code to understand legacy theme JSON directly
- legacy JSON must terminate at the normalization layer

---

## **18. First Implementation Batch**

If work starts immediately, the first coding batch should be:

1. Create `ThemeID` and the canonical metadata/style/assets structs.
2. Introduce `QDThemeService` without changing desktop JSON behavior yet.
3. Move theme loading resolution out of `QDDesktop` into the service.
4. Publish the resulting style through the existing `QW::StyleSystem`.
5. Keep existing theme JSON files operational through compatibility adapters.

That batch gives Citadel the new architecture center without breaking current desktop content.


