"""R1: the fence one cell south of (16, 5) stops the player."""

from __future__ import annotations

import time

import rpg_control as rpg


def main() -> None:
    rpg.start()
    try:
        entity_id = rpg.reset_town()
        cell_x, cell_y = rpg.START_CELL
        # Town decor y=4, x=14..17 is the fence (solid tiles 81-83). Up is +cell
        # y, so the fence is five steps Down: (16, 5), then one more into y=4.
        for _ in range(5):
            cell_y -= 1
            rpg.step(entity_id, "Down", rpg.cell_world(cell_x, cell_y))
        blocked = rpg.player_position(entity_id)
        rpg.tap("Down")
        # A successful step finishes in STEP_SECONDS (0.2). Stay on this cell.
        time.sleep(0.6)
        stayed = rpg.player_position(entity_id)
        if not rpg.near(stayed, blocked):
            raise SystemExit(f"fence did not block the player: {blocked} -> {stayed}")
        print(f"blocked by the fence at cell ({cell_x}, {cell_y}): {stayed}")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
