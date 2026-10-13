"""T2: reachable-range highlight and per-cell movement.

    python3 Script/automation/2d-rpg/battle_move.py

Enemy turns play themselves (AI), so this drives a *player* unit: it waits for
the Archer's turn, checks the reachable cells are highlighted, walks the cursor
one cell and confirms, and asserts the unit's Transform moved. Writes a
screenshot with the reachable cells highlighted.
"""

from __future__ import annotations

import json
import socket
import subprocess
import sys
import time

import rpg_control as rpg

SCENE = "Content/Scenes/BattleRuins1.scene.json"
OUT = rpg.ROOT / "Engine" / "Saved" / "Automation" / "battle-move" / "battle_move.png"


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
    return (rpg.call("ui.query", {"entry": "BattleHUD", "widget": f"Row{i}"}).get("text") or "")


def hud_menu(i: int) -> str:
    return (rpg.call("ui.query", {"entry": "BattleHUD", "widget": f"Action{i}"}).get("text") or "")


def active_name() -> str:
    for i in range(1, 7):
        text = hud_row(i)
        if text.lstrip().startswith(">"):
            for name in ("Bat", "Slime", "Ghost", "Archer", "Mage", "Knight"):
                if name in text:
                    return name
    return ""


def entity_id(name: str) -> int:
    for e in rpg.call("entity.list"):
        if isinstance(e, dict) and e.get("name") == name:
            return int(e["id"])
    raise SystemExit(f"no entity named {name}")


def position(entity: int) -> tuple[float, float]:
    comp = rpg.call("component.get", {"id": entity, "type": "TransformComponent"})
    pos = rpg._find_field(comp, "_position")
    return (float(pos[0]), float(pos[1]))


def visible_highlights() -> int:
    count = 0
    for e in rpg.call("entity.list"):
        if not isinstance(e, dict) or not str(e.get("name", "")).startswith("ReachHighlight"):
            continue
        comp = rpg.call("component.get", {"id": int(e["id"]), "type": "Sprite2DComponent"})
        if rpg._find_field(comp, "bVisible"):
            count += 1
    return count


def menu_label() -> str:
    return next((hud_menu(i).strip().lstrip("> ").strip() for i in range(1, 4)
                 if hud_menu(i).lstrip().startswith(">")), "") or ""


def wait_for_player_turn(name: str, timeout: float = 30.0) -> None:
    def ready():
        return True if active_name() == name else None

    rpg.wait_until(ready, timeout, f"{name}'s turn")


def main() -> None:
    port = start_with_port()
    try:
        rpg.call("scene.load", {"path": SCENE})
        rpg.wait_until(lambda: scene_name() if scene_name() == "BattleRuins1" else None,
                       20.0, "BattleRuins1 active")
        rpg.wait_until(lambda: hud_row(1).strip() or None, 20.0, "HUD ready")

        wait_for_player_turn("Archer")
        archer = entity_id("UnitArcher")
        start = position(archer)
        print(f"Archer start {start}")

        def highlighted():
            n = visible_highlights()
            return n if n > 0 else None

        n = rpg.wait_until(highlighted, 15.0, "reachable cells highlighted")
        print(f"reachable cells highlighted: {n}")
        if n < 2:
            raise SystemExit(f"expected several reachable cells, got {n}")

        OUT.parent.mkdir(parents=True, exist_ok=True)
        rpc(port, "capture_screenshot", {"target": "presentation", "path": str(OUT), "warmup_frames": 4})
        print(f"screenshot with highlights -> {OUT}")

        # Walk the cursor left one cell and confirm: the unit steps there.
        import time
        rpg.call("input.inject_key", {"key": "Left", "action": "hold", "frames": 1})
        time.sleep(0.3)
        rpg.tap("Space")

        def moved():
            return position(archer) if position(archer) != start else None

        walked = rpg.wait_until(moved, 6.0, "Archer left its start cell")
        print(f"Archer walked {start} -> {walked}")
        if walked[0] >= start[0]:
            raise SystemExit(f"Archer moved the wrong way: {start} -> {walked}")
        print("battle move OK")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
