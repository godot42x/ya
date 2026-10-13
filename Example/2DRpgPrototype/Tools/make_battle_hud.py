#!/usr/bin/env python3
"""Emit Content/UI/BattleHUD.yaui.json.

The .yaui format is verbose and easy to get subtly wrong by hand, so it is
generated here. The panel is the battle chrome:

  * a top-left status card: the round title, the acting unit, and the turn
    order (Row1..Row6);
  * a bottom-centred action bar: the horizontal Attack / <skill> / Wait
    buttons (Action1..Action3), a target prompt between them, and the
    Victory/Defeat banner centred over everything.

BattleHUD.lua finds these by name and never bakes the layout into code.
"""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def text(name: str, value: str, size: int, color: list[float]) -> dict:
    return {"version": 2, "typeId": "engine.text",
            "fields": {"_text": value, "_fontSize": size, "_color": color,
                       "__base__": {"UIElement": {"_name": name}}},
            "children": [], "childSlots": []}


def box_slot(weight: float = 1.0) -> dict:
    return {"type": "box", "sizeRule": 0, "weight": weight,
            "margin": {"left": 0.0, "top": 0.0, "right": 0.0, "bottom": 0.0},
            "crossAlignment": 0, "minSize": [0.0, 0.0], "maxSize": [1000000.0, 1000000.0],
            "preferredSize": [0.0, 0.0], "participatesInLayout": True,
            "reserveSpaceWhenHidden": True}


def container(name: str, direction: int, spacing: float, padding: list[float],
              children: list[dict], slot_weight: float = 1.0) -> dict:
    return {"version": 2, "typeId": "engine.container",
            "fields": {"_boxLayout": {"_direction": direction, "_spacing": spacing, "_padding": padding},
                       "__base__": {"UIElement": {"_name": name}}},
            "children": children,
            "childSlots": [box_slot(slot_weight) for _ in children]}


def border(name: str, children: list[dict], slot_weight: float = 1.0,
           color: list[float] | None = None, corner: float = 10.0) -> dict:
    return {"version": 2, "typeId": "engine.border",
            "fields": {"_color": color or [0.08, 0.1, 0.12, 0.9], "_cornerRadius": corner,
                       "__base__": {"UIElement": {"_name": name}}},
            "children": children,
            "childSlots": [{"type": "content", "hAlign": 0, "vAlign": 0,
                            "padding": {"left": 0.0, "top": 0.0, "right": 0.0, "bottom": 0.0},
                            "preferredSize": [0.0, 0.0]}],
            }


def canvas_slot(anchor: list[float], offset: list[float], pivot: list[float],
                max_size: list[float], width_mode: int = 1, height_mode: int = 1,
                fixed: list[float] | None = None) -> dict:
    """A point-anchored canvas slot. anchorY 0 = top, 1 = bottom (Dialogue's
    bottom-centre panel anchors at [0,1] with a -20 offset). `pivot` is which
    point of the panel lands on the anchor, so [0.5, 1] hangs the panel above a
    bottom anchor and [0.5, 0.5] centres it on any anchor."""
    return {
        "type": "canvas",
        "anchorMin": anchor, "anchorMax": anchor,
        "offset": offset, "minSize": [0.0, 0.0], "maxSize": max_size,
        "offsets": {"left": 0.0, "top": 0.0, "right": 0.0, "bottom": 0.0},
        "alignmentH": 0, "alignmentV": 0,
        "widthSizeMode": width_mode, "heightSizeMode": height_mode,
        "pivot": pivot,
        "preferredSize": [0.0, 0.0],
        "fixedSize": fixed or [0.0, 0.0],
    }


def build() -> dict:
    # ── status card (top-left): round, acting unit, turn order ──────────────
    status_rows = [text("Title", "Round 1", 16, [0.95, 0.84, 0.45, 1.0]),
                   text("Header", "Acting: -", 14, [0.85, 0.85, 0.9, 1.0])]
    for i in range(1, 7):
        status_rows.append(text(f"Row{i}", "", 14, [0.75, 0.9, 1.0, 1.0]))
    status_column = container("StatusColumn", 1, 3.0, [12.0, 10.0], status_rows)
    status_card = border("StatusBox", [status_column])
    # Top-left: the card's top-left corner sits just inside the screen corner.
    status_slot = canvas_slot([0.0, 0.0], [14.0, 14.0], [0.0, 0.0], [320.0, 1000000.0])

    # ── action bar (bottom-centre): Attack / Skill / Wait ───────────────────
    actions = []
    for i in range(1, 4):
        actions.append(text(f"Action{i}", "", 18, [0.9, 0.9, 0.92, 1.0]))
    actions.append(text("Hint", "", 14, [0.85, 0.85, 0.9, 1.0]))
    action_row = container("ActionRow", 0, 22.0, [18.0, 12.0], actions)
    action_bar = border("ActionBar", [action_row], color=[0.07, 0.09, 0.11, 0.92])
    # Bottom-centre: the bar's bottom edge sits just above the screen bottom.
    action_slot = canvas_slot([0.5, 1.0], [0.0, -18.0], [0.5, 1.0], [900.0, 200.0])

    # ── outcome banner (centre) ─────────────────────────────────────────────
    banner = text("Outcome", "", 28, [0.95, 0.9, 0.6, 1.0])
    outcome_column = container("OutcomeColumn", 1, 0.0, [22.0, 14.0], [banner])
    outcome_box = border("OutcomeBox", [outcome_column], color=[0.06, 0.07, 0.1, 0.94])
    outcome_slot = canvas_slot([0.5, 0.5], [0.0, 0.0], [0.5, 0.5], [500.0, 200.0])

    root = {
        "version": 2, "typeId": "engine.panel",
        "fields": {"__base__": {"UIElement": {"_name": "BattleHUD", "_visibility": "Visible"}}},
        "behaviors": [{"type": "script.lua", "data": {"script": "Content/UI/BattleHUD.lua"}}],
        "children": [status_card, action_bar, outcome_box],
        "childSlots": [status_slot, action_slot, outcome_slot],
    }
    return root


def main() -> None:
    path = ROOT / "Content/UI/BattleHUD.yaui.json"
    path.write_text(json.dumps(build(), indent=2) + "\n", encoding="utf-8")
    print(f"wrote {path}")


if __name__ == "__main__":
    main()
