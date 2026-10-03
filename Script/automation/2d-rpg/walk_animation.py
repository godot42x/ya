"""SpriteAnimation: standing faces down, walking right cycles the right-facing row, release stands again."""

from __future__ import annotations

import time

import rpg_control as rpg

# hero_walk.png is a 3x4 grid; v0 = row / 4 (down 0, left 1, right 2, up 3).
ROW_DOWN_V0 = 0.0
ROW_RIGHT_V0 = 0.5
STAND_U0 = 1.0 / 3.0


def uv_rect(entity_id: int) -> tuple[float, float]:
    component = rpg.call("component.get", {"id": entity_id, "type": "Sprite2DComponent"})
    rect = rpg._find_field(component, "uvRect")
    if not isinstance(rect, list) or len(rect) < 4:
        raise SystemExit(f"Sprite2DComponent has no uvRect: {component}")
    return (float(rect[0]), float(rect[1]))


def close(a: float, b: float) -> bool:
    return abs(a - b) < 1e-3


def main() -> None:
    rpg.start()
    try:
        entity_id = rpg.reset_town()
        u0, v0 = uv_rect(entity_id)
        if not (close(u0, STAND_U0) and close(v0, ROW_DOWN_V0)):
            raise SystemExit(f"player should start standing down, uvRect=({u0}, {v0})")

        rpg.call("input.inject_key", {"key": "Right", "action": "hold", "frames": 90})
        columns = set()
        deadline = time.time() + 1.0
        while time.time() < deadline:
            u0, v0 = uv_rect(entity_id)
            if not close(v0, ROW_RIGHT_V0):
                raise SystemExit(f"walking right should use the right row, uvRect=({u0}, {v0})")
            columns.add(round(u0 * 3))
            time.sleep(0.03)
        if len(columns) < 2:
            raise SystemExit(f"the walk cycle never changed frame: columns={sorted(columns)}")

        time.sleep(1.5)
        u0, v0 = uv_rect(entity_id)
        if not (close(u0, STAND_U0) and close(v0, ROW_RIGHT_V0)):
            raise SystemExit(f"released player should stand facing right, uvRect=({u0}, {v0})")
        print(f"walk cycle frames seen: {sorted(columns)}; standing right afterwards")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
