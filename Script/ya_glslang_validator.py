#!/usr/bin/env python3
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


STAGE_MAP = {
    "vertex": "vert",
    "vert": "vert",
    "fragment": "frag",
    "frag": "frag",
    "geometry": "geom",
    "geom": "geom",
    "compute": "comp",
    "comp": "comp",
    "tesc": "tesc",
    "tese": "tese",
    "rgen": "rgen",
    "rint": "rint",
    "rahit": "rahit",
    "rchit": "rchit",
    "rmiss": "rmiss",
    "rcall": "rcall",
    "mesh": "mesh",
    "task": "task",
}

ERROR_RE = re.compile(r"^(ERROR|WARNING):\s+(.+?):(\d+):\s+(.*)$")
TYPE_RE = re.compile(r"^\s*#type\s+(\w+)\s*$")
INCLUDE_RE = re.compile(r'^\s*#include\s+"([^"]+)"\s*$')
STAGE_DEFINE_RE = re.compile(r"\bSHADER_STAGE_(VERTEX|FRAGMENT|GEOMETRY|COMPUTE|TESC|TESE|RGEN|RINT|RAHIT|RCHIT|RMISS|RCALL|MESH|TASK)\b")

STAGE_DEFINE_MAP = {
    "vert": "SHADER_STAGE_VERTEX",
    "frag": "SHADER_STAGE_FRAGMENT",
    "geom": "SHADER_STAGE_GEOMETRY",
    "comp": "SHADER_STAGE_COMPUTE",
    "tesc": "SHADER_STAGE_TESC",
    "tese": "SHADER_STAGE_TESE",
    "rgen": "SHADER_STAGE_RGEN",
    "rint": "SHADER_STAGE_RINT",
    "rahit": "SHADER_STAGE_RAHIT",
    "rchit": "SHADER_STAGE_RCHIT",
    "rmiss": "SHADER_STAGE_RMISS",
    "rcall": "SHADER_STAGE_RCALL",
    "mesh": "SHADER_STAGE_MESH",
    "task": "SHADER_STAGE_TASK",
}


def get_workspace_root() -> Path:
    return Path(__file__).resolve().parent.parent


def get_validator_path() -> str:
    env_path = os.environ.get("YA_GLSLANG_VALIDATOR")
    if env_path:
        return env_path

    validator = shutil.which("glslangValidator") or shutil.which("glslangValidator.exe")
    if validator:
        return validator

    raise FileNotFoundError("glslangValidator executable not found; set YA_GLSLANG_VALIDATOR or add it to PATH")


def split_lines(text: str) -> list[str]:
    if not text:
        return [""]
    return text.splitlines()


def normalize_text(text: str) -> str:
    return text.encode("utf-8", errors="replace").decode("utf-8")


def parse_args(argv: list[str]) -> dict[str, object]:
    forward_args: list[str] = []
    include_dirs: list[str] = []
    stage: str | None = None
    input_file: str | None = None
    use_stdin = False

    index = 0
    while index < len(argv):
        arg = argv[index]
        if arg == "--stdin":
            use_stdin = True
        elif arg == "-S" and index + 1 < len(argv):
            stage = argv[index + 1]
            index += 1
        elif arg.startswith("-I") and arg != "-I":
            include_dirs.append(arg[2:])
        elif arg == "-I" and index + 1 < len(argv):
            include_dirs.append(argv[index + 1])
            index += 1
        elif arg == "-o" and index + 1 < len(argv):
            index += 1
        elif arg.startswith("-"):
            forward_args.append(arg)
        else:
            input_file = arg
        index += 1

    return {
        "forward_args": forward_args,
        "include_dirs": include_dirs,
        "stage": stage,
        "input_file": input_file,
        "use_stdin": use_stdin,
    }


def is_shader_like(source: str) -> bool:
    return bool(re.search(r"^\s*#type\s+\w+", source, re.MULTILINE) or re.search(r"\bvoid\s+main\s*\(", source))


def get_stage_id(stage_name: str) -> str:
    return STAGE_MAP.get(stage_name, stage_name)


def get_stage_blocks(source: str) -> list[dict[str, object]]:
    lines = split_lines(source)
    markers: list[tuple[str, int]] = []
    for idx, line in enumerate(lines, start=1):
        match = TYPE_RE.match(line)
        if match:
            markers.append((match.group(1), idx))

    if not markers:
        return []

    blocks: list[dict[str, object]] = []
    for index, (stage_name, marker_line) in enumerate(markers):
        start_line = marker_line + 1
        end_line = markers[index + 1][1] - 1 if index + 1 < len(markers) else len(lines)
        block_lines = lines[start_line - 1:end_line]
        line_map = list(range(start_line, end_line + 1))
        blocks.append(
            {
                "stage_name": stage_name,
                "stage_id": get_stage_id(stage_name),
                "source": "\n".join(block_lines),
                "line_map": line_map,
            }
        )
    return blocks


def get_macro_stage_blocks(source: str) -> list[dict[str, object]]:
    source_lines = split_lines(source)
    line_map = list(range(1, len(source_lines) + 1))
    found = []
    for match in STAGE_DEFINE_RE.finditer(source):
        stage_name = match.group(1).lower()
        stage_id = get_stage_id(stage_name)
        if stage_id not in found:
            found.append(stage_id)

    blocks: list[dict[str, object]] = []
    for stage_id in found:
        blocks.append(
            {
                "stage_name": stage_id,
                "stage_id": stage_id,
                "source": source,
                "line_map": line_map,
                "stage_define": STAGE_DEFINE_MAP.get(stage_id),
            }
        )
    return blocks


def inject_stage_define(source: str, stage_define: str | None) -> str:
    if not stage_define:
        return source

    lines = split_lines(source)
    insertion = f"#define {stage_define} 1"
    for index, line in enumerate(lines):
        if line.lstrip().startswith("#version"):
            lines.insert(index + 1, insertion)
            return "\n".join(lines)

    return insertion + "\n" + source


def resolve_include_path(include_name: str, current_dir: Path, search_dirs: list[Path]) -> Path:
    for candidate in [current_dir / include_name, *[base / include_name for base in search_dirs]]:
        if candidate.exists():
            return candidate.resolve()
    raise FileNotFoundError(f"include not found: {include_name}")


def resolve_includes(
    source: str,
    current_dir: Path,
    search_dirs: list[Path],
    line_map: list[int],
    visited: set[Path],
) -> tuple[list[str], list[int]]:
    input_lines = split_lines(source)
    output_lines: list[str] = []
    output_map: list[int] = []

    for index, line in enumerate(input_lines):
        mapped_line = line_map[index] if index < len(line_map) else line_map[-1]
        match = INCLUDE_RE.match(line)
        if not match:
            output_lines.append(line)
            output_map.append(mapped_line)
            continue

        include_path = resolve_include_path(match.group(1), current_dir, search_dirs)
        if include_path in visited:
            continue
        visited.add(include_path)

        child_source = include_path.read_text(encoding="utf-8")
        child_lines = split_lines(child_source)
        child_map = [mapped_line] * len(child_lines)
        resolved_lines, resolved_map = resolve_includes(
            child_source,
            include_path.parent,
            search_dirs,
            child_map,
            visited,
        )
        output_lines.extend(resolved_lines)
        output_map.extend(resolved_map)

    return output_lines, output_map


def run_validator(
    validator_path: str,
    forward_args: list[str],
    stage_id: str,
    source: str,
    line_map: list[int],
    stage_define: str | None = None,
) -> tuple[int, list[str]]:
    source = normalize_text(inject_stage_define(source, stage_define))
    with tempfile.NamedTemporaryFile("w", suffix=".glsl", encoding="utf-8", delete=False) as handle:
        handle.write(source)
        tmp_path = Path(handle.name)

    try:
        command = [validator_path, *forward_args, "-S", stage_id, str(tmp_path)]
        completed = subprocess.run(command, capture_output=True, text=True, check=False)
    finally:
        tmp_path.unlink(missing_ok=True)

    messages: list[str] = []
    combined_output = completed.stdout.splitlines() + completed.stderr.splitlines()
    for raw_line in combined_output:
        match = ERROR_RE.match(raw_line)
        if not match:
            continue
        severity, _path, line_no, message = match.groups()
        mapped_line = int(line_no)
        if 1 <= mapped_line <= len(line_map):
            mapped_line = line_map[mapped_line - 1]
        messages.append(f"{severity}: stdin:{mapped_line}: [{stage_id}] {message}")
    return completed.returncode, messages


def main(argv: list[str]) -> int:
    parsed = parse_args(argv)
    workspace_root = get_workspace_root()
    validator_path = get_validator_path()

    if parsed["use_stdin"]:
        source = sys.stdin.read()
    elif parsed["input_file"]:
        source = Path(str(parsed["input_file"])).read_text(encoding="utf-8")
    else:
        source = ""

    source = normalize_text(source)

    if not source or not is_shader_like(source):
        return 0

    include_dirs = [workspace_root / "Engine/Shader/GLSL"]
    include_dirs.extend(Path(path) for path in parsed["include_dirs"] if path)

    input_file = parsed["input_file"]
    if input_file:
        current_dir = Path(str(input_file)).resolve().parent
    elif parsed["include_dirs"]:
        current_dir = Path(str(parsed["include_dirs"][0])).resolve()
    else:
        current_dir = workspace_root

    stage_blocks = get_stage_blocks(source)
    if not stage_blocks:
        stage_blocks = get_macro_stage_blocks(source)
    if not stage_blocks:
        source_lines = split_lines(source)
        stage_id = str(parsed["stage"] or "vert")
        stage_blocks = [
            {
                "stage_name": stage_id,
                "stage_id": stage_id,
                "source": source,
                "line_map": list(range(1, len(source_lines) + 1)),
                "stage_define": None,
            }
        ]

    requested_stage = str(parsed["stage"] or "")
    requested_stage_id = get_stage_id(requested_stage) if requested_stage else ""
    if len(stage_blocks) > 1:
        selected_blocks = stage_blocks
    else:
        selected_blocks = [
            block for block in stage_blocks if not requested_stage_id or str(block["stage_id"]) == requested_stage_id
        ]
    if not selected_blocks:
        selected_blocks = stage_blocks

    all_messages: list[str] = []
    has_failure = False

    for block in selected_blocks:
        resolved_lines, resolved_map = resolve_includes(
            str(block["source"]),
            current_dir,
            include_dirs,
            list(block["line_map"]),
            set(),
        )
        exit_code, messages = run_validator(
            validator_path,
            list(parsed["forward_args"]),
            str(block["stage_id"]),
            "\n".join(resolved_lines),
            resolved_map,
            str(block.get("stage_define") or "") or None,
        )
        all_messages.extend(messages)
        if exit_code != 0:
            has_failure = True

    for message in all_messages:
        print(message)

    return 2 if has_failure else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))