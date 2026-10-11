"""Town stall, house pot, charm, and the well.

scene.load clears Persist, so the refusal happens before the house. The door
back to town is world.loadScene and keeps the coins.
"""

from __future__ import annotations

import time

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


def body_text() -> str:
    return query("Dialogue", "Body").get("text") or ""


def close_dialogue() -> None:
    rpg.tap("Space")

    def closed():
        if not query("Dialogue", "Body").get("visible"):
            return True
        return None

    rpg.wait_until(closed, 2.0, "dialogue closed")


def choose_first(page_mark: str) -> None:
    def choices_shown():
        text = body_text()
        mark = query("Dialogue", "Mark1").get("text") or ""
        if page_mark in text and mark == ">":
            return text
        return None

    rpg.wait_until(choices_shown, 3.0, "merchant choices")
    # Confirm while the line is still typing only finishes the page.
    def line_done():
        if body_text().endswith("coins."):
            return True
        return None

    rpg.wait_until(line_done, 3.0, "merchant line finished")
    rpg.tap("Space")


def refuse_at_stall() -> str:
    entity_id = rpg.reset_town()
    cell = (16, 10)
    for _ in range(6):
        cell = (cell[0] + 1, cell[1])
        rpg.step(entity_id, "Right", rpg.cell_world(*cell))
    for _ in range(2):
        cell = (cell[0], cell[1] + 1)
        rpg.step(entity_id, "Up", rpg.cell_world(*cell))
    rpg.tap("Right")
    time.sleep(0.15)
    rpg.tap("Space")
    choose_first("three coins")

    def poor():
        text = body_text()
        if text.endswith("storage pot."):
            return text
        return None

    text = rpg.wait_until(poor, 2.0, "cannot afford the charm")
    close_dialogue()
    return text


def carry_coins_home() -> None:
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
        text = body_text()
        if "brass key was inside" in text:
            return text
        return None

    rpg.wait_until(pot_open, 3.0, "pot dialogue")
    close_dialogue()

    rpg.step(entity_id, "Left", HOUSE_START)
    rpg.step(entity_id, "Down", (5.5, 3.5))
    rpg.step(entity_id, "Down", (5.5, 2.5))
    rpg.step(entity_id, "Down", (5.5, 1.5))
    rpg.tap("Down")

    def back_in_town():
        if scene_name() != "Town":
            return None
        try:
            arrived = rpg.player_id()
        except SystemExit:
            return None
        if rpg.near(rpg.player_position(arrived), rpg.cell_world(21, 12)):
            return arrived
        return None

    return rpg.wait_until(back_in_town, 15.0, "returned to town")


def buy_and_wake(entity_id: int) -> str:
    rpg.step(entity_id, "Right", rpg.cell_world(22, 12))
    rpg.tap("Right")
    time.sleep(0.15)
    rpg.tap("Space")
    choose_first("three coins")

    def sold():
        text = body_text()
        if text.endswith("the square."):
            return text
        return None

    rpg.wait_until(sold, 2.0, "charm sold")
    close_dialogue()

    rpg.step(entity_id, "Down", rpg.cell_world(22, 11))
    cell = (22, 11)
    for _ in range(4):
        cell = (cell[0] - 1, cell[1])
        rpg.step(entity_id, "Left", rpg.cell_world(*cell))
    for _ in range(3):
        cell = (cell[0], cell[1] + 1)
        rpg.step(entity_id, "Up", rpg.cell_world(*cell))
    rpg.tap("Up")
    time.sleep(0.15)
    rpg.tap("Space")

    def awake():
        text = body_text()
        title = query("QuestNotice", "Title")
        notice = query("QuestNotice", "Body")
        if (
            text.endswith("awake again.")
            and title.get("visible")
            and title.get("text") == "Quest complete"
            and notice.get("text") == "The quiet well"
        ):
            return text
        return None

    return rpg.wait_until(awake, 3.0, "well ending")


def main() -> None:
    rpg.start()
    try:
        refused = refuse_at_stall()
        entity_id = carry_coins_home()
        ending = buy_and_wake(entity_id)
        print(f"playthrough:\n  {refused}\n  {ending}")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
