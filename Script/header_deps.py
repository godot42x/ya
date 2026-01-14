#!/usr/bin/env python3
"""
Header Dependency Analyzer for C++ Projects

This script analyzes C++ header file dependencies and generates dependency graphs
in multiple formats (text tree, Mermaid, Graphviz DOT).

Features:
- Detects circular dependencies
- Supports multiple output formats
- Configurable search paths
- Filters by directory/file patterns

Usage:
    python header_deps.py <source_dir> [options]

Examples:
    python header_deps.py ../Engine/Source --format mermaid
    python header_deps.py ../Engine/Source --entry Core/Common/FWD.h --depth 3
    python header_deps.py ../Engine/Source --detect-cycles
"""

import argparse
import os
import re
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple


class HeaderDependencyAnalyzer:
    """Analyzes C++ header file dependencies."""

    # Regex pattern to match #include directives
    INCLUDE_PATTERN = re.compile(
        r'^\s*#\s*include\s+[<"]([^>"]+)[>"]', re.MULTILINE
    )

    def __init__(
        self,
        source_dir: str,
        include_dirs: Optional[List[str]] = None,
        exclude_patterns: Optional[List[str]] = None,
    ):
        """
        Initialize the analyzer.

        Args:
            source_dir: Root directory to scan for headers
            include_dirs: Additional include directories to search
            exclude_patterns: Patterns to exclude from analysis
        """
        self.source_dir = Path(source_dir).resolve()
        self.include_dirs = [self.source_dir]
        if include_dirs:
            self.include_dirs.extend(Path(d).resolve() for d in include_dirs)

        self.exclude_patterns = exclude_patterns or []

        # Cache for parsed dependencies
        self._dependencies: Dict[str, List[str]] = {}
        # All discovered headers
        self._all_headers: Set[str] = set()
        # Map from header name to full path
        self._header_paths: Dict[str, Path] = {}

    def _should_exclude(self, path: str) -> bool:
        """Check if a path should be excluded."""
        for pattern in self.exclude_patterns:
            if re.search(pattern, path):
                return True
        return False

    def _find_header(self, include_name: str, from_file: Optional[Path] = None) -> Optional[Path]:
        """
        Find the full path of an included header.

        Args:
            include_name: The include path (e.g., "Core/Common/FWD.h")
            from_file: The file that contains the include directive

        Returns:
            Full path to the header if found, None otherwise
        """
        # Check cache first
        if include_name in self._header_paths:
            return self._header_paths[include_name]

        # Try relative to the including file first
        if from_file:
            relative_path = from_file.parent / include_name
            if relative_path.exists():
                self._header_paths[include_name] = relative_path
                return relative_path

        # Search in include directories
        for include_dir in self.include_dirs:
            full_path = include_dir / include_name
            if full_path.exists():
                self._header_paths[include_name] = full_path
                return full_path

        return None

    def _parse_includes(self, file_path: Path) -> List[str]:
        """
        Parse include directives from a header file.

        Args:
            file_path: Path to the header file

        Returns:
            List of included header names
        """
        try:
            content = file_path.read_text(encoding='utf-8', errors='ignore')
        except Exception as e:
            print(f"Warning: Could not read {file_path}: {e}")
            return []

        includes = self.INCLUDE_PATTERN.findall(content)
        return [inc for inc in includes if not self._should_exclude(inc)]

    def _normalize_path(self, path: Path) -> str:
        """Convert a path to a normalized relative string."""
        try:
            rel_path = path.relative_to(self.source_dir)
            return str(rel_path).replace('\\', '/')
        except ValueError:
            return str(path).replace('\\', '/')

    def scan_directory(self) -> None:
        """Scan the source directory for all header files."""
        header_extensions = {'.h', '.hpp', '.hxx', '.hh', '.H'}

        for root, _, files in os.walk(self.source_dir):
            for file in files:
                if Path(file).suffix in header_extensions:
                    full_path = Path(root) / file
                    rel_path = self._normalize_path(full_path)

                    if not self._should_exclude(rel_path):
                        self._all_headers.add(rel_path)
                        self._header_paths[rel_path] = full_path

    def analyze_file(self, header: str, visited: Optional[Set[str]] = None) -> Dict[str, List[str]]:
        """
        Analyze dependencies for a single header file.

        Args:
            header: Relative path to the header
            visited: Set of already visited headers (for cycle detection)

        Returns:
            Dictionary mapping headers to their direct dependencies
        """
        if visited is None:
            visited = set()

        if header in self._dependencies:
            return self._dependencies

        if header in visited:
            # Circular dependency detected
            return self._dependencies

        visited.add(header)

        # Find the actual file
        file_path = self._header_paths.get(header)
        if file_path is None:
            file_path = self._find_header(header)

        if file_path is None or not file_path.exists():
            self._dependencies[header] = []
            return self._dependencies

        # Parse includes
        includes = self._parse_includes(file_path)
        resolved_includes = []

        for inc in includes:
            # Try to find the header
            inc_path = self._find_header(inc, file_path)
            if inc_path:
                normalized = self._normalize_path(inc_path)
                resolved_includes.append(normalized)
                # Recursively analyze
                self.analyze_file(normalized, visited.copy())
            else:
                # External header (system or third-party)
                resolved_includes.append(f"<{inc}>")

        self._dependencies[header] = resolved_includes
        return self._dependencies

    def analyze_all(self) -> Dict[str, List[str]]:
        """Analyze all headers in the source directory."""
        self.scan_directory()
        for header in self._all_headers:
            self.analyze_file(header)
        return self._dependencies

    def detect_cycles(self) -> List[List[str]]:
        """
        Detect circular dependencies.

        Returns:
            List of cycles, where each cycle is a list of headers
        """
        cycles = []
        visited = set()
        rec_stack = []

        def dfs(header: str) -> bool:
            visited.add(header)
            rec_stack.append(header)

            for dep in self._dependencies.get(header, []):
                if dep.startswith('<'):
                    continue  # Skip external headers

                if dep not in visited:
                    if dfs(dep):
                        return True
                elif dep in rec_stack:
                    # Found a cycle
                    cycle_start = rec_stack.index(dep)
                    cycle = rec_stack[cycle_start:] + [dep]
                    cycles.append(cycle)
                    return True

            rec_stack.pop()
            return False

        for header in self._dependencies:
            if header not in visited:
                dfs(header)

        return cycles

    def get_dependency_tree(
        self, entry: str, max_depth: int = -1
    ) -> Dict[str, any]:
        """
        Get dependency tree starting from an entry point.

        Args:
            entry: Entry header file
            max_depth: Maximum depth to traverse (-1 for unlimited)

        Returns:
            Nested dictionary representing the tree
        """
        def build_tree(header: str, depth: int, visited: Set[str]) -> Dict:
            if max_depth >= 0 and depth > max_depth:
                return {"...": {}}

            if header in visited:
                return {"(circular)": {}}

            visited = visited | {header}
            deps = self._dependencies.get(header, [])

            tree = {}
            for dep in deps:
                if dep.startswith('<'):
                    tree[dep] = {}
                else:
                    tree[dep] = build_tree(dep, depth + 1, visited)

            return tree

        self.analyze_file(entry)
        return {entry: build_tree(entry, 0, set())}


class OutputFormatter:
    """Formats dependency analysis results."""

    @staticmethod
    def to_text_tree(
        tree: Dict, indent: str = "", is_last: bool = True, prefix: str = ""
    ) -> str:
        """Format as ASCII tree."""
        lines = []

        items = list(tree.items())
        for i, (name, children) in enumerate(items):
            is_last_item = i == len(items) - 1
            connector = "└── " if is_last_item else "├── "
            lines.append(f"{prefix}{connector}{name}")

            if children:
                extension = "    " if is_last_item else "│   "
                child_output = OutputFormatter.to_text_tree(
                    children, indent, is_last_item, prefix + extension
                )
                if child_output:
                    lines.append(child_output)

        return "\n".join(lines)

    @staticmethod
    def to_mermaid(dependencies: Dict[str, List[str]], title: str = "Header Dependencies") -> str:
        """Format as Mermaid diagram."""
        lines = [
            "```mermaid",
            f"graph TD",
            f"    subgraph {title.replace(' ', '_')}",
        ]

        # Create node ID mapping (sanitize names)
        def sanitize_id(name: str) -> str:
            return re.sub(r'[^a-zA-Z0-9_]', '_', name.replace('<', 'ext_').replace('>', ''))

        # Group by directory
        dir_groups = defaultdict(list)
        for header in dependencies:
            if header.startswith('<'):
                dir_groups['External'].append(header)
            else:
                dir_name = str(Path(header).parent) or 'Root'
                dir_groups[dir_name].append(header)

        # Output subgraphs for each directory
        for dir_name, headers in sorted(dir_groups.items()):
            safe_dir = sanitize_id(dir_name)
            lines.append(f"        subgraph {safe_dir}[{dir_name}]")
            for header in headers:
                node_id = sanitize_id(header)
                display_name = Path(header).name if not header.startswith('<') else header
                lines.append(f'            {node_id}["{display_name}"]')
            lines.append("        end")

        lines.append("    end")

        # Add edges
        for header, deps in dependencies.items():
            src_id = sanitize_id(header)
            for dep in deps:
                dst_id = sanitize_id(dep)
                lines.append(f"    {src_id} --> {dst_id}")

        lines.append("```")
        return "\n".join(lines)

    @staticmethod
    def to_graphviz(dependencies: Dict[str, List[str]], title: str = "Header Dependencies") -> str:
        """Format as Graphviz DOT."""
        lines = [
            f'digraph "{title}" {{',
            '    rankdir=TB;',
            '    node [shape=box, fontname="Consolas", fontsize=10];',
            '    edge [fontsize=8];',
            '',
        ]

        def sanitize_id(name: str) -> str:
            return '"' + name.replace('"', '\\"') + '"'

        # Group nodes by directory
        dir_groups = defaultdict(list)
        for header in dependencies:
            if header.startswith('<'):
                dir_groups['External'].append(header)
            else:
                dir_name = str(Path(header).parent) or 'Root'
                dir_groups[dir_name].append(header)

        # Create subgraphs
        for i, (dir_name, headers) in enumerate(sorted(dir_groups.items())):
            lines.append(f'    subgraph cluster_{i} {{')
            lines.append(f'        label="{dir_name}";')
            if dir_name == 'External':
                lines.append('        style=dashed;')
                lines.append('        color=gray;')
            else:
                lines.append('        style=rounded;')
            for header in headers:
                display = Path(header).name if not header.startswith('<') else header
                lines.append(f'        {sanitize_id(header)} [label="{display}"];')
            lines.append('    }')
            lines.append('')

        # Add edges
        for header, deps in dependencies.items():
            for dep in deps:
                lines.append(f'    {sanitize_id(header)} -> {sanitize_id(dep)};')

        lines.append('}')
        return "\n".join(lines)

    @staticmethod
    def to_simple_list(dependencies: Dict[str, List[str]]) -> str:
        """Format as simple list."""
        lines = []
        for header in sorted(dependencies.keys()):
            deps = dependencies[header]
            lines.append(f"{header}:")
            for dep in sorted(deps):
                lines.append(f"  -> {dep}")
            lines.append("")
        return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="Analyze C++ header file dependencies",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )

    parser.add_argument(
        "source_dir",
        help="Root source directory to analyze",
    )

    parser.add_argument(
        "--entry", "-e",
        help="Entry point header file (relative path)",
    )

    parser.add_argument(
        "--depth", "-d",
        type=int,
        default=-1,
        help="Maximum depth to traverse (-1 for unlimited)",
    )

    parser.add_argument(
        "--format", "-f",
        choices=["tree", "mermaid", "graphviz", "list"],
        default="tree",
        help="Output format (default: tree)",
    )

    parser.add_argument(
        "--output", "-o",
        help="Output file (default: stdout)",
    )

    parser.add_argument(
        "--detect-cycles", "-c",
        action="store_true",
        help="Detect and report circular dependencies",
    )

    parser.add_argument(
        "--include-dir", "-I",
        action="append",
        dest="include_dirs",
        help="Additional include directory (can be specified multiple times)",
    )

    parser.add_argument(
        "--exclude", "-x",
        action="append",
        dest="exclude_patterns",
        help="Regex pattern to exclude (can be specified multiple times)",
    )

    parser.add_argument(
        "--all", "-a",
        action="store_true",
        help="Analyze all headers (ignore --entry)",
    )

    args = parser.parse_args()

    # Initialize analyzer
    analyzer = HeaderDependencyAnalyzer(
        source_dir=args.source_dir,
        include_dirs=args.include_dirs,
        exclude_patterns=args.exclude_patterns,
    )

    # Perform analysis
    if args.all or not args.entry:
        print("Scanning directory...", file=__import__('sys').stderr)
        dependencies = analyzer.analyze_all()
    else:
        dependencies = analyzer.analyze_file(args.entry)

    # Detect cycles if requested
    if args.detect_cycles:
        cycles = analyzer.detect_cycles()
        if cycles:
            print("=" * 60)
            print("CIRCULAR DEPENDENCIES DETECTED:")
            print("=" * 60)
            for i, cycle in enumerate(cycles, 1):
                print(f"\nCycle {i}:")
                print("  " + " -> ".join(cycle))
            print()

    # Format output
    formatter = OutputFormatter()

    if args.entry and not args.all:
        tree = analyzer.get_dependency_tree(args.entry, args.depth)
        if args.format == "tree":
            output = formatter.to_text_tree(tree)
        elif args.format == "mermaid":
            output = formatter.to_mermaid(dependencies, f"Dependencies of {args.entry}")
        elif args.format == "graphviz":
            output = formatter.to_graphviz(dependencies, f"Dependencies of {args.entry}")
        else:
            output = formatter.to_simple_list(dependencies)
    else:
        if args.format == "tree":
            # For all headers, show as list with dependencies
            output = formatter.to_simple_list(dependencies)
        elif args.format == "mermaid":
            output = formatter.to_mermaid(dependencies)
        elif args.format == "graphviz":
            output = formatter.to_graphviz(dependencies)
        else:
            output = formatter.to_simple_list(dependencies)

    # Write output
    if args.output:
        Path(args.output).write_text(output, encoding='utf-8')
        print(f"Output written to {args.output}", file=__import__('sys').stderr)
    else:
        print(output)


if __name__ == "__main__":
    main()
