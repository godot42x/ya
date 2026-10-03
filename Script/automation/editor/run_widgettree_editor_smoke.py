#!/usr/bin/env python3

import argparse
import os
import socket
import subprocess
import sys
import time


def wait_for_port(port: int, timeout_s: float) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(1.0)
        try:
            sock.connect(("127.0.0.1", port))
            return
        except OSError:
            time.sleep(0.5)
        finally:
            sock.close()
    raise TimeoutError(f"automation control server did not start on port {port}")


def tail(path: str, lines: int = 80) -> str:
    if not os.path.exists(path):
        return ""
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        content = handle.readlines()
    return "".join(content[-lines:])


def main() -> int:
    parser = argparse.ArgumentParser(description="Run a bounded WidgetTree editor smoke")
    parser.add_argument("--port", type=int, default=19995)
    parser.add_argument("--project", default="Example/HelloMaterial/HelloMaterial.yaproject")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--startup-timeout", type=int, default=120)
    parser.add_argument("--frame-budget", type=int, default=240)
    parser.add_argument("--min-frame-delta", type=int, default=30)
    parser.add_argument("--perf-window", type=float, default=0.0,
                        help="seconds per steady-state window; 0 keeps the smoke functional only")
    parser.add_argument("--min-fps", type=float, default=25.0)
    parser.add_argument("--pie", action="store_true", help="gate the steady state while the game runs too")
    parser.add_argument("--presentation-shot", default="Engine/Saved/Automation/widgettree-editor-smoke-presentation.png")
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    workspace = os.path.dirname(os.path.dirname(os.path.dirname(script_dir)))
    engine_log = os.path.join(workspace, "Engine", "Saved", "Automation", "widgettree-editor-smoke.log")
    os.makedirs(os.path.dirname(engine_log), exist_ok=True)

    if not args.skip_build:
        result = subprocess.run(
            [sys.executable, os.path.join(workspace, "Script", "ya.py"), "build", "--project", args.project, "--editor"],
            cwd=workspace,
        )
        if result.returncode != 0:
            return result.returncode

    with open(engine_log, "w", encoding="utf-8") as log_handle:
        engine = subprocess.Popen(
            [
                sys.executable,
                os.path.join(workspace, "Script", "ya.py"),
                "run-editor",
                "--project",
                args.project,
                "--",
                "--editor-chrome=widgettree",
                f"--automation-control-port={args.port}",
                # Bounded on purpose: the finally-block kills this process,
                # not the engine it spawned.
                f"--max-lifetime-seconds={args.startup_timeout + 600}",
                *([] if args.perf_window > 0.0 else [f"--exit-after-frame={args.frame_budget}"]),
            ],
            cwd=workspace,
            stdout=log_handle,
            stderr=subprocess.STDOUT,
        )

        try:
            wait_for_port(args.port, float(args.startup_timeout))
            result = subprocess.run(
                [
                    sys.executable,
                    os.path.join(script_dir, "test_widgettree_editor_smoke.py"),
                    "--port",
                    str(args.port),
                    "--presentation-shot",
                    args.presentation_shot,
                    "--min-frame-delta",
                    str(args.min_frame_delta),
                    "--timeout",
                    str(args.startup_timeout),
                    "--engine-log",
                    engine_log,
                    "--perf-window",
                    str(args.perf_window),
                    "--min-fps",
                    str(args.min_fps),
                    *(["--pie"] if args.pie else []),
                ],
                cwd=workspace,
            )
            if engine.poll() is None:
                try:
                    engine.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    engine.kill()
                    engine.wait()
                    raise RuntimeError("engine did not exit after smoke quit")

            if result.returncode != 0:
                print(tail(engine_log), file=sys.stderr)
                return result.returncode
            if engine.returncode != 0:
                print(tail(engine_log), file=sys.stderr)
                raise RuntimeError(f"engine exited with {engine.returncode}")
            return 0
        finally:
            if engine.poll() is None:
                engine.kill()
                engine.wait()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise
