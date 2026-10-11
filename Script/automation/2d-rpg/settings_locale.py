"""Esc opens settings. Space switches the open dialogue to Chinese."""

from __future__ import annotations

import time

import rpg_control as rpg


def query(entry: str, widget: str) -> dict:
    result = rpg.call("ui.query", {"entry": entry, "widget": widget})
    if not isinstance(result, dict):
        raise SystemExit(f"ui.query {entry}.{widget} returned {result!r}")
    return result


def main() -> None:
    rpg.start()
    try:
        entity_id = rpg.reset_town()
        rpg.step(entity_id, "Left", rpg.cell_world(15, 10))
        facing_cell = rpg.cell_world(14, 10)
        rpg.step(entity_id, "Left", facing_cell)
        rpg.tap("Down")
        time.sleep(0.15)
        stayed = rpg.player_position(entity_id)
        if not rpg.near(stayed, facing_cell):
            raise SystemExit(f"facing the NPC moved the player: {stayed}")

        rpg.tap("Space")

        def greeted():
            body = query("Dialogue", "Body")
            text = body.get("text") or ""
            if body.get("visible") and text.endswith("Town!"):
                return text
            return None

        rpg.wait_until(greeted, 2.0, "english greeting")

        rpg.tap("Escape")

        def settings_english():
            title = query("Settings", "Title")
            body = query("Settings", "Body")
            if (
                title.get("visible")
                and title.get("text") == "Settings"
                and "English" in (body.get("text") or "")
            ):
                return body.get("text")
            return None

        rpg.wait_until(settings_english, 2.0, "settings in english")

        rpg.tap("Space")

        def dialogue_chinese():
            body = query("Dialogue", "Body")
            text = body.get("text") or ""
            panel = query("Settings", "Body")
            if text == "欢迎来到小镇！" and "中文" in (panel.get("text") or ""):
                return text
            return None

        switched = rpg.wait_until(dialogue_chinese, 2.0, "chinese greeting")
        print(switched)

        rpg.tap("Escape")

        def settings_closed():
            title = query("Settings", "Title")
            if not title.get("visible"):
                return True
            return None

        rpg.wait_until(settings_closed, 2.0, "settings closed")
        still = query("Dialogue", "Body").get("text") or ""
        if still != "欢迎来到小镇！":
            raise SystemExit(f"dialogue lost the chinese line: {still!r}")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
