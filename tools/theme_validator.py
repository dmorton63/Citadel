#!/usr/bin/env python3
import argparse
import json
import math
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

HEX_COLOR_RE = re.compile(r"^#(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$")
MAX_THEME_MATERIALS = 16


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate Citadel theme JSON files (theme blocks and desktop theme sections)."
    )
    parser.add_argument("paths", nargs="+", help="JSON files to validate")
    parser.add_argument(
        "--contrast",
        action="store_true",
        help="Enable contrast checks for common text/background pairs",
    )
    parser.add_argument(
        "--min-contrast",
        type=float,
        default=4.5,
        help="Minimum WCAG contrast ratio to require (default: 4.5)",
    )
    return parser.parse_args()


@dataclass
class Finding:
    level: str
    path: str
    message: str


class Validator:
    def __init__(self, enable_contrast: bool, min_contrast: float) -> None:
        self.enable_contrast = enable_contrast
        self.min_contrast = min_contrast
        self.findings: List[Finding] = []

    def add(self, level: str, path: str, message: str) -> None:
        self.findings.append(Finding(level=level, path=path, message=message))

    def validate_file(self, file_path: Path) -> None:
        try:
            text = file_path.read_text(encoding="utf-8")
            root = json.loads(text)
        except Exception as exc:
            self.add("error", str(file_path), f"JSON parse failed: {exc}")
            return

        theme_value, source = self._extract_theme_node(root)
        if theme_value is None:
            self.add("warn", str(file_path), "No theme object found")
            return

        self._validate_theme_node(theme_value, f"{file_path}::{source}")

    def _extract_theme_node(self, root: Any) -> Tuple[Optional[Any], str]:
        if isinstance(root, dict):
            desktop = root.get("desktop")
            if isinstance(desktop, dict) and "theme" in desktop:
                return desktop.get("theme"), "desktop.theme"

            if isinstance(root.get("theme"), dict):
                return root.get("theme"), "root.theme"

            if any(k in root for k in ("colors", "effects", "animations", "base", "overrides", "id", "file", "path", "definition", "assets")):
                return root, "root"

        return None, "unknown"

    def _validate_theme_node(self, node: Any, path: str) -> None:
        if isinstance(node, str):
            self.add("info", path, "Theme references external path/string; structural validation skipped")
            return

        if not isinstance(node, dict):
            self.add("error", path, "Theme node must be object or string")
            return

        # Canonical selector checks
        tid = node.get("id")
        if tid is not None and not isinstance(tid, str):
            self.add("error", f"{path}.id", "Theme id must be string")

        # Validate optional direct palette form used by seasonal desktop files.
        for key in ("accent", "accentLight", "accentDark", "text", "textSecondary", "panel", "panelBorder"):
            if key in node:
                self._validate_color(node.get(key), f"{path}.{key}")

        # Validate full schema forms.
        overrides = node.get("overrides")
        if overrides is not None:
            if not isinstance(overrides, dict):
                self.add("error", f"{path}.overrides", "Must be object")
            else:
                self._validate_overrides(overrides, f"{path}.overrides")

        definition = node.get("definition")
        if definition is not None:
            if not isinstance(definition, dict):
                self.add("error", f"{path}.definition", "Must be object")
            else:
                self._validate_definition(definition, f"{path}.definition")

        explicit_assets_count = 0
        if "assets" in node:
            explicit_assets_count += 1
            self._validate_assets(node.get("assets"), f"{path}.assets", enforce_required=True)

        if isinstance(definition, dict) and "assets" in definition:
            explicit_assets_count += 1
            self._validate_assets(definition.get("assets"), f"{path}.definition.assets", enforce_required=True)

        if isinstance(tid, str) and tid.lower() == "custom" and explicit_assets_count == 0:
            self.add("error", f"{path}.assets", "custom themes must provide explicit assets")

        if self.enable_contrast:
            self._validate_contrast(node, path)

    def _validate_definition(self, definition: Dict[str, Any], path: str) -> None:
        colors = definition.get("colors")
        if colors is not None:
            if not isinstance(colors, dict):
                self.add("error", f"{path}.colors", "Must be object")
            else:
                for key in (
                    "windowBackground",
                    "titleBarGradientStart",
                    "titleBarGradientEnd",
                    "buttonNormal",
                    "buttonHover",
                    "buttonPressed",
                    "buttonGlow",
                    "textPrimary",
                    "textSecondary",
                    "border",
                    "shadow",
                    "accentPrimary",
                    "accentSecondary",
                ):
                    if key in colors:
                        self._validate_color(colors.get(key), f"{path}.colors.{key}")

        effects = definition.get("effects")
        if effects is not None and not isinstance(effects, dict):
            self.add("error", f"{path}.effects", "Must be object")

        font = definition.get("font")
        if font is not None:
            if not isinstance(font, dict):
                self.add("error", f"{path}.font", "Must be object")
            else:
                size = font.get("size")
                if size is not None:
                    self._validate_int_range(size, f"{path}.font.size", 1, 255)

    def _validate_overrides(self, overrides: Dict[str, Any], path: str) -> None:
        palette = overrides.get("palette")
        if palette is not None:
            if not isinstance(palette, dict):
                self.add("error", f"{path}.palette", "Must be object")
            else:
                for key in ("accent", "accentLight", "accentDark", "panel", "panelBorder", "text", "textSecondary"):
                    if key in palette:
                        self._validate_color(palette.get(key), f"{path}.palette.{key}")

        metrics = overrides.get("metrics")
        if metrics is not None:
            if not isinstance(metrics, dict):
                self.add("error", f"{path}.metrics", "Must be object")
            else:
                for key in ("cornerRadius", "buttonCornerRadius", "borderWidth"):
                    if key in metrics:
                        self._validate_int_range(metrics.get(key), f"{path}.metrics.{key}", 0, 128)

        effects = overrides.get("effects")
        if effects is not None:
            if not isinstance(effects, dict):
                self.add("error", f"{path}.effects", "Must be object")
            else:
                self._validate_effects(effects, f"{path}.effects")

        font = overrides.get("font")
        if font is not None:
            if not isinstance(font, dict):
                self.add("error", f"{path}.font", "Must be object")
            else:
                if "size" in font:
                    self._validate_int_range(font.get("size"), f"{path}.font.size", 1, 255)
                if "family" in font and not isinstance(font.get("family"), str):
                    self.add("error", f"{path}.font.family", "Must be string")

        materials = overrides.get("materials")
        if materials is not None:
            if not isinstance(materials, dict):
                self.add("error", f"{path}.materials", "Must be object")
            else:
                if len(materials) > MAX_THEME_MATERIALS:
                    self.add("error", f"{path}.materials", f"Too many materials ({len(materials)} > {MAX_THEME_MATERIALS})")
                for m_name, m_def in materials.items():
                    if not isinstance(m_def, dict):
                        self.add("error", f"{path}.materials.{m_name}", "Material must be object")
                        continue
                    for key in ("fillNormal", "fillHover", "fillPressed", "text", "border"):
                        if key in m_def:
                            self._validate_color(m_def.get(key), f"{path}.materials.{m_name}.{key}")
                    if "shineIntensity" in m_def:
                        self._validate_float_range(m_def.get("shineIntensity"), f"{path}.materials.{m_name}.shineIntensity", 0.0, 1.0)

    def _validate_effects(self, effects: Dict[str, Any], path: str) -> None:
        border = effects.get("border")
        if isinstance(border, dict):
            if "color" in border:
                self._validate_color(border.get("color"), f"{path}.border.color")
            if "width" in border:
                self._validate_int_range(border.get("width"), f"{path}.border.width", 0, 32)
            if "radius" in border:
                self._validate_int_range(border.get("radius"), f"{path}.border.radius", 0, 128)

        shadow = effects.get("shadow")
        if isinstance(shadow, dict):
            if "blur" in shadow:
                self._validate_int_range(shadow.get("blur"), f"{path}.shadow.blur", 0, 256)
            if "color" in shadow:
                self._validate_color(shadow.get("color"), f"{path}.shadow.color")

        transparency = effects.get("transparency")
        if isinstance(transparency, dict):
            for key in ("windowOpacity", "panelOpacity"):
                if key in transparency:
                    self._validate_int_range(transparency.get(key), f"{path}.transparency.{key}", 0, 255)

    def _validate_assets(self, assets: Any, path: str, enforce_required: bool) -> None:
        if not isinstance(assets, dict):
            self.add("error", path, "Must be object")
            return

        sections = {
            "icons": ("settings", "terminal", "folder", "start", "shutdown"),
            "backgrounds": ("desktopPrimary", "desktopSecondary", "lockScreen"),
            "textures": ("glassNoise", "panelOverlay"),
            "illustrations": ("boot", "setup", "recovery"),
        }

        for section, keys in sections.items():
            node = assets.get(section)
            if node is not None and not isinstance(node, dict):
                self.add("error", f"{path}.{section}", "Must be object")
                continue

            if not isinstance(node, dict):
                if enforce_required:
                    self.add("error", f"{path}.{section}", "Missing required section")
                continue

            for key in keys:
                value = node.get(key)
                key_path = f"{path}.{section}.{key}"
                if value is None:
                    if enforce_required:
                        self.add("error", key_path, "Missing required asset path")
                    continue
                if not isinstance(value, str) or not value:
                    self.add("error", key_path, "Asset path must be non-empty string")

    def _validate_color(self, value: Any, path: str) -> None:
        if not isinstance(value, str):
            self.add("error", path, "Color must be string (#RRGGBB or #AARRGGBB)")
            return
        if not HEX_COLOR_RE.match(value):
            self.add("error", path, f"Invalid color format: {value}")

    def _validate_int_range(self, value: Any, path: str, lo: int, hi: int) -> None:
        if not isinstance(value, int):
            self.add("error", path, f"Must be integer in range [{lo}, {hi}]")
            return
        if value < lo or value > hi:
            self.add("error", path, f"Out of range: {value} not in [{lo}, {hi}]")

    def _validate_float_range(self, value: Any, path: str, lo: float, hi: float) -> None:
        if not isinstance(value, (int, float)):
            self.add("error", path, f"Must be number in range [{lo}, {hi}]")
            return
        f = float(value)
        if math.isnan(f) or f < lo or f > hi:
            self.add("error", path, f"Out of range: {f} not in [{lo}, {hi}]")

    def _parse_hex_color(self, value: str) -> Optional[Tuple[int, int, int]]:
        if not HEX_COLOR_RE.match(value):
            return None
        raw = value[1:]
        if len(raw) == 8:
            raw = raw[2:]  # ignore alpha for contrast
        r = int(raw[0:2], 16)
        g = int(raw[2:4], 16)
        b = int(raw[4:6], 16)
        return (r, g, b)

    def _luminance(self, rgb: Tuple[int, int, int]) -> float:
        def convert(c: int) -> float:
            x = c / 255.0
            return x / 12.92 if x <= 0.03928 else ((x + 0.055) / 1.055) ** 2.4

        r, g, b = rgb
        return 0.2126 * convert(r) + 0.7152 * convert(g) + 0.0722 * convert(b)

    def _contrast_ratio(self, a: Tuple[int, int, int], b: Tuple[int, int, int]) -> float:
        l1 = self._luminance(a)
        l2 = self._luminance(b)
        lighter = max(l1, l2)
        darker = min(l1, l2)
        return (lighter + 0.05) / (darker + 0.05)

    def _validate_contrast(self, node: Dict[str, Any], path: str) -> None:
        pairs: List[Tuple[str, str, str]] = []

        # Compact palette form.
        if "text" in node and "panel" in node:
            pairs.append(("text", "panel", f"{path}.text vs {path}.panel"))

        # Overrides form.
        overrides = node.get("overrides")
        if isinstance(overrides, dict):
            palette = overrides.get("palette")
            if isinstance(palette, dict):
                if "text" in palette and "panel" in palette:
                    pairs.append(("palette.text", "palette.panel", f"{path}.overrides.palette.text vs panel"))

        for lhs, rhs, label in pairs:
            lv = self._lookup(node, lhs)
            rv = self._lookup(node, rhs)
            if not isinstance(lv, str) or not isinstance(rv, str):
                continue
            lrgb = self._parse_hex_color(lv)
            rrgb = self._parse_hex_color(rv)
            if not lrgb or not rrgb:
                continue
            ratio = self._contrast_ratio(lrgb, rrgb)
            if ratio < self.min_contrast:
                self.add("warn", label, f"Contrast ratio {ratio:.2f} below {self.min_contrast:.2f}")

    def _lookup(self, node: Dict[str, Any], dotted: str) -> Any:
        cur: Any = node
        for part in dotted.split("."):
            if not isinstance(cur, dict):
                return None
            cur = cur.get(part)
        return cur


def main() -> int:
    args = parse_args()
    validator = Validator(enable_contrast=args.contrast, min_contrast=args.min_contrast)

    for p in args.paths:
        validator.validate_file(Path(p))

    errors = [f for f in validator.findings if f.level == "error"]
    warns = [f for f in validator.findings if f.level == "warn"]
    infos = [f for f in validator.findings if f.level == "info"]

    for f in validator.findings:
        print(f"[{f.level.upper():5}] {f.path}: {f.message}")

    print(
        f"summary: files={len(args.paths)} errors={len(errors)} warnings={len(warns)} info={len(infos)}"
    )

    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
