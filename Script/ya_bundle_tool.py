#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import stat
from dataclasses import dataclass, field
from pathlib import Path


WORKSPACE_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_EDITOR_PLUGIN = WORKSPACE_ROOT / "Engine/Plugins/ya-editor/ya-editor.yaplugin"
ANSI_ESCAPE_RE = re.compile(r"\x1b\[[0-9;]*m")
ENGINE_CONTENT_DIR = WORKSPACE_ROOT / "Engine/Content"
ENGINE_SHADER_GLSL_DIR = WORKSPACE_ROOT / "Engine/Shader" / "GLSL"
ENGINE_SHADER_SLANG_DIR = WORKSPACE_ROOT / "Engine/Shader" / "Slang"
ENGINE_THIRDPARTY_RESOURCE_DIRS = [
    WORKSPACE_ROOT / "Engine/ThirdParty" / "LearnOpenGL" / "resources",
    WORKSPACE_ROOT / "Engine/ThirdParty" / "Vulkan-Samples-Assets",
]


def _read_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def _write_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as stream:
        json.dump(data, stream, indent=2, ensure_ascii=False)
        stream.write("\n")


def _force_remove_readonly(func, path, exc_info) -> None:
    del exc_info
    os.chmod(path, stat.S_IWRITE | stat.S_IREAD | stat.S_IEXEC)
    func(path)


def _prepare_tree_for_removal(path: Path) -> None:
    if not path.exists():
        return
    for root, dirs, files in os.walk(path):
        for entry in dirs:
            candidate = Path(root) / entry
            try:
                os.chmod(candidate, stat.S_IRWXU)
            except OSError:
                pass
        for entry in files:
            candidate = Path(root) / entry
            try:
                os.chmod(candidate, stat.S_IRUSR | stat.S_IWUSR)
            except OSError:
                pass


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

    def resolve_path(self, value: Path) -> Path:
        if value.is_absolute():
            return value.resolve()
        if value.exists():
            return value.resolve()
        return (self.source_path.parent / value).resolve()


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
    descriptor = ProjectDescriptor(
        source_path=path.resolve(),
        name=data["name"],
        main_module=data["mainModule"],
        modules=[(root / entry).resolve() for entry in data.get("modules", [])],
        plugins=[(root / entry).resolve() for entry in data.get("plugins", [])],
        content_dir=(root / data.get("contentDir", "Content")).resolve(),
    )
    if not descriptor.modules:
        raise RuntimeError(f"Project descriptor requires at least one module manifest: {descriptor.source_path}")
    for module_path in descriptor.modules:
        if not module_path.is_file():
            raise RuntimeError(f"Project module manifest not found: {module_path}")
    for plugin_path in descriptor.plugins:
        if not plugin_path.is_file():
            raise RuntimeError(f"Project plugin descriptor not found: {plugin_path}")
    if not descriptor.content_dir.exists():
        raise RuntimeError(f"Project content dir not found: {descriptor.content_dir}")
    default_scene = data.get("defaultScene")
    if default_scene:
        resolved_default_scene = descriptor.resolve_path(Path(default_scene))
        if not resolved_default_scene.is_file():
            raise RuntimeError(f"Project defaultScene not found: {resolved_default_scene}")
    return descriptor


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
    if project.main_module not in {manifest.name for manifest in manifests}:
        raise RuntimeError(
            f"Project main module is not provided by project modules/plugins: {project.main_module}"
        )
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


def _resolve_rpath_dependencies(targetfile: Path, linkdirs: list[Path], destination_dir: Path) -> None:
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
        if basename == targetfile.name:
            continue
        if basename in seen:
            continue
        seen.add(basename)
        destination = destination_dir / basename
        if destination.exists():
            continue
        for directory in search_dirs:
            candidate = directory / basename
            if candidate.exists():
                _copy_file(candidate, WORKSPACE_ROOT, destination)
                break


def _copy_file(source: Path, package_root: Path, destination: Path | None = None) -> None:
    destination_path = destination if destination is not None else package_root / source.relative_to(WORKSPACE_ROOT)
    destination_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination_path)


def _copy_tree_to(source: Path, destination: Path) -> None:
    if destination.exists():
        shutil.rmtree(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, destination)


def _copy_tree(source: Path, package_root: Path) -> None:
    destination = package_root / source.relative_to(WORKSPACE_ROOT)
    _copy_tree_to(source, destination)


def _copy_filtered_tree(source: Path, destination: Path, *, skip_dir_names: set[str], allow_file) -> None:
    if destination.exists():
        shutil.rmtree(destination)

    copied_any = False
    for root, dirs, files in os.walk(source):
        dirs[:] = [entry for entry in dirs if entry not in skip_dir_names]
        root_path = Path(root)
        relative_root = root_path.relative_to(source)
        for filename in files:
            source_file = root_path / filename
            if not allow_file(source_file):
                continue
            destination_file = destination / relative_root / filename
            destination_file.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source_file, destination_file)
            copied_any = True

    if not copied_any:
        destination.mkdir(parents=True, exist_ok=True)


def _copy_engine_runtime_resources(package_root: Path) -> None:
    if ENGINE_CONTENT_DIR.exists():
        _copy_tree_to(ENGINE_CONTENT_DIR, package_root / "Engine" / "Content")

    if ENGINE_SHADER_GLSL_DIR.exists():
        _copy_filtered_tree(
            ENGINE_SHADER_GLSL_DIR,
            package_root / "Engine" / "Shader" / "GLSL",
            skip_dir_names={"Generated"},
            allow_file=lambda path: path.suffix == ".glsl",
        )

    if ENGINE_SHADER_SLANG_DIR.exists():
        _copy_filtered_tree(
            ENGINE_SHADER_SLANG_DIR,
            package_root / "Engine" / "Shader" / "Slang",
            skip_dir_names={"Generated"},
            allow_file=lambda path: path.suffix == ".slang",
        )

    for resource_dir in ENGINE_THIRDPARTY_RESOURCE_DIRS:
        if resource_dir.exists():
            _copy_tree(resource_dir, package_root)


def _project_package_dir(package_root: Path, project: ProjectDescriptor) -> Path:
    return package_root / project.name


def _rewrite_project_descriptor_for_package(project: ProjectDescriptor) -> dict:
    data = _read_json(project.source_path)
    default_scene = data.get("defaultScene")
    if default_scene:
        resolved_default_scene = project.resolve_path(Path(default_scene))
        try:
            data["defaultScene"] = resolved_default_scene.relative_to(project.source_path.parent).as_posix()
        except ValueError:
            try:
                data["defaultScene"] = resolved_default_scene.relative_to(WORKSPACE_ROOT).as_posix()
            except ValueError:
                data["defaultScene"] = resolved_default_scene.as_posix()
    return data


def _copy_project_bundle(project: ProjectDescriptor, include_editor: bool, package_root: Path) -> tuple[Path, list[ModuleManifest]]:
    project_package_dir = _project_package_dir(package_root, project)
    _write_json(project_package_dir / project.source_path.name, _rewrite_project_descriptor_for_package(project))

    if project.content_dir.exists():
        _copy_tree_to(project.content_dir, project_package_dir / "Content")

    project_root = project.source_path.parent
    manifests = collect_enabled_module_manifests(project, include_editor)
    for manifest_path in project.modules:
        _copy_file(manifest_path, package_root, project_package_dir / manifest_path.relative_to(project_root))

    for plugin_path in project.plugins:
        _copy_file(plugin_path, package_root, project_package_dir / plugin_path.relative_to(project_root))
        plugin = load_plugin_descriptor(plugin_path)
        for module_path in plugin.modules:
            if module_path.exists():
                _copy_file(module_path, package_root, project_package_dir / module_path.relative_to(project_root))
        for content_dir in plugin.content_dirs:
            if content_dir.exists():
                _copy_tree_to(content_dir, project_package_dir / content_dir.relative_to(project_root))
        for config_file in plugin.config_files:
            if config_file.exists():
                _copy_file(config_file, package_root, project_package_dir / config_file.relative_to(project_root))

    return project_package_dir, manifests


def _copy_editor_plugin_bundle(package_root: Path) -> None:
    plugin = load_plugin_descriptor(DEFAULT_EDITOR_PLUGIN)
    _copy_file(plugin.source_path, package_root)
    for module_path in plugin.modules:
        if module_path.exists():
            _copy_file(module_path, package_root)
    for content_dir in plugin.content_dirs:
        if content_dir.exists():
            _copy_tree(content_dir, package_root)
    for config_file in plugin.config_files:
        if config_file.exists():
            _copy_file(config_file, package_root)


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


def _copy_macos_vulkan_runtime(package_root: Path, binaries_dir: Path) -> None:
    sdk_dir = _find_repo_local_vulkan_sdk_macos_dir()
    if sdk_dir is None:
        return

    icd_dir = sdk_dir / "share" / "vulkan" / "icd.d"
    if icd_dir.exists():
        _copy_tree(icd_dir, package_root)

    runtime_libs = [sdk_dir / "lib" / "libMoltenVK.dylib"]

    for lib in sorted(set(runtime_libs)):
        if lib.exists():
            _copy_file(lib, package_root, binaries_dir / lib.name)


def _read_macos_rpaths(binary: Path) -> set[str]:
    result = subprocess.run(
        ["otool", "-l", str(binary)],
        cwd=WORKSPACE_ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    rpaths: set[str] = set()
    pending_rpath = False
    for line in result.stdout.splitlines():
        stripped = line.strip()
        if stripped == "cmd LC_RPATH":
            pending_rpath = True
            continue
        if pending_rpath and stripped.startswith("path "):
            rpaths.add(stripped.split(" ", 2)[1])
            pending_rpath = False
    return rpaths


def _ensure_macos_rpath(binary: Path, rpath: str) -> None:
    if rpath in _read_macos_rpaths(binary):
        return
    subprocess.run(["install_name_tool", "-add_rpath", rpath, str(binary)], cwd=WORKSPACE_ROOT, check=True)


def _patch_macos_runtime_layout(runtime_binary: Path,
                                engine_binaries_dir: Path,
                                project_binaries_dir: Path) -> None:
    if runtime_binary.exists():
        _ensure_macos_rpath(runtime_binary, "@loader_path/Engine/Binaries")

    if engine_binaries_dir.exists():
        for candidate in engine_binaries_dir.iterdir():
            if candidate.is_file() and candidate.suffix == ".dylib":
                _ensure_macos_rpath(candidate, "@loader_path")

    if not project_binaries_dir.exists():
        return
    for candidate in project_binaries_dir.iterdir():
        if candidate.is_file() and candidate.suffix == ".dylib":
            _ensure_macos_rpath(candidate, "@loader_path")
            _ensure_macos_rpath(candidate, "@loader_path/../../Engine/Binaries")


def create_package(project: ProjectDescriptor, include_editor: bool, output: Path) -> None:
    package_root = output.resolve()
    if package_root.exists():
        _prepare_tree_for_removal(package_root)
        shutil.rmtree(package_root, onerror=_force_remove_readonly)
    package_root.mkdir(parents=True, exist_ok=True)
    engine_binaries_dir = package_root / "Engine" / "Binaries"
    project_binaries_dir = _project_package_dir(package_root, project) / "Binaries"
    engine_binaries_dir.mkdir(parents=True, exist_ok=True)
    project_binaries_dir.mkdir(parents=True, exist_ok=True)

    _copy_engine_runtime_resources(package_root)
    project_package_dir, manifests = _copy_project_bundle(project, include_editor, package_root)
    if include_editor:
        _copy_editor_plugin_bundle(package_root)

    module_target_names = {manifest.binary for manifest in manifests}
    for manifest in manifests:
        binary_path = _targetfile_for(manifest.binary)
        _copy_file(binary_path, package_root, project_binaries_dir / binary_path.name)
        _, _, _, linkdirs = _target_info(manifest.binary)
        _resolve_rpath_dependencies(binary_path, linkdirs, engine_binaries_dir)

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
            destination = engine_binaries_dir / targetfile.name
        _copy_file(targetfile, package_root, destination)
        _resolve_rpath_dependencies(targetfile, linkdirs, engine_binaries_dir)

    if sys.platform == "darwin":
        _copy_macos_vulkan_runtime(package_root, engine_binaries_dir)
        _patch_macos_runtime_layout(
            package_root / _targetfile_for("ya-runtime").name,
            engine_binaries_dir,
            project_binaries_dir,
        )

    verify_package_contents(
        project,
        include_editor,
        package_root,
        project_package_dir,
        engine_binaries_dir,
        project_binaries_dir,
    )


def verify_package_contents(project: ProjectDescriptor,
                            include_editor: bool,
                            package_root: Path,
                            project_package_dir: Path,
                            engine_binaries_dir: Path,
                            project_binaries_dir: Path) -> None:
    editor_binary = engine_binaries_dir / _targetfile_for("ya-editor").name
    required_files = [
        package_root / _targetfile_for("ya-runtime").name,
        engine_binaries_dir / _targetfile_for("ya-engine").name,
        project_package_dir / project.source_path.name,
    ]
    if include_editor:
        required_files.append(editor_binary)

    project_root = project.source_path.parent
    for manifest_path in project.modules:
        required_files.append(project_package_dir / manifest_path.relative_to(project_root))
    for plugin_path in project.plugins:
        required_files.append(project_package_dir / plugin_path.relative_to(project_root))
    for manifest in collect_enabled_module_manifests(project, include_editor):
        required_files.append(project_binaries_dir / _targetfile_for(manifest.binary).name)

    missing = [path for path in required_files if not path.exists()]
    if missing:
        raise RuntimeError(
            "Package verification failed; missing files:\n" + "\n".join(f"  {path}" for path in missing)
        )
    if not include_editor and editor_binary.exists():
        raise RuntimeError(f"Package verification failed; runtime-only package unexpectedly contains editor module: {editor_binary}")
    if (package_root / "Example").exists():
        raise RuntimeError(f"Package verification failed; packaged output still contains Example/: {package_root / 'Example'}")
    if (package_root / "Engine" / "Shader" / "GLSL" / "Generated").exists():
        raise RuntimeError("Package verification failed; Engine/Shader/GLSL/Generated should not be packaged")
    if (package_root / "Engine" / "Shader" / "Slang" / "Generated").exists():
        raise RuntimeError("Package verification failed; Engine/Shader/Slang/Generated should not be packaged")
    root_dylibs = sorted(path.name for path in package_root.glob("*.dylib"))
    if root_dylibs:
        raise RuntimeError(
            "Package verification failed; shared libraries should live under Engine/Binaries or <Project>/Binaries:\n"
            + "\n".join(f"  {name}" for name in root_dylibs)
        )
    project_module_binaries = {_targetfile_for(manifest.binary).name for manifest in collect_enabled_module_manifests(project, include_editor)}
    leaked_engine_binaries = sorted(name for name in project_module_binaries if (engine_binaries_dir / name).exists())
    if leaked_engine_binaries:
        raise RuntimeError(
            "Package verification failed; project module binaries leaked into Engine/Binaries:\n"
            + "\n".join(f"  {name}" for name in leaked_engine_binaries)
        )


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
