#!/usr/bin/env bash
# Module-boundary CI matrix: the four profile × linkage combinations must come
# from the same module sources/deps/package descriptions.
#
# Usage: Script/ci.sh [engine-shared | engine-monolith | gui-shared | gui-monolith | all]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

run_engine_shared() {
    echo "== engine + shared =="
    xmake f -c -m debug --ya_profile=engine --ya_linkage=shared -y
    xmake f -m debug --ya_profile=engine --ya_linkage=shared -y
    xmake b -r ya-engine
    # xmake b accepts one target per invocation (xmake 3.0.8).
    for t in ya-testing ya-gui-closure-test ya-gui-widgets-test ya-ecs-core-test ya-resource-core-test \
             ya-resource-runtime-closure-test ya-rhi-vulkan-smoke ya-render-3d-test; do
        xmake b "$t"
    done
    xmake r ya-testing
    xmake r ya-resource-runtime-closure-test
    xmake r ya-rhi-vulkan-smoke
    xmake r ya-gui-widgets-test
    python3 Script/ya_module_lint.py
}

run_engine_monolith() {
    echo "== engine + monolith =="
    xmake f -c -m debug --ya_profile=engine --ya_linkage=monolith -y
    xmake f -m debug --ya_profile=engine --ya_linkage=monolith -y
    xmake b -r ya-runtime
    xmake b -r ya-editor
    xmake b ya-runtime
    xmake b HelloMaterial
}

run_gui_shared() {
    echo "== gui + shared =="
    xmake f -c -m debug --ya_profile=gui --ya_linkage=shared -y
    xmake f -m debug --ya_profile=gui --ya_linkage=shared -y
    xmake b -r ya-gui-framework
    xmake b -r ya-gui-minimal-host
    xmake b ya-gui-closure-test
    xmake b ya-gui-widgets-test
    xmake r ya-gui-closure-test
    xmake r ya-gui-widgets-test
}

run_gui_monolith() {
    echo "== gui + monolith =="
    xmake f -c -m debug --ya_profile=gui --ya_linkage=monolith -y
    xmake f -m debug --ya_profile=gui --ya_linkage=monolith -y
    xmake b -r ya-gui-minimal-host
}

case "${1:-all}" in
    engine-shared) run_engine_shared ;;
    engine-monolith) run_engine_monolith ;;
    gui-shared) run_gui_shared ;;
    gui-monolith) run_gui_monolith ;;
    all)
        run_engine_shared
        run_engine_monolith
        run_gui_shared
        run_gui_monolith
        ;;
    *) echo "usage: $0 [engine-shared|engine-monolith|gui-shared|gui-monolith|all]" >&2; exit 2 ;;
esac

echo "CI matrix OK"
