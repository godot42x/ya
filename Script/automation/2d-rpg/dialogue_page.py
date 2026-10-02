"""R2: face the NPC, confirm, and turn the dialogue to the next page."""

from __future__ import annotations

import time

import rpg_control as rpg


def query_body() -> dict:
    result = rpg.call("ui.query", {"entry": "Dialogue", "widget": "Body"})
    if not isinstance(result, dict):
        raise SystemExit(f"ui.query returned {result!r}")
    return result


def main() -> None:
    rpg.start()
    try:
        entity_id = rpg.reset_town()
        # NPC stands on (14, 9). Two steps left reach (14, 10), the cell above.
        rpg.step(entity_id, "Left", rpg.cell_world(15, 10))
        facing_cell = rpg.cell_world(14, 10)
        arrived = rpg.step(entity_id, "Left", facing_cell)
        # Down is blocked by the NPC, which also turns the player to face them.
        rpg.tap("Down")
        time.sleep(0.15)
        stayed = rpg.player_position(entity_id)
        if not rpg.near(stayed, facing_cell):
            raise SystemExit(f"facing the NPC moved the player: {stayed}")

        rpg.tap("Space")

        def page_open():
            body = query_body()
            text = body.get("text") or ""
            if body.get("visible") and "Welcome" in text:
                return text
            return None

        first = rpg.wait_until(page_open, 2.0, "dialogue page 1")
        # One confirm finishes a partial reveal or advances; the next one
        # lands on page 2. They have to be different frames, which two calls are.
        rpg.tap("Space")
        time.sleep(0.15)
        rpg.tap("Space")

        def page_turned():
            body = query_body()
            text = body.get("text") or ""
            if body.get("visible") and "fences" in text and text != first:
                return text
            return None

        second = rpg.wait_until(page_turned, 2.0, "dialogue page 2")
        print(f"dialogue turned:\n  {first}\n  {second}")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
