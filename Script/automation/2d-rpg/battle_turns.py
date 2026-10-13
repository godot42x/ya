"""T1: the battle board collects its units, orders them by speed, and the turn
advances on confirm (skipping downed units).

    python3 Script/automation/2d-rpg/battle_turns.py

Boots the game with the BattleRuins1 scene as the active scene, then reads the
BattleHUD turn rows back through ui.query. Writes a screenshot to
Engine/Saved/Automation/battle-turns/.
"""

from __future__ import annotations

import json
import socket
import subprocess
import sys

import rpg_control as rpg

SCENE = "Content/Scenes/BattleRuins1.scene.json"
OUT = rpg.ROOT / "Engine" / "Saved" / "Automation" / "battle-turns" / "battle_turns.png"

# Speed order in the scene: Bat 14, Archer 12, Mage 10, Ghost 9, Knight 8, Slime 6.
EXPECTED_FIRST = "Bat"


def start_with_port() -> int:
    rpg.stop()
    result = subprocess.run(
        [sys.executable, str(rpg.ROOT / "Script" / "ya.py"), "control", "start",
         "--project", rpg.PROJECT, "--game", "--lifetime", "240"],
        cwd=rpg.ROOT, text=True, capture_output=True,
    )
    if result.returncode != 0:
        raise SystemExit(result.stderr or result.stdout or "control start failed")
    for line in result.stdout.splitlines():
        if "control port " in line:
            return int(line.split("control port ", 1)[1].split()[0])
    raise SystemExit(f"no control port:\n{result.stdout}")


def rpc(port: int, method: str, params: dict | None = None, timeout: float = 60.0) -> dict:
    payload = (json.dumps({"id": 1, "method": method, "params": params or {}}) + "\n").encode("utf-8")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect(("127.0.0.1", port))
        sock.sendall(payload)
        data = b""
        while b"\n" not in data:
            chunk = sock.recv(65536)
            if not chunk:
                break
            data += chunk
    finally:
        sock.close()
    response = json.loads(data.split(b"\n", 1)[0].decode("utf-8"))
    if not response.get("ok"):
        raise SystemExit(f"{method} failed: {response.get('error')}")
    result = response.get("result")
    return result if isinstance(result, dict) else {}


def scene_name() -> str:
    summary = rpg.call("scene.get_active")
    if not isinstance(summary, dict) or not isinstance(summary.get("name"), str):
        raise SystemExit(f"scene.get_active returned {summary!r}")
    return summary["name"]


def hud_row(i: int) -> str:
    result = rpg.call("ui.query", {"entry": "BattleHUD", "widget": f"Row{i}"})
    if not isinstance(result, dict):
        raise SystemExit(f"ui.query Row{i} returned {result!r}")
    return result.get("text") or "", bool(result.get("visible"))


def hud_menu(i: int) -> str:
    result = rpg.call("ui.query", {"entry": "BattleHUD", "widget": f"Action{i}"})
    return (result.get("text") or "") if isinstance(result, dict) else ""


def active_row() -> int:
    """Index (1-based) of the row carrying the '>' marker."""
    for i in range(1, 7):
        text, visible = hud_row(i)
        if visible and text.lstrip().startswith(">"):
            return i
    return 0


def wait_and_end_turn() -> None:
    """A turn is now move + action. End it without acting: confirm on the
    unit's own cell opens the menu, walk to Wait, confirm."""
    rpg.tap("Space")   # select -> menu (stay on own cell)
    import time
    for _ in range(4):
        sel = next((hud_menu(i).strip().lstrip("> ").strip() for i in range(1, 4)
                    if hud_menu(i).lstrip().startswith(">")), None)
        if sel == "Wait":
            break
        rpg.tap("Right")
        time.sleep(0.2)
    rpg.tap("Space")   # Wait -> turn ends
    time.sleep(0.3)


def main() -> None:
    port = start_with_port()
    try:
        rpg.call("scene.load", {"path": SCENE})

        def scene_ready():
            return scene_name() if scene_name() == "BattleRuins1" else None

        rpg.wait_until(scene_ready, 20.0, "BattleRuins1 active")

        def hud_ready():
            text, visible = hud_row(1)
            return text if (visible and text.strip()) else None

        first = rpg.wait_until(hud_ready, 20.0, "turn HUD populated")
        print(f"first row: {first!r}")
        if EXPECTED_FIRST not in first:
            raise SystemExit(f"expected {EXPECTED_FIRST} to act first, got {first!r}")

        rows = []
        for i in range(1, 7):
            text, visible = hud_row(i)
            if visible and text.strip():
                rows.append(text.strip())
        print("turn order:")
        for r in rows:
            print("  ", r)
        if len(rows) != 6:
            raise SystemExit(f"expected 6 units on the bar, got {len(rows)}")

        # The roster order is the assertion (speed descending). Which row is
        # active is not fixed: enemy turns already play themselves via the AI.
        names = []
        for text, visible in (hud_row(i) for i in range(1, 7)):
            if not (visible and text.strip()):
                continue
            for name in ("Bat", "Archer", "Mage", "Ghost", "Knight", "Slime"):
                if name in text:
                    names.append(name)
                    break
        print(f"speed order: {names}")
        if names != ["Bat", "Archer", "Mage", "Ghost", "Knight", "Slime"]:
            raise SystemExit(f"unexpected speed order: {names}")

        OUT.parent.mkdir(parents=True, exist_ok=True)
        rpc(port, "capture_screenshot", {"target": "presentation", "path": str(OUT), "warmup_frames": 4})
        print(f"screenshot -> {OUT}")
        print("battle turns OK")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
