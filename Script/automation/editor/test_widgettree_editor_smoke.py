#!/usr/bin/env python3

import argparse
import json
import os
import socket
import sys
import time


class AutomationClient:
    def __init__(self, port: int):
        self.port = port

    def call(self, method: str, timeout: float = 30.0, **params) -> dict:
        request = {"id": 1, "method": method, "params": params}
        payload = (json.dumps(request) + "\n").encode("utf-8")

        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        try:
            sock.connect(("127.0.0.1", self.port))
            sock.sendall(payload)

            response_data = b""
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                response_data += chunk
                if b"\n" in response_data:
                    break

            if not response_data:
                raise RuntimeError("empty automation response")
            return json.loads(response_data.split(b"\n")[0].decode("utf-8"))
        finally:
            sock.close()


def require_ok(result: dict, label: str) -> dict:
    if not result.get("ok"):
        raise RuntimeError(f"{label} failed: {result.get('error')}")
    return result.get("result", {})


def wait_for_frame_progress(client: AutomationClient, min_frames: int, timeout_s: float) -> dict:
    deadline = time.time() + timeout_s
    first_state = require_ok(client.call("get_world_view_state"), "get_world_view_state(initial)")
    start_frame = int(first_state.get("frame_index", 0))
    last_state = first_state
    while time.time() < deadline:
        last_state = require_ok(client.call("get_world_view_state"), "get_world_view_state(progress)")
        current = int(last_state.get("frame_index", 0))
        if current >= start_frame + min_frames:
            return last_state
        time.sleep(0.1)
    raise RuntimeError(
        f"frame progression stalled: start={start_frame} current={int(last_state.get('frame_index', 0))} min_delta={min_frames}"
    )


# Log lines the steady state must not produce. The first is FontManager
# rebuilding a raster size it just evicted (a per-frame working set larger than
# the cache: 46 ms per rebuild in the editor, FPS 7). The others are the viewport
# image and the panel disagreeing on device pixels (the magnified, blurry view).
FONT_BUILD_MARK = "lazily building"
FAILURE_MARKS = (
    "raster sizes are in use at once",
    "is not the panel device size",
)


def count_log(path: str, needle: str) -> int:
    if not path or not os.path.exists(path):
        return 0
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return sum(1 for line in handle if needle in line)


def measure_steady_state(client: AutomationClient, label: str, seconds: float, min_fps: float,
                         max_font_builds: int, engine_log: str) -> None:
    builds_before = count_log(engine_log, FONT_BUILD_MARK)
    first = require_ok(client.call("get_world_view_state"), f"{label}: get_world_view_state(start)")
    t0 = time.time()
    time.sleep(seconds)
    last = require_ok(client.call("get_world_view_state"), f"{label}: get_world_view_state(end)")
    elapsed = time.time() - t0
    frames = int(last.get("frame_index", 0)) - int(first.get("frame_index", 0))
    fps = frames / elapsed if elapsed > 0 else 0.0
    builds = count_log(engine_log, FONT_BUILD_MARK) - builds_before
    print(f"   {label}: {frames} frames in {elapsed:.1f}s = {fps:.1f} fps, font builds in window: {builds}")
    if fps < min_fps:
        raise RuntimeError(f"{label}: {fps:.1f} fps is below the {min_fps:.1f} fps gate")
    if builds > max_font_builds:
        raise RuntimeError(f"{label}: {builds} font rebuilds in a steady-state window (max {max_font_builds})")


def main() -> int:
    parser = argparse.ArgumentParser(description="WidgetTree editor smoke assertions")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--presentation-shot", required=True)
    parser.add_argument("--min-frame-delta", type=int, default=30)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--engine-log", default="", help="stdout capture of the engine; scanned for font rebuilds")
    parser.add_argument("--perf-window", type=float, default=0.0, help="seconds per steady-state window; 0 skips the gate")
    parser.add_argument("--min-fps", type=float, default=25.0)
    parser.add_argument("--max-steady-font-builds", type=int, default=0)
    parser.add_argument("--pie", action="store_true", help="also gate the steady state while the game is running")
    args = parser.parse_args()

    client = AutomationClient(args.port)

    print("1. ping")
    require_ok(client.call("ping"), "ping")

    print("2. verify editor world view state")
    # The control port opens before the first frame is rendered, so the extent is
    # legitimately empty for a moment. Wait for the first render, then judge it.
    state = require_ok(client.call("get_world_view_state"), "get_world_view_state")
    first_render_deadline = time.time() + args.timeout
    while (float(state.get("rendered_viewport_extent", {}).get("width", 0.0)) <= 0.0
           and time.time() < first_render_deadline):
        time.sleep(0.2)
        state = require_ok(client.call("get_world_view_state"), "get_world_view_state")
    # The extent the renderer actually produced for the host viewport: the editor
    # authoring panel. A window resize or a resolution setting change must not be
    # what makes this non-empty, so an empty extent means the world view is not
    # being rendered at all.
    rendered = state.get("rendered_viewport_extent", {})
    if float(rendered.get("width", 0.0)) <= 0.0 or float(rendered.get("height", 0.0)) <= 0.0:
        raise RuntimeError(f"world view did not render: {rendered}")
    resolution = state.get("render_resolution", {})
    if float(resolution.get("width", 0.0)) <= 0.0 or float(resolution.get("height", 0.0)) <= 0.0:
        raise RuntimeError(f"invalid render resolution: {resolution}")

    print("3. set editor camera")
    require_ok(
        client.call(
            "set_editor_camera",
            position=[0.0, 1.5, 6.0],
            rotation=[-10.0, 180.0, 0.0],
        ),
        "set_editor_camera",
    )

    print("4. wait for stable frame progression")
    progressed = wait_for_frame_progress(client, args.min_frame_delta, args.timeout)
    if bool(progressed.get("is_runtime")):
        raise RuntimeError("editor smoke unexpectedly entered runtime mode")

    if args.perf_window > 0.0:
        print("4b. steady-state perf gate (editing)")
        time.sleep(4.0)  # let first-use font and pipeline builds finish
        measure_steady_state(client, "editing", args.perf_window, args.min_fps,
                             args.max_steady_font_builds, args.engine_log)
        if args.pie:
            print("4c. steady-state perf gate (play in editor)")
            require_ok(client.call("set_app_state", state="runtime"), "set_app_state(runtime)")
            time.sleep(6.0)
            measure_steady_state(client, "play", args.perf_window, args.min_fps,
                                 args.max_steady_font_builds, args.engine_log)
            require_ok(client.call("set_app_state", state="stopped"), "set_app_state(stopped)")
            time.sleep(1.0)
        for mark in FAILURE_MARKS:
            hits = count_log(args.engine_log, mark)
            if hits:
                raise RuntimeError(f"engine log contains {hits} x '{mark}'")

    print("5. capture presentation screenshot")
    shot = require_ok(
        client.call(
            "capture_screenshot",
            path=args.presentation_shot,
            target="presentation",
            warmup_frames=5,
        ),
        "capture_screenshot",
    )
    screenshot_path = shot.get("path") or args.presentation_shot
    if not os.path.exists(screenshot_path):
        raise RuntimeError(f"presentation screenshot was not written: {screenshot_path}")
    if os.path.getsize(screenshot_path) <= 0:
        raise RuntimeError(f"presentation screenshot is empty: {screenshot_path}")

    print("6. quit")
    require_ok(client.call("quit"), "quit")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise
