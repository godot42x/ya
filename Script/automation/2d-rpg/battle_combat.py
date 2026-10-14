"""T3: combat resolves -- move, open the action menu, attack, and the target
loses HP; a defeated unit leaves the board.

    python3 Script/automation/2d-rpg/battle_combat.py

Deterministic: the first unit to act is the Bat (spd 14, enemy). The test
drives the *player's* fastest unit instead by waiting for its turn, so the
assertion is about a player action reducing enemy HP.

Writes a screenshot to Engine/Saved/Automation/battle-combat/.
"""

from __future__ import annotations

import json
import socket
import subprocess
import sys
import time

import rpg_control as rpg

SCENE = "Content/Scenes/BattleRuins1.scene.json"
OUT = rpg.ROOT / "Engine" / "Saved" / "Automation" / "battle-combat" / "battle_combat.png"
ORIGIN = (-8.0, -6.0)


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
    s = rpg.call("scene.get_active")
    return s.get("name") if isinstance(s, dict) else None


def hud_row(i: int) -> str:
    r = rpg.call("ui.query", {"entry": "BattleHUD", "widget": f"Order{i}"})
    return (r.get("text") or "") if isinstance(r, dict) else ""


def hud_menu(i: int) -> str:
    r = rpg.call("ui.query", {"entry": "BattleHUD", "widget": f"Action{i}"})
    return (r.get("text") or "") if isinstance(r, dict) else ""


def active_is(name: str) -> bool:
    for i in range(1, 7):
        text = hud_row(i)
        if text.lstrip().startswith("\u25b6") and name in text:
            return True
    return False


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


def tap(key: str = "Space") -> None:
    rpg.call("input.inject_key", {"key": key, "action": "hold", "frames": 1})


def wait_turn(name: str, timeout: float = 20.0) -> None:
    rpg.wait_until(lambda: True if active_is(name) else None, timeout, f"{name} turn")


def end_turn_with_wait() -> None:
    """End the active unit's turn by picking Wait from the action menu.

    The unit starts in `select`; confirm-on-own-cell opens the menu, then we
    walk the highlight down to the last entry (Wait) and confirm.
    """
    tap("Space")            # select -> menu
    time.sleep(0.3)
    # Menu entries: Attack [, Skill], Wait. Walk down until 'Wait' is marked.
    for _ in range(4):
        sel = None
        for i in range(1, 4):
            text = hud_menu(i)
            if text.lstrip().startswith(">"):
                sel = text.strip().lstrip("> ").strip()
                break
        if sel == "Wait":
            break
        tap("Right")
        time.sleep(0.2)
    tap("Space")            # choose Wait -> turn ends
    time.sleep(0.3)


def main() -> None:
    port = start_with_port()
    try:
        rpg.call("scene.load", {"path": SCENE})
        rpg.wait_until(lambda: scene_name() if scene_name() == "BattleRuins1" else None,
                       20.0, "BattleRuins1 active")

        # Wait for a player unit with a ranged attack to act: the Archer (spd 12)
        # is second, right after the Bat (spd 14, enemy). We let the enemy Bat
        # take its (AI-less: wait) turn, then act with the Archer.
        rpg.wait_until(lambda: hud_row(1).strip() != "" , 20.0, "HUD ready")

        # Advance to the Archer's turn. Enemy turns play themselves (AI);
        # only player turns are skipped with Wait.
        deadline = time.time() + 40.0
        while not active_is("Archer") and time.time() < deadline:
            if active_is("Mage") or active_is("Knight"):
                end_turn_with_wait()
            else:
                time.sleep(0.25)
        if not active_is("Archer"):
            raise SystemExit("never reached the Archer's turn")

        # The Archer starts at cell (8, 4); the enemies are on row 7. Move the
        # cursor up two cells to (8, 6) -- (8, 7) holds the Bat and cannot be
        # entered, so three presses would land on a blocked cell and read as
        # "stay". From (8, 6) the Bat is one cell away and is the snapped
        # target.
        enemy = "Bat"
        hp_before = entity_hp(enemy)
        print(f"{enemy} hp before: {hp_before}")

        for _ in range(2):
            tap("Up")
            time.sleep(0.25)
        tap("Space")   # move
        time.sleep(1.2)

        def menu_open():
            for i in range(1, 4):
                if "Attack" in hud_menu(i):
                    return True
            return None

        rpg.wait_until(menu_open, 5.0, "action menu open")
        print("menu:", [hud_menu(i) for i in range(1, 4)])

        tap("Space")   # pick Attack -> target phase
        time.sleep(0.5)
        tap("Space")   # confirm the snapped target -> hit
        time.sleep(1.0)

        hp_after = entity_hp(enemy)
        print(f"{enemy} hp after: {hp_after}")
        if hp_after >= hp_before:
            raise SystemExit(f"attack did not reduce {enemy} hp ({hp_before} -> {hp_after})")

        OUT.parent.mkdir(parents=True, exist_ok=True)
        rpc(port, "capture_screenshot", {"target": "presentation", "path": str(OUT), "warmup_frames": 4})
        print(f"screenshot -> {OUT}")
        print("battle combat OK")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
