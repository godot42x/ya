"""Property overrides rename the map, the camera target, and the dialogue entry.

The fixture scene has no TilemapGround, no Player, and no Dialogue entry.
Walking, camera clamp, and the talk box only work if the script properties
took the overrides. Town / House / TownLarge are not modified.
"""

from __future__ import annotations

import time

import rpg_control as rpg

SCENE = "Content/Scenes/PropertyOverride.scene.json"
START = (5.5, 4.5)
RIGHT = (6.5, 4.5)
# 10x7 map at the origin, view larger than the map: a found WalkMap centres
# the camera. A missed override follows Hero instead, or never leaves (8, 6).
MAP_CENTER = (5.0, 3.5)


def scene_name() -> str:
    summary = rpg.call("scene.get_active")
    if not isinstance(summary, dict) or not isinstance(summary.get("name"), str):
        raise SystemExit(f"scene.get_active returned {summary!r}")
    return summary["name"]


def entity_id(name: str) -> int:
    entities = rpg.call("entity.list")
    if not isinstance(entities, list):
        raise SystemExit(f"entity.list returned {type(entities).__name__}")
    for entity in entities:
        if isinstance(entity, dict) and entity.get("name") == name:
            return int(entity["id"])
    raise SystemExit(f"{name} entity is not in the scene")


def query_body() -> dict:
    result = rpg.call("ui.query", {"entry": "TalkBox", "widget": "Body"})
    if not isinstance(result, dict):
        raise SystemExit(f"ui.query returned {result!r}")
    return result


def main() -> None:
    rpg.start()
    try:
        rpg.call("scene.load", {"path": SCENE})

        def ready():
            if scene_name() != "PropertyOverride":
                return None
            try:
                hero = entity_id("Hero")
            except SystemExit:
                return None
            position = rpg.player_position(hero)
            if rpg.near(position, START):
                return hero
            return None

        hero = rpg.wait_until(ready, 15.0, f"Hero at {START}")
        camera = entity_id("Camera")

        arrived = rpg.step(hero, "Right", RIGHT)
        print(f"mapName override: walked {START} -> {arrived}")

        def camera_centred():
            position = rpg.player_position(camera)
            return position if rpg.near(position, MAP_CENTER, tol=0.2) else None

        centre = rpg.wait_until(camera_centred, 2.0, f"camera at map centre {MAP_CENTER}")
        print(f"targetName/mapName override: camera at {centre}")

        rpg.tap("Space")

        def page_open():
            body = query_body()
            text = body.get("text") or ""
            if body.get("visible") and "Tiny Town" in text:
                return text
            return None

        page = rpg.wait_until(page_open, 2.0, "TalkBox page")
        rpg.call("input.inject_key", {"key": "Left", "action": "hold", "frames": 1})
        time.sleep(0.5)
        stayed = rpg.player_position(hero)
        if not rpg.near(stayed, RIGHT):
            raise SystemExit(f"dialogueName did not lock movement: {stayed}")
        print(f"dialogueName override: TalkBox locked the hero ({page!r})")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
