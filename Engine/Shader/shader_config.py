#!/usr/bin/env python3
"""Utilities for reading shader config from JSONC and generating limit headers."""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Any

try:
    import json5
except ImportError as exc:
    raise SystemExit(
        "Missing Python dependency 'json5'. Install it with: python -m pip install -r requirements.txt"
    ) from exc

def loads_jsonc(content: str) -> Any:
    return json5.loads(content)


def load_jsonc_file(path: str | Path) -> dict[str, Any]:
    config_path = Path(path)
    return loads_jsonc(config_path.read_text(encoding="utf-8"))


def extract_shader_defines(path: str | Path) -> dict[str, int | float | str | bool]:
    cfg = load_jsonc_file(path)
    return dict(cfg.get("shader", {}).get("defines", {}))


def _to_define_literal(value: int | float | str | bool) -> str:
    if isinstance(value, bool):
        return "1" if value else "0"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float) and value.is_integer():
        return str(int(value))
    return str(value)


def build_limits_content(config_path: str | Path) -> str:
    defines = extract_shader_defines(config_path)
    header_lines = [
        f"// Auto-generated from {config_path} - DO NOT EDIT.",
        "// Modify Engine.jsonc shader.defines instead.",
        "",
    ]
    for name, value in defines.items():
        header_lines.append(f"#undef {name}")
        header_lines.append(f"#define {name} {_to_define_literal(value)}")
    header_lines.append("")
    return "\n".join(header_lines)


def write_if_changed(path: str | Path, content: str) -> bool:
    output_path = Path(path)
    existing = output_path.read_text(encoding="utf-8") if output_path.exists() else None
    if existing == content:
        return False
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(content, encoding="utf-8", newline="\n")
    return True


def sync_limits_files(config_path: str | Path, glsl_path: str | Path, slang_path: str | Path) -> None:
    content = build_limits_content(config_path)
    if write_if_changed(glsl_path, content):
        print(f"[shader-config] Generated {glsl_path}")
    if write_if_changed(slang_path, content):
        print(f"[shader-config] Generated {slang_path}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate shader limits headers from Engine.jsonc")
    parser.add_argument("--config", required=True, help="Path to Engine.jsonc")
    parser.add_argument("--glsl-output", required=True, help="Path to generated GLSL limits file")
    parser.add_argument("--slang-output", required=True, help="Path to generated Slang limits file")
    args = parser.parse_args()

    sync_limits_files(args.config, args.glsl_output, args.slang_output)


if __name__ == "__main__":
    main()