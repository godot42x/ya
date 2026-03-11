#!/usr/bin/env python3
"""
glsl_gen_header.py — Generate C++ headers from GLSL shader files (std140 layout).

Usage:
    python glsl_gen_header.py <input.glsl> [<more.glsl> ...] \
        --output-dir <dir> [--namespace <ns>] [--force]

Parses GLSL struct definitions and std140 uniform blocks from one or more
GLSL files (resolving #include relative to each file's directory), then
emits a single C++ header that:
  - declares each struct with alignas(16) and explicit padding fields
  - emits static_assert size/offset guards for every member

The generated header format matches what slang_gen_header.py produces so
that C++ consumer code can switch between backends without changes.
"""

import argparse
import os
import re
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
# std140 type table: glsl_name -> (cpp_type, alignment, size_bytes)
# ---------------------------------------------------------------------------
# Matrices in std140: each column padded to vec4 (4 floats = 16 bytes)
_STD140: dict[str, tuple[str, int, int]] = {
    # scalars
    "float":  ("float",      4,  4),
    "int":    ("int32_t",    4,  4),
    "uint":   ("uint32_t",   4,  4),
    "bool":   ("uint32_t",   4,  4),  # bool is 4 bytes in std140
    # vec2 / ivec2 / uvec2
    "vec2":   ("glm::vec2",  8,  8),
    "ivec2":  ("glm::ivec2", 8,  8),
    "uvec2":  ("glm::uvec2", 8,  8),
    "bvec2":  ("glm::bvec2", 8,  8),
    # vec3 / ivec3 / uvec3  — base alignment 16, but only 12 bytes
    "vec3":   ("glm::vec3",  16, 12),
    "ivec3":  ("glm::ivec3", 16, 12),
    "uvec3":  ("glm::uvec3", 16, 12),
    "bvec3":  ("glm::bvec3", 16, 12),
    # vec4 variants
    "vec4":   ("glm::vec4",  16, 16),
    "ivec4":  ("glm::ivec4", 16, 16),
    "uvec4":  ("glm::uvec4", 16, 16),
    "bvec4":  ("glm::bvec4", 16, 16),
    # matrices (column-major, each col padded to vec4)
    "mat2":   ("glm::mat2",   16, 32),  # 2 cols × 16
    "mat3":   ("glm::mat3x4", 16, 48),  # 3 cols × 16  — use mat3x4 for correct layout
    "mat4":   ("glm::mat4",   16, 64),  # 4 cols × 16
    "mat2x2": ("glm::mat2",   16, 32),
    "mat2x3": ("glm::mat2x4", 16, 32),  # 2 cols of vec4
    "mat2x4": ("glm::mat2x4", 16, 32),
    "mat3x2": ("glm::mat3x4", 16, 48),
    "mat3x3": ("glm::mat3x4", 16, 48),
    "mat3x4": ("glm::mat3x4", 16, 48),
    "mat4x2": ("glm::mat4",   16, 64),
    "mat4x3": ("glm::mat4",   16, 64),
    "mat4x4": ("glm::mat4",   16, 64),
}

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _align_up(offset: int, align: int) -> int:
    return (offset + align - 1) & ~(align - 1)


def _strip_comments(src: str) -> str:
    """Remove // line comments and /* block comments */ from GLSL source."""
    # Block comments first
    src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.DOTALL)
    # Line comments
    src = re.sub(r'//[^\n]*', '', src)
    return src


def _extract_defines(src: str) -> dict[str, int]:
    """Extract #define NAME integer_literal from source."""
    defines: dict[str, int] = {}
    pattern = re.compile(
        r'^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\s+'
        r'((?:0[xX][0-9a-fA-F]+)|(?:0[0-7]*)|(?:[1-9][0-9]*))\s*$',
        re.MULTILINE,
    )
    for m in pattern.finditer(src):
        defines[m.group(1)] = int(m.group(2), 0)
    return defines


def _eval_array_size(expr: str, defines: dict[str, int]) -> int | str:
    """Try to evaluate a constant array-size expression. Returns int or the original string."""
    expr = expr.strip()
    # Simple macro name
    if expr in defines:
        return defines[expr]
    # Try substituting all known macros then eval
    substituted = expr
    for name, val in sorted(defines.items(), key=lambda kv: -len(kv[0])):
        substituted = substituted.replace(name, str(val))
    try:
        result = eval(substituted, {"__builtins__": {}})  # noqa: S307 — arithmetic only
        return int(result)
    except Exception:
        return expr  # keep as-is


# ---------------------------------------------------------------------------
# GLSL struct / uniform-block parser
# ---------------------------------------------------------------------------

# A parsed field: (glsl_type, cpp_name, array_size_or_None)
Field = tuple[str, str, int | str | None]

# A parsed struct: name -> list of Field
StructMap = dict[str, list[Field]]


def _parse_fields(body: str, defines: dict[str, int]) -> list[Field]:
    """Parse the body of a struct or uniform block into a list of Fields."""
    fields: list[Field] = []
    # Each statement: <type> <name>[<size>]; — allow whitespace variations
    stmt_re = re.compile(
        r'\b([A-Za-z_][A-Za-z0-9_]*(?:\s*\[\s*\d+\s*\])*)\s+'  # type (with optional old-style array dim)
        r'([A-Za-z_][A-Za-z0-9_]*)'                              # field name
        r'(?:\s*\[\s*([^\]]+?)\s*\])?'                           # optional array [size]
        r'\s*;',
        re.DOTALL,
    )
    for m in stmt_re.finditer(body):
        glsl_type = m.group(1).strip()
        name      = m.group(2).strip()
        arr_expr  = m.group(3)
        array_size = _eval_array_size(arr_expr, defines) if arr_expr is not None else None
        fields.append((glsl_type, name, array_size))
    return fields


def _parse_structs(src: str, defines: dict[str, int]) -> StructMap:
    """Extract all `struct Name { ... };` definitions."""
    structs: StructMap = {}
    struct_re = re.compile(
        r'\bstruct\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{([^}]*)\}\s*;',
        re.DOTALL,
    )
    for m in struct_re.finditer(src):
        name = m.group(1)
        body = m.group(2)
        structs[name] = _parse_fields(body, defines)
    return structs


def _parse_uniform_blocks(src: str, defines: dict[str, int]) -> list[tuple[str, list[Field]]]:
    """Extract `layout(...) uniform BlockName { ... } instanceName;`."""
    blocks: list[tuple[str, list[Field]]] = []
    # Match layout qualifier containing std140 (or any layout, we treat all as std140)
    block_re = re.compile(
        r'layout\s*\([^)]*\)\s*uniform\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{([^}]*)\}',
        re.DOTALL,
    )
    for m in block_re.finditer(src):
        name = m.group(1)
        body = m.group(2)
        blocks.append((name, _parse_fields(body, defines)))
    return blocks


# ---------------------------------------------------------------------------
# Resolve includes
# ---------------------------------------------------------------------------

def _load_source(path: Path, visited: set[Path]) -> str:
    """Load a GLSL file, recursively resolving #include directives."""
    if path in visited:
        return ""
    visited.add(path)
    raw = path.read_text(encoding="utf-8")
    # Remove stage markers (#type vertex / #type geometry …)
    raw = re.sub(r'#\s*type\s+\w+', '', raw)
    # Resolve includes
    def replace_include(m: re.Match) -> str:
        inc_name = m.group(1)
        inc_path = path.parent / inc_name
        if inc_path.exists():
            return _load_source(inc_path, visited)
        return f"// include not found: {inc_name}\n"
    combined = re.sub(r'#\s*include\s+"([^"]+)"', replace_include, raw)
    return combined


# ---------------------------------------------------------------------------
# std140 layout computation
# ---------------------------------------------------------------------------

# Layout result per field
LayoutField = dict  # keys: name, cpp_type, offset, size, array_count, padding


def _resolve_type(glsl_type: str, struct_map: StructMap) -> tuple[str, int, int]:
    """Return (cpp_type, alignment, size_bytes) for a GLSL type."""
    if glsl_type in _STD140:
        return _STD140[glsl_type]
    if glsl_type in struct_map:
        # Compute struct alignment/size on demand
        _, align, size = _compute_struct_layout(struct_map[glsl_type], struct_map)
        return glsl_type, align, size
    # Unknown — fall back to uint32_t
    print(f"WARNING: unknown GLSL type '{glsl_type}', treated as uint32_t", file=sys.stderr)
    return "uint32_t", 4, 4


def _compute_struct_layout(
    fields: list[Field],
    struct_map: StructMap,
) -> tuple[list[LayoutField], int, int]:
    """
    Apply std140 rules and return (layout_fields, struct_alignment, struct_size).

    struct_alignment = max(16, max member alignment)
    struct_size      = align_up(raw_size, struct_alignment)
    """
    layout: list[LayoutField] = []
    offset = 0
    struct_align = 16  # std140: struct base alignment is at least 16

    for glsl_type, name, array_size in fields:
        cpp_type, align, size = _resolve_type(glsl_type, struct_map)
        struct_align = max(struct_align, align)

        if array_size is not None:
            # std140 array: element alignment = max(member_align, 16)
            elem_align = max(align, 16)
            elem_size  = _align_up(size, 16)  # each element padded to multiple of 16
            offset = _align_up(offset, elem_align)
            total = elem_size * (array_size if isinstance(array_size, int) else 1)
            layout.append({
                "name":        name,
                "cpp_type":    cpp_type,
                "offset":      offset,
                "size":        total,
                "array_count": array_size,
                "elem_size":   elem_size,
                "padding":     0,
            })
            offset += total
        else:
            offset = _align_up(offset, align)
            layout.append({
                "name":        name,
                "cpp_type":    cpp_type,
                "offset":      offset,
                "size":        size,
                "array_count": None,
                "padding":     0,
            })
            offset += size

    struct_size = _align_up(offset, struct_align)
    return layout, struct_align, struct_size


# ---------------------------------------------------------------------------
# Topological sort of structs (dependencies before dependents)
# ---------------------------------------------------------------------------

def _topo_sort(structs: StructMap) -> list[str]:
    visited: set[str] = set()
    order:   list[str] = []

    def visit(name: str):
        if name in visited:
            return
        visited.add(name)
        for glsl_type, _, _ in structs.get(name, []):
            if glsl_type in structs:
                visit(glsl_type)
        order.append(name)

    for name in structs:
        visit(name)
    return order


# ---------------------------------------------------------------------------
# C++ header emission
# ---------------------------------------------------------------------------

def _cpp_array_suffix(array_count: int | str | None, defines: dict[str, int]) -> str:
    if array_count is None:
        return ""
    # Try to use a macro name if count matches a define value
    if isinstance(array_count, int):
        candidates = [k for k, v in defines.items() if v == array_count]
        if len(candidates) == 1:
            return f"[{candidates[0]}]"
    return f"[{array_count}]"


def _emit_header(
    structs: StructMap,
    defines: dict[str, int],
    namespace: str,
    source_files: list[str],
) -> str:
    lines: list[str] = []

    files_str = ", ".join(source_files)
    lines += [
        f"// Auto-generated by glsl_gen_header.py from: {files_str}",
        "// DO NOT EDIT — modify the corresponding .glsl file instead.",
        "#pragma once",
        "#include <cstdint>",
        "#include <cstddef>",
        "#include <glm/glm.hpp>",
        "",
        "#ifdef __clang__",
        "  #define GLSL_OFFSETOF(s, m) __builtin_offsetof(s, m)",
        "#else",
        "  #define GLSL_OFFSETOF(s, m) offsetof(s, m)",
        "#endif",
        "",
        f"namespace {namespace} {{",
        "",
    ]

    # Constants
    if defines:
        lines.append("// Constants from #define macros")
        for name, val in defines.items():
            lines.append(f"constexpr uint32_t {name} = {val};")
        lines.append("")

    # Structs in dependency order
    order = _topo_sort(structs)
    for struct_name in order:
        fields = structs[struct_name]
        layout, struct_align, struct_size = _compute_struct_layout(fields, structs)

        lines.append(f"struct alignas(16) {struct_name}")
        lines.append("{")

        pad_idx = 0
        for i, lf in enumerate(layout):
            name      = lf["name"]
            cpp_type  = lf["cpp_type"]
            arr_count = lf["array_count"]
            offset    = lf["offset"]
            size      = lf["size"]

            arr_suffix = _cpp_array_suffix(arr_count, defines)
            lines.append(f"    {cpp_type} {name}{arr_suffix};")

            # Insert explicit padding if there's a gap before the next field
            if i + 1 < len(layout):
                next_offset = layout[i + 1]["offset"]
                gap = next_offset - (offset + size)
                if gap > 0:
                    lines.append(f"    uint8_t _pad{pad_idx}[{gap}];")
                    pad_idx += 1
            else:
                # Trailing padding to reach struct_size
                gap = struct_size - (offset + size)
                if gap > 0:
                    lines.append(f"    uint8_t _pad{pad_idx}[{gap}];")

        lines.append("};")
        lines.append(f'static_assert(sizeof({struct_name}) == {struct_size}, "Size mismatch for {struct_name}");')
        for lf in layout:
            if lf["array_count"] is not None:
                lines.append(
                    f'static_assert(GLSL_OFFSETOF({struct_name}, {lf["name"]}) == {lf["offset"]}, '
                    f'"Offset mismatch for {struct_name}::{lf["name"]}");'
                )
            else:
                lines.append(
                    f'static_assert(GLSL_OFFSETOF({struct_name}, {lf["name"]}) == {lf["offset"]}, '
                    f'"Offset mismatch for {struct_name}::{lf["name"]}");'
                )
        lines.append("")

    lines.append(f"}} // namespace {namespace}")
    lines.append("")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def _process_one_glsl(input_path: Path, output_dir: Path, namespace: str, force: bool):
    parent = input_path.parent.name
    file_stem = input_path.stem
    stem = (
        f"{parent}.{file_stem}.glsl.h"
        if file_stem.lower() != parent.lower()
        else f"{parent}.glsl.h"
    )
    header_path = output_dir / stem

    if not force and header_path.exists():
        if input_path.stat().st_mtime <= header_path.stat().st_mtime:
            print(f"[glsl-gen] {header_path.name} is up-to-date, skipping.")
            return

    visited: set[Path] = set()
    combined_src = _load_source(input_path.resolve(), visited)
    clean = _strip_comments(combined_src)
    defines = _extract_defines(combined_src)
    structs = _parse_structs(clean, defines)
    for block_name, fields in _parse_uniform_blocks(clean, defines):
        if block_name not in structs:
            structs[block_name] = fields

    if not structs:
        print(f"WARNING: no structs found in {input_path}", file=sys.stderr)

    output_dir.mkdir(parents=True, exist_ok=True)
    header = _emit_header(structs, defines, namespace, [input_path.name])
    header_path.write_text(header, encoding="utf-8")
    print(f"[glsl-gen] Generated: {header_path}")


def main():
    parser = argparse.ArgumentParser(description="Generate C++ headers from GLSL files")
    parser.add_argument("inputs",       nargs="+", help="Input .glsl file(s)")
    parser.add_argument("--output-dir", required=True, help="Output directory for the generated header")
    parser.add_argument("--namespace",  default="ya::glsl_types", help="C++ namespace")
    parser.add_argument("--output",     default=None, help="Override output filename (single-file merge mode only)")
    parser.add_argument("--force",      action="store_true", help="Re-generate even if header is newer than sources")
    args = parser.parse_args()

    input_paths = [Path(p) for p in args.inputs]
    for p in input_paths:
        if not p.exists():
            print(f"ERROR: file not found: {p}", file=sys.stderr)
            sys.exit(1)

    output_dir = Path(args.output_dir)
    t_start = time.perf_counter()
    count = 0

    if args.output:
        # Explicit output name: merge all inputs into one header
        header_path = output_dir / args.output
        if not args.force and header_path.exists():
            newest_src = max(p.stat().st_mtime for p in input_paths)
            if newest_src <= header_path.stat().st_mtime:
                print(f"[glsl-gen] {header_path.name} is up-to-date, skipping.")
                count = len(input_paths)
                elapsed_ms = int((time.perf_counter() - t_start) * 1000)
                print(f"[glsl-gen] {count} file(s) processed in {elapsed_ms}ms")
                return
        visited: set[Path] = set()
        combined_src = ""
        for p in input_paths:
            combined_src += _load_source(p.resolve(), visited) + "\n"
        clean = _strip_comments(combined_src)
        defines = _extract_defines(combined_src)
        structs = _parse_structs(clean, defines)
        for block_name, fields in _parse_uniform_blocks(clean, defines):
            if block_name not in structs:
                structs[block_name] = fields
        if not structs:
            print(f"WARNING: no structs found in {args.inputs}", file=sys.stderr)
        output_dir.mkdir(parents=True, exist_ok=True)
        header = _emit_header(structs, defines, args.namespace, [p.name for p in input_paths])
        header_path.write_text(header, encoding="utf-8")
        print(f"[glsl-gen] Generated: {header_path}")
        count = len(input_paths)
    else:
        # Batch mode: process each file independently
        for p in input_paths:
            _process_one_glsl(p, output_dir, args.namespace, args.force)
            count += 1

    elapsed_ms = int((time.perf_counter() - t_start) * 1000)
    print(f"[glsl-gen] {count} file(s) processed in {elapsed_ms}ms")


if __name__ == "__main__":
    main()
