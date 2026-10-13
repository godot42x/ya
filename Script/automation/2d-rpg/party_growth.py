"""G1: progression -- the party growth rules, exercised in a running game.

    python3 Script/automation/2d-rpg/party_growth.py

Loads FxScene whose PartySelfTest controller runs the Party.lua contract once
and logs one line per check. The test reads those lines and fails on any FAIL.
Driving a full battle to a level-up is covered by progression.py; this pins the
growth math itself, which is what G1 is.
"""

from __future__ import annotations

import subprocess
import sys
import time
from pathlib import Path

import rpg_control as rpg

SCENE = "Content/Scenes/FxScene.scene.json"


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


def main() -> None:
    port, log = start_with_port()
    try:
        rpg.call("scene.load", {"path": SCENE})
        rpg.wait_until(lambda: True if scene_name() == "FxScene" else None, 20.0, "FxScene active")
        time.sleep(1.5)
    finally:
        rpg.stop()

    text = log.read_text(encoding="utf-8", errors="ignore") if log and log.exists() else ""
    lines = [line.strip() for line in text.splitlines() if "PartySelfTest" in line]
    print("PartySelfTest lines:")
    for line in lines:
        print("  ", line.split("PartySelfTest", 1)[1].strip() if "PartySelfTest" in line else line)

    checks = [line for line in lines if " OK " in line or " FAIL " in line]
    if not checks:
        raise SystemExit("the party self-test did not run")
    failures = [line for line in checks if " FAIL " in line]
    if failures:
        raise SystemExit(f"{len(failures)} party check(s) failed")
    print(f"party growth OK ({len(checks)} checks passed)")


if __name__ == "__main__":
    main()
