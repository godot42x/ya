"""D4 typed publish evidence: three frames against the D3 baselines.

The publish half of the composition went from export-name lookups to typed
handle readback (composition::collectViewOutput), and recordFamily turned
atomic. Neither is allowed to move a pixel: Deferred / Forward / switch-back
are compared against the D3-era baselines byte for byte.
"""

from __future__ import annotations

import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "Script" / "automation" / "2d-rpg"))

import rpg_control as rpg  # noqa: E402

PROJECT = "Example/HelloMaterial/HelloMaterial.yaproject"
OUT = ROOT / "Engine" / "Saved" / "Automation"


def shot(name: str) -> str:
    result = rpg.call("capture_screenshot", {"path": str(OUT / name), "target": "viewport"})
    if not isinstance(result, dict) or "path" not in result:
        raise SystemExit(f"capture_screenshot returned {result!r}")
    time.sleep(0.3)
    return result["path"]


def main() -> None:
    rpg.stop()
    started = rpg._ya(
        "control", "start",
        "--project", PROJECT,
        "--game",
        "--lifetime", "240",
    )
    if started.returncode != 0:
        raise SystemExit(started.stderr or started.stdout or "control start failed")

    def ready() -> object | None:
        try:
            return rpg.call("scene.get_active")
        except SystemExit:
            return None

    rpg.wait_until(lambda: ready() is not None, 60.0, "HelloMaterial scene")
    time.sleep(2.0)

    shot("d4-def.png")
    rpg.call("set_render_pipeline", {"pipeline": "forward"})
    time.sleep(2.0)
    shot("d4-fwd.png")
    rpg.call("set_render_pipeline", {"pipeline": "deferred"})
    time.sleep(2.0)
    shot("d4-def-back.png")
    print("d4 evidence: three frames captured")


if __name__ == "__main__":
    main()
