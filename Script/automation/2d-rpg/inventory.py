"""Opening the house pot puts a brass key in Persist. I lists it."""

from __future__ import annotations

import rpg_control as rpg

HOUSE = "Content/Scenes/House.scene.json"
HOUSE_START = (5.5, 4.5)
HOUSE_RIGHT = (6.5, 4.5)


def scene_name() -> str:
    summary = rpg.call("scene.get_active")
    if not isinstance(summary, dict) or not isinstance(summary.get("name"), str):
        raise SystemExit(f"scene.get_active returned {summary!r}")
    return summary["name"]


def query(entry: str, widget: str) -> dict:
    result = rpg.call("ui.query", {"entry": entry, "widget": widget})
    if not isinstance(result, dict):
        raise SystemExit(f"ui.query {entry}/{widget} returned {result!r}")
    return result


def main() -> None:
    rpg.start()
    try:
        rpg.call("scene.load", {"path": HOUSE})

        def ready():
            if scene_name() != "House":
                return None
            try:
                entity_id = rpg.player_id()
            except SystemExit:
                return None
            position = rpg.player_position(entity_id)
            return entity_id if rpg.near(position, HOUSE_START) else None

        entity_id = rpg.wait_until(ready, 15.0, "House player")
        rpg.step(entity_id, "Right", HOUSE_RIGHT)
        rpg.tap("Space")

        def pot_open():
            text = query("Dialogue", "Body").get("text") or ""
            if "brass key was inside" in text:
                return text
            return None

        opened = rpg.wait_until(pot_open, 3.0, "pot dialogue")
        rpg.tap("Space")

        def dialogue_closed():
            body = query("Dialogue", "Body")
            if not body.get("visible"):
                return True
            return None

        rpg.wait_until(dialogue_closed, 2.0, "dialogue closed")
        rpg.tap("I")

        def listed():
            body = query("Inventory", "Body")
            text = body.get("text") or ""
            if body.get("visible") and text == "Brass Key x1\nCoin x5":
                return text
            return None

        bag = rpg.wait_until(listed, 2.0, "inventory line")
        print(f"inventory:\n  {opened}\n  {bag}")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
