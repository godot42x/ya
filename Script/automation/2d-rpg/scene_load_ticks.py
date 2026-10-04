"""scene.load while the game is running restarts play, so scripts tick on the new scene."""

from __future__ import annotations

import rpg_control as rpg

HOUSE = "Content/Scenes/House.scene.json"
TOWN_LARGE = "Content/Scenes/TownLarge.scene.json"

# House tilemap sits at the origin. The player is authored on cell (5, 4);
# the cell to the right is floor, the chest is one further on.
HOUSE_START = (5.5, 4.5)
HOUSE_RIGHT = (6.5, 4.5)

# hero_walk.png is 3x4. idle_down is row 0; idle_right is row 2 (v0 = 0.5).
ROW_DOWN_V0 = 0.0
ROW_RIGHT_V0 = 0.5


def scene_name() -> str:
    summary = rpg.call("scene.get_active")
    if not isinstance(summary, dict) or not isinstance(summary.get("name"), str):
        raise SystemExit(f"scene.get_active returned {summary!r}")
    return summary["name"]


def load_scene(path: str, name: str) -> int:
    rpg.call("scene.load", {"path": path})

    def ready():
        if scene_name() != name:
            return None
        try:
            return rpg.player_id()
        except SystemExit:
            return None

    return rpg.wait_until(ready, 15.0, f"{name} with a Player")


def uv_origin(entity_id: int) -> tuple[float, float]:
    component = rpg.call("component.get", {"id": entity_id, "type": "Sprite2DComponent"})
    rect = rpg._find_field(component, "uvRect")
    if not isinstance(rect, list) or len(rect) < 2:
        raise SystemExit(f"Sprite2DComponent has no uvRect: {component}")
    return (float(rect[0]), float(rect[1]))


def walk_house() -> None:
    entity_id = load_scene(HOUSE, "House")

    def at_start():
        position = rpg.player_position(entity_id)
        return position if rpg.near(position, HOUSE_START) else None

    rpg.wait_until(at_start, 5.0, f"House player at {HOUSE_START}")
    arrived = rpg.step(entity_id, "Right", HOUSE_RIGHT)
    print(f"House: walked one cell {HOUSE_START} -> {arrived}")


def face_town_large() -> None:
    # The authored player sits just off the 64x64 tilemap (world 32.5,32.5
    # against a map that ends at 32), so a step cannot land. Holding a
    # direction still runs Player.lua: it turns and plays idle_right.
    entity_id = load_scene(TOWN_LARGE, "TownLarge")

    def standing_down():
        _u0, v0 = uv_origin(entity_id)
        return True if abs(v0 - ROW_DOWN_V0) < 1e-3 else None

    rpg.wait_until(standing_down, 5.0, "TownLarge player standing down")
    before = rpg.player_position(entity_id)
    rpg.call("input.inject_key", {"key": "Right", "action": "hold", "frames": 30})

    def facing_right():
        _u0, v0 = uv_origin(entity_id)
        return True if abs(v0 - ROW_RIGHT_V0) < 1e-3 else None

    rpg.wait_until(facing_right, 2.0, "TownLarge player facing right")
    stayed = rpg.player_position(entity_id)
    if not rpg.near(stayed, before):
        raise SystemExit(f"off-map player moved: {before} -> {stayed}")
    print(f"TownLarge: scripts ticked, facing changed to right at {stayed}")


def main() -> None:
    rpg.start()
    try:
        walk_house()
        face_town_large()
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
