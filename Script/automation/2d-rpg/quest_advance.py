"""G4: the quest loop -- accept at the Elder, clear the ruins, see it complete.

    python3 Script/automation/2d-rpg/quest_advance.py

Boots the Camp scene (the project default), walks to the Elder and accepts the
tutorial quest from the talk menu, checks the journal shows it active with both
objectives pending, walks onto the RuinsGate into BattleRuins1, wins the battle
with W/A/S/D + Space (kbd_main_flow's driver), and then checks:

  * the journal lists the quest as complete (state line reads "Done." and both
    objectives read [x]);
  * the quest log lines prove the reward was granted exactly once --
    "Quests: completed 'tutorial_ruins'" appears once and the reward line once;
  * the coin reward landed in the bag (the journal's reward line, and the
    "loot"/"Quests: reward" log lines).

The battle is fought with the same W/A/S/D + Space driver as kbd_main_flow, so
this also covers "a battle outcome advances a quest" on the real win path.
"""

from __future__ import annotations

import json
import socket
import subprocess
import sys
import time
from pathlib import Path

import rpg_control as rpg
import shot

BATTLE_DELAY = 2.5  # Battle.lua returnDelay; the banner holds, then Camp returns

OUT_DIR = rpg.ROOT / "Engine" / "Saved" / "Automation" / "quest-advance"
PLAYER_UNITS = ["Archer", "Mage", "Knight"]
KNOWN_UNITS = ["Bat", "Slime", "Ghost", "Archer", "Mage", "Knight"]

PORT = 0
LOG: Path | None = None


def start_with_port() -> tuple[int, Path]:
    rpg.stop()
    result = subprocess.run(
        [sys.executable, str(rpg.ROOT / "Script" / "ya.py"), "control", "start",
         "--project", rpg.PROJECT, "--game", "--lifetime", "900"],
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


def rpc(method: str, params: dict | None = None, timeout: float = 60.0) -> dict:
    payload = (json.dumps({"id": 1, "method": "invoke",
                           "params": {"name": method, "args": params or {}}}) + "\n").encode("utf-8")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect(("127.0.0.1", PORT))
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
    if isinstance(result, dict) and set(result.keys()) == {"result"}:
        result = result["result"]
    return result if isinstance(result, dict) else {}


def screenshot(path: Path) -> None:
    """Capture a non-black frame (shot.py retries the compose-timing black frame
    and rewrites the RGBA capture as viewable RGB)."""
    shot.capture(PORT, path)


def scene_name() -> str:
    try:
        s = rpc("scene.get_active")
    except Exception:
        return ""
    return s.get("name") if isinstance(s, dict) else ""


def widget(entry: str, name: str) -> dict:
    try:
        r = rpc("ui.query", {"entry": entry, "widget": name})
    except Exception:
        return {}
    return r if isinstance(r, dict) else {}


def tap(key: str, settle: float = 0.12) -> None:
    rpc("input.inject_key", {"key": key, "action": "down"})
    time.sleep(0.10)
    rpc("input.inject_key", {"key": key, "action": "up"})
    time.sleep(settle)


def journal_open() -> None:
    if not (widget("Journal", "Journal").get("visible")):
        tap("J")


def journal_body() -> str:
    return widget("Journal", "Body").get("text") or ""


def journal_rows() -> list[str]:
    out = []
    for i in range(1, 9):
        w = widget("Journal", f"QuestRow{i}Label")
        if w.get("visible") and (w.get("text") or "").strip():
            out.append((w.get("text") or "").strip())
    return out


# ── battle driver (kbd_main_flow's) ────────────────────────────────────────

def menu(i: int) -> str:
    return widget("BattleHUD", f"Action{i}").get("text") or ""


def row(i: int) -> str:
    return widget("BattleHUD", f"Order{i}").get("text") or ""


def active() -> str:
    for i in range(1, 7):
        text = row(i)
        if text.lstrip().startswith("\u25b6"):
            for name in KNOWN_UNITS:
                if name in text:
                    return name
    return ""


def unit_alive(name: str) -> bool:
    for i in range(1, 7):
        text = row(i)
        if name in text:
            return "(down)" not in text
    return False


def live_enemies() -> list[str]:
    return [n for n in ("Bat", "Slime", "Ghost") if unit_alive(n)]


def unit_positions() -> dict[str, tuple[float, float]]:
    out: dict[str, tuple[float, float]] = {}
    try:
        entities = rpc("entity.list")
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
                    comp = rpc("component.get", {"id": int(entity["id"]), "type": "TransformComponent"})
                except Exception:
                    continue
                pos = comp.get("_position") if isinstance(comp, dict) else None
                if isinstance(pos, list) and len(pos) >= 2:
                    out[known] = (float(pos[0]), float(pos[1]))
                break
    return out


MAX_STEPS_PER_TURN = 2


def walk_toward(target: str, positions: dict[str, tuple[float, float]]) -> None:
    me = positions.get(active())
    tgt = positions.get(target)
    if not me or not tgt:
        return
    dx = int(round(tgt[0] - me[0]))
    dy = int(round(tgt[1] - me[1]))
    budget = max(0, abs(dx) + abs(dy) - 1)
    steps = min(budget, MAX_STEPS_PER_TURN)
    horizontal = "D" if dx > 0 else "A"
    vertical = "W" if dy > 0 else "S"
    keys: list[str] = []
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


def play_victory_turn() -> None:
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


def win_battle() -> str:
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
                raise SystemExit(f"battle stalled on {who}: rows={[row(i) for i in range(1, 7)]}")
        else:
            same, last_sig = 0, sig
        if who in PLAYER_UNITS:
            play_victory_turn()
        else:
            time.sleep(0.25)
    if live_enemies():
        raise SystemExit(f"battle not won; still alive: {live_enemies()}")
    o = widget("BattleHUD", "Outcome")
    return (o.get("text") or "") if o.get("visible") else ""


# ── camp walk ──────────────────────────────────────────────────────────────

def accept_from_elder() -> str:
    """Walk to the Elder and accept the quest from the road talk menu.

    Layout (Camp.scene.json): the player starts on cell (14,4); the Elder sits
    on (13,5) -- one cell up, one cell left. W steps up to (14,5); A is refused
    by the Elder's own cell but turns the player left; Space then talks to the
    faced cell."""
    rpg.wait_until(lambda: scene_name() == "Camp" or None, 20.0, "Camp active")

    # Wait for the player to be controllable (script loaded, standing on the
    # authored start cell). scene_name() answers before the first logic frame,
    # so a tap sent then is dropped by the input system.
    START = (0.5, -5.5)

    def ready():
        try:
            entity_id = rpg.player_id()
        except SystemExit:
            return None
        return entity_id if rpg.near(rpg.player_position(entity_id), START) else None

    rpg.wait_until(ready, 15.0, "player controllable at Camp start")
    time.sleep(0.5)

    tap("W")
    time.sleep(0.5)
    tap("A")
    time.sleep(0.4)
    tap("Space")

    def greeting():
        text = widget("Dialogue", "Body").get("text") or ""
        if "awake" in text:
            return text
        return None

    rpg.wait_until(greeting, 3.0, "elder greeting")

    # Advance until the road page's choices are up.
    def roads():
        return True if (widget("Dialogue", "Mark1").get("text") or "") == ">" else None

    for _ in range(6):
        if roads():
            break
        tap("Space")
        time.sleep(0.4)
    if not roads():
        raise SystemExit(f"elder never showed a choice menu; body={widget('Dialogue', 'Body').get('text')!r}")

    # Choice 1 is highlighted ("What is out there?") -> the errand page, whose
    # onShow accepts the quest and raises the notice. A confirm either finishes
    # the typewriter or picks the choice, so press until the notice shows.
    def accepted():
        title = widget("QuestNotice", "Title")
        if title.get("visible") and title.get("text") == "Quest accepted":
            return widget("QuestNotice", "Body").get("text")
        return None

    accepted_title = None
    for _ in range(6):
        tap("Space")
        time.sleep(0.5)
        accepted_title = accepted()
        if accepted_title is not None:
            break
    if accepted_title is None:
        raise SystemExit("quest accepted notice never appeared")
    # Close the dialogue (the errand page has no next, so confirm closes it).
    for _ in range(8):
        if not widget("Dialogue", "Body").get("visible"):
            break
        tap("Space")
        time.sleep(0.3)
    return accepted_title


def main() -> None:
    global PORT, LOG
    PORT, LOG = start_with_port()
    try:
        OUT_DIR.mkdir(parents=True, exist_ok=True)
        accepted = accept_from_elder()
        print(f"accepted: {accepted!r}")

        # The journal shows the quest active with both objectives pending.
        journal_open()
        body = rpg.wait_until(
            lambda: journal_body() if "The overrun ruins" in journal_body() else None,
            3.0, "journal lists active quest")
        print("journal (active):")
        for line in body.splitlines():
            print("   ", line)
        if "[ ]" not in body:
            raise SystemExit(f"active journal has no pending objective:\n{body}")
        if "0/3" not in body:
            raise SystemExit(f"active journal does not show the 0/3 kill progress:\n{body}")
        if "Reach the ruins" not in body:
            raise SystemExit(f"active journal is missing the visit objective:\n{body}")
        # The left list has one row, selected, naming the quest.
        rows = journal_rows()
        print(f"journal rows: {rows}")
        if len(rows) != 1 or "The overrun ruins" not in rows[0] or not rows[0].startswith(">"):
            raise SystemExit(f"journal quest list is wrong: {rows}")
        screenshot(OUT_DIR / "journal_active.png")
        # Close the journal so the player can walk.
        tap("J")
        time.sleep(0.3)

        # Walk onto the RuinsGate (two cells below the player).
        for _ in range(3):
            tap("S")
            time.sleep(0.45)
        rpg.wait_until(lambda: scene_name() == "BattleRuins1" or None, 25.0, "battle entered")

        outcome = win_battle()
        print(f"battle outcome: {outcome!r}")
        if outcome != "Victory!":
            raise SystemExit(f"expected a victory, got {outcome!r}")

        # The battle returns to Camp after the banner holds.
        rpg.wait_until(lambda: scene_name() == "Camp" or None, 25.0, "returned to Camp")
        time.sleep(1.0)

        # The journal now lists the quest complete, both objectives done.
        journal_open()
        done = rpg.wait_until(
            lambda: journal_body() if "Done." in journal_body() else None,
            3.0, "journal lists completed quest")
        print("journal (complete):")
        for line in done.splitlines():
            print("   ", line)
        if "[ ]" in done:
            raise SystemExit(f"completed journal still has a pending objective:\n{done}")
        if "3/3" not in done:
            raise SystemExit(f"completed journal does not show the 3/3 kill progress:\n{done}")
        if "Reward" not in done:
            raise SystemExit(f"completed journal is missing the reward line:\n{done}")
        # The list still has the single row, now marked complete in the body.
        done_rows = journal_rows()
        print(f"journal rows (complete): {done_rows}")
        if len(done_rows) != 1 or "The overrun ruins" not in done_rows[0]:
            raise SystemExit(f"completed journal list is wrong: {done_rows}")
        screenshot(OUT_DIR / "journal_complete.png")
    finally:
        rpg.stop()

    text = LOG.read_text(encoding="utf-8", errors="ignore") if LOG and LOG.exists() else ""
    completions = [ln for ln in text.splitlines() if "Quests: completed 'tutorial_ruins'" in ln]
    rewards = [ln for ln in text.splitlines() if "Quests: reward 'tutorial_ruins'" in ln]
    print(f"completion log lines: {len(completions)}  reward log lines: {len(rewards)}")
    if len(completions) != 1:
        raise SystemExit(f"expected exactly one completion, saw {len(completions)}")
    if len(rewards) != 1:
        raise SystemExit(f"reward not paid exactly once (saw {len(rewards)})")
    print("quest advance OK (accepted -> battle won -> completed, reward once)")


if __name__ == "__main__":
    main()
