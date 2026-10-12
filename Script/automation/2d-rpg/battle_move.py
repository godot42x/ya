"""T2: reachable-range highlight and per-cell movement.

    python3 Script/automation/2d-rpg/battle_move.py

Boots BattleRuins1, then drives the active unit with the cursor onto a
reachable cell and checks the unit's Transform moved there. Writes a
screenshot with the reachable cells highlighted.
"""

from __future__ import annotations

import json
import socket
import subprocess
import sys

import rpg_control as rpg

SCENE = "Content/Scenes/BattleRuins1.scene.json"
OUT = rpg.ROOT / "Engine" / "Saved" / "Automation" / "battle-move" / "battle_move.png"

# BattleRuins1 board is 16x12 at origin (-8, -6), cell (x, y) centre =
# (-8 + x + 0.5, -6 + y + 0.5).
ORIGIN = (-8.0, -6.0)


def cell_world(cx: int, cy: int) -> tuple[float, float]:
    return (ORIGIN[0] + cx + 0.5, ORIGIN[1] + cy + 0.5)


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
    return summary.get("name") if isinstance(summary, dict) else None


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
    """Count the ReachHighlight sprites that are currently visible."""
    count = 0
    for e in rpg.call("entity.list"):
        if not isinstance(e, dict) or not str(e.get("name", "")).startswith("ReachHighlight"):
            continue
        comp = rpg.call("component.get", {"id": int(e["id"]), "type": "Sprite2DComponent"})
        if rpg._find_field(comp, "bVisible"):
            count += 1
    return count


def main() -> None:
    port = start_with_port()
    try:
        rpg.call("scene.load", {"path": SCENE})
        rpg.wait_until(lambda: scene_name() if scene_name() == "BattleRuins1" else None,
                       20.0, "BattleRuins1 active")

        # First to act is the Bat (spd 14, enemy) on the enemy row. Read where
        # it starts and step left twice: any two open cells work, so the test
        # does not hardcode the board layout.
        bat = entity_id("EnemyBat")
        start = position(bat)
        print(f"Bat start {start}")

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

        # Drive the cursor left one cell and confirm: the unit walks. One cell
        # always works (two would cross the Slime on the enemy row).
        import time
        rpg.call("input.inject_key", {"key": "Left", "action": "hold", "frames": 1})
        time.sleep(0.3)
        rpg.tap("Space")

        walked = rpg.wait_until(
            lambda: position(bat) if position(bat) != start else None,
            6.0, "Bat left its start cell",
        )
        print(f"Bat walked {start} -> {walked}")
        if walked[0] >= start[0]:
            raise SystemExit(f"Bat moved the wrong way: {start} -> {walked}")
        print("battle move OK")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
