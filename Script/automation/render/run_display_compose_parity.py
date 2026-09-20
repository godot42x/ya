#!/usr/bin/env python3
"""Display compose must not change color.

A View's display image is already graded and gamma-encoded by the finalize pass.
Display compose (the presentation graph) copies it onto the swapchain, so the
windowed image must be byte-identical to the View's display image. That is not a
style preference: grading again here applies an ACES curve plus a second gamma to
the same pixels, which is exactly the regression this gate exists to catch. It
shipped once -- the window was washed out while the editor viewport (which
samples the display image directly) looked correct.

The two automation screenshot targets read the two independent ends of that
path, so comparing them is the whole assertion:

    --screenshot-target=viewport       reads the View's display image
    --screenshot-target=presentation   reads the swapchain the present wrote

Both runs must use the same frame, project and render resolution, and the window
must be the same size as the render resolution (display compose stretches the
render image to the swapchain, so unequal extents make the two ends legitimately
differ).
"""

import argparse
import hashlib
import os
import subprocess
import sys


def md5(path: str) -> str:
    with open(path, "rb") as handle:
        return hashlib.md5(handle.read()).hexdigest()


def run_screenshot(workspace: str, project: str, target: str, output: str, frame: int) -> int:
    command = [
        sys.executable,
        os.path.join(workspace, "Script", "ya.py"),
        "run",
        "--project",
        project,
        "--",
        f"--exit-after-frame={frame}",
        f"--screenshot-target={target}",
        f"--screenshot={output}",
        "--log-level=error",
    ]
    result = subprocess.run(command, cwd=workspace)
    if result.returncode != 0:
        print(f"{target}: runtime exited {result.returncode}", file=sys.stderr)
        return result.returncode
    if not os.path.exists(output):
        print(f"{target}: no screenshot was written to {output}", file=sys.stderr)
        return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", default="Example/HelloMaterial/HelloMaterial.yaproject")
    parser.add_argument("--frame", type=int, default=90)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--out-dir", default="/tmp")
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    workspace = os.path.dirname(os.path.dirname(os.path.dirname(script_dir)))

    if not args.skip_build:
        build = subprocess.run(
            [sys.executable, os.path.join(workspace, "Script", "ya.py"), "build", "--project", args.project],
            cwd=workspace,
        )
        if build.returncode != 0:
            return build.returncode

    viewport_shot = os.path.join(args.out_dir, "display-compose-parity-viewport.png")
    present_shot = os.path.join(args.out_dir, "display-compose-parity-presentation.png")

    for target, output in (("viewport", viewport_shot), ("presentation", present_shot)):
        code = run_screenshot(workspace, args.project, target, output, args.frame)
        if code != 0:
            return code

    viewport_md5 = md5(viewport_shot)
    present_md5 = md5(present_shot)
    print(f"viewport     : {viewport_md5}  {viewport_shot}")
    print(f"presentation : {present_md5}  {present_shot}")

    if viewport_md5 != present_md5:
        print(
            "FAIL: display compose changed the image. The presentation pass must "
            "copy the View's display image without grading it again.",
            file=sys.stderr,
        )
        return 1

    print("PASS: display compose is a pass-through")
    return 0


if __name__ == "__main__":
    sys.exit(main())

