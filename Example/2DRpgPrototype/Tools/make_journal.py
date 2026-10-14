#!/usr/bin/env python3
"""Emit Content/UI/Journal.yaui.json.

The journal is the quest log (G4). It is the same entry the v1 town errands
already used, rebuilt as a list + detail:

  * left  -- QuestRow1..8, one button per accepted quest, in accept order;
  * right -- Body, the selected quest's title, description, one line per
             objective (with its count) and the reward line;
  * the header keeps Title / Hint.

Body keeps the v1 text shape (title, then the description, prefixed "Done."
once the quest is complete), so the older errand automation that reads it
still passes; the objective and reward lines are appended under it.

The .yaui format is verbose and easy to get subtly wrong by hand, so it is
generated here, the same way BattleHUD.yaui.json is.

    python3 Example/2DRpgPrototype/Tools/make_journal.py
"""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

ROW_COUNT = 8


def text(name: str, value: str, size: int, color: list[float],
         wrap: bool = False) -> dict:
    fields: dict = {"_text": value, "_fontSize": size, "_color": color,
                    "__base__": {"UIElement": {"_name": name}}}
    if wrap:
        fields["_bWrap"] = True
        fields["_maxWrapWidth"] = 380.0
    return {"version": 2, "typeId": "engine.text", "fields": fields,
            "children": [], "childSlots": []}


def box_slot(size_rule: int = 0, weight: float = 1.0) -> dict:
    return {"type": "box", "sizeRule": size_rule, "weight": weight,
            "margin": {"left": 0.0, "top": 0.0, "right": 0.0, "bottom": 0.0},
            "crossAlignment": 0, "minSize": [0.0, 0.0], "maxSize": [1000000.0, 1000000.0],
            "preferredSize": [0.0, 0.0], "participatesInLayout": True,
            "reserveSpaceWhenHidden": True}


def container(name: str, direction: int, spacing: float, padding: list[float],
              children: list[dict]) -> dict:
    return {"version": 2, "typeId": "engine.container",
            "fields": {"_boxLayout": {"_direction": direction, "_spacing": spacing,
                                      "_padding": padding},
                       "__base__": {"UIElement": {"_name": name}}},
            "children": children,
            "childSlots": [box_slot() for _ in children]}


def button(name: str, label_name: str) -> dict:
    return {"version": 2, "typeId": "engine.button",
            "fields": {"__base__": {"UIElement": {"_name": name}}},
            "children": [text(label_name, "", 15, [0.92, 0.92, 0.94, 1.0])],
            "childSlots": [{"type": "content", "hAlign": 0, "vAlign": 0,
                            "padding": {"left": 10.0, "top": 5.0, "right": 10.0, "bottom": 5.0},
                            "preferredSize": [0.0, 0.0]}]}


def build() -> dict:
    header = container("Header", 0, 14.0, [0.0, 0.0], [
        text("Title", "Journal", 18, [0.95, 0.84, 0.45, 1.0]),
        text("Hint", "J", 13, [0.68, 0.7, 0.76, 0.9]),
    ])

    rows = []
    for i in range(1, ROW_COUNT + 1):
        rows.append(button(f"QuestRow{i}", f"QuestRow{i}Label"))
    quest_list = container("QuestList", 1, 4.0, [6.0, 4.0], rows)

    detail = container("Detail", 0, 0.0, [8.0, 6.0], [
        text("Body", "No quests.", 15, [0.96, 0.96, 0.94, 1.0], wrap=True),
    ])

    body = container("BodyRow", 0, 16.0, [0.0, 0.0], [quest_list, detail])

    column = container("Column", 1, 10.0, [18.0, 14.0], [header, body, text(
        "FooterHint", "Up / Down select", 12, [0.6, 0.62, 0.66, 1.0])])

    root = {
        "version": 2, "typeId": "engine.panel",
        "fields": {"__base__": {"UIElement": {"_name": "Journal", "_visibility": "Hidden"}}},
        "behaviors": [{"type": "script.lua", "data": {"script": "Content/UI/Journal.lua"}}],
        "children": [{
            "version": 2, "typeId": "engine.border",
            "fields": {"_color": [0.06, 0.07, 0.1, 0.95], "_cornerRadius": 10.0,
                       "__base__": {"UIElement": {"_name": "Box"}}},
            "children": [column],
            "childSlots": [{"type": "content", "hAlign": 0, "vAlign": 0,
                            "padding": {"left": 0.0, "top": 0.0, "right": 0.0, "bottom": 0.0},
                            "preferredSize": [0.0, 0.0]}],
        }],
        "childSlots": [{
            "type": "canvas",
            "anchorMin": [0.5, 0.5], "anchorMax": [0.5, 0.5],
            "offset": [0.0, 0.0], "minSize": [720.0, 430.0], "maxSize": [720.0, 430.0],
            "offsets": {"left": 0.0, "top": 0.0, "right": 0.0, "bottom": 0.0},
            "alignmentH": 0, "alignmentV": 0,
            "widthSizeMode": 0, "heightSizeMode": 0,
            "pivot": [0.5, 0.5],
            "preferredSize": [0.0, 0.0], "fixedSize": [720.0, 430.0],
        }],
    }
    return root


def main() -> None:
    path = ROOT / "Content/UI/Journal.yaui.json"
    path.write_text(json.dumps(build(), indent=2) + "\n", encoding="utf-8")
    print(f"wrote {path}")


if __name__ == "__main__":
    main()
