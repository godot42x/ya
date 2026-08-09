#!/usr/bin/env python3
"""Module include lint: scans every engine module's own sources for forbidden
engine include prefixes (boundary rules in
.agent/plan/module-boundary-cleanup/plan.md §6).

Usage:
    python3 Script/ya_module_lint.py

Exit code 0 = clean; 1 = violations found.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "Engine", "Source")

# module name -> physical directory (relative to Engine/Source)
MODULES = {
    "ya-foundation-core": "Foundation/Core",
    "ya-rhi": "Foundation/RHI",
    "ya-rhi-backend-common": "Foundation/RHI/Backend",
    "ya-rhi-vulkan": "Foundation/RHI/Backend/Vulkan",
    "ya-hierarchy": "Framework/Hierarchy",
    "ya-gui-resources": "Framework/GUI/Runtime/Resource",
    "ya-gui-draw2d": "Framework/GUI/Runtime/Draw2D",
    "ya-gui-widgets": "Framework/GUI/Runtime/Widgets",
    "ya-gui-compose": "Framework/GUI/Runtime/Compose",
    "ya-scene-core": "Framework/Game/Scene/Core",
    "ya-scene-runtime": "Framework/Game/Scene/Runtime",
    "ya-scene-serialization": "Framework/Game/Scene/Serialization",
    "ya-scene-3d": "Framework/Game/Scene/Scene3D",
    "ya-ecs-core": "Framework/Game/Gameplay/ECS/Core",
    "ya-gameplay-systems": "Framework/Game/Gameplay/Systems",
    "ya-component-linkage": "Framework/Game/Gameplay/Linkage",
    "ya-resource-core": "Framework/Game/Resource/Core",
    "ya-resource-loader": "Framework/Game/Resource/Loader",
    "ya-resource-runtime": "Framework/Game/Resource",
    "ya-render-graph": "Framework/Game/Render/Graph",
    "ya-render-3d": "Framework/Game/Render/Render3D",
    "ya-render-ecs-adapters": "Framework/Game/Render/Adapters",
    "ya-physics": "Framework/Game/Physics",
    "ya-host": "Product/Host",
    "ya-editor": "Product/Editor",
}

# Forbidden engine include prefixes per module (mirrors the xmake lint table).
FORBIDDEN = {
    "ya-foundation-core": ["RHI/", "GUI/", "Host/", "Editor/", "Scene/", "ECS/", "Resource/", "Render3D/", "Physics/", "Gameplay/", "Hierarchy/"],
    "ya-rhi": ["GUI/", "Host/", "Editor/", "Scene/", "ECS/", "Resource/", "Render3D/", "Physics/", "Gameplay/", "Hierarchy/"],
    "ya-rhi-backend-common": ["GUI/", "Host/", "Editor/", "Scene/", "ECS/", "Resource/", "Render3D/", "Physics/", "Gameplay/", "Hierarchy/"],
    "ya-rhi-vulkan": ["GUI/", "Host/", "Editor/", "Scene/", "ECS/", "Resource/", "Render3D/", "Physics/", "Gameplay/", "Hierarchy/"],
    "ya-hierarchy": ["RHI/", "GUI/", "Host/", "Editor/", "Scene/", "ECS/", "Resource/", "Render3D/", "Physics/", "Gameplay/"],
    "ya-gui-resources": ["Host/", "Editor/", "Scene/", "ECS/", "Resource/", "Render3D/", "Physics/", "Gameplay/"],
    "ya-gui-draw2d": ["Host/", "Editor/", "Scene/", "ECS/", "Resource/", "Render3D/", "Physics/", "Gameplay/"],
    "ya-gui-widgets": ["Host/", "Editor/", "Scene/", "ECS/", "Resource/", "Render3D/", "Physics/", "Gameplay/", "RHI/"],
    "ya-gui-compose": ["Host/", "Editor/", "Scene/", "ECS/", "Resource/", "Render3D/", "Physics/", "Gameplay/"],
    "ya-ecs-core": ["Host/", "Render3D/", "GUI/", "Physics/", "Resource/", "Scene/", "Gameplay/"],
    "ya-gameplay-systems": ["Host/", "Render3D/", "Render/", "GUI/", "Editor/"],
    "ya-component-linkage": ["Host/", "Render3D/", "Editor/"],
    "ya-resource-core": ["Host/", "Editor/", "Scene/", "ECS/", "RHI/", "Render3D/", "GUI/", "Physics/", "Gameplay/"],
    "ya-resource-loader": ["Host/", "Editor/", "Scene/", "ECS/", "Render3D/", "GUI/", "Physics/", "Gameplay/"],
    "ya-resource-runtime": ["Host/", "Editor/", "Scene/", "ECS/", "Render3D/", "Physics/", "Gameplay/"],
    "ya-scene-core": ["Host/", "Editor/", "Render3D/"],
    "ya-scene-runtime": ["Host/", "Editor/", "Render3D/"],
    "ya-scene-serialization": ["Host/", "Editor/", "Render3D/"],
    "ya-scene-3d": ["Host/", "Editor/", "Render3D/", "GUI/", "Resource/", "Physics/", "Gameplay/"],
    "ya-render-graph": ["Host/", "Editor/", "Render3D/", "Gameplay/", "Physics/"],
    "ya-render-3d": ["Editor/"],
    "ya-render-ecs-adapters": ["Host/", "Editor/", "GUI/", "Physics/"],
    "ya-physics": ["Host/", "Editor/", "Render3D/", "GUI/"],
    "ya-host": ["Editor/"],
    "ya-editor": [],
}

INCLUDE_RE = re.compile(r'#include\s*[<"]([A-Za-z0-9_/.-]+)\.h[">]')
DEPS_CALL_RE = re.compile(r'add_deps\s*\(')


def _matching_block(text: str, open_paren: int) -> str:
    """Return the text inside the add_deps(...) call starting at open_paren."""
    depth = 1
    i = open_paren
    while i < len(text) and depth > 0:
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
        i += 1
    return text[open_paren:i - 1]


def lint_public_deps() -> list:
    """Scan module xmake.lua files for redundant dependency declarations:
    - the same dep listed twice inside one add_deps call;
    - the same dep declared both public and private across calls.
    Public is the transitive surface; a private re-declaration is noise."""
    violations = []
    for root, dirs, files in os.walk(os.path.join(SRC, "..")):
        dirs[:] = [d for d in dirs if d != "build"]
        if "xmake.lua" not in files:
            continue
        path = os.path.join(root, "xmake.lua")
        rel = os.path.relpath(path, ROOT)
        try:
            text = open(path, encoding="utf-8", errors="ignore").read()
        except OSError:
            continue
        public_deps = set()
        private_deps = set()
        for m in DEPS_CALL_RE.finditer(text):
            block = _matching_block(text, m.end())
            deps = set(re.findall(r'"([A-Za-z0-9_-]+)"', block))
            seen = {}
            for dep in deps:
                seen[dep] = seen.get(dep, 0) + 1
            for dep, count in seen.items():
                if count > 1:
                    violations.append(f"[ya-module-lint] duplicate dep: {rel} lists \"{dep}\" {count}x in one add_deps")
            if re.search(r'public\s*=\s*true', block):
                public_deps |= deps
            else:
                private_deps |= deps
        for dep in sorted(public_deps & private_deps):
            violations.append(f"[ya-module-lint] redundant dep: {rel} declares \"{dep}\" both public and private")
    return violations


def main() -> int:
    violations = 0
    for module, rel_dir in MODULES.items():
        phys = os.path.join(SRC, rel_dir)
        if not os.path.isdir(phys):
            continue
        forbidden = FORBIDDEN.get(module, [])
        for root, dirs, files in os.walk(phys):
            dirs[:] = [d for d in dirs if d != "include"]
            for f in files:
                if not f.endswith((".h", ".cpp")):
                    continue
                path = os.path.join(root, f)
                try:
                    text = open(path, encoding="utf-8", errors="ignore").read()
                except OSError:
                    continue
                for m in INCLUDE_RE.finditer(text):
                    inc = m.group(1) + "/"
                    for prefix in forbidden:
                        if inc.startswith(prefix):
                            print(
                                f"[ya-module-lint] forbidden: {os.path.relpath(path, ROOT)} "
                                f'includes "{prefix}" ({module})'
                            )
                            violations += 1
    for message in lint_public_deps():
        print(message)
        violations += 1
    if violations:
        print(f"ya-module-lint: {violations} violation(s)")
        return 1
    print("ya-module-lint: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
