#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import platform
import subprocess
import sys
from difflib import get_close_matches
from pathlib import Path


WORKSPACE_ROOT = Path(__file__).resolve().parent.parent
SCRIPT_DIR = WORKSPACE_ROOT / "Script"
SUBCOMMAND_PARSERS: dict[str, argparse.ArgumentParser] = {}


class YaArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        argv = sys.argv[1:]
        if argv:
            subparser = SUBCOMMAND_PARSERS.get(argv[0])
            if subparser is not None:
                self.exit(2, f"{self.prog}: error: {message}\n\n{subparser.format_help()}")
        self.print_usage(sys.stderr)
        self.exit(2, f"{self.prog}: error: {message}\n")


class YaSubcommandParser(YaArgumentParser):
    def error(self, message: str) -> None:
        self.exit(2, f"{self.prog}: error: {message}\n\n{self.format_help()}")


def _fail_command(command_name: str, message: str):
    subparser = SUBCOMMAND_PARSERS.get(command_name)
    if subparser is not None:
        raise SystemExit(f"{message}\n\n{subparser.format_help()}")
    raise SystemExit(message)


def _run(cmd: list[str], *, cwd: Path = WORKSPACE_ROOT) -> None:
    subprocess.run(cmd, cwd=cwd, check=True, env=_build_subprocess_env())


def _capture(cmd: list[str], *, cwd: Path = WORKSPACE_ROOT) -> str:
    result = subprocess.run(cmd, cwd=cwd, check=True, text=True, capture_output=True, env=_build_subprocess_env())
    return result.stdout


def _build_subprocess_env() -> dict[str, str]:
    env = dict(os.environ)
    if platform.system() == "Darwin":
        sdk_lib_dirs = sorted(
            {
                str(path)
                for path in (WORKSPACE_ROOT / "Engine" / "ThirdParty" / "VulkanSDK").glob("*/macOS/lib")
                if path.is_dir()
            }
        )
        if sdk_lib_dirs:
            existing = env.get("DYLD_LIBRARY_PATH", "")
            merged = sdk_lib_dirs + ([existing] if existing else [])
            env["DYLD_LIBRARY_PATH"] = ":".join(merged)
    return env


def _normalize_engine_args(engine_args: list[str]) -> list[str]:
    if engine_args and engine_args[0] == "--":
        return engine_args[1:]
    return engine_args


def _format_command_examples(command_name: str) -> str:
    return (
        f"examples:\n"
        f"  python3 Script/ya.py {command_name} --project Example/HelloMaterial/HelloMaterial.yaproject\n"
        f"  python3 Script/ya.py {command_name} --project Example/HelloMaterial/HelloMaterial.yaproject -- --exit-after-frame=5"
    )


def _collect_project_candidates(project: str) -> list[Path]:
    requested = Path(project)
    requested_name = requested.name
    candidates = [
        path
        for path in WORKSPACE_ROOT.glob("**/*.yaproject")
        if "Engine/Saved/Package" not in path.as_posix() and not path.is_relative_to(WORKSPACE_ROOT / "Package")
    ]

    same_name = [path for path in candidates if path.name == requested_name]
    if same_name:
        return sorted(same_name)

    candidate_names = [path.name for path in candidates]
    close_names = set(get_close_matches(requested_name, candidate_names, n=3, cutoff=0.6))
    return sorted(path for path in candidates if path.name in close_names)


def _format_project_not_found(project: str) -> str:
    lines = [f"project not found: {project}"]
    suggestions = _collect_project_candidates(project)
    if suggestions:
        lines.append("")
        lines.append("did you mean:")
        for candidate in suggestions[:3]:
            try:
                display = candidate.relative_to(WORKSPACE_ROOT)
            except ValueError:
                display = candidate
            lines.append(f"  {display}")
    return "\n".join(lines)


def _validate_project_argument(project: str, command_name: str) -> None:
    project_path = Path(project)
    if project_path.suffix == ".yamodule":
        suggestions = _collect_project_candidates(project)
        lines = [
            f"--project expects a .yaproject file, not a .yamodule: {project}",
            "",
            ".yamodule is a module manifest; run/package need the project descriptor.",
        ]
        if suggestions:
            lines.append("")
            lines.append("did you mean:")
            for candidate in suggestions[:3]:
                try:
                    display = candidate.relative_to(WORKSPACE_ROOT)
                except ValueError:
                    display = candidate
                lines.append(f"  {display}")
        lines.append("")
        lines.append(_format_command_examples(command_name))
        _fail_command(command_name, "\n".join(lines))
    if project_path.suffix and project_path.suffix != ".yaproject":
        _fail_command(
            command_name,
            f"--project expects a .yaproject file: {project}\n\n{_format_command_examples(command_name)}",
        )


def _resolve_project_path(project: str | None, command_name: str | None = None) -> Path | None:
    if project:
        _validate_project_argument(project, command_name or "run")
        project_path = (WORKSPACE_ROOT / project).resolve() if not Path(project).is_absolute() else Path(project).resolve()
        if not project_path.is_file():
            _fail_command(command_name or "run", _format_project_not_found(project))
        return project_path
    return None


def _require_project_path(project: str | None, command_name: str) -> Path:
    project_path = _resolve_project_path(project, command_name)
    if project_path:
        return project_path
    _fail_command(
        command_name,
        f"{command_name} requires --project.\n"
        f"{_format_command_examples(command_name)}"
    )


def _setup_workspace() -> None:
    _run([sys.executable, str(SCRIPT_DIR / "setup_submodules.py")])
    _run([sys.executable, str(SCRIPT_DIR / "setup_3rd_party.py")])
    if platform.system() == "Darwin":
        _run([sys.executable, str(SCRIPT_DIR / "setup_vulkan_sdk_macos.py")])


def _apply_config(mode: str, force: bool, config_args: list[str]) -> None:
    _setup_workspace()
    if force:
        _run(["xmake", "c"])
    _run(["xmake", "f", "-c", "-y"])
    _run(["xmake", "f", "-m", mode, "-y"])
    if config_args:
        _run(["xmake", "f", *config_args])
    _run(["xmake", "project", "-k", "compile_commands"])


def _build_targets_for(project_path: Path | None, include_editor: bool) -> list[str]:
    if project_path:
        cmd = [
            sys.executable,
            str(SCRIPT_DIR / "ya_bundle_tool.py"),
            "build-targets",
            "--project",
            str(project_path),
        ]
        if include_editor:
            cmd.append("--editor")
        output = _capture(cmd)
        return [line.strip() for line in output.splitlines() if line.strip()]

    targets = ["ya-runtime"]
    if include_editor:
        targets.append("ya-editor")
    return targets


def _build_targets(targets: list[str], build_args: list[str]) -> None:
    for target in targets:
        _run(["xmake", "b", *build_args, target])


def _run_runtime(project_path: Path | None, include_editor: bool, run_args: list[str], engine_args: list[str]) -> None:
    cmd = ["xmake", "r", *run_args, "ya-runtime"]
    if project_path:
        cmd.append(f"--ya-project={project_path}")
    if include_editor:
        cmd.append("--editor")
    cmd.extend(engine_args)
    _run(cmd)


def _default_package_output(project_path: Path | None) -> Path:
    if project_path:
        return WORKSPACE_ROOT / "Package" / project_path.stem
    return WORKSPACE_ROOT / "Package" / "ya-runtime"


def cmd_cfg(args: argparse.Namespace) -> None:
    _apply_config(args.mode, args.force, args.config_arg)


def cmd_setup(_: argparse.Namespace) -> None:
    _setup_workspace()


def cmd_build(args: argparse.Namespace) -> None:
    project_path = _resolve_project_path(args.project, "build")
    if args.force:
        _run(["xmake", "c"])
    if args.config_arg:
        _run(["xmake", "f", *args.config_arg])
        _run(["xmake", "project", "-k", "compile_commands"])
    targets = _build_targets_for(project_path, args.editor)
    _build_targets(targets, args.build_arg)


def cmd_run(args: argparse.Namespace) -> None:
    project_path = _require_project_path(args.project, "run")
    if args.force:
        _run(["xmake", "c"])
    if args.config_arg:
        _run(["xmake", "f", *args.config_arg])
        _run(["xmake", "project", "-k", "compile_commands"])
    targets = _build_targets_for(project_path, False)
    _build_targets(targets, args.build_arg)
    _run_runtime(project_path, False, args.run_arg, _normalize_engine_args(args.engine_args))


def cmd_run_editor(args: argparse.Namespace) -> None:
    project_path = _resolve_project_path(args.project, "run-editor")
    if args.force:
        _run(["xmake", "c"])
    if args.config_arg:
        _run(["xmake", "f", *args.config_arg])
        _run(["xmake", "project", "-k", "compile_commands"])
    targets = _build_targets_for(project_path, True)
    _build_targets(targets, args.build_arg)
    _run_runtime(project_path, True, args.run_arg, _normalize_engine_args(args.engine_args))


def cmd_package(args: argparse.Namespace) -> None:
    project_path = _require_project_path(args.project, "package")
    if args.force:
        _run(["xmake", "c"])
    if args.config_arg:
        _run(["xmake", "f", *args.config_arg])
        _run(["xmake", "project", "-k", "compile_commands"])
    targets = _build_targets_for(project_path, args.editor)
    _build_targets(targets, args.build_arg)
    output = Path(args.output).resolve() if args.output else _default_package_output(project_path)
    cmd = [
        sys.executable,
        str(SCRIPT_DIR / "ya_bundle_tool.py"),
        "package",
        "--project",
        str(project_path),
        "--output",
        str(output),
    ]
    if args.editor:
        cmd.append("--editor")
    if args.smoke_run:
        cmd.append("--smoke-run")
    _run(cmd)


def cmd_test(args: argparse.Namespace) -> None:
    test_target = f"{args.target}-testing"
    if args.force:
        _run(["xmake", "c"])
    _run(["xmake", "b", *args.build_arg, test_target])
    cmd = ["xmake", "r", test_target]
    if args.filter:
        cmd.append(f"--gtest_filter={args.filter}")
    _run(cmd)


def cmd_vulkan_sdk_macos(args: argparse.Namespace) -> None:
    cmd = [sys.executable, str(SCRIPT_DIR / "setup_vulkan_sdk_macos.py")]
    if args.version:
        cmd.extend(["--version", args.version])
    else:
        cmd.append("--latest")
    if args.force:
        cmd.append("--force")
    _run(cmd)


def _add_common_build_flags(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--project", help="Path to a .yaproject file.")
    parser.add_argument("--force", action="store_true", help="Clean before build.")
    parser.add_argument("--config-arg", action="append", default=[], help="Extra argument forwarded to `xmake f`.")
    parser.add_argument("--build-arg", action="append", default=[], help="Extra argument forwarded to `xmake b`.")


def build_parser() -> argparse.ArgumentParser:
    parser = YaArgumentParser(
        prog="python3 Script/ya.py",
        description="YA workflow launcher. Uses xmake for build/run, Python for orchestration.",
    )
    subparsers = parser.add_subparsers(dest="command", parser_class=YaSubcommandParser)

    cfg = subparsers.add_parser("cfg", help="Setup prerequisites and refresh xmake configuration.")
    SUBCOMMAND_PARSERS["cfg"] = cfg
    cfg.add_argument("--mode", default="debug", choices=["debug", "releasedbg", "release", "profile"])
    cfg.add_argument("--force", action="store_true", help="Clean before configure.")
    cfg.add_argument("--config-arg", action="append", default=[], help="Extra argument forwarded to `xmake f`.")
    cfg.set_defaults(func=cmd_cfg)

    setup = subparsers.add_parser("setup", help="Run prerequisite setup scripts.")
    SUBCOMMAND_PARSERS["setup"] = setup
    setup.set_defaults(func=cmd_setup)

    build = subparsers.add_parser("build", help="Build ya-runtime or a project closure.")
    SUBCOMMAND_PARSERS["build"] = build
    _add_common_build_flags(build)
    build.add_argument("--editor", action="store_true", help="Also build ya-editor alongside the host runtime.")
    build.set_defaults(func=cmd_build)

    run = subparsers.add_parser("run", help="Build and run a project through ya-runtime.")
    SUBCOMMAND_PARSERS["run"] = run
    _add_common_build_flags(run)
    run.add_argument("--run-arg", action="append", default=[], help="Extra argument forwarded to `xmake r`.")
    run.add_argument("engine_args", nargs=argparse.REMAINDER, help="Arguments forwarded to ya-runtime after `--`.")
    run.set_defaults(func=cmd_run)

    run_editor = subparsers.add_parser("run-editor", help="Build and run ya-runtime in editor mode.")
    SUBCOMMAND_PARSERS["run-editor"] = run_editor
    _add_common_build_flags(run_editor)
    run_editor.add_argument("--run-arg", action="append", default=[], help="Extra argument forwarded to `xmake r`.")
    run_editor.add_argument("engine_args", nargs=argparse.REMAINDER, help="Arguments forwarded to ya-runtime after `--`.")
    run_editor.set_defaults(func=cmd_run_editor)

    package = subparsers.add_parser("package", help="Build and collect a minimal package.")
    SUBCOMMAND_PARSERS["package"] = package
    _add_common_build_flags(package)
    package.add_argument("--editor", action="store_true", help="Also package editor-side modules.")
    package.add_argument("--output", help="Package output directory.")
    package.add_argument("--smoke-run", action="store_true", help="Run the packaged runtime once after bundling.")
    package.set_defaults(func=cmd_package)

    test = subparsers.add_parser("test", help="Build and run a GoogleTest target.")
    SUBCOMMAND_PARSERS["test"] = test
    test.add_argument("--target", default="ya", help="Base test target name, e.g. ya -> ya-testing.")
    test.add_argument("--filter", help="GoogleTest filter.")
    test.add_argument("--force", action="store_true", help="Clean before build.")
    test.add_argument("--build-arg", action="append", default=[], help="Extra argument forwarded to `xmake b`.")
    test.set_defaults(func=cmd_test)

    vulkan = subparsers.add_parser(
        "vulkan-sdk-macos",
        help="Install or refresh the shared macOS Vulkan SDK (symlinked into this checkout).",
    )
    SUBCOMMAND_PARSERS["vulkan-sdk-macos"] = vulkan
    vulkan.add_argument("--version", help="Requested Vulkan SDK version.")
    vulkan.add_argument("--force", action="store_true", help="Reinstall even if already present.")
    vulkan.set_defaults(func=cmd_vulkan_sdk_macos)

    return parser


def main() -> int:
    parser = build_parser()
    if len(sys.argv) == 1:
        parser.print_help()
        return 0
    args = parser.parse_args()
    if not hasattr(args, "func"):
        parser.print_help()
        return 0
    args.func(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
