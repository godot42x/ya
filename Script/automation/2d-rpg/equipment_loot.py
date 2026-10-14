"""G2: battle loot -- a won battle drops one item into the bag.

    python3 Script/automation/2d-rpg/equipment_loot.py

Drives the real fight to a victory with the same Camp -> ruins -> battle driver
the keyboard main-flow run uses (kbd_main_flow), then reads the battle log for
the drop line Battle.lua writes on a victory ("Battle: loot <id> x1"). It
checks the id is one of the loot table's equippable entries, so the drop feeds
the equipment screen rather than a dead-end item.

The model contract (roll pins a row, award adds to the bag) is covered by
equipment.py's EquipmentSelfTest; this pins the wiring: a real victory actually
rolls and deposits.
"""

from __future__ import annotations

import subprocess
import sys
import time
from pathlib import Path

import rpg_control as rpg
import kbd_main_flow as kbd

LOOT_TABLE = {"iron_sword", "leather_armor", "speed_ring"}


def start_with_port() -> tuple[int, Path]:
    rpg.stop()
    result = subprocess.run(
        [sys.executable, str(rpg.ROOT / "Script" / "ya.py"), "control", "start",
         "--project", rpg.PROJECT, "--game", "--lifetime", "600"],
        cwd=rpg.ROOT, text=True, capture_output=True,
    )
    if result.returncode != 0:
        raise SystemExit(result.stderr or result.stdout or "control start failed")
    port, log = None, None
    for line in result.stdout.splitlines():
        if "control port " in line:
            port = int(line.split("control port ", 1)[1].split()[0])
        if "log " in line and ".log" in line:
            log = Path(line.split("log ", 1)[1].strip())
    if port is None:
        raise SystemExit(f"no control port:\n{result.stdout}")
    return port, log


def main() -> None:
    port, log = start_with_port()
    kbd.PORT = port
    try:
        rpg.wait_until(lambda: kbd.scene_name() == "Camp", 20.0, "Camp active")
        # Walk down onto the RuinsGate door (two cells below the player).
        for _ in range(2):
            kbd.tap("S")
            time.sleep(0.45)
        rpg.wait_until(lambda: kbd.scene_name() == "BattleRuins1", 20.0, "battle scene loaded")
        rpg.wait_until(lambda: kbd.row(1).strip() or None, 20.0, "battle HUD ready")

        deadline = time.time() + 420.0
        while time.time() < deadline and kbd.live_enemies():
            if kbd.active() in kbd.PLAYER_UNITS:
                kbd.play_victory_turn()
            else:
                time.sleep(0.25)
        if kbd.live_enemies():
            raise SystemExit(f"battle not won; still alive: {kbd.live_enemies()}")
        print("battle won")
        time.sleep(1.0)
    finally:
        rpg.stop()

    text = log.read_text(encoding="utf-8", errors="ignore") if log and log.exists() else ""
    drops = []
    for line in text.splitlines():
        if "Battle: loot " in line:
            drops.append(line.split("Battle: loot ", 1)[1].strip())
    print(f"loot lines: {drops}")
    if not drops:
        raise SystemExit("victory but no loot dropped")
    first = drops[0].split()[0]
    if first not in LOOT_TABLE:
        raise SystemExit(f"dropped '{first}' is not an equippable loot-table id")
    if "x1" not in drops[0]:
        raise SystemExit(f"drop is not a single item: {drops[0]!r}")
    print(f"battle loot OK (dropped {first}, one of {sorted(LOOT_TABLE)})")


if __name__ == "__main__":
    main()
