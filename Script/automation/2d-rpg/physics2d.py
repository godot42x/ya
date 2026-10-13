"""P1: 2D physics -- a dynamic body falls onto a static floor and rests.

    python3 Script/automation/2d-rpg/physics2d.py

Loads PhysicsScene: a static floor, a dynamic crate above it, and a sensor
plate. The test waits for the crate to fall, then reads its transform back to
confirm it came to rest on the floor (it did not fall through, and it did not
stay put).
"""

from __future__ import annotations

import json
import socket
import subprocess
import sys
import time

import rpg_control as rpg

SCENE = "Content/Scenes/PhysicsScene.scene.json"


def start_with_port() -> int:
    rpg.stop()
    result = subprocess.run(
        [sys.executable, str(rpg.ROOT / "Script" / "ya.py"), "control", "start",
         "--project", rpg.PROJECT, "--game", "--lifetime", "150"],
        cwd=rpg.ROOT, text=True, capture_output=True,
    )
    if result.returncode != 0:
        raise SystemExit(result.stderr or result.stdout or "control start failed")
    for line in result.stdout.splitlines():
        if "control port " in line:
            return int(line.split("control port ", 1)[1].split()[0])
    raise SystemExit(f"no control port:\n{result.stdout}")


def scene_name() -> str:
    try:
        s = rpg.call("scene.get_active")
    except SystemExit:
        return ""
    return s.get("name") if isinstance(s, dict) else ""


def pos(name: str) -> tuple[float, float]:
    for e in rpg.call("entity.list"):
        if isinstance(e, dict) and e.get("name") == name:
            comp = rpg.call("component.get", {"id": int(e["id"]), "type": "TransformComponent"})
            p = rpg._find_field(comp, "_position")
            return (float(p[0]), float(p[1]))
    raise SystemExit(f"no entity {name}")


def main() -> None:
    start_with_port()
    try:
        rpg.call("scene.load", {"path": SCENE})
        rpg.wait_until(lambda: True if scene_name() == "PhysicsScene" else None, 20.0, "PhysicsScene active")
        # Let the body reconcile and the world step a few times.
        time.sleep(0.3)
        start = pos("Crate")
        print(f"Crate start: {start}")

        # It falls: y decreases (gravity is -y).
        def fell():
            y = pos("Crate")[1]
            return y if y < start[1] - 0.5 else None

        fell_to = rpg.wait_until(fell, 5.0, "crate to fall")
        print(f"Crate fell to y={fell_to:.2f}")

        # It comes to rest (velocity ~0) instead of falling through the floor.
        time.sleep(1.5)
        rest = pos("Crate")
        print(f"Crate rest: {rest}")
        if rest[1] < -6.0:
            raise SystemExit(f"crate fell through the floor (y={rest[1]:.2f})")
        if abs(rest[1] - pos("Crate")[1]) > 0.05:
            raise SystemExit("crate did not settle")
        print("physics2d OK")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
