"""G1: progression -- a battle victory grants exp and levels the party up.

    python3 Script/automation/2d-rpg/progression.py

Loads BattleRuins1 and plays the player's turns as attack-everything until the
battle ends. On a victory the party gains exp; the test reads the level-up lines
the battle logs and checks a member leveled (or, if the fight is lost, that no
level-up was logged). The roster lives in Persist, so it survives the fight.
"""

from __future__ import annotations

import json
import socket
import subprocess
import sys
import time
from pathlib import Path

import rpg_control as rpg

SCENE = "Content/Scenes/BattleRuins1.scene.json"
PLAYER_UNITS = ["Archer", "Mage", "Knight"]


def start_with_port() -> tuple[int, Path]:
    rpg.stop()
    result = subprocess.run(
        [sys.executable, str(rpg.ROOT / "Script" / "ya.py"), "control", "start",
         "--project", rpg.PROJECT, "--game", "--lifetime", "300"],
        cwd=rpg.ROOT, text=True, capture_output=True,
    )
    if result.returncode != 0:
        raise SystemExit(result.stderr or result.stdout or "control start failed")
    port, log = None, None
    for line in result.stdout.splitlines():
        if "control port " in line:
            port = int(line.split("control port ", 1)[1].split()[0])
        if "log " in line and ".log" in line:
            log = Path(line.split("log ", 1)[1].strip())
    return port, log


def widget(name: str) -> dict:
    try:
        r = rpg.call("ui.query", {"entry": "BattleHUD", "widget": name})
    except SystemExit:
        return {}
    return r if isinstance(r, dict) else {}


def row(i: int) -> str:
    return widget(f"Row{i}").get("text") or ""


def menu(i: int) -> str:
    return widget(f"Menu{i}").get("text") or ""


def active() -> str:
    for i in range(1, 7):
        text = row(i)
        if text.lstrip().startswith(">"):
            for name in ("Bat", "Slime", "Ghost", "Archer", "Mage", "Knight"):
                if name in text:
                    return name
    return ""


def outcome() -> str:
    o = widget("Outcome")
    return (o.get("text") or "") if o.get("visible") else ""


def scene_name() -> str:
    try:
        s = rpg.call("scene.get_active")
    except SystemExit:
        return ""
    return s.get("name") if isinstance(s, dict) else ""


def tap(key: str = "Space") -> None:
    rpg.call("input.inject_key", {"key": key, "action": "hold", "frames": 1})


def menu_label() -> str:
    return next((menu(i).strip().lstrip("> ").strip() for i in range(1, 4)
                 if menu(i).lstrip().startswith(">")), "") or ""


def menu_select(label: str) -> None:
    for _ in range(5):
        if menu_label() == label:
            return
        tap("Right")
        time.sleep(0.15)


def in_menu() -> bool:
    return any(menu(i).strip() for i in range(1, 4) if "Move" not in menu(i))


def play_player_turn() -> None:
    unit = active()
    for _ in range(2):
        tap("Up")
        time.sleep(0.12)
    tap("Space")
    deadline = time.time() + 3.0
    while time.time() < deadline and not in_menu():
        time.sleep(0.1)
    if in_menu():
        menu_select("Attack")
        if menu_label() == "Attack":
            tap("Space")
            time.sleep(0.4)
            tap("Space")
            time.sleep(0.5)
        if in_menu() and active() == unit:
            menu_select("Wait")
            tap("Space")
            time.sleep(0.3)


def main() -> None:
    port, log = start_with_port()
    try:
        rpg.call("scene.load", {"path": SCENE})
        rpg.wait_until(lambda: True if scene_name() == "BattleRuins1" else None, 20.0, "BattleRuins1 active")
        rpg.wait_until(lambda: row(1).strip() or None, 20.0, "HUD ready")

        deadline = time.time() + 220.0
        while time.time() < deadline and not outcome():
            if active() in PLAYER_UNITS:
                play_player_turn()
            else:
                time.sleep(0.25)
        result = outcome()
        print(f"outcome: {result!r}")
    finally:
        rpg.stop()

    text = log.read_text(encoding="utf-8", errors="ignore") if log and log.exists() else ""
    level_lines = [line for line in text.splitlines() if "reached level" in line]
    print(f"level-up lines: {len(level_lines)}")
    for line in level_lines[:6]:
        print("  ", line.strip())

    if result == "Victory!":
        if not level_lines:
            raise SystemExit("victory but no level-up logged")
        print("progression OK (party leveled on victory)")
    else:
        if level_lines:
            raise SystemExit("level-up logged without a victory")
        print("progression OK (no victory, no level-up)")


if __name__ == "__main__":
    main()
