"""Reproduce the user's keyboard run: W/A/S/D + Space only, and WIN the battle.

The user reports the main flow cannot be completed with wasd+space. The other
automation drives the game with arrow keys and deliberately loses (Wait), so
two things were never exercised together: the W/A/S/D key names and the
victory path (grantVictoryReward -> Party.gainExp -> carryPartyState ->
returnTimer). This script walks the gate with W/S, fights the battle to a
player victory with S + Space, and expects the game back in Camp.

    python3 Script/automation/2d-rpg/kbd_main_flow.py
"""

from __future__ import annotations

import json
import socket
import subprocess
import sys
import time

import rpg_control as rpg

OUT_DIR = rpg.ROOT / "Engine" / "Saved" / "Automation" / "kbd-main-flow"
PLAYER_UNITS = ["Archer", "Mage", "Knight"]
KNOWN_UNITS = ["Bat", "Slime", "Ghost", "Archer", "Mage", "Knight"]


def start_with_port() -> int:
    rpg.stop()
    result = subprocess.run(
        [sys.executable, str(rpg.ROOT / "Script" / "ya.py"), "control", "start",
         "--project", rpg.PROJECT, "--game", "--lifetime", "900"],
        cwd=rpg.ROOT, text=True, capture_output=True,
    )
    if result.returncode != 0:
        raise SystemExit(result.stderr or result.stdout or "control start failed")
    for line in result.stdout.splitlines():
        if "control port " in line:
            return int(line.split("control port ", 1)[1].split()[0])
    raise SystemExit(f"no control port:\n{result.stdout}")


def rpc(port: int, method: str, params: dict | None = None, timeout: float = 60.0) -> dict:
    """One automation call over the control socket. Registered commands are
    reached through `invoke` ({name, args}); that is what the ya.py bridge
    sends, so calling the method name directly answers `unknown method`."""
    payload = (json.dumps({"id": 1, "method": "invoke",
                           "params": {"name": method, "args": params or {}}}) + "\n").encode("utf-8")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect(("127.0.0.1", port))
        sock.sendall(payload)
        data = b""
        while b"\n" not in data:
            chunk = sock.recv(65536)
            if not chunk:
                break
            data += chunk
    finally:
        sock.close()
    response = json.loads(data.split(b"\n", 1)[0].decode("utf-8"))
    if not response.get("ok"):
        raise SystemExit(f"{method} failed: {response.get('error')}")
    result = response.get("result")
    # `invoke` answers {result: <handler return>}; the ya.py bridge unwraps
    # that layer, so a direct socket call has to do the same.
    if isinstance(result, dict) and set(result.keys()) == {"result"}:
        result = result["result"]
    return result if isinstance(result, dict) else {}


PORT = 0  # set by main(); every query rides the one socket connection


def scene_name() -> str:
    try:
        s = rpc(PORT, "scene.get_active")
    except Exception:
        return ""
    return s.get("name") if isinstance(s, dict) else ""


def widget(name: str) -> dict:
    try:
        r = rpc(PORT, "ui.query", {"entry": "BattleHUD", "widget": name})
    except Exception:
        return {}
    return r if isinstance(r, dict) else {}


def row(i: int) -> str:
    """Turn-order row text. The HUD keeps the roster in Order1..6 (it is the
    acting-unit panel now, not a plain list)."""
    return widget(f"Order{i}").get("text") or ""


def menu(i: int) -> str:
    return widget(f"Action{i}").get("text") or ""


def active() -> str:
    """The acting unit: the order row carrying the ▶ marker."""
    for i in range(1, 7):
        text = row(i)
        if text.lstrip().startswith("\u25b6"):
            for name in ("Bat", "Slime", "Ghost", "Archer", "Mage", "Knight"):
                if name in text:
                    return name
    return ""


def unit_alive(name: str) -> bool:
    for i in range(1, 7):
        text = row(i)
        if name in text:
            return "(down)" not in text
    return False


def menu_label() -> str:
    """Label of the highlighted action entry. The bar renders `> [1] Attack`,
    so strip the marker and the hotkey prefix."""
    for i in range(1, 5):
        text = menu(i)
        if text.lstrip().startswith(">"):
            label = text.strip().lstrip(">").strip()
            if label.startswith("["):
                label = label.split("]", 1)[1].strip() if "]" in label else label
            return label
    return ""


def tap(key: str, settle: float = 0.12) -> None:
    """Press and release `key`. Explicit down/up rather than `hold`: a hold
    releases at a poll boundary, and when that boundary lands inside the same
    logic frame the press's edge is never observed by the script, so a Space
    silently does nothing."""
    assert key in ("W", "A", "S", "D", "Space"), key
    rpc(PORT, "input.inject_key", {"key": key, "action": "down"})
    time.sleep(0.10)
    rpc(PORT, "input.inject_key", {"key": key, "action": "up"})
    time.sleep(settle)


def in_menu() -> bool:
    """True when the real action list is up (not the select-phase Move chip).
    The bar may also be empty mid-transition, which is not a menu."""
    return menu_label() in ACTION_LABELS


# Every label the action bar may show as "deal damage": the basic Attack, and
# each unit's skill (the mage offers Firebolt, the archer Pierce).
ACTION_LABELS = ("Attack", "Firebolt", "Pierce")
NON_ACTION_LABELS = ("Move", "Wait", "Back", "")


def menu_select(label: str) -> None:
    for _ in range(6):
        if menu_label() == label:
            return
        before = menu_label()
        tap("D")
        deadline = time.time() + 0.6
        while time.time() < deadline and menu_label() == before:
            time.sleep(0.05)


def live_enemies() -> list[str]:
    return [n for n in ("Bat", "Slime", "Ghost") if unit_alive(n)]


def unit_positions() -> dict[str, tuple[float, float]]:
    """World position of every combatant, keyed by its display name. The board
    is one cell per world unit, so the deltas are cell steps."""
    out: dict[str, tuple[float, float]] = {}
    try:
        entities = rpc(PORT, "entity.list")
    except Exception:
        return out
    if not isinstance(entities, list):
        return out
    for entity in entities:
        if not isinstance(entity, dict):
            continue
        name = entity.get("name") or ""
        for known in KNOWN_UNITS:
            if name.endswith(known):
                try:
                    comp = rpc(PORT, "component.get",
                               {"id": int(entity["id"]), "type": "TransformComponent"})
                except Exception:
                    continue
                pos = comp.get("_position") if isinstance(comp, dict) else None
                if isinstance(pos, list) and len(pos) >= 2:
                    out[known] = (float(pos[0]), float(pos[1]))
                break
    return out


# Cells a unit may cross in one turn. Party authors move 3 for the knight and
# archer and 2 for the mage, so stepping at most 2 is always within reach. A
# longer walk lands on a cell the unit cannot enter, and Battle.lua reads that
# confirm as "stay here" -- the unit never closes the distance.
MAX_STEPS_PER_TURN = 2


def walk_toward(target: str, positions: dict[str, tuple[float, float]]) -> int:
    """Tap the W/A/S/D keys that step the cursor toward `target`, at most
    MAX_STEPS_PER_TURN cells this turn. The cursor starts on the active unit
    every select phase, so the deltas between the two units are the presses.
    One cell is always left between walker and target: the enemy's own cell is
    blocked, and confirming on it would read as "stay here". Returns the number
    of steps tapped."""
    me = positions.get(active())
    tgt = positions.get(target)
    if not me or not tgt:
        return 0
    dx = int(round(tgt[0] - me[0]))
    dy = int(round(tgt[1] - me[1]))
    budget = max(0, abs(dx) + abs(dy) - 1)  # never step onto the target cell
    steps = min(budget, MAX_STEPS_PER_TURN)
    horizontal = "D" if dx > 0 else "A"
    vertical = "W" if dy > 0 else "S"
    keys: list[str] = []
    # Spend the budget on the longer axis first, then the remainder.
    if abs(dx) >= abs(dy):
        first = min(abs(dx), steps)
        keys += [horizontal] * first
        keys += [vertical] * min(abs(dy), steps - first)
    else:
        first = min(abs(dy), steps)
        keys += [vertical] * first
        keys += [horizontal] * min(abs(dx), steps - first)
    for key in keys:
        tap(key)
        time.sleep(0.15)
    return len(keys)


def play_victory_turn() -> None:
    """One player turn driven only by W/A/S/D and Space.

    Approach the nearest living enemy first (a capped walk, so the cursor
    lands on a cell the unit can actually enter), then let Space advance the
    phase machine: select -> menu (picks the highlighted entry, Attack when a
    target is in reach and Wait otherwise) -> target -> hit. Each Space either
    opens the menu, picks the action, or confirms the snapped target, so the
    loop ends the moment the active unit changes -- no need to guess which
    phase the bar is showing."""
    unit = active()
    positions = unit_positions()
    enemies = [n for n in live_enemies() if n in positions and unit in positions]
    if enemies:
        def dist(name: str) -> int:
            a, b = positions[unit], positions[name]
            return abs(int(round(a[0] - b[0]))) + abs(int(round(a[1] - b[1])))

        walk_toward(min(enemies, key=dist), positions)

    for _ in range(8):
        if active() != unit:
            return
        tap("Space")
        time.sleep(0.9)
    raise SystemExit(
        f"could not end {unit}'s turn; rows={[row(i) for i in range(1, 7)]} "
        f"menu={[menu(i) for i in range(1, 5)]}")



def main() -> None:
    global PORT
    PORT = start_with_port()
    try:
        rpg.wait_until(lambda: scene_name() == "Camp", 20.0, "Camp active")
        print("scene Camp")

        # Walk down onto the RuinsGate door with S (the user's key), two cells.
        for _ in range(2):
            tap("S")
            time.sleep(0.45)

        rpg.wait_until(lambda: scene_name() == "BattleRuins1", 20.0, "battle scene loaded")
        print("entered battle with S key")
        rpg.wait_until(lambda: row(1).strip() or None, 20.0, "battle HUD ready")

        deadline = time.time() + 420.0
        last_sig, same = None, 0
        while time.time() < deadline:
            if not live_enemies():
                break
            who = active()
            sig = (who, tuple(row(i) for i in range(1, 7)))
            if sig == last_sig:
                same += 1
                if same > 14:
                    raise SystemExit(f"battle stalled on {who}: rows={ [row(i) for i in range(1,7)] } menu={[menu(i) for i in range(1,4)]}")
            else:
                same, last_sig = 0, sig
            if who in PLAYER_UNITS:
                t0 = time.time()
                play_victory_turn()
                print(f"  {time.time() - t0:5.1f}s  {who} -> left {live_enemies()}", flush=True)
            else:
                time.sleep(0.25)

        outcome_widget = widget("Outcome")
        outcome = (outcome_widget.get("text") or "") if outcome_widget.get("visible") else ""
        print(f"enemies left: {live_enemies()} outcome: {outcome!r}")
        if live_enemies():
            raise SystemExit(f"battle not won; still alive: {live_enemies()}")

        rpg.wait_until(lambda: scene_name() == "Camp", 25.0, "returned to Camp after victory")
        back = rpg.player_id()
        print(f"back in Camp, player entity {back}")
        print("kbd main flow OK")
    finally:
        rpg.stop()


if __name__ == "__main__":
    main()
