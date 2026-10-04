"""scene.load while the game is running restarts play, so scripts tick on the new scene."""

from __future__ import annotations

import rpg_control as rpg

HOUSE = "Content/Scenes/House.scene.json"
TOWN_LARGE = "Content/Scenes/TownLarge.scene.json"

# House tilemap sits at the origin. The player is authored on cell (5, 4);
# the cell to the right is floor, the chest is one further on.
HOUSE_START = (5.5, 4.5)
HOUSE_RIGHT = (6.5, 4.5)


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


def walk_house() -> None:
    entity_id = load_scene(HOUSE, "House")

    def at_start():
        position = rpg.player_position(entity_id)
        return position if rpg.near(position, HOUSE_START) else None

    rpg.wait_until(at_start, 5.0, f"House player at {HOUSE_START}")
    arrived = rpg.step(entity_id, "Right", HOUSE_RIGHT)
    print(f"House: walked one cell {HOUSE_START} -> {arrived}")


# TownLarge's tilemap origin is (-32, -32) on a 64x64 map. The fixture spawns
# on the centre cell (32, 32); that cell's centre is world (0.5, 0.5). The
# cell to the right is grass inside the clear disc.
TOWN_LARGE_START = (0.5, 0.5)
TOWN_LARGE_RIGHT = (1.5, 0.5)


def walk_town_large() -> None:
    entity_id = load_scene(TOWN_LARGE, "TownLarge")

    def at_start():
        position = rpg.player_position(entity_id)
        return position if rpg.near(position, TOWN_LARGE_START) else None

    rpg.wait_until(at_start, 5.0, f"TownLarge player at {TOWN_LARGE_START}")
    arrived = rpg.step(entity_id, "Right", TOWN_LARGE_RIGHT)
    print(f"TownLarge: walked one cell {TOWN_LARGE_START} -> {arrived}")


def main() -> None:
    rpg.start()
    try:
        walk_house()
        walk_town_large()
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
