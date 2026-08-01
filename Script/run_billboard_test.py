#!/usr/bin/env python3
"""Billboard 集成测试启动脚本。

1. 构建引擎 (editor 模式)
2. 启动引擎 (带自动化控制端口)
3. 运行 billboard 测试脚本
4. 清理退出

Usage:
    python3 Script/run_billboard_test.py [--port PORT] [--project PROJECT]
"""

import argparse
import os
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description="Billboard 集成测试启动器")
    parser.add_argument("--port", type=int, default=19999, help="自动化控制 TCP 端口")
    parser.add_argument("--project", default="Example/HelloMaterial/HelloMaterial.yaproject",
                        help="测试项目路径")
    parser.add_argument("--screenshot", default="test_billboard_output.png",
                        help="截图输出路径")
    parser.add_argument("--skip-build", action="store_true", help="跳过构建步骤")
    parser.add_argument("--timeout", type=int, default=120, help="引擎启动超时（秒）")
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))

    # 1. 构建
    if not args.skip_build:
        print("=" * 50)
        print("Step 1: Building engine...")
        print("=" * 50)
        result = subprocess.run(
            [sys.executable, os.path.join(script_dir, "ya.py"), "build",
             "--project", args.project, "--editor"],
            cwd=os.path.dirname(script_dir),
        )
        if result.returncode != 0:
            print("Build failed!")
            return 1
        print("Build OK.\n")
    else:
        print("Build skipped (--skip-build).\n")

    # 2. 启动引擎（后台）
    print("=" * 50)
    print(f"Step 2: Launching engine on port {args.port}...")
    print("=" * 50)

    engine_process = subprocess.Popen(
        [
            sys.executable, os.path.join(script_dir, "ya.py"), "run-editor",
            "--project", args.project,
            "--",
            f"--automation-control-port={args.port}",
        ],
        cwd=os.path.dirname(script_dir),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    print(f"Engine PID: {engine_process.pid}")

    # 3. 等待引擎启动
    print("Waiting for engine to start...", end=" ", flush=True)
    sock = None
    deadline = time.time() + args.timeout
    while time.time() < deadline:
        try:
            import socket
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(1.0)
            sock.connect(("127.0.0.1", args.port))
            sock.close()
            print("connected!")
            break
        except (ConnectionRefusedError, OSError):
            time.sleep(1)
            continue
    else:
        print("TIMEOUT - engine did not start in time")
        engine_process.kill()
        engine_process.wait()
        return 1

    # 4. 运行测试
    print("\n" + "=" * 50)
    print("Step 3: Running billboard tests...")
    print("=" * 50 + "\n")
    test_script = os.path.join(script_dir, "test_billboard.py")
    result = subprocess.run(
        [sys.executable, test_script,
         "--port", str(args.port),
         "--screenshot", args.screenshot],
        cwd=os.path.dirname(script_dir),
    )

    # 5. 确保引擎退出
    if engine_process.poll() is None:
        print("\nWaiting for engine to exit...")
        try:
            engine_process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            print("Engine still running, forcing quit...")
            engine_process.kill()
            engine_process.wait()

    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
