#!/usr/bin/env python3
"""
slang_gen_header.py — Generate C++ headers from Slang reflection JSON.

Usage:
    python slang_gen_header.py <input.slang> <output_dir> [--namespace <ns>] [--entry <name>]

Steps:
    1. Run slangc to produce reflection JSON (+ throwaway .spv).
    2. Parse the JSON, collect struct definitions in dependency order.
    3. Emit a C++ header with alignas(16) + static_assert(sizeof/offsetof) guards.

The script is incremental: if the output header is newer than the .slang source,
it will be skipped (pass --force to override).
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

_builtin_print = print
def print(*args, **kwargs):
    filename = sys._getframe(1).f_code.co_filename
    lineno = sys._getframe(1).f_lineno
    try:
        rel = os.path.relpath(filename)
    except ValueError:
        rel = filename
    return _builtin_print(f"{rel}:{lineno}", *args, **kwargs)

# ---------------------------------------------------------------------------
# Slang scalar type -> C++ type mapping
# ---------------------------------------------------------------------------
SCALAR_MAP = {
    "float32": "float",
    "float16": "uint16_t",
    "int32":   "int32_t",
    "uint32":  "uint32_t",
    "int16":   "int16_t",
    "uint16":  "uint16_t",
    "int8":    "int8_t",
    "uint8":   "uint8_t",
    "bool":    "uint32_t",  # GLSL bool is 4 bytes in std140
}

# Maps struct name -> total byte size (from elementVarLayout or uniformStride)
STRUCT_SIZES: dict[str, int] = {}

# Maps macro name -> int value, extracted from .slang source
SLANG_DEFINES: dict[str, int] = {}

# Reverse map: int value -> list of macro names (for array size substitution)
_VALUE_TO_DEFINES: dict[int, list[str]] = {}


def extract_defines(slang_source: str):
    """Extract #define NAME <integer> from .slang source code.

    Populates SLANG_DEFINES and _VALUE_TO_DEFINES.
    Only simple integer literal defines are supported (no expressions).
    """
    SLANG_DEFINES.clear()
    _VALUE_TO_DEFINES.clear()
    # Match: #define IDENTIFIER integer_literal
    # Supports decimal, hex (0x...), octal (0...) literals
    pattern = re.compile(
        r'^\s*#\s*define\s+([A-Z_][A-Z0-9_]*)\s+((?:0[xX][0-9a-fA-F]+)|(?:0[0-7]*)|(?:[1-9][0-9]*))\s*$',
        re.MULTILINE,
    )
    for m in pattern.finditer(slang_source):
        name = m.group(1)
        value = int(m.group(2), 0)  # auto-detect base
        SLANG_DEFINES[name] = value
        _VALUE_TO_DEFINES.setdefault(value, []).append(name)


# GLM vector type prefix for each scalar base type
_GLM_VEC_PREFIX = {
    "float32": "",      # glm::vec2, glm::vec3, glm::vec4
    "int32":   "i",     # glm::ivec2, glm::ivec3, glm::ivec4
    "uint32":  "u",     # glm::uvec2, glm::uvec3, glm::uvec4
    "float64": "d",     # glm::dvec2 ...
    "bool":    "b",     # glm::bvec2 ...
}

# GLM matrix type prefix for each scalar base type
_GLM_MAT_PREFIX = {
    "float32": "",      # glm::mat3, glm::mat4
    "float64": "d",     # glm::dmat3 ...
}


def slang_type_to_cpp(type_node: dict) -> tuple[str, str]:
    """Convert a Slang type node to (cpp_type, array_suffix).

    Vectors and matrices are mapped to glm types (e.g. glm::vec3, glm::mat4)
    whenever a known GLM prefix exists for the scalar base type.
    """
    kind = type_node.get("kind", "")
    if kind == "scalar":
        return SCALAR_MAP.get(type_node["scalarType"], "uint32_t"), ""
    elif kind == "vector":
        scalar = type_node["elementType"]["scalarType"]
        count = type_node["elementCount"]
        prefix = _GLM_VEC_PREFIX.get(scalar)
        if prefix is not None:
            return f"glm::{prefix}vec{count}", ""
        # Fallback to raw array for exotic scalar types
        base = SCALAR_MAP.get(scalar, "uint32_t")
        return base, f'[{count}]'
    elif kind == "matrix":
        scalar = type_node["elementType"]["scalarType"]
        cols = type_node["columnCount"]
        rows = type_node["rowCount"]
        prefix = _GLM_MAT_PREFIX.get(scalar)
        if prefix is not None:
            # std140 layout: each column is padded to vec4 (4 elements).
            # When rows < 4, we must use glm::matCx4 so the C++ memory layout
            # matches the GPU's column-padded representation.
            # e.g. mat3 (3x3) -> glm::mat3x4 (3 columns of vec4 = 48 bytes)
            std140_rows = 4 if rows < 4 else rows
            if cols == std140_rows:
                return f"glm::{prefix}mat{cols}", ""
            else:
                return f"glm::{prefix}mat{cols}x{std140_rows}", ""
        # Fallback to raw array
        base = SCALAR_MAP.get(scalar, "float")
        return base, f'[{cols}][{rows}]'
    elif kind == "array":
        base, suffix = slang_type_to_cpp(type_node["elementType"])
        count = type_node["elementCount"]
        # Try to substitute the literal count with a macro name if one matches
        count_str = str(count)
        candidates = _VALUE_TO_DEFINES.get(count, [])
        if len(candidates) == 1:
            count_str = candidates[0]
        elif len(candidates) > 1:
            # Multiple macros have the same value; keep literal to avoid ambiguity
            pass
        return base, f'[{count_str}]{suffix}'
    elif kind == "struct":
        return type_node["name"], ""
    return "uint8_t", ""


def collect_structs(type_node: dict, visited: set, ordered: list):
    """Recursively collect all struct definitions in dependency order."""
    if type_node is None:
        return
    kind = type_node.get("kind", "")
    # Unwrap constantBuffer to get the inner elementType struct
    if kind == "constantBuffer":
        # Capture struct total size from elementVarLayout if available
        evl = type_node.get("elementVarLayout", {})
        evl_size = evl.get("binding", {}).get("size")
        evl_type = evl.get("type", {})
        if evl_type.get("kind") == "struct" and evl_size is not None:
            STRUCT_SIZES[evl_type["name"]] = evl_size
        elem = type_node.get("elementType")
        if elem:
            collect_structs(elem, visited, ordered)
        return
    if kind == "array":
        # Capture struct total size from uniformStride if available
        stride = type_node.get("uniformStride")
        elem = type_node.get("elementType")
        if elem and elem.get("kind") == "struct" and stride is not None:
            STRUCT_SIZES[elem["name"]] = stride
        if elem:
            collect_structs(elem, visited, ordered)
        return
    if kind in ("vector", "matrix"):
        elem = type_node.get("elementType")
        if elem:
            collect_structs(elem, visited, ordered)
        return
    if kind != "struct":
        return
    name = type_node["name"]
    if name in visited:
        return
    # Capture nested struct occupied size from parent-field reflection.
    # Example: a struct may end at byte 36 by its last field, but when embedded
    # in std140 it can occupy 48 bytes due to trailing padding/alignment.
    for field in type_node.get("fields", []):
        field_type = field.get("type", {})
        binding = field.get("binding", {})
        if field_type.get("kind") == "struct" and "size" in binding:
            field_struct_name = field_type.get("name")
            if field_struct_name:
                STRUCT_SIZES[field_struct_name] = max(
                    STRUCT_SIZES.get(field_struct_name, 0),
                    binding["size"],
                )
    # Recurse into field types first (dependencies before dependents)
    for field in type_node.get("fields", []):
        collect_structs(field["type"], visited, ordered)
    visited.add(name)
    ordered.append(type_node)


def is_uniform_struct(struct_node: dict) -> bool:
    """Check if a struct has uniform binding fields (offset/size).
    Structs like VSInput/VSOutput use varyingInput/varyingOutput bindings
    and should not be emitted as C++ structs."""
    fields = struct_node.get("fields", [])
    if not fields:
        return False
    for field in fields:
        binding = field.get("binding", {})
        if "offset" in binding and "size" in binding:
            return True
    return False


def gen_struct(struct_node: dict) -> str:
    """Generate C++ struct definition with auto-padding and static_assert guards."""
    name = struct_node["name"]
    fields = struct_node.get("fields", [])

    # Determine total size: prefer STRUCT_SIZES (from elementVarLayout/uniformStride),
    # otherwise compute from last field.
    if name in STRUCT_SIZES:
        total_size = STRUCT_SIZES[name]
    elif fields:
        last = fields[-1]
        total_size = last["binding"]["offset"] + last["binding"]["size"]
    else:
        total_size = 0

    lines = []
    # Only enforce alignas(16) when the struct's total size is a multiple of 16
    # (i.e. std140 ConstantBuffer structs). For smaller/non-aligned structs
    # (e.g. bare uniforms), let C++ use natural alignment so sizeof matches.
    if total_size > 0 and total_size % 16 == 0:
        lines.append(f"struct alignas(16) {name}")
    else:
        lines.append(f"struct {name}")
    lines.append("{")

    cursor = 0  # tracks the current byte position
    pad_idx = 0

    for field in fields:
        offset = field["binding"]["offset"]
        size = field["binding"]["size"]

        # Insert padding if there's a gap between cursor and this field's offset
        if offset > cursor:
            gap = offset - cursor
            lines.append(f"    uint8_t _pad{pad_idx}[{gap}];")
            pad_idx += 1

        cpp_type, suffix = slang_type_to_cpp(field["type"])
        lines.append(f"    {cpp_type} {field['name']}{suffix};")
        cursor = offset + size

    # Tail padding: if total_size > cursor, add padding at the end
    if total_size > cursor:
        gap = total_size - cursor
        lines.append(f"    uint8_t _pad{pad_idx}[{gap}];")

    lines.append("};")

    lines.append(f'static_assert(sizeof({name}) == {total_size}, "Size mismatch for {name}");')
    for field in fields:
        offset = field["binding"]["offset"]
        fname = field["name"]
        lines.append(
            f'static_assert(SLANG_OFFSETOF({name}, {fname}) == {offset}, '
            f'"Offset mismatch for {name}::{fname}");'
        )
    lines.append("")
    return "\n".join(lines)


def generate_header(json_path: str, output_path: str, namespace: str, slang_source: str):
    """Parse reflection JSON and emit C++ header."""
    # Extract #define macros from .slang source before generating
    extract_defines(slang_source)

    with open(json_path, "r", encoding="utf-8") as f:
        data = json.load(f)

    visited = set()
    ordered = []
    # Scan top-level parameters (ConstantBuffers, push constants, etc.)
    for param in data.get("parameters", []):
        collect_structs(param.get("type"), visited, ordered)
    # Also scan entry point parameters (vertex inputs, etc.)
    for ep in data.get("entryPoints", []):
        for param in ep.get("parameters", []):
            collect_structs(param.get("type"), visited, ordered)

    # Filter: only emit structs with uniform bindings (offset/size)
    ordered = [s for s in ordered if is_uniform_struct(s)]

    lines = []
    lines.append("// Auto-generated by slang_gen_header.py")
    lines.append("// DO NOT EDIT — modify the corresponding .slang file instead.")
    lines.append("#pragma once")
    lines.append("#include <cstdint>")
    lines.append("#include <cstddef>")
    lines.append("#include <glm/glm.hpp>")
    lines.append("")
    lines.append("// Use __builtin_offsetof to ensure constexpr-compatible offsetof")
    lines.append("// (MSVC's reinterpret_cast-based offsetof is not a constant expression in clangd)")
    lines.append("#ifdef __clang__")
    lines.append("  #define SLANG_OFFSETOF(s, m) __builtin_offsetof(s, m)")
    lines.append("#else")
    lines.append("  #define SLANG_OFFSETOF(s, m) offsetof(s, m)")
    lines.append("#endif")
    lines.append("")
    if namespace:
        lines.append(f"namespace {namespace} {{")
        lines.append("")

    # Emit constexpr constants extracted from .slang #define macros
    if SLANG_DEFINES:
        lines.append("// Constants extracted from .slang source #define macros")
        for macro_name, macro_val in SLANG_DEFINES.items():
            lines.append(f"constexpr uint32_t {macro_name} = {macro_val};")
        lines.append("")
    for struct_node in ordered:
        lines.append(gen_struct(struct_node))
    if namespace:
        lines.append(f"}} // namespace {namespace}")
    lines.append("")
    lines.append("#ifdef __clang__")
    lines.append("  #undef SLANG_OFFSETOF")
    lines.append("#endif")

    with open(output_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


def _file_sub_namespace(slang_file: Path, slang_root: Path | None) -> tuple[str, str]:
    """Derive a dot-separated file stem and C++ sub-namespace from the .slang file path.

    Uses the path relative to *slang_root* (if provided) to build a hierarchy.

    Examples (slang_root = Engine/Shader/Slang):
        DeferredRender/GBufferPass.slang  -> ("DeferredRender.GBufferPass", "DeferredRender::GBufferPass")
        PhongLit.slang                    -> ("PhongLit",                   "PhongLit")
        Common/Helper.slang               -> ("Common.Helper",              "Common::Helper")
    """
    if slang_root is not None:
        try:
            rel = slang_file.resolve().relative_to(slang_root.resolve())
        except ValueError:
            rel = Path(slang_file.stem)
    else:
        rel = Path(slang_file.stem)

    # rel is e.g. DeferredRender/GBufferPass.slang — drop the .slang suffix
    parts = list(rel.parent.parts) + [rel.stem]
    dot_stem = ".".join(parts)        # DeferredRender.GBufferPass
    ns_suffix = "::".join(parts)      # DeferredRender::GBufferPass
    return dot_stem, ns_suffix


def _detect_entry_and_stage(slang_source: str, preferred_entry: str) -> tuple[str, str] | None:
    """Detect a valid (entry, stage) pair from slang source.

    Priority:
      1) The user-provided preferred entry name if present in source.
      2) Common defaults (vertMain/fragMain/geomMain/compMain).
      3) Any [shader("stage")] function declaration discovered by regex.
    """
    stage_by_entry = {
        "vertMain": "vertex",
        "fragMain": "fragment",
        "geomMain": "geometry",
        "compMain": "compute",
    }

    def has_func(name: str) -> bool:
        return re.search(rf"\b{re.escape(name)}\s*\(", slang_source) is not None

    if preferred_entry in stage_by_entry and has_func(preferred_entry):
        return preferred_entry, stage_by_entry[preferred_entry]

    for entry, stage in stage_by_entry.items():
        if has_func(entry):
            return entry, stage

    # Generic fallback: match [shader("...")] <ret> <name>(...)
    # Supports simple signatures used in this repo.
    pattern = re.compile(
        r"\[\s*shader\s*\(\s*\"(?P<stage>[a-zA-Z_]+)\"\s*\)\s*\]"
        r"\s*[\w:<>\[\]\s]+?\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\(",
        re.MULTILINE,
    )
    for m in pattern.finditer(slang_source):
        stage = m.group("stage").lower()
        name = m.group("name")
        if stage in {"vertex", "fragment", "geometry", "compute"}:
            return name, stage

    return None


def _process_one_slang(
    slang_file: Path,
    output_dir: Path,
    namespace: str,
    entry: str,
    force: bool,
    include_dirs: list[Path] | None = None,
    slang_root: Path | None = None,
):
    dot_stem, ns_suffix = _file_sub_namespace(slang_file, slang_root)
    basename = slang_file.stem  # still used for slangc intermediate files
    json_path = output_dir / f"{basename}.reflection.json"
    header_path = output_dir / f"{dot_stem}.slang.h"
    stamp_path = output_dir / f"{basename}.stamp"
    spv_path = output_dir / f"{basename}.spv"
    slang_source = slang_file.read_text(encoding="utf-8")

    if "@gen-skip" in slang_source:
        print(f"[slang-gen] {basename} marked with @gen-skip, skipping.")
        stamp_path.touch()
        return

    entry_stage = _detect_entry_and_stage(slang_source, entry)
    if entry_stage is None:
        print(f"[slang-gen] {basename} has no usable shader entry, skipping.")
        stamp_path.touch()
        return
    resolved_entry, resolved_stage = entry_stage

    src_mtime = slang_file.stat().st_mtime
    if not force:
        # Skip if header is newer than source (success path)
        if header_path.exists() and src_mtime <= header_path.stat().st_mtime:
            # print(f"[slang-gen] {basename} is up-to-date, skipping.")
            return
        # Skip if stamp is newer than source (previous attempt unchanged, e.g. known slangc failure)
        if stamp_path.exists() and src_mtime <= stamp_path.stat().st_mtime:
            print(f"[slang-gen] {basename} unchanged since last attempt, skipping.")
            return

    output_dir.mkdir(parents=True, exist_ok=True)
    print(f"[slang-gen] Generating C++ header for {slang_file} ...")

    cmd = [
        "slangc",
        str(slang_file),
        "-reflection-json", str(json_path),
        "-entry", resolved_entry,
        "-stage", resolved_stage,
        "-target", "spirv",
        "-o", str(spv_path),
    ]
    # Prefer compiler-native include search paths (-I)
    for inc in (include_dirs or []):
        cmd.extend(["-I", str(inc)])
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"WARNING: slangc failed (exit {result.returncode}) for {slang_file}, skipping header gen:", file=sys.stderr)
        print(f"  cmd: {' '.join(cmd)}", file=sys.stderr)
        if result.stdout.strip():
            print(result.stdout, file=sys.stdout)
        if result.stderr.strip():
            print(result.stderr, file=sys.stderr)
        else:
            print("  (slangc produced no output — try running the cmd above manually)", file=sys.stderr)
        # Write stamp so next build skips this file if source hasn't changed
        stamp_path.touch()
        return

    full_ns = f"{namespace}::{ns_suffix}" if ns_suffix else namespace
    generate_header(str(json_path), str(header_path), full_ns, slang_source)
    print(f"[slang-gen] Generated: {header_path}")


def main():
    parser = argparse.ArgumentParser(
        description="Generate C++ headers from Slang reflection JSON."
    )
    parser.add_argument("inputs", nargs="+", help="Path to .slang source file(s)")
    parser.add_argument("--output-dir", required=True, help="Directory for generated files")
    parser.add_argument("--namespace", default="ya::slang_types",
                        help="C++ namespace for generated types (default: ya::slang_types)")
    parser.add_argument("--entry", default="vertMain",
                        help="Entry point name for reflection (default: vertMain)")
    parser.add_argument("--include-dir", action="append", dest="include_dirs", metavar="DIR",
                        help="Extra include directory passed to slangc via -I (repeatable)")
    parser.add_argument("--slang-root", default=None,
                        help="Root directory of .slang sources (used to compute relative path for namespaces)")
    parser.add_argument("--force", action="store_true",
                        help="Force regeneration even if header is up-to-date")
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    include_dirs = [Path(d) for d in (args.include_dirs or [])]
    slang_root = Path(args.slang_root) if args.slang_root else None
    t_start = time.perf_counter()
    count = 0
    for input_path in args.inputs:
        slang_file = Path(input_path)
        if not slang_file.exists():
            print(f"ERROR: Slang source not found: {slang_file}", file=sys.stderr)
            continue
        _process_one_slang(slang_file, output_dir, args.namespace, args.entry, args.force, include_dirs, slang_root)
        count += 1

    elapsed_ms = int((time.perf_counter() - t_start) * 1000)
    print(f"[slang-gen] {count} file(s) processed in {elapsed_ms}ms")


if __name__ == "__main__":
    main()
