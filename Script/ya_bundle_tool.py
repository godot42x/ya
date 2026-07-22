#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path


WORKSPACE_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_EDITOR_PLUGIN = WORKSPACE_ROOT / "Engine/Plugins/ya-editor/ya-editor.yaplugin"
ANSI_ESCAPE_RE = re.compile(r"\x1b\[[0-9;]*m")
ENGINE_RUNTIME_RESOURCE_DIRS = [
    WORKSPACE_ROOT / "Engine/Shader",
    WORKSPACE_ROOT / "Engine/Content",
]


def _read_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


@dataclass
class ModuleManifest:
    source_path: Path
    name: str
    kind: str
    binary: str


@dataclass
class PluginDescriptor:
    source_path: Path
    name: str
    modules: list[Path] = field(default_factory=list)
    content_dirs: list[Path] = field(default_factory=list)
    config_files: list[Path] = field(default_factory=list)


@dataclass
class ProjectDescriptor:
    source_path: Path
    name: str
    main_module: str
    modules: list[Path]
    plugins: list[Path]
    content_dir: Path


def load_module_manifest(path: Path) -> ModuleManifest:
    data = _read_json(path)
    return ModuleManifest(
        source_path=path.resolve(),
        name=data["name"],
        kind=data["kind"],
        binary=data["binary"],
    )


def load_plugin_descriptor(path: Path) -> PluginDescriptor:
    data = _read_json(path)
    root = path.resolve().parent
    return PluginDescriptor(
        source_path=path.resolve(),
        name=data["name"],
        modules=[(root / entry).resolve() for entry in data.get("modules", [])],
        content_dirs=[(root / entry).resolve() for entry in data.get("contentDirs", [])],
        config_files=[(root / entry).resolve() for entry in data.get("configFiles", [])],
    )


def load_project_descriptor(path: Path) -> ProjectDescriptor:
    data = _read_json(path)
    root = path.resolve().parent
    return ProjectDescriptor(
        source_path=path.resolve(),
        name=data["name"],
        main_module=data["mainModule"],
        modules=[(root / entry).resolve() for entry in data.get("modules", [])],
        plugins=[(root / entry).resolve() for entry in data.get("plugins", [])],
        content_dir=(root / data.get("contentDir", "Content")).resolve(),
    )


def _iter_enabled_plugin_descriptors(project: ProjectDescriptor, include_editor: bool) -> list[PluginDescriptor]:
    plugins = [load_plugin_descriptor(path) for path in project.plugins]
    if include_editor:
        plugins.append(load_plugin_descriptor(DEFAULT_EDITOR_PLUGIN))
    return plugins


def collect_enabled_module_manifests(project: ProjectDescriptor, include_editor: bool) -> list[ModuleManifest]:
    manifests: list[ModuleManifest] = []
    seen_names: set[str] = set()

    def add_manifest(path: Path) -> None:
        manifest = load_module_manifest(path)
        if manifest.kind == "editor" and not include_editor:
            return
        if manifest.name in seen_names:
            return
        seen_names.add(manifest.name)
        manifests.append(manifest)

    for path in project.modules:
        add_manifest(path)
    for plugin in _iter_enabled_plugin_descriptors(project, include_editor):
        for path in plugin.modules:
            add_manifest(path)
    return manifests


def resolve_build_targets(project: ProjectDescriptor, include_editor: bool) -> list[str]:
    targets = ["ya-runtime"]
    if include_editor:
        targets.append("ya-editor")
    for manifest in collect_enabled_module_manifests(project, include_editor):
        if manifest.binary not in targets:
            targets.append(manifest.binary)
    return targets


def _targetfile_for(target: str) -> Path:
    result = subprocess.run(
        ["xmake", "show", "-t", target],
        cwd=WORKSPACE_ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    output = ANSI_ESCAPE_RE.sub("", result.stdout)
    match = re.search(r"targetfile:\s+([^\n]+)", output)
    if not match:
        raise RuntimeError(f"Cannot resolve targetfile for target: {target}")
    return (WORKSPACE_ROOT / match.group(1).strip()).resolve()


def _target_info(target: str) -> tuple[str, Path | None, list[str], list[Path]]:
    result = subprocess.run(
        ["xmake", "show", "-t", target],
        cwd=WORKSPACE_ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    output = ANSI_ESCAPE_RE.sub("", result.stdout)
    kind = "unknown"
    targetfile: Path | None = None
    deps: list[str] = []
    linkdirs: list[Path] = []
    in_deps = False
    in_linkdirs = False
    for line in output.splitlines():
        stripped = line.strip()
        if stripped.startswith("kind:"):
            kind = stripped.split(":", 1)[1].strip()
            in_deps = False
            in_linkdirs = False
        elif stripped.startswith("targetfile:"):
            targetfile = (WORKSPACE_ROOT / stripped.split(":", 1)[1].strip()).resolve()
            in_deps = False
            in_linkdirs = False
        elif stripped.startswith("deps:"):
            in_deps = True
            in_linkdirs = False
        elif stripped.startswith("linkdirs:"):
            in_deps = False
            in_linkdirs = True
        elif in_deps:
            match = re.match(r"->\s+([A-Za-z0-9_.-]+)\s+->", stripped)
            if match:
                deps.append(match.group(1))
            elif stripped and not stripped.startswith("->"):
                in_deps = False
        elif in_linkdirs:
            match = re.match(r"->\s+(.+?)\s+->", stripped)
            if match:
                linkdirs.append(Path(match.group(1).strip()))
            elif stripped and not stripped.startswith("->"):
                in_linkdirs = False
    return kind, targetfile, deps, linkdirs


def _collect_target_closure(initial_targets: list[str]) -> list[str]:
    ordered: list[str] = []
    seen: set[str] = set()
    queue = list(initial_targets)
    while queue:
        target = queue.pop(0)
        if target in seen:
            continue
        seen.add(target)
        ordered.append(target)
        _, _, deps, _ = _target_info(target)
        for dep in deps:
            if dep not in seen and dep not in queue:
                queue.append(dep)
    return ordered


def _resolve_rpath_dependencies(targetfile: Path, linkdirs: list[Path], package_root: Path) -> None:
    result = subprocess.run(
        ["otool", "-L", str(targetfile)],
        cwd=WORKSPACE_ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    seen: set[str] = set()
    search_dirs = [targetfile.parent, *linkdirs]
    for line in result.stdout.splitlines()[1:]:
        stripped = line.strip()
        if not stripped.startswith("@rpath/"):
            continue
        basename = Path(stripped.split(" ", 1)[0]).name
        if basename in seen:
            continue
        seen.add(basename)
        destination = package_root / basename
        if destination.exists():
            continue
        for directory in search_dirs:
            candidate = directory / basename
            if candidate.exists():
                _copy_file(candidate, package_root, destination)
                break


def _copy_file(source: Path, package_root: Path, destination: Path | None = None) -> None:
    destination_path = destination if destination is not None else package_root / source.relative_to(WORKSPACE_ROOT)
    destination_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination_path)


def _copy_tree(source: Path, package_root: Path) -> None:
    destination = package_root / source.relative_to(WORKSPACE_ROOT)
    if destination.exists():
        shutil.rmtree(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, destination)


def _find_repo_local_vulkan_sdk_macos_dir() -> Path | None:
    sdk_root = WORKSPACE_ROOT / "Engine" / "ThirdParty" / "VulkanSDK"
    if not sdk_root.is_dir():
        return None

    candidates: list[Path] = []
    for version_dir in sdk_root.iterdir():
        sdk_dir = version_dir / "macOS"
        icd_json = sdk_dir / "share" / "vulkan" / "icd.d" / "MoltenVK_icd.json"
        moltenvk = sdk_dir / "lib" / "libMoltenVK.dylib"
        if icd_json.is_file() and moltenvk.is_file():
            candidates.append(sdk_dir)

    if not candidates:
        return None
    return sorted(candidates)[-1]


def _copy_macos_vulkan_runtime(package_root: Path) -> None:
    sdk_dir = _find_repo_local_vulkan_sdk_macos_dir()
    if sdk_dir is None:
        return

    icd_dir = sdk_dir / "share" / "vulkan" / "icd.d"
    layer_dir = sdk_dir / "share" / "vulkan" / "explicit_layer.d"
    runtime_dirs = [icd_dir, layer_dir]
    for directory in runtime_dirs:
        if directory.exists():
            _copy_tree(directory, package_root)

    runtime_libs = [sdk_dir / "lib" / "libMoltenVK.dylib"]
    for manifest_dir, key in ((icd_dir, "ICD"), (layer_dir, "layer")):
        if not manifest_dir.is_dir():
            continue
        for manifest_path in manifest_dir.glob("*.json"):
            try:
                data = _read_json(manifest_path)
            except json.JSONDecodeError:
                continue
            info = data.get(key, {})
            library_path = info.get("library_path")
            if not library_path:
                continue
            candidate = (manifest_path.parent / library_path).resolve()
            if candidate.is_file():
                runtime_libs.append(candidate)

    for lib in sorted(set(runtime_libs)):
        if lib.exists():
            _copy_file(lib, package_root)


def create_package(project: ProjectDescriptor, include_editor: bool, output: Path) -> None:
    package_root = output.resolve()
    if package_root.exists():
        shutil.rmtree(package_root)
    package_root.mkdir(parents=True, exist_ok=True)

    for resource_dir in ENGINE_RUNTIME_RESOURCE_DIRS:
        if resource_dir.exists():
            _copy_tree(resource_dir, package_root)

    _copy_file(project.source_path, package_root)
    if project.content_dir.exists():
        _copy_tree(project.content_dir, package_root)

    manifests = collect_enabled_module_manifests(project, include_editor)
    module_target_names = {manifest.binary for manifest in manifests}
    for manifest in manifests:
        _copy_file(manifest.source_path, package_root)
        binary_path = _targetfile_for(manifest.binary)
        module_destination = package_root / manifest.source_path.parent.relative_to(WORKSPACE_ROOT) / binary_path.name
        _copy_file(binary_path, package_root, module_destination)

    for plugin in _iter_enabled_plugin_descriptors(project, include_editor):
        _copy_file(plugin.source_path, package_root)
        for content_dir in plugin.content_dirs:
            if content_dir.exists():
                _copy_tree(content_dir, package_root)
        for config_file in plugin.config_files:
            if config_file.exists():
                _copy_file(config_file, package_root)

    runtime_targets = ["ya-runtime", "ya-engine"]
    if include_editor:
        runtime_targets.append("ya-editor")
    runtime_targets.extend(manifest.binary for manifest in manifests)
    for target in _collect_target_closure(runtime_targets):
        kind, targetfile, _, linkdirs = _target_info(target)
        if targetfile is None or kind not in {"binary", "shared"}:
            continue
        if target in module_target_names:
            continue
        if target == "ya-runtime":
            destination = package_root / targetfile.name
        else:
            destination = package_root / targetfile.name
        _copy_file(targetfile, package_root, destination)
        _resolve_rpath_dependencies(targetfile, linkdirs, package_root)

    if sys.platform == "darwin":
        _copy_macos_vulkan_runtime(package_root)


def main() -> int:
    parser = argparse.ArgumentParser(description="Resolve YA project bundles and collect minimal packages.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    targets_parser = subparsers.add_parser("build-targets", help="Print required build targets for a project.")
    targets_parser.add_argument("--project", required=True, type=Path)
    targets_parser.add_argument("--editor", action="store_true")

    package_parser = subparsers.add_parser("package", help="Collect a minimal runtime package for a project.")
    package_parser.add_argument("--project", required=True, type=Path)
    package_parser.add_argument("--editor", action="store_true")
    package_parser.add_argument("--output", required=True, type=Path)

    args = parser.parse_args()
    project = load_project_descriptor((WORKSPACE_ROOT / args.project).resolve() if not args.project.is_absolute() else args.project.resolve())

    if args.command == "build-targets":
        for target in resolve_build_targets(project, args.editor):
            print(target)
        return 0

    if args.command == "package":
        output = (WORKSPACE_ROOT / args.output).resolve() if not args.output.is_absolute() else args.output.resolve()
        create_package(project, args.editor, output)
        print(output)
        return 0

    return 1


if __name__ == "__main__":
    sys.exit(main())
