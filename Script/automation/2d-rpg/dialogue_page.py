"""R2: face the NPC, confirm, and turn the dialogue to the next page."""

from __future__ import annotations

import time

import rpg_control as rpg


def query(widget: str) -> dict:
    result = rpg.call("ui.query", {"entry": "Dialogue", "widget": widget})
    if not isinstance(result, dict):
        raise SystemExit(f"ui.query {widget} returned {result!r}")
    return result


def query_body() -> dict:
    return query("Body")


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

        def choices_shown():
            body = query_body()
            text = body.get("text") or ""
            first = query("Choice1")
            third = query("Choice3")
            first_text = first.get("text") or ""
            third_text = third.get("text") or ""
            first_rect = first.get("rect") or {}
            third_rect = third.get("rect") or {}
            body_rect = body.get("rect") or {}
            ready = (
                first.get("visible")
                and first_text == "Where is the well?"
                and query("Mark1").get("text") == ">"
                and third.get("visible")
                and third_text == "Goodbye"
                and (first_rect.get("h") or 0) >= 16
                and (third_rect.get("h") or 0) >= 16
                and (third_rect.get("y") or 0) > (first_rect.get("y") or 0)
                and (first_rect.get("x") or 0) > (body_rect.get("x") or 0) + 40
                and "Where is the well?" not in text
            )
            # The menu is up while the line is still typing, not after it.
            if "fences" in text and "not me." not in text and not ready:
                raise SystemExit("choices stayed hidden while the line was still revealing")
            if "not me." in text and ready:
                return first_text
            return None

        choices = rpg.wait_until(choices_shown, 3.0, "dialogue choices")

        def label_x(name: str) -> float:
            return float((query(name).get("rect") or {}).get("x") or 0)

        anchored = {name: label_x(name) for name in ("Choice1", "Choice2", "Choice3")}

        def wait_mark(name: str) -> None:
            def ready():
                if query(name).get("text") == ">":
                    return True
                return None

            rpg.wait_until(ready, 1.0, name)

        rpg.tap("Down")
        wait_mark("Mark2")
        rpg.tap("Down")
        wait_mark("Mark3")
        moved = {name: label_x(name) for name in anchored}
        slid = {
            name: moved[name]
            for name, x in anchored.items()
            if abs(moved[name] - x) > 1.0
        }
        if slid:
            raise SystemExit(f"choice labels slid: {anchored} -> {moved}")
        xs = [label_x(name) for name in ("Choice1", "Choice2", "Choice3")]
        if max(xs) - min(xs) > 1.0:
            raise SystemExit(f"choice labels are not one column: {xs}")
        rpg.tap("Up")
        rpg.tap("Up")
        wait_mark("Mark1")
        rpg.tap("Space")

        def well_page():
            text = query_body().get("text") or ""
            if "north of the square" in text:
                return text
            return None

        third = rpg.wait_until(well_page, 2.0, "dialogue choice")
        print(f"dialogue turned:\n  {first}\n  {second}\n  {choices}\n  {third}")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
