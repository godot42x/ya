#!/usr/bin/env python3
"""One-shot diagnosis: open a .yaui in the designer and enter Play, capturing
screenshots after each step. Diagnostic harness for the designer-preview and
play-viewport issues; not a gated smoke."""

import argparse
import json
import os
import socket
import subprocess
import sys
import time


class AutomationClient:
    def __init__(self, port: int):
        self.port = port

    def call(self, method: str, timeout: float = 60.0, **params) -> dict:
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


def wait_for_port(port: int, timeout_s: float) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(1.0)
        try:
            sock.connect(("127.0.0.1", port))
            sock.close()
            return
        except OSError:
            time.sleep(0.5)
    raise TimeoutError(f"automation control server did not start on port {port}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Designer preview + play diagnosis")
    parser.add_argument("--port", type=int, default=19997)
    parser.add_argument("--project", default="Example/2dRpgPrototype/2dRpgPrototype.yaproject")
    parser.add_argument("--ui-doc", default="Content:UI/Dialogue.yaui.json")
    parser.add_argument("--startup-timeout", type=int, default=180)
    parser.add_argument("--no-project-open", action="store_true")
    parser.add_argument("--skip-designer", action="store_true")
    parser.add_argument("--window-width", type=int, default=0)
    parser.add_argument("--window-height", type=int, default=0)
    parser.add_argument("--out-dir", default="Engine/Saved/Automation/diag")
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    workspace = os.path.dirname(os.path.dirname(os.path.dirname(script_dir)))
    os.makedirs(os.path.join(workspace, args.out_dir), exist_ok=True)
    engine_log = os.path.join(workspace, args.out_dir, "diag.log")

    with open(engine_log, "w", encoding="utf-8") as log_handle:
        engine = subprocess.Popen(
            [
                sys.executable,
                os.path.join(workspace, "Script", "ya.py"),
                "run-editor",
                "--project",
                args.project,
                "--",
                f"--automation-control-port={args.port}",
                *([f"--width={args.window_width}", f"--height={args.window_height}"]
                  if args.window_width > 0 and args.window_height > 0 else []),
                f"--max-lifetime-seconds={args.startup_timeout + 600}",
            ],
            cwd=workspace,
            stdout=log_handle,
            stderr=subprocess.STDOUT,
        )

        def require_ok(result: dict, label: str) -> dict:
            if not result.get("ok"):
                raise RuntimeError(f"{label} failed: {result.get('error')}")
            return result.get("result", {})

        try:
            wait_for_port(args.port, float(args.startup_timeout))
            client = AutomationClient(args.port)

            time.sleep(3.0)  # let the editor window finish building
            # A CLI --project launch skips the browser phase entirely
            # (EditorLaunchFlow::begin goes straight to Editor), so there is no
            # browser session to route project.open through.
            if not args.no_project_open:
                print("project.open:", json.dumps(require_ok(
                    client.call("invoke", name="project.open", args={"path": args.project}), "project.open")))
                time.sleep(6.0)  # splash -> editor main window

            if not args.skip_designer:
                print("ui_designer.open:", json.dumps(require_ok(
                    client.call("invoke", name="ui_designer.open", args={"path": args.ui_doc}), "ui_designer.open")))
                time.sleep(3.0)
            shot = require_ok(client.call(
                "capture_screenshot",
                target="presentation",
                path=os.path.join(workspace, args.out_dir, "designer.png")), "capture designer")
            print("designer shot:", shot.get("path"))

            shot = require_ok(client.call(
                "capture_screenshot",
                target="viewport",
                path=os.path.join(workspace, args.out_dir, "baseline-viewport.png")),
                "capture pre-play viewport")
            print("pre-play viewport shot:", shot.get("path"))

            print("runtime.play:", json.dumps(require_ok(
                client.call("invoke", name="runtime.play", args={}), "runtime.play")))
            time.sleep(6.0)
            shot = require_ok(client.call(
                "capture_screenshot",
                target="presentation",
                path=os.path.join(workspace, args.out_dir, "play.png")), "capture play")
            print("play shot:", shot.get("path"))
            shot = require_ok(client.call(
                "capture_screenshot",
                target="viewport",
                path=os.path.join(workspace, args.out_dir, "play-viewport.png")), "capture play viewport")
            print("play viewport shot:", shot.get("path"))

            print("runtime.stop:", json.dumps(require_ok(
                client.call("invoke", name="runtime.stop", args={}), "runtime.stop")))
            time.sleep(2.0)
            return 0
        except Exception as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return 1
        finally:
            if engine.poll() is None:
                try:
                    client.call("quit", timeout=10.0)
                    engine.wait(timeout=15)
                except Exception:
                    engine.kill()
                    engine.wait()


if __name__ == "__main__":
    raise SystemExit(main())
