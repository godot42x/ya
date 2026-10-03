"""Drive the 2D RPG prototype through `ya.py control` and read the town back."""

from __future__ import annotations

import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PROJECT = "Example/2DRpgPrototype/2DRpgPrototype.yaproject"

# TilemapGround sits at (-16, -10). The player entity is the cell centre;
# Sprite2DComponent.pivot puts the feet on the cell's bottom edge, so the
# entity is not lifted above that centre.
TILEMAP_ORIGIN = (-16.0, -10.0)
FOOT_LIFT = 0.0
CELL = 1.0
START_CELL = (16, 10)


def cell_world(cell_x: int, cell_y: int) -> tuple[float, float]:
    x = TILEMAP_ORIGIN[0] + cell_x + 0.5 * CELL
    y = TILEMAP_ORIGIN[1] + cell_y + 0.5 * CELL + FOOT_LIFT
    return (x, y)


def _ya(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(ROOT / "Script" / "ya.py"), *args],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )


def start() -> None:
    # A live instance may already be stopped out of play (scene.load does that).
    # Always begin from a fresh process so the startup scene is in runtime.
    stop()
    result = _ya(
        "control", "start",
        "--project", PROJECT,
        "--game",
        "--lifetime", "240",
    )
    if result.returncode != 0:
        raise SystemExit(result.stderr or result.stdout or "control start failed")
    sys.stderr.write(result.stdout)


def stop() -> None:
    result = _ya("control", "stop", "--project", PROJECT, "--game")
    sys.stderr.write(result.stdout)
    if result.stderr:
        sys.stderr.write(result.stderr)


def call(method: str, params: dict | None = None) -> object:
    result = _ya(
        "control", "call",
        "--project", PROJECT,
        "--game",
        method,
        json.dumps(params or {}),
    )
    if result.returncode != 0:
        detail = (result.stderr or result.stdout or "").strip()
        raise SystemExit(f"{method} failed: {detail}")
    return json.loads(result.stdout)


def _find_field(node: object, name: str) -> object | None:
    if isinstance(node, dict):
        if name in node:
            return node[name]
        for value in node.values():
            found = _find_field(value, name)
            if found is not None:
                return found
    return None


def player_id() -> int:
    entities = call("entity.list")
    if not isinstance(entities, list):
        raise SystemExit(f"entity.list returned {type(entities).__name__}")
    for entity in entities:
        if isinstance(entity, dict) and entity.get("name") == "Player":
            return int(entity["id"])
    raise SystemExit("Player entity is not in the scene")


def player_position(entity_id: int) -> tuple[float, float]:
    component = call("component.get", {"id": entity_id, "type": "TransformComponent"})
    position = _find_field(component, "_position")
    if not isinstance(position, list) or len(position) < 2:
        raise SystemExit(f"TransformComponent has no _position: {component}")
    return (float(position[0]), float(position[1]))


def near(actual: tuple[float, float], expected: tuple[float, float], tol: float = 0.08) -> bool:
    return abs(actual[0] - expected[0]) <= tol and abs(actual[1] - expected[1]) <= tol


def wait_until(predicate, timeout: float, what: str):
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        last = predicate()
        if last:
            return last
        time.sleep(0.05)
    raise SystemExit(f"timed out waiting for {what}; last={last!r}")


def reset_town() -> int:
    # The project default scene is Town, and startup already entered runtime.
    # scene.load would stop runtime and leave scripts unticked, so don't reload.
    start_pos = cell_world(*START_CELL)

    def ready():
        try:
            entity_id = player_id()
        except SystemExit:
            return None
        position = player_position(entity_id)
        if near(position, start_pos):
            return entity_id
        return None

    return wait_until(ready, 15.0, f"player at {start_pos}")


def step(entity_id: int, key: str, target: tuple[float, float]) -> tuple[float, float]:
    call("input.inject_key", {"key": key, "action": "hold", "frames": 1})

    def arrived():
        position = player_position(entity_id)
        return position if near(position, target) else None

    return wait_until(arrived, 2.0, f"step {key} to {target}")


def tap(key: str) -> None:
    call("input.inject_key", {"key": key, "action": "hold", "frames": 1})
