#!/usr/bin/env python3
"""Emit Content/UI/Inventory.yaui.json.

The .yaui format is verbose and easy to get subtly wrong by hand, so it is
generated here. The panel is the G2 bag + equipment screen:

  * a header (title, the active member, the hotkey hint);
  * a left bag column  (BagRow1..BagRow6 -> Bag1..Bag6);
  * a right gear column (SlotWeapon / SlotArmor / SlotAccessory, each a button
    with a Slot*Label child) and the member's effective stats line;
  * a detail strip (DetailName / DetailStats / DetailDesc) for the selection;
  * an action row (PrevMember / NextMember / EquipButton / UnequipButton).

Inventory.lua finds these by name and never bakes the layout into code.
"""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

BAG_ROWS = 6
PANEL = [760.0, 440.0]


def text(name: str, value: str, size: int, color: list[float]) -> dict:
    return {"version": 2, "typeId": "engine.text",
            "fields": {"_text": value, "_fontSize": size, "_color": color,
                       "__base__": {"UIElement": {"_name": name}}},
            "children": [], "childSlots": []}


def child_text(name: str, value: str, size: int, color: list[float]) -> dict:
    """A text that fills a content control (button) instead of a box cell."""
    return text(name, value, size, color)


def button(name: str, label: str, label_size: int,
           label_color: list[float]) -> dict:
    return {"version": 2, "typeId": "engine.button",
            "fields": {"__base__": {"UIElement": {"_name": name}}},
            "children": [child_text(f"{name}Label", label, label_size, label_color)],
            "childSlots": [{"type": "content", "hAlign": 0, "vAlign": 0,
                            "padding": {"left": 10.0, "top": 5.0, "right": 10.0, "bottom": 5.0},
                            "preferredSize": [0.0, 0.0]}]}


def box_slot(size_rule: int = 0, weight: float = 1.0,
             preferred: list[float] | None = None) -> dict:
    """A box-layout edge. size_rule 0 = Auto (size to content), 1 = Fill (take
    the leftover space, split by weight). Dialogue's Row uses a Fill Spacer to
    absorb the gap, so this is the engine's existing idiom."""
    return {"type": "box", "sizeRule": size_rule, "weight": weight,
            "margin": {"left": 0.0, "top": 0.0, "right": 0.0, "bottom": 0.0},
            "crossAlignment": 0, "minSize": [0.0, 0.0],
            "maxSize": [1000000.0, 1000000.0],
            "preferredSize": preferred or [0.0, 0.0],
            "participatesInLayout": True, "reserveSpaceWhenHidden": True}


def container(name: str, direction: int, spacing: float, padding: list[float],
              children: list[dict], rules: list[int] | None = None) -> dict:
    """`rules[i]` is the size rule for child i (default Auto), so the flexible
    row can be Fill and the fixed ones Auto in one call."""
    rules = rules or [0] * len(children)
    return {"version": 2, "typeId": "engine.container",
            "fields": {"_boxLayout": {"_direction": direction, "_spacing": spacing,
                                      "_padding": padding},
                       "__base__": {"UIElement": {"_name": name}}},
            "children": children,
            "childSlots": [box_slot(rules[i]) for i in range(len(children))]}


def border(name: str, children: list[dict], color: list[float],
           corner: float = 10.0) -> dict:
    return {"version": 2, "typeId": "engine.border",
            "fields": {"_color": color, "_cornerRadius": corner,
                       "__base__": {"UIElement": {"_name": name}}},
            "children": children,
            "childSlots": [{"type": "content", "hAlign": 0, "vAlign": 0,
                            "padding": {"left": 0.0, "top": 0.0, "right": 0.0, "bottom": 0.0},
                            "preferredSize": [0.0, 0.0]}]}


def centered_slot(size: list[float]) -> dict:
    return {
        "type": "canvas",
        "anchorMin": [0.5, 0.5], "anchorMax": [0.5, 0.5],
        "offset": [0.0, 0.0], "minSize": size, "maxSize": size,
        "offsets": {"left": 0.0, "top": 0.0, "right": 0.0, "bottom": 0.0},
        "alignmentH": 0, "alignmentV": 0,
        "widthSizeMode": 0, "heightSizeMode": 0,
        "pivot": [0.5, 0.5],
        "preferredSize": [0.0, 0.0],
        "fixedSize": size,
    }


def build() -> dict:
    header = container("Header", 0, 14.0, [0.0, 0.0], [
        text("Title", "Inventory", 18, [0.95, 0.84, 0.45, 1.0]),
        text("MemberLabel", "-", 15, [0.85, 0.88, 0.95, 1.0]),
        text("Hint", "I", 13, [0.68, 0.70, 0.76, 0.9]),
    ])

    bag_children = [text("BagTitle", "Bag", 14, [0.72, 0.78, 0.86, 1.0])]
    for i in range(1, BAG_ROWS + 1):
        bag_children.append(button(f"BagRow{i}", "", 15, [0.92, 0.92, 0.94, 1.0]))
    bag_column = container("BagColumn", 1, 4.0, [6.0, 4.0], bag_children,
                           rules=[0] + [1] * BAG_ROWS)

    gear_children = [
        text("EquipTitle", "Equipped", 14, [0.72, 0.78, 0.86, 1.0]),
        button("SlotWeapon", "-", 15, [0.92, 0.92, 0.94, 1.0]),
        button("SlotArmor", "-", 15, [0.92, 0.92, 0.94, 1.0]),
        button("SlotAccessory", "-", 15, [0.92, 0.92, 0.94, 1.0]),
        text("StatsTitle", "Stats", 14, [0.72, 0.78, 0.86, 1.0]),
        text("Stats", "-", 15, [0.90, 0.95, 0.86, 1.0]),
    ]
    gear_column = container("GearColumn", 1, 4.0, [6.0, 4.0], gear_children,
                            rules=[0, 0, 0, 0, 0, 1])

    body = container("Body", 0, 24.0, [0.0, 0.0], [bag_column, gear_column],
                     rules=[1, 1])

    detail = container("Detail", 1, 2.0, [8.0, 6.0], [
        text("DetailName", "", 16, [0.98, 0.92, 0.70, 1.0]),
        text("DetailStats", "", 14, [0.85, 0.92, 0.98, 1.0]),
        text("DetailDesc", "", 13, [0.78, 0.80, 0.84, 1.0]),
    ])

    actions = container("Actions", 0, 10.0, [0.0, 0.0], [
        button("PrevMember", "<", 16, [0.9, 0.9, 0.94, 1.0]),
        button("NextMember", ">", 16, [0.9, 0.9, 0.94, 1.0]),
        button("EquipButton", "Equip", 16, [0.80, 0.96, 0.82, 1.0]),
        button("UnequipButton", "Unequip", 16, [0.98, 0.86, 0.72, 1.0]),
    ])

    column = container("Column", 1, 10.0, [18.0, 14.0],
                       [header, body, detail, actions],
                       rules=[0, 1, 0, 0])
    box = border("Box", [column], [0.07, 0.09, 0.12, 0.96])

    return {
        "version": 2, "typeId": "engine.panel",
        "fields": {"__base__": {"UIElement": {"_name": "Inventory",
                                              "_visibility": "Hidden"}}},
        "behaviors": [{"type": "script.lua", "data": {"script": "Content/UI/Inventory.lua"}}],
        "children": [box],
        "childSlots": [centered_slot(PANEL)],
    }


def main() -> None:
    path = ROOT / "Content/UI/Inventory.yaui.json"
    path.write_text(json.dumps(build(), indent=2) + "\n", encoding="utf-8")
    print(f"wrote {path}")


if __name__ == "__main__":
    main()
