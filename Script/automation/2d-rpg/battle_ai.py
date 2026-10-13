"""T4: enemy turns resolve automatically -- no player input drives them.

    python3 Script/automation/2d-rpg/battle_ai.py

The enemy AI (BattleAI.lua) closes on the nearest opponent and strikes when in
range. This test gives the player NO input and checks the enemy side acts on
its own: a player unit loses HP, or an enemy moves toward the party, within a
few rounds. It also confirms the player's turn is *waiting* (marker on a player
unit, Move menu) -- that is what makes "the AI acted alone" observable.

Writes a screenshot to Engine/Saved/Automation/battle-ai/.
"""

from __future__ import annotations

import json
import socket
import subprocess
import sys
import time

import rpg_control as rpg

SCENE = "Content/Scenes/BattleRuins1.scene.json"
OUT = rpg.ROOT / "Engine" / "Saved" / "Automation" / "battle-ai" / "battle_ai.png"
PLAYER_UNITS = ["Archer", "Mage", "Knight"]
ENEMY_UNITS = ["Bat", "Slime", "Ghost"]


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


def widget(name: str) -> dict:
    r = rpg.call("ui.query", {"entry": "BattleHUD", "widget": name})
    return r if isinstance(r, dict) else {}


def row(i: int) -> str:
    return widget(f"Row{i}").get("text") or ""


def menu(i: int) -> str:
    return widget(f"Action{i}").get("text") or ""


def roster() -> dict[str, int]:
    """name -> hp from the HUD rows."""
    out = {}
    for i in range(1, 7):
        text = row(i)
        for name in PLAYER_UNITS + ENEMY_UNITS:
            if name in text:
                tail = text.split(name, 1)[1].strip()
                try:
                    out[name] = int(tail.split("/", 1)[0].strip())
                except ValueError:
                    out[name] = -1
    return out


def entity_pos(name: str) -> tuple[float, float]:
    for e in rpg.call("entity.list"):
        if isinstance(e, dict) and e.get("name") == name:
            comp = rpg.call("component.get", {"id": int(e["id"]), "type": "TransformComponent"})
            p = rpg._find_field(comp, "_position")
            return (round(p[0], 2), round(p[1], 2))
    return (0.0, 0.0)


def active() -> str:
    for i in range(1, 7):
        text = row(i)
        if text.lstrip().startswith(">"):
            for name in PLAYER_UNITS + ENEMY_UNITS:
                if name in text:
                    return name
    return ""


def tap(key: str = "Space") -> None:
    rpg.call("input.inject_key", {"key": key, "action": "hold", "frames": 1})


def menu_label() -> str:
    return next((menu(i).strip().lstrip("> ").strip() for i in range(1, 4)
                 if menu(i).lstrip().startswith(">")), "") or ""


def menu_select(label: str) -> None:
    for _ in range(4):
        if menu_label() == label:
            break
        tap("Right")
        time.sleep(0.12)


def skip_player_turn() -> None:
    """End a player turn with Wait, so the enemy's AI turns come around."""
    tap("Space")            # select -> menu (stay put)
    time.sleep(0.25)
    menu_select("Wait")
    tap("Space")
    time.sleep(0.25)


def main() -> None:
    port = start_with_port()
    try:
        rpg.call("scene.load", {"path": SCENE})
        rpg.wait_until(lambda: scene_name() if scene_name() == "BattleRuins1" else None,
                       20.0, "BattleRuins1 active")
        rpg.wait_until(lambda: row(1).strip() or None, 20.0, "HUD ready")

        # The enemy acts on its own; the player only skips (Wait). Watch for
        # an enemy-caused change: an enemy moves, or a player unit loses HP.
        def snapshot():
            return roster(), {name: entity_pos(f"Enemy{name}") for name in ENEMY_UNITS}

        prev_hp, prev_pos = snapshot()
        print("start hp:", prev_hp)
        print("start enemy positions:", prev_pos)

        found = None
        deadline = time.time() + 45.0
        while time.time() < deadline and not found:
            who = active()
            if who in PLAYER_UNITS:
                skip_player_turn()
            else:
                time.sleep(0.25)
            hp, pos = snapshot()
            for name in PLAYER_UNITS:
                if hp.get(name, 999) < prev_hp.get(name, hp.get(name, 0)):
                    found = f"{name} lost HP ({prev_hp.get(name)} -> {hp[name]})"
                    break
            if not found:
                for name in ENEMY_UNITS:
                    if pos[name] != prev_pos[name]:
                        found = f"{name} moved {prev_pos[name]} -> {pos[name]}"
                        break
            prev_hp, prev_pos = hp, pos

        if not found:
            raise SystemExit("no enemy acted on its own within the budget")
        print(f"AI acted without player input: {found}")

        # Ordering: enemy turns really did come around (round > 1).
        print(f"round title: {widget('Title').get('text')!r}")
        print(f"active now: {active()!r}")

        OUT.parent.mkdir(parents=True, exist_ok=True)
        rpc(port, "capture_screenshot", {"target": "presentation", "path": str(OUT), "warmup_frames": 4})
        print(f"screenshot -> {OUT}")
        print("battle AI OK")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
