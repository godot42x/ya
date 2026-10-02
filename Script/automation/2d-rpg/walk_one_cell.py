"""R0: one injected step moves the player exactly one cell to the right."""

from __future__ import annotations

import rpg_control as rpg


def main() -> None:
    rpg.start()
    try:
        entity_id = rpg.reset_town()
        origin = rpg.cell_world(*rpg.START_CELL)
        target = rpg.cell_world(rpg.START_CELL[0] + 1, rpg.START_CELL[1])
        arrived = rpg.step(entity_id, "Right", target)
        if rpg.near(arrived, origin):
            raise SystemExit(f"player did not leave {origin}: {arrived}")
        two = rpg.cell_world(rpg.START_CELL[0] + 2, rpg.START_CELL[1])
        if rpg.near(arrived, two):
            raise SystemExit(f"player walked two cells: {arrived}")
        print(f"walked one cell: {origin} -> {arrived}")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
