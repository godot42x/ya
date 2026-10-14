"""G2: equipment and inventory -- equipping raises stats, unequipping restores.

    python3 Script/automation/2d-rpg/equipment.py

Loads EquipmentScene, whose EquipmentSelfTest controller runs the Items/Party
equipment contract once and logs one line per check (EquippingRaisesStats /
UnequipRestoresBase and their neighbours). The test reads those lines and fails
on any FAIL.

Then it drives the real panel: I opens Inventory, the selection sits on the
first bag row (iron_sword), and clicking Equip writes the weapon into the
weapon slot and raises the ATK shown on the stats line. Clicking the weapon
slot (or Unequip) takes it back out and the stat falls again -- the UI and the
model agree because both read Party.stats.
"""

from __future__ import annotations

import re
import subprocess
import sys
import time
from pathlib import Path

import rpg_control as rpg

SCENE = "Content/Scenes/EquipmentScene.scene.json"


def start_with_port() -> tuple[int, Path]:
    rpg.stop()
    result = subprocess.run(
        [sys.executable, str(rpg.ROOT / "Script" / "ya.py"), "control", "start",
         "--project", rpg.PROJECT, "--game", "--lifetime", "120"],
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


def scene_name() -> str:
    try:
        s = rpg.call("scene.get_active")
    except SystemExit:
        return ""
    return s.get("name") if isinstance(s, dict) else ""


def widget(name: str) -> dict:
    try:
        r = rpg.call("ui.query", {"entry": "Inventory", "widget": name})
    except SystemExit:
        return {}
    return r if isinstance(r, dict) else {}


def text_of(name: str) -> str:
    return widget(name).get("text") or ""


def atk_of(stats_line: str) -> int | None:
    m = re.search(r"ATK\s+(\d+)", stats_line)
    return int(m.group(1)) if m else None


def click(name: str) -> None:
    rpg.call("ui.click", {"entry": "Inventory", "widget": name})


def main() -> None:
    port, log = start_with_port()
    try:
        rpg.call("scene.load", {"path": SCENE})
        rpg.wait_until(lambda: True if scene_name() == "EquipmentScene" else None, 20.0,
                       "EquipmentScene active")
        # The controller runs on its first update; give the entry time to mount.
        time.sleep(1.5)

        # ── UI phase: open the panel, equip, check, unequip ─────────────────
        rpg.tap("I")

        def opened():
            return widget("BagRow1Label").get("visible") or None

        rpg.wait_until(opened, 3.0, "Inventory open")

        def bag_ready():
            rows = [text_of(f"BagRow{i}Label").strip().lstrip("> ").strip() for i in range(1, 4)]
            return rows if rows[0].startswith("Iron Sword") else None

        rows = rpg.wait_until(bag_ready, 3.0, "bag rows")
        print("bag rows:", rows)

        base_atk = atk_of(text_of("Stats"))
        click("EquipButton")

        def equipped():
            slot = text_of("SlotWeaponLabel")
            stats = text_of("Stats")
            atk = atk_of(stats)
            if slot.strip() == "Iron Sword" and atk is not None and base_atk is not None \
                    and atk == base_atk + 3:
                return (slot, stats, atk)
            return None

        slot, stats, armed_atk = rpg.wait_until(equipped, 3.0, "weapon equipped and atk up")
        print(f"equipped: slot={slot!r} stats={stats!r} ({base_atk} -> {armed_atk})")

        click("UnequipButton")

        def restored():
            atk = atk_of(text_of("Stats"))
            if text_of("SlotWeaponLabel").strip() == "-" and atk == base_atk:
                return atk
            return None

        restored_atk = rpg.wait_until(restored, 3.0, "unequipped and atk restored")
        print(f"unequipped: atk back to {restored_atk}")
        rpg.tap("I")
    finally:
        rpg.stop()

    text = log.read_text(encoding="utf-8", errors="ignore") if log and log.exists() else ""
    lines = [line.strip() for line in text.splitlines() if "EquipmentSelfTest" in line]
    print("EquipmentSelfTest lines:")
    for line in lines:
        print("  ", line.split("EquipmentSelfTest", 1)[1].strip() if "EquipmentSelfTest" in line else line)

    checks = [line for line in lines if " OK " in line or " FAIL " in line]
    if not checks:
        raise SystemExit("the equipment self-test did not run")
    failures = [line for line in checks if " FAIL " in line]
    if failures:
        raise SystemExit(f"{len(failures)} equipment check(s) failed")
    print(f"equipment OK ({len(checks)} model checks + UI equip/unequip passed)")


if __name__ == "__main__":
    main()
