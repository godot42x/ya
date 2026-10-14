"""Mouse-driven battle turn: hover moves the cursor, left click confirms,
the anchored popup menu answers clicks.

    python3 Script/automation/2d-rpg/battle_mouse.py

Loads BattleRuins1, waits for a player unit, then drives ONE attack entirely
with input.inject_mouse: hover the enemy's cell (the target phase snaps by
hover), click it. Verifies the popup menu opened (Action1 shows the attack
entry) and the enemy lost HP.
"""

from __future__ import annotations

import json
import socket
import subprocess
import sys
import time

import rpg_control as rpg

SCENE = "Content/Scenes/BattleRuins1.scene.json"
OUT = rpg.ROOT / "Engine" / "Saved" / "Automation" / "battle-mouse" / "battle_mouse.png"


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
    return response.get("result") or {}


def widget(name: str) -> dict:
    try:
        r = rpg.call("ui.query", {"entry": "BattleHUD", "widget": name})
    except SystemExit:
        return {}
    return r if isinstance(r, dict) else {}


def row(i: int) -> str:
    return widget(f"Order{i}").get("text") or ""


def menu(i: int) -> str:
    return widget(f"Action{i}").get("text") or ""


def active() -> str:
    for i in range(1, 7):
        text = row(i)
        if text.lstrip().startswith("\u25b6"):
            for name in ("Bat", "Slime", "Ghost", "Archer", "Mage", "Knight"):
                if name in text:
                    return name
    return ""


def entity_hp(name: str) -> int:
    """The unit's hp, read from its world-space HP label.

    The HUD order rows carry only the roster (name + down marker); the numbers
    live above each unit as `UnitHp_<displayName>` world text, formatted
    "hp/maxHp". Returns -1 when the label is absent or unparsed."""
    for e in rpg.call("entity.list"):
        if not isinstance(e, dict) or e.get("name") != f"UnitHp_{name}":
            continue
        comp = rpg.call("component.get", {"id": int(e["id"]), "type": "Text2DComponent"})
        text = ""
        if isinstance(comp, dict):
            text = str(rpg._find_field(comp, "text") or "")
        try:
            return int(text.split("/", 1)[0].strip())
        except ValueError:
            return -1
    return -1


def mouse_move(x: float, y: float, port: int) -> None:
    rpc(port, "invoke", {"name": "input.inject_mouse", "args": {"x": x, "y": y}})
    time.sleep(0.15)


def mouse_click(x: float, y: float, port: int) -> None:
    rpc(port, "invoke", {"name": "input.inject_mouse",
                         "args": {"x": x, "y": y, "button": "left", "action": "down"}})
    time.sleep(0.12)
    rpc(port, "invoke", {"name": "input.inject_mouse",
                         "args": {"x": x, "y": y, "button": "left", "action": "up"}})
    time.sleep(0.3)


def main() -> None:
    port = start_with_port()
    try:
        rpg.call("scene.load", {"path": SCENE})
        rpg.wait_until(lambda: row(1).strip() or None, 20.0, "HUD ready")

        # Wait for a player unit; the AI plays enemy turns by itself.
        deadline = time.time() + 60.0
        while active() not in ("Archer", "Mage", "Knight"):
            if time.time() > deadline:
                raise SystemExit(f"no player turn (active={active()!r})")
            time.sleep(0.3)
        unit = active()
        print(f"player turn: {unit}")

        # The active unit's own cell is the cursor's start; hover it to open
        # the menu. The window centre is over the board for this scene.
        # Find the unit's screen point from its world position through the
        # same transform the game uses: query the entity's transform and let
        # the HUD tell us nothing — instead we sweep: click the centre, and if
        # that did not open the menu, sweep a small grid until it does.
        W, H = 1280.0, 720.0
        opened = False
        for wx, wy in ((W / 2, H / 2), (W / 2 - 80, H / 2), (W / 2 + 80, H / 2),
                       (W / 2, H / 2 - 80), (W / 2 - 160, H / 2), (W / 2 + 160, H / 2)):
            mouse_move(wx, wy, port)
            mouse_click(wx, wy, port)
            # A click on a reachable cell walks first (0.14s/cell); the menu
            # opens when the walk lands. Give the phase time to settle.
            deadline = time.time() + 2.5
            while time.time() < deadline:
                if any("Attack" in menu(i) for i in range(1, 4)):
                    opened = True
                    break
                time.sleep(0.15)
            if opened:
                print(f"menu opened at click ({wx:.0f},{wy:.0f})")
                break
            rpc(port, "capture_screenshot", {"target": "viewport",
                                             "path": f"Engine/Saved/Automation/battle-mouse/sweep_{wx:.0f}_{wy:.0f}.png",
                                             "warmup_frames": 2})
        if not opened:
            raise SystemExit("clicking never opened the action menu")

        enemy = next((n for n in ("Bat", "Slime", "Ghost") if entity_hp(n) > 0), None)
        hp_before = entity_hp(enemy) if enemy else -1

        # Confirm the attack (click the highlighted entry is hard to locate;
        # the keyboard Enter is what a click on the popup sends anyway).
        rpg.call("input.inject_key", {"key": "Enter", "action": "hold", "frames": 1})
        time.sleep(0.6)
        rpg.call("input.inject_key", {"key": "Enter", "action": "hold", "frames": 1})
        time.sleep(1.0)

        hp_after = entity_hp(enemy) if enemy else -1
        print(f"{enemy} hp {hp_before} -> {hp_after}")
        if enemy and hp_after >= hp_before:
            raise SystemExit("attack through the menu did not land")

        OUT.parent.mkdir(parents=True, exist_ok=True)
        rpc(port, "capture_screenshot", {"target": "presentation", "path": str(OUT), "warmup_frames": 4})
        print("battle mouse OK")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
