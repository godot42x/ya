#!/usr/bin/env python3
"""Emit Content/UI/FadeOverlay.yaui.json.

A full-screen tinted panel used for scene transitions (fade to black). The
panel covers the whole viewport and its alpha is driven by FadeOverlay.lua,
which other scripts reach with `ui.get("FadeOverlay")`.

    python3 Example/2DRpgPrototype/Tools/make_fade_overlay.py
"""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def build() -> dict:
    shade = {
        "version": 2, "typeId": "engine.border",
        "fields": {"_color": [0.0, 0.0, 0.0, 0.0], "_cornerRadius": 0.0,
                   "__base__": {"UIElement": {"_name": "Shade"}}},
        "children": [],
        "childSlots": [],
    }
    root = {
        "version": 2, "typeId": "engine.panel",
        "fields": {"__base__": {"UIElement": {"_name": "FadeOverlay", "_visibility": "Visible"}}},
        "behaviors": [{"type": "script.lua", "data": {"script": "Content/UI/FadeOverlay.lua"}}],
        "children": [shade],
        # Fill the whole viewport; the child border stretches to it.
        "childSlots": [{
            "type": "canvas",
            "anchorMin": [0.0, 0.0], "anchorMax": [1.0, 1.0],
            "offset": [0.0, 0.0], "minSize": [0.0, 0.0], "maxSize": [1000000.0, 1000000.0],
            "offsets": {"left": 0.0, "top": 0.0, "right": 0.0, "bottom": 0.0},
            "alignmentH": 0, "alignmentV": 0,
            "widthSizeMode": 0, "heightSizeMode": 0,
            "pivot": [0.0, 0.0], "preferredSize": [0.0, 0.0], "fixedSize": [0.0, 0.0],
        }],
    }
    return root


def main() -> None:
    path = ROOT / "Content/UI/FadeOverlay.yaui.json"
    path.write_text(json.dumps(build(), indent=2) + "\n", encoding="utf-8")
    print(f"wrote {path}")


if __name__ == "__main__":
    main()
