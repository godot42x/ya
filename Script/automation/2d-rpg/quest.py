"""Accept the pot errand in town, then open the house pot and see it done.

scene.load restarts play and clears Persist, so the house half does not
depend on the town half. Opening the pot marks the quest done on its own.
"""

from __future__ import annotations

import time

import rpg_control as rpg

HOUSE = "Content/Scenes/House.scene.json"
HOUSE_START = (5.5, 4.5)
HOUSE_RIGHT = (6.5, 4.5)
ACTIVE = "The storage pot\nLook inside the storage pot in the house."
DONE = "The storage pot\nDone. You looked inside. A brass key was there."


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


def accept_in_town() -> str:
    entity_id = rpg.reset_town()
    rpg.step(entity_id, "Left", rpg.cell_world(15, 10))
    facing_cell = rpg.cell_world(14, 10)
    rpg.step(entity_id, "Left", facing_cell)
    rpg.tap("Down")
    time.sleep(0.15)
    rpg.tap("Space")

    def page_open():
        text = query("Dialogue", "Body").get("text") or ""
        if "Welcome" in text:
            return text
        return None

    rpg.wait_until(page_open, 2.0, "dialogue page 1")
    rpg.tap("Space")
    time.sleep(0.15)
    rpg.tap("Space")

    def choices_shown():
        body = query("Dialogue", "Body").get("text") or ""
        choice = query("Dialogue", "Choice1").get("text") or ""
        mark = query("Dialogue", "Mark1").get("text") or ""
        if "not me." in body and choice == "Where is the well?" and mark == ">":
            return choice
        return None

    rpg.wait_until(choices_shown, 3.0, "dialogue choices")
    rpg.tap("Down")
    time.sleep(0.05)
    rpg.tap("Space")

    def errand_page():
        text = query("Dialogue", "Body").get("text") or ""
        if "Have a look inside." in text:
            return text
        return None

    rpg.wait_until(errand_page, 2.0, "errand page")

    def accepted():
        title = query("QuestNotice", "Title")
        body = query("QuestNotice", "Body")
        if (
            title.get("visible")
            and title.get("text") == "Quest accepted"
            and body.get("text") == "The storage pot"
        ):
            return title["text"]
        return None

    rpg.wait_until(accepted, 2.0, "quest accepted notice")
    rpg.tap("Space")

    def dialogue_closed():
        if not query("Dialogue", "Body").get("visible"):
            return True
        return None

    rpg.wait_until(dialogue_closed, 2.0, "dialogue closed")
    rpg.tap("J")

    def listed():
        body = query("Journal", "Body")
        text = body.get("text") or ""
        if body.get("visible") and text == ACTIVE:
            return text
        return None

    return rpg.wait_until(listed, 2.0, "active quest")


def complete_in_house() -> str:
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

    rpg.wait_until(pot_open, 3.0, "pot dialogue")

    def completed():
        title = query("QuestNotice", "Title")
        body = query("QuestNotice", "Body")
        if (
            title.get("visible")
            and title.get("text") == "Quest complete"
            and body.get("text") == "The storage pot"
        ):
            return title["text"]
        return None

    rpg.wait_until(completed, 2.0, "quest complete notice")
    rpg.tap("Space")

    def dialogue_closed():
        if not query("Dialogue", "Body").get("visible"):
            return True
        return None

    rpg.wait_until(dialogue_closed, 2.0, "dialogue closed")
    rpg.tap("J")

    def listed():
        body = query("Journal", "Body")
        text = body.get("text") or ""
        if body.get("visible") and text == DONE:
            return text
        return None

    return rpg.wait_until(listed, 2.0, "done quest")


def main() -> None:
    rpg.start()
    try:
        active = accept_in_town()
        done = complete_in_house()
        print(f"quest:\n  {active}\n  {done}")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
