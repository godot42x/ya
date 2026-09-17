#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time
from difflib import get_close_matches
from pathlib import Path


WORKSPACE_ROOT = Path(__file__).resolve().parent.parent
SCRIPT_DIR = WORKSPACE_ROOT / "Script"
SUBCOMMAND_PARSERS: dict[str, argparse.ArgumentParser] = {}

# An instance's claim and its discovery record live here; the engine writes both
# (Core/Os/InstanceRegistry). The control client only reads them, and never has
# to reproduce the engine's file-name hashing: the record carries its own key.
INSTANCE_DIR = Path(tempfile.gettempdir()) / "ya-instances"
ANSI_ESCAPE_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")

# A control instance is started by tooling, so it is bounded by default: an agent
# that forgets one must not leave it holding the GPU, the port and the build
# outputs forever. --lifetime 0 is the explicit opt-out, not the default.
DEFAULT_LIFETIME_S = 1800
DEFAULT_STARTUP_TIMEOUT_S = 120
STOP_GRACE_S = 10
STOP_KILL_GRACE_S = 5


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
        targets.append("ya-game-editor")
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


# --- control: attach to a live instance instead of starting another one -------


def _read_instance_records() -> list[dict[str, str]]:
    records: list[dict[str, str]] = []
    if not INSTANCE_DIR.is_dir():
        return records
    for path in sorted(INSTANCE_DIR.glob("*.instance.json")):
        fields: dict[str, str] = {}
        try:
            content = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for line in content.splitlines():
            name, separator, value = line.partition("=")
            if separator:
                fields[name] = value
        if fields.get("pid") and fields.get("key"):
            fields["recordPath"] = str(path)
            records.append(fields)
    return records


def _is_process_alive(pid: int) -> bool:
    if pid <= 0:
        return False
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    except OSError:
        return False
    return True


def _select_instances(project: Path | None = None, mode: str | None = None) -> tuple[list[dict[str, str]], int]:
    """Live instances matching the filters, plus how many stale records were dropped."""
    live: list[dict[str, str]] = []
    stale = 0
    for record in _read_instance_records():
        if not _is_process_alive(int(record["pid"])):
            stale += 1
            try:
                os.unlink(record["recordPath"])
            except OSError:
                pass
            continue
        if project is not None and record.get("project") != str(project):
            continue
        if mode is not None and record.get("mode") != mode:
            continue
        live.append(record)
    live.sort(key=lambda record: int(record.get("startedAtUnixMs", "0")))
    return live, stale


def _describe_instance(record: dict[str, str]) -> str:
    port = record.get("controlPort", "0")
    started = int(record.get("startedAtUnixMs", "0")) // 1000
    uptime = max(0, int(time.time()) - started) if started else 0
    reachable = f"control port {port}" if port != "0" else "no control port (not drivable)"
    project = record.get("project") or "<no project>"
    return f"pid {record['pid']:<7} {record.get('mode', '?'):<6} {uptime:>5}s  {reachable:<34} {project}"


def _rpc(port: int, method: str, params: dict | None = None, timeout: float = 10.0) -> dict:
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.sendall((json.dumps({"method": method, "id": 1, "params": params or {}}) + "\n").encode("utf-8"))
        data = b""
        while b"\n" not in data:
            chunk = sock.recv(65536)
            if not chunk:
                raise RuntimeError("engine closed the automation connection")
            data += chunk
    response = json.loads(data.decode("utf-8"))
    if not response.get("ok"):
        raise RuntimeError(response.get("error", "engine rpc failed"))
    return response.get("result", {})


def _free_tcp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def _runtime_binary_path() -> Path:
    result = subprocess.run(
        ["xmake", "show", "-t", "ya-runtime"],
        cwd=WORKSPACE_ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    match = re.search(r"targetfile:\s+([^\n]+)", ANSI_ESCAPE_RE.sub("", result.stdout))
    if not match:
        raise RuntimeError("cannot resolve the ya-runtime binary path from `xmake show -t ya-runtime`")
    binary = (WORKSPACE_ROOT / match.group(1).strip()).resolve()
    if not binary.is_file():
        raise RuntimeError(f"ya-runtime is not built at {binary}; run `python3 Script/ya.py build --editor` first")
    return binary


def _wait_for_instance(pid: int, timeout_s: float, log_path: Path) -> dict[str, str] | None:
    """Wait for p pid's own record; the launcher knows the pid, so readiness is
    exact rather than a port probe that could reach a different process."""
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        for record in _read_instance_records():
            if int(record["pid"]) == pid:
                return record
        if not _is_process_alive(pid):
            print(_tail_file(log_path, 25), file=sys.stderr)
            raise SystemExit(f"engine exited during startup (exit before publishing its record); log: {log_path}")
        time.sleep(0.25)
    print(_tail_file(log_path, 25), file=sys.stderr)
    raise SystemExit(f"engine did not become ready within {timeout_s:.0f}s; log: {log_path}")


def _tail_file(path: Path, lines: int) -> str:
    try:
        content = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return ""
    return "\n".join(content[-lines:])


def _control_filter(args: argparse.Namespace, command_name: str) -> tuple[Path | None, str | None]:
    project = _resolve_project_path(args.project, command_name) if args.project else None
    mode = "game" if getattr(args, "game", False) else ("editor" if getattr(args, "editor", False) else None)
    return project, mode


def _control_attach(project: Path, mode: str | None, command_name: str) -> dict[str, str]:
    live, _ = _select_instances(project, mode)
    drivable = [record for record in live if record.get("controlPort", "0") != "0"]
    if drivable:
        return drivable[0]
    if live:
        _fail_command(
            command_name,
            f"instance pid {live[0]['pid']} is running for {project} but serves no automation control "
            f"port, so it cannot be driven.\nrestart it under control:\n"
            f"  python3 Script/ya.py control stop --project {project}\n"
            f"  python3 Script/ya.py control start --project {project}",
        )
    _fail_command(
        command_name,
        f"no live instance for {project} ({mode or 'editor'}). start one first:\n"
        f"  python3 Script/ya.py control start --project {project}",
    )


def cmd_control_status(args: argparse.Namespace) -> None:
    project, mode = _control_filter(args, "control")
    live, stale = _select_instances(project, mode)
    if not live:
        print("no live engine instances")
    else:
        print("live engine instances:")
        for record in live:
            print(f"  {_describe_instance(record)}")
    if stale:
        print(f"(dropped {stale} record(s) of exited processes)")


def cmd_control_start(args: argparse.Namespace) -> None:
    project = _require_project_path(args.project, "control")
    mode = "game" if args.game else "editor"

    live, stale = _select_instances(project, mode)
    if live:
        # Attaching is the point: a second engine on one project is refused by the
        # kernel anyway, and the caller wants control, not another process.
        print(f"already running, attaching instead of starting another one (dropped {stale} stale record(s)):")
        print(f"  {_describe_instance(live[0])}")
        return

    port = args.port if args.port else _free_tcp_port()
    lifetime = args.lifetime if args.lifetime is not None else DEFAULT_LIFETIME_S
    log_path = WORKSPACE_ROOT / "Engine" / "Saved" / "Automation" / f"control-{mode}-{project.stem}-{port}.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)

    if args.build:
        _build_targets(_build_targets_for(project, mode == "editor"), args.build_arg)
    binary = _runtime_binary_path()

    cmd = [str(binary), f"--ya-project={project}", f"--automation-control-port={port}"]
    if mode == "editor":
        cmd.append("--editor")
    cmd.append(f"--max-lifetime-seconds={lifetime}")
    cmd.extend(args.engine_arg)

    print("starting: " + " ".join(cmd))
    with open(log_path, "w", encoding="utf-8") as log_handle:
        engine = subprocess.Popen(
            cmd,
            cwd=WORKSPACE_ROOT,
            env=_build_subprocess_env(),
            stdin=subprocess.DEVNULL,
            stdout=log_handle,
            stderr=subprocess.STDOUT,
            # Survives the shell / tool call that launched it, so one instance
            # outlives the command that asked for it -- paired with the lifetime
            # ceiling above, which is what keeps that from being forever.
            start_new_session=True,
        )

    record = _wait_for_instance(engine.pid, float(args.startup_timeout), log_path)
    assert record is not None
    print(f"running: pid {record['pid']} ({record.get('mode')}) for {record.get('project')}")
    print(f"  lifetime ceiling {lifetime:.0f}s (0 = unlimited)")
    print(f"  log {log_path}")
    port = record.get("controlPort", "0")
    if port == "0":
        print("  no automation control port: this run cannot be driven (stop it with `control stop`)")
        return
    bridge = SCRIPT_DIR / "ya_mcp_bridge.py"
    print(f"  control port {port}")
    print(f"  call: python3 Script/ya.py control call --project {args.project} <method> '{{\"k\":\"v\"}}'")
    print(f"  mcp:  python3 Script/ya.py control mcp --project {args.project}")
    print(f"  raw:  python3 {bridge.relative_to(WORKSPACE_ROOT)} --port {port}")


def cmd_control_stop(args: argparse.Namespace) -> None:
    project, mode = _control_filter(args, "control")
    if project is None and not args.all:
        _fail_command("control", "control stop needs --project, or --all to stop every live instance.")

    live, stale = _select_instances(project, mode)
    if not live:
        print(f"nothing to stop (dropped {stale} record(s) of exited processes)")
        return

    failures = 0
    for record in live:
        pid = int(record["pid"])
        port = int(record.get("controlPort", "0"))
        print(f"stopping pid {pid} ({record.get('mode')}, {record.get('project')})")

        asked = False
        if port and _is_process_alive(pid):
            try:
                _rpc(port, "quit", timeout=5.0)
                asked = True
            except (OSError, RuntimeError, ValueError):
                pass
        if asked and _wait_for_exit(pid, STOP_GRACE_S):
            print("  asked it to quit; exited cleanly")
            continue

        # A run that ignores (or never had) the control channel still has to end:
        # leaving it because the polite path failed is how strays accumulate.
        if _is_process_alive(pid):
            _terminate(pid, signal.SIGTERM)
        if _wait_for_exit(pid, STOP_KILL_GRACE_S):
            print("  did not quit on request; terminated")
            continue
        if _is_process_alive(pid):
            _terminate(pid, signal.SIGKILL)
        if _wait_for_exit(pid, STOP_KILL_GRACE_S):
            print("  ignored SIGTERM; killed")
            continue
        print(f"  still alive after SIGKILL; pid {pid} needs a manual look", file=sys.stderr)
        failures += 1

    for record in live:
        if not _is_process_alive(int(record["pid"])):
            try:
                os.unlink(record["recordPath"])
            except OSError:
                pass
    if failures:
        raise SystemExit(1)


def _terminate(pid: int, sig: int) -> None:
    try:
        os.kill(pid, sig)
    except (ProcessLookupError, PermissionError):
        pass


def _wait_for_exit(pid: int, timeout_s: float) -> bool:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if not _is_process_alive(pid):
            return True
        time.sleep(0.2)
    return not _is_process_alive(pid)


def _bridge_command(record: dict[str, str], extra: list[str]) -> list[str]:
    return [sys.executable, str(SCRIPT_DIR / "ya_mcp_bridge.py"), "--port", record["controlPort"]] + extra


def cmd_control_call(args: argparse.Namespace) -> None:
    project, mode = _control_filter(args, "control")
    if project is None:
        _fail_command("control", "control call needs --project to pick the instance to talk to.")
    record = _control_attach(project, mode if mode else "editor", "control")
    cmd = _bridge_command(record, ["--call", args.method, args.params])
    result = subprocess.run(cmd, cwd=WORKSPACE_ROOT, env=_build_subprocess_env())
    if result.returncode != 0:
        # The remote call failed; the caller has to see that, not exit 0.
        raise SystemExit(result.returncode)


def cmd_control_help(_: argparse.Namespace) -> None:
    SUBCOMMAND_PARSERS["control"].print_help()


def cmd_control_mcp(args: argparse.Namespace) -> None:
    project, mode = _control_filter(args, "control")
    if project is None:
        _fail_command("control", "control mcp needs --project to pick the instance to talk to.")
    record = _control_attach(project, mode if mode else "editor", "control")
    # exec, not spawn: the bridge must not leave a wrapper process behind for the
    # whole session.
    os.execv(sys.executable, _bridge_command(record, []))



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
    build.add_argument("--editor", action="store_true", help="Also build ya-game-editor alongside the host runtime.")
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

    control = subparsers.add_parser(
        "control",
        help="Drive a live engine instance: attach to the one already running, or start exactly one.",
        description=(
            "Instance-lifecycle entry for automation. `start` reuses the live instance for a "
            "project instead of starting a second one, and every instance it starts carries a "
            "wall-clock ceiling, so a forgotten run ends by itself."
        ),
    )
    SUBCOMMAND_PARSERS["control"] = control
    control.set_defaults(func=cmd_control_help)
    control_sub = control.add_subparsers(dest="control_command", parser_class=YaSubcommandParser)

    status = control_sub.add_parser("status", help="List live instances (and drop records of exited ones).")
    status.add_argument("--project", help="Only this project.")
    status.add_argument("--editor", action="store_true", help="Only editor instances.")
    status.add_argument("--game", action="store_true", help="Only game instances.")
    status.set_defaults(func=cmd_control_status)

    control_start = control_sub.add_parser(
        "start",
        help="Attach to the live instance for a project, or start exactly one with a lifetime ceiling.",
    )
    control_start.add_argument("--project", help="Path to a .yaproject file.")
    control_start.add_argument("--game", action="store_true", help="Run the game view instead of the editor.")
    control_start.add_argument("--editor", action="store_true", help="Run the editor (default).")
    control_start.add_argument("--port", type=int, help="Automation control port; default picks a free one.")
    control_start.add_argument(
        "--lifetime",
        type=float,
        help=f"Wall-clock ceiling in seconds (default {DEFAULT_LIFETIME_S}); 0 disables it.",
    )
    control_start.add_argument(
        "--startup-timeout",
        type=float,
        default=float(DEFAULT_STARTUP_TIMEOUT_S),
        help="Seconds to wait for the instance to publish itself.",
    )
    control_start.add_argument("--build", action="store_true", help="Build before starting.")
    control_start.add_argument("--build-arg", action="append", default=[], help="Extra argument forwarded to `xmake b`.")
    control_start.add_argument("--engine-arg", action="append", default=[], help="Extra argument forwarded to ya-runtime.")
    control_start.set_defaults(func=cmd_control_start)

    control_stop = control_sub.add_parser("stop", help="Quit the matching instance(s), escalating if needed.")
    control_stop.add_argument("--project", help="Only this project.")
    control_stop.add_argument("--editor", action="store_true", help="Only editor instances (default: every mode).")
    control_stop.add_argument("--game", action="store_true", help="Only game instances.")
    control_stop.add_argument("--all", action="store_true", help="Stop every live instance, not just one project's.")
    control_stop.set_defaults(func=cmd_control_stop)

    control_call = control_sub.add_parser("call", help="Call one automation method on the live instance.")
    control_call.add_argument("--project", help="Path to a .yaproject file.")
    control_call.add_argument("--editor", action="store_true", help="Talk to the editor instance (default).")
    control_call.add_argument("--game", action="store_true", help="Talk to the game instance.")
    control_call.add_argument("method", help="Automation method, e.g. eval_js or a registered command name.")
    control_call.add_argument("params", nargs="?", default="{}", help="JSON object of arguments.")
    control_call.set_defaults(func=cmd_control_call)

    control_mcp = control_sub.add_parser("mcp", help="Serve MCP on stdin/stdout for the live instance.")
    control_mcp.add_argument("--project", help="Path to a .yaproject file.")
    control_mcp.add_argument("--editor", action="store_true", help="Talk to the editor instance (default).")
    control_mcp.add_argument("--game", action="store_true", help="Talk to the game instance.")
    control_mcp.set_defaults(func=cmd_control_mcp)

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
