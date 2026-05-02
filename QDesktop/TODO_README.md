# QDesktop - Desktop Environment TODO

## Overview

QDesktop will be a JSON-driven desktop environment with multiple visual style options:
- **Vista** - Windows Vista/Aero-style with glass, transparency, glow effects
- **Metro** - Windows 8/10 flat design with clean edges and accent colors
- **QWStyle** - Modern hybrid inspired by Windows 11/macOS (our unique take)

Features include:
- Glass/transparency effects (Vista mode)
- Glow highlights on focused elements
- Drop shadows
- Smooth gradients and blur effects
- Animated transitions

The desktop layout, themes, and styling will be fully configurable via JSON files.

---

## Architecture: UIStyle System

### Global Style Selector
```cpp
// QCommon/include/QCUIStyle.h
namespace QC
{
    enum class UIStyle : u8
    {
        Vista,    // Aero glass, gradients, glow, transparency
        Metro,    // Flat design, solid colors, sharp edges
        QWStyle   // Modern hybrid (Win11/macOS inspired, unique)
    };

    /// Global UI style - affects all control rendering
    UIStyle currentUIStyle();
    void setUIStyle(UIStyle style);
}
```

### Style-Aware Rendering
- Controls query `currentUIStyle()` during paint
- Frame class adapts borders/shadows based on style
- Theme colors shift per-style (glass tints vs flat colors)
- Animation timings vary (Vista: smooth, Metro: snappy, QWStyle: fluid)

### Style Characteristics

| Feature          | Vista              | Metro              | QWStyle            |
|------------------|--------------------|--------------------|--------------------|
| Borders          | 3D raised/sunken   | Flat, 1px          | Subtle rounded     |
| Backgrounds      | Gradient + glass   | Solid flat         | Soft gradient      |
| Shadows          | Drop shadow        | None/minimal       | Soft ambient       |
| Corners          | Slight rounding    | Square             | Rounded (6-8px)    |
| Hover effect     | Glow               | Color shift        | Subtle lift        |
| Focus indicator  | Glowing border     | Accent underline   | Ring + shadow      |
| Animations       | Smooth fade        | Quick snap         | Fluid spring       |

---

## Phase 1: JSON Parser & File Infrastructure

### 1.1 JSON Parser (`QDesktop/QDJson.h/.cpp`)
- [ ] Implement lightweight JSON tokenizer
- [ ] JSON value types: null, bool, number, string, array, object
- [ ] Parse from string buffer
- [ ] Error handling with line/column info
- [ ] Memory-efficient design (no STL)

### 1.2 JSON File Loading
- [ ] Integrate with QFileSystem VFS to read `.json` files
- [ ] Support for file watching (detect changes)
- [ ] Cached parsing for performance

---

## Phase 2: Theme System

### 2.1 Theme Definition (`QDesktop/QDTheme.h/.cpp`)
- [ ] `QDTheme` class to hold all visual settings
- [ ] Color palette (primary, secondary, accent, background, text, etc.)
- [ ] Font settings (when we have fonts)
- [ ] Border styles (width, radius, color)
- [ ] Shadow settings (offset, blur, color, spread)
- [ ] Glow settings (color, intensity, radius)
- [ ] Transparency/opacity levels
- [ ] Animation timings

### 2.2 Theme JSON Schema
```json
{
  "name": "Vista Glass",
  "colors": {
    "windowBackground": "#80FFFFFF",
    "titleBarGradientStart": "#CC3A6EA5",
    "titleBarGradientEnd": "#CC1E4A73",
    "buttonNormal": "#40FFFFFF",
    "buttonHover": "#60FFFFFF",
    "buttonPressed": "#30FFFFFF",
    "buttonGlow": "#8052B4E5",
    "textPrimary": "#FFFFFFFF",
    "textSecondary": "#CCFFFFFF",
    "border": "#40000000",
    "shadow": "#40000000"
  },
  "effects": {
    "glassBlur": 20,
    "shadowOffset": { "x": 4, "y": 4 },
    "shadowBlur": 10,
    "glowRadius": 8,
    "borderRadius": 6,
    "borderWidth": 1
  },
  "animations": {
    "hoverDuration": 150,
    "pressDuration": 50,
    "windowOpenDuration": 200
  }
}
```

---

## Phase 3: Enhanced Renderer

### 3.0 Graphics Abstraction (COMPLETE)
- [x] `QGraphics` module created with abstract `IPainter` interface
- [x] `QGPen` class for stroke styles (color, width, dash)
- [x] `QGBrush` class for fill styles (solid, gradient)
- [x] `QCGeometry.h` - Point, Size, Rect, Margins in QCommon
- [x] `QCColor.h` - Color with blend(), lerp(), darker(), lighter()
- [x] Window implements IPainter interface
- [x] Controls paint through IPainter abstraction

### 3.1 Alpha Blending (`QWRenderer` extensions)
- [x] Full ARGB color support
- [x] Alpha blending in fillRect, blit operations
- [ ] Porter-Duff compositing modes
- [ ] Pre-multiplied alpha optimization

### 3.2 Gradient Rendering
- [x] Linear gradients (horizontal, vertical)
- [ ] Diagonal gradients
- [ ] Radial gradients
- [ ] Multi-stop gradient support

### 3.3 Blur Effects
- [ ] Box blur (fast, for real-time)
- [ ] Gaussian blur (higher quality)
- [ ] Cached blur textures for glass effect

### 3.4 Shadow Rendering
- [x] Drop shadow with configurable offset/blur (in Frame)
- [x] Inner shadow for pressed states (in Frame)
- [x] Soft shadow layers (in Frame)
- [ ] Shadow caching for performance

### 3.5 Glow Effects
- [ ] Outer glow for focused elements
- [ ] Inner glow for buttons
- [ ] Bloom effect for highlights

### 3.6 Rounded Corners
- [ ] Rounded rectangle primitives
- [ ] Anti-aliased edges

---

## Phase 4: Desktop Manager

### 4.0 Desktop Shell (COMPLETE)
- [x] `QDAccent.h/.cpp` - Accent color system (Electric Blue, Teal, Orange, Purple)
- [x] `DesktopColors` - Style-aware color palettes for all UI elements
- [x] `QDDesktop.h/.cpp` - Main desktop manager that orchestrates layout
- [x] `QDTopBar.h/.cpp` - Top header bar (logo, title, clock, system icons)
- [x] `QDSidebar.h/.cpp` - Left dock (Home, Apps, Settings, Files, Terminal, Power)
- [x] `QDTaskbar.h/.cpp` - Bottom task switcher (window buttons, tray, clock)
- [x] Background gradients per UIStyle (QWStyle/Metro/Vista)
- [x] Hit testing for all desktop zones

### 4.1 Core Desktop Manager Enhancements
- [x] Basic desktop layout management
- [x] Window registration in taskbar
- [x] Active window tracking
- [ ] Singleton pattern (global accessor)
- [ ] Theme loading and switching from JSON
- [ ] Wallpaper handling (when image loading available)
- [ ] Desktop icon grid management
- [ ] System tray / notification area (functional)

### 4.2 Desktop Layout JSON
```json
{
  "wallpaper": "/system/wallpapers/default.bmp",
  "theme": "/system/themes/vista-glass.json",
  "taskbar": {
    "position": "bottom",
    "height": 48,
    "autoHide": false
  },
  "icons": {
    "gridSize": 80,
    "spacing": 10,
    "labelPosition": "bottom"
  },
  "shortcuts": [
    {
      "name": "Terminal",
      "icon": "/system/icons/terminal.bmp",
      "command": "terminal",
      "position": { "row": 0, "col": 0 }
    }
  ]
}
```

### 4.3 Layout Switching
- [ ] Save current layout to JSON
- [ ] Load layout from JSON
- [ ] Hot-reload support
- [ ] Layout profiles (work, gaming, etc.)

---

## Phase 5: Enhanced Controls

### 5.0 Control Infrastructure (COMPLETE)
- [x] `IControl` interface with full event handling
- [x] `ControlBase` - Common base class with bounds, state, hierarchy
- [x] `Container` - Base class for controls that hold children
- [x] `Frame` - Dedicated border/shadow/gradient rendering component
- [x] `Panel` - Decorated container (Container + Frame)
- [x] Controls paint via `IPainter` abstraction

### 5.1 Base Control Enhancements
- [x] `ControlBase` - Common control base class
  - [x] Event handling (IEventReceiver)
  - [x] Bounds and state management
  - [x] Parent/window hierarchy
- [x] Themed rendering (per UIStyle)
- [ ] Animation state machine
- [ ] Layout constraints

### 5.2 Vista-Style Button (`QW::Controls::Button` upgrade)
- [x] Basic button with background/border
- [ ] Gradient background with glass effect
- [ ] Glow on hover (animated fade-in)
- [ ] Press animation (darken + slight shrink)
- [ ] Disabled state (desaturated)
- [ ] Focus ring with glow

### 5.3 Text Box (`QW::Controls::TextBox` upgrade)
- [x] Basic text input with cursor
- [x] Selection highlighting
- [ ] Glass-style background
- [ ] Glowing border on focus
- [ ] Cursor blinking animation
- [ ] Placeholder text with alpha

### 5.4 Controls Created
- [x] `QW::Controls::ScrollBar` - Vertical/horizontal scrollbar with thumb
- [x] `QW::Controls::ComboBox` - Dropdown combo box
- [x] `QW::Controls::Label` - Text label control
- [x] `QW::Controls::ListView` - Multi-column list with selection

### 5.5 Controls Still Needed
- [ ] `QW::Controls::CheckBox` - Check mark with glow animation
- [ ] `QW::Controls::RadioButton` - Radio with glow
- [ ] `QW::Controls::Slider` - Track with thumb
- [ ] `QW::Controls::ProgressBar` - Progress indicator
- [ ] `QW::Controls::TabControl` - Tabbed container
- [ ] `QW::Controls::TreeView` - Hierarchical view with expand animation
- [ ] `QW::Controls::Toolbar` - Toolbar with button groups
- [ ] `QW::Controls::Menu` - Popup menus
- [ ] `QW::Controls::Tooltip` - Fade-in tooltip
- [ ] `QW::Controls::StatusBar` - Status bar

### 5.6 UIStyle-Aware Controls (NEW)
- [x] All controls query `currentUIStyle()` for rendering
- [ ] Frame adapts to style (3D vs flat vs rounded)
- [ ] Per-style color palettes
- [ ] Per-style hover/focus effects

---

## Phase 6: Window Chrome

### 6.1 Vista-Style Window Frame
- [ ] Glass title bar with gradient
- [ ] Glowing window border on focus
- [ ] Drop shadow surrounding window
- [ ] Rounded corners (top only or all)
- [ ] Resize handles with cursor changes

### 6.2 Title Bar Buttons
- [ ] Close button (red glow on hover)
- [ ] Minimize button
- [ ] Maximize/Restore button
- [ ] Custom title bar buttons support

### 6.3 Window Animations
- [ ] Open animation (fade + scale)
- [ ] Close animation (fade out)
- [ ] Minimize animation (shrink to taskbar)
- [ ] Maximize animation (expand to screen)

---

## Phase 7: Taskbar

### 7.1 Taskbar Implementation (`QDesktop/QDTaskbar.h/.cpp`)
- [ ] Glass background with blur
- [ ] Start button with orb glow
- [ ] Window buttons with previews (stretch goal)
- [ ] Quick launch area
- [ ] System tray
- [ ] Clock display

### 7.2 Start Menu
- [ ] Glass panel
- [ ] Search box
- [ ] Pinned items
- [ ] Recent items
- [ ] All programs flyout

---

## Phase 8: Compositor Enhancements

### 8.1 Glass/Blur Compositor (`QWCompositor` upgrade)
- [ ] Per-window blur regions
- [ ] Background sampling for glass effect
- [ ] Blur cache management
- [ ] Composition order handling

### 8.2 Animation System
- [ ] Frame-based animation timer
- [ ] Easing functions (ease-in, ease-out, ease-in-out)
- [ ] Property animation (position, size, opacity, color)
- [ ] Animation chaining

---

## Implementation Order (Suggested)

### Sprint 1: Foundation
1. JSON Parser
2. Theme loading
3. Gradient rendering
4. Alpha blending improvements

### Sprint 2: Visual Effects
1. Drop shadows
2. Glow effects  
3. Rounded corners
4. Blur (basic)

### Sprint 3: Controls
1. Button upgrade with Vista style
2. TextBox upgrade
3. ScrollBar
4. CheckBox, RadioButton

### Sprint 4: Windows
1. Vista window chrome
2. Title bar effects
3. Window shadow
4. Basic animations

### Sprint 5: Desktop
1. Desktop Manager
2. Taskbar
3. Layout loading
4. Theme switching

### Sprint 6: Polish
1. Start Menu
2. Remaining controls
3. Animation refinement
4. Performance optimization

---

## Technical Notes

### Memory Considerations
- Pre-allocate blur buffers
- Cache shadow textures
- Pool animation objects
- Lazy theme loading

### Performance
- Dirty rectangle tracking for partial redraws
- Hardware acceleration hooks (future)
- LOD for blur (lower quality during animations)
- Skip effects on slow systems

### Color Format
- All colors as ARGB (0xAARRGGBB)
- Alpha: 0x00 = transparent, 0xFF = opaque
- Pre-multiplied alpha for faster blending

---

## File Structure

```
QDesktop/
├── CMakeLists.txt
├── TODO_README.md
├── include/
│   ├── QDAccent.h          # Accent colors & style palettes (DONE)
│   ├── QDDesktop.h         # Desktop manager (DONE)
│   ├── QDTopBar.h          # Top header bar (DONE)
│   ├── QDSidebar.h         # Left dock/launcher (DONE)
│   ├── QDTaskbar.h         # Bottom task switcher (DONE)
│   ├── QDTheme.h           # Theme definitions (TODO)
│   ├── QDStartMenu.h       # Start menu (TODO)
│   ├── QDDesktopIcons.h    # Icon grid (TODO)
│   ├── QDJson.h            # JSON parser (TODO)
│   └── QDAnimation.h       # Animation system (TODO)
├── src/
│   ├── QDAccent.cpp        # (DONE)
│   ├── QDDesktop.cpp       # (DONE)
│   ├── QDTopBar.cpp        # (DONE)
│   ├── QDSidebar.cpp       # (DONE)
│   ├── QDTaskbar.cpp       # (DONE)
│   └── ... (future files)
└── themes/
    └── (JSON theme files for testing)
```

---

## Dependencies

- `QCommon` - Types, geometry, colors, UIStyle
- `QGraphics` - IPainter abstraction
- `QWindowing` - Window management (Window implements IPainter)
- `QWControls` - Control widgets
- `QFileSystem` - Reading JSON files (future)
- `QEvent` - Input handling (future)

---

## Questions to Resolve

1. **Blur implementation** - True Gaussian blur is expensive. Box blur cascade? Pre-blurred wallpaper?
2. **Font rendering** - Need TrueType or bitmap fonts for proper text
3. **Image loading** - BMP loader exists? PNG support needed?
4. **Timer system** - Need millisecond-precision timer for animations
5. **Multi-monitor** - Support multiple displays eventually?
6. **UIStyle persistence** - Save style preference to config file? Registry-like system?
7. **Style transitions** - Animate between styles or instant switch?

---

## Completed Infrastructure Summary

### QCommon
- `QCTypes.h` - Core types (u8, u16, u32, i8, i16, i32, etc.)
- `QCGeometry.h` - Point, Size, Rect, Margins
- `QCColor.h` - Color with ARGB, blend, lerp, predefined colors
- `QCUIStyle.h` - UIStyle enum (Vista, Metro, QWStyle) + style helpers

### QGraphics
- `QGPainter.h` - IPainter abstract interface
- `QGPen.h` - Pen class for strokes
- `QGBrush.h` - Brush class for fills

### QWindowing
- `Window` implements IPainter
- Full rendering methods: fillRect, drawRect, drawLine, gradients, borders

### QWControls
- `IControl` / `ControlBase` - Control hierarchy
- `Container` - Child control management
- `Frame` - Visual decoration (borders, shadows, gradients)
- `Panel` - Container + Frame
- `Button`, `Label`, `TextBox`, `ListView`, `ScrollBar`, `ComboBox`

### QDesktop (NEW)
- `QDAccent` - Accent colors (Electric Blue, Teal, Orange, Purple)
- `DesktopColors` - Style-aware color palettes
- `QDDesktop` - Main desktop orchestrator
- `QDTopBar` - Top header (32px - logo, title, clock, system icons)
- `QDSidebar` - Left dock (64px - Home, Apps, Settings, Files, Terminal, Power)
- `QDTaskbar` - Bottom bar (40px - window buttons, tray, clock)

---

*Last Updated: February 10, 2026*
