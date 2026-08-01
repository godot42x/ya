#!/usr/bin/env python3

import argparse
import os
import subprocess
import sys
import time


def main() -> int:
    parser = argparse.ArgumentParser(description="Run isolated billboard regression smoke")
    parser.add_argument("--port", type=int, default=19993)
    parser.add_argument("--project", default="Example/HelloMaterial/HelloMaterial.yaproject")
    parser.add_argument("--screenshot", default="Engine/Saved/Automation/BillboardSmoke-isolated.png")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    workspace = os.path.dirname(os.path.dirname(os.path.dirname(script_dir)))

    if not args.skip_build:
        result = subprocess.run(
            [sys.executable, os.path.join(workspace, "Script", "ya.py"), "build", "--project", args.project, "--editor"],
            cwd=workspace,
        )
        if result.returncode != 0:
            return result.returncode

    engine = subprocess.Popen(
        [
            sys.executable,
            os.path.join(workspace, "Script", "ya.py"),
            "run-editor",
            "--project",
            args.project,
            "--",
            f"--automation-control-port={args.port}",
        ],
        cwd=workspace,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    deadline = time.time() + args.timeout
    while time.time() < deadline:
        try:
            import socket

            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(1.0)
            sock.connect(("127.0.0.1", args.port))
            sock.close()
            break
        except OSError:
            time.sleep(1)
    else:
        engine.kill()
        engine.wait()
        raise SystemExit("automation control server did not start in time")

    try:
        result = subprocess.run(
            [
                sys.executable,
                os.path.join(script_dir, "test_billboard_isolated.py"),
                "--port",
                str(args.port),
                "--screenshot",
                args.screenshot,
            ],
            cwd=workspace,
        )
        return result.returncode
    finally:
        if engine.poll() is None:
            try:
                engine.wait(timeout=10)
            except subprocess.TimeoutExpired:
                engine.kill()
                engine.wait()


if __name__ == "__main__":
    raise SystemExit(main())
