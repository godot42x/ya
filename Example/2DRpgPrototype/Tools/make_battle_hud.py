#!/usr/bin/env python3
"""Emit Content/UI/BattleHUD.yaui.json.

The .yaui format is verbose and easy to get subtly wrong by hand, so it is
generated here. The panel is a left-anchored vertical box: a round title, an
"Acting: <unit>" header, and six turn rows (Row1..Row6). BattleHUD.lua finds
these by name and never bakes the layout into code.
"""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

BASE = {"__base__": {"UIElement": {}}}


def text(name: str, value: str, size: int, color: list[float], wrap: bool = False,
         max_wrap: float | None = None) -> dict:
    fields = {"_text": value, "_fontSize": size, "_color": color,
              "__base__": {"UIElement": {"_name": name}}}
    if wrap:
        fields["_bWrap"] = True
        if max_wrap is not None:
            fields["_maxWrapWidth"] = max_wrap
    return {"version": 2, "typeId": "engine.text", "fields": fields,
            "children": [], "childSlots": []}


def box_slot() -> dict:
    return {"type": "box", "sizeRule": 0, "weight": 1.0,
            "margin": {"left": 0.0, "top": 0.0, "right": 0.0, "bottom": 0.0},
            "crossAlignment": 0, "minSize": [0.0, 0.0], "maxSize": [1000000.0, 1000000.0],
            "preferredSize": [0.0, 0.0], "participatesInLayout": True,
            "reserveSpaceWhenHidden": True}


def build() -> dict:
    rows = [text("Title", "Round 1", 16, [0.95, 0.84, 0.45, 1.0]),
            text("Header", "Acting: -", 14, [0.85, 0.85, 0.9, 1.0])]
    for i in range(1, 7):
        rows.append(text(f"Row{i}", "", 14, [0.75, 0.9, 1.0, 1.0]))

    # The action menu sits under the roster: attack / skill / wait.
    menu_tabs = []
    for i in range(1, 4):
        menu_tabs.append(text(f"Menu{i}", "", 15, [0.9, 0.9, 0.92, 1.0]))
    menu_column = {
        "version": 2, "typeId": "engine.container",
        "fields": {"_boxLayout": {"_direction": 1, "_spacing": 2.0, "_padding": [0.0, 6.0]},
                   "__base__": {"UIElement": {"_name": "Menu"}}},
        "children": menu_tabs,
        "childSlots": [box_slot() for _ in menu_tabs],
    }
    hint = text("Hint", "", 13, [0.8, 0.8, 0.85, 1.0])
    outcome = text("Outcome", "", 22, [0.6, 0.95, 0.65, 1.0])

    column = {
        "version": 2, "typeId": "engine.container",
        "fields": {"_boxLayout": {"_direction": 1, "_spacing": 4.0, "_padding": [14.0, 12.0]},
                   "__base__": {"UIElement": {"_name": "Column"}}},
        "children": rows + [menu_column, hint, outcome],
        "childSlots": [box_slot() for _ in rows] + [box_slot(), box_slot(), box_slot()],
    }
    border = {
        "version": 2, "typeId": "engine.border",
        "fields": {"_color": [0.08, 0.1, 0.12, 0.9], "_cornerRadius": 10.0,
                   "__base__": {"UIElement": {"_name": "Box"}}},
        "children": [column],
        "childSlots": [{"type": "content", "hAlign": 0, "vAlign": 0,
                        "padding": {"left": 0.0, "top": 0.0, "right": 0.0, "bottom": 0.0},
                        "preferredSize": [0.0, 0.0]}],
    }
    root = {
        "version": 2, "typeId": "engine.panel",
        "fields": {"__base__": {"UIElement": {"_name": "BattleHUD", "_visibility": "Visible"}}},
        "behaviors": [{"type": "script.lua", "data": {"script": "Content/UI/BattleHUD.lua"}}],
        "children": [border],
        "childSlots": [{
            "type": "canvas",
            "anchorMin": [0.0, 0.0], "anchorMax": [0.0, 0.0],
            "offset": [16.0, 16.0], "minSize": [0.0, 0.0], "maxSize": [300.0, 1000000.0],
            "offsets": {"left": 16.0, "top": 0.0, "right": 0.0, "bottom": 0.0},
            "alignmentH": 0, "alignmentV": 0,
            "widthSizeMode": 1, "heightSizeMode": 1,
            "pivot": [0.0, 0.0], "preferredSize": [0.0, 0.0], "fixedSize": [0.0, 0.0],
        }],
    }
    return root


def main() -> None:
    path = ROOT / "Content/UI/BattleHUD.yaui.json"
    path.write_text(json.dumps(build(), indent=2) + "\n", encoding="utf-8")
    print(f"wrote {path}")


if __name__ == "__main__":
    main()
