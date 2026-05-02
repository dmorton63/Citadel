#!/usr/bin/env python3
import argparse
import json
from pathlib import Path
from typing import Any, Dict, Optional


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Preview key Citadel theme values from desktop/theme JSON")
    p.add_argument("path", help="Path to JSON file")
    return p.parse_args()


def extract_theme_node(root: Any) -> Optional[Dict[str, Any]]:
    if isinstance(root, dict):
        desktop = root.get("desktop")
        if isinstance(desktop, dict):
            theme = desktop.get("theme")
            if isinstance(theme, dict):
                return theme
        if isinstance(root.get("theme"), dict):
            return root.get("theme")
        if any(k in root for k in ("id", "base", "overrides", "colors", "effects", "animations")):
            return root if isinstance(root, dict) else None
    return None


def get(d: Dict[str, Any], *keys: str) -> Any:
    cur: Any = d
    for key in keys:
        if not isinstance(cur, dict):
            return None
        cur = cur.get(key)
    return cur


def print_kv(label: str, value: Any) -> None:
    if value is None:
        value = "<unset>"
    print(f"{label:24} {value}")


def main() -> int:
    args = parse_args()
    path = Path(args.path)
    try:
        root = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        print(f"error: failed to read/parse {path}: {exc}")
        return 1

    theme = extract_theme_node(root)
    if not theme:
        print("error: no theme object found")
        return 1

    print(f"file: {path}")
    print("-" * 56)

    print_kv("theme.id", theme.get("id"))
    print_kv("theme.base", theme.get("base"))
    print_kv("theme.file", theme.get("file") or theme.get("path"))

    print("\n[compact palette]")
    for key in ("accent", "accentLight", "accentDark", "text", "textSecondary", "panel", "panelBorder"):
        print_kv(key, theme.get(key))

    palette = get(theme, "overrides", "palette")
    print("\n[overrides.palette]")
    if isinstance(palette, dict):
        for key in ("accent", "accentLight", "accentDark", "text", "textSecondary", "panel", "panelBorder"):
            print_kv(key, palette.get(key))
    else:
        print("<unset>")

    metrics = get(theme, "overrides", "metrics")
    print("\n[overrides.metrics]")
    if isinstance(metrics, dict):
        for key in ("cornerRadius", "buttonCornerRadius", "borderWidth"):
            print_kv(key, metrics.get(key))
    else:
        print("<unset>")

    effects = get(theme, "overrides", "effects")
    print("\n[overrides.effects]")
    if isinstance(effects, dict):
        border = effects.get("border") if isinstance(effects.get("border"), dict) else {}
        shadow = effects.get("shadow") if isinstance(effects.get("shadow"), dict) else {}
        print_kv("border.color", border.get("color"))
        print_kv("border.width", border.get("width"))
        print_kv("border.radius", border.get("radius"))
        print_kv("shadow.color", shadow.get("color"))
        print_kv("shadow.blur", shadow.get("blur"))
    else:
        print("<unset>")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
