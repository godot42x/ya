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


def main() -> int:
    parser = argparse.ArgumentParser(description="WidgetTree editor smoke assertions")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--presentation-shot", required=True)
    parser.add_argument("--min-frame-delta", type=int, default=30)
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args()

    client = AutomationClient(args.port)

    print("1. ping")
    require_ok(client.call("ping"), "ping")

    print("2. verify editor world view state")
    state = require_ok(client.call("get_world_view_state"), "get_world_view_state")
    viewport = state.get("viewport_rect", {})
    if float(viewport.get("width", 0.0)) <= 0.0 or float(viewport.get("height", 0.0)) <= 0.0:
        raise RuntimeError(f"invalid viewport rect: {viewport}")

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
