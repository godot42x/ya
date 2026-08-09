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
    xmake b ya-testing ya-gui-closure-test ya-ecs-core-test ya-resource-core-test ya-render-3d-test
    xmake r ya-testing
    python3 Script/ya_module_lint.py
}

run_engine_monolith() {
    echo "== engine + monolith =="
    xmake f -c -m debug --ya_profile=engine --ya_linkage=monolith -y
    xmake f -m debug --ya_profile=engine --ya_linkage=monolith -y
    xmake b -r ya-runtime ya-editor
    xmake b ya-runtime HelloMaterial
}

run_gui_shared() {
    echo "== gui + shared =="
    xmake f -c -m debug --ya_profile=gui --ya_linkage=shared -y
    xmake f -m debug --ya_profile=gui --ya_linkage=shared -y
    xmake b -r ya-gui-framework ya-gui-minimal-host ya-gui-closure-test
    xmake r ya-gui-closure-test
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
