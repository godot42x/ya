# Editor release checklist

This is the Phase 9 gate that decides whether we may say **retained editor ready**.
Any row that is not **Pass** blocks that claim.

Host for recorded Pass rows: macOS 15.5, Clang, Vulkan, Apple M5. Date: 2026-09-04.

| ID | Gate | Evidence | Status |
| --- | --- | --- | --- |
| 9A | Bounded widgettree editor smoke | `python3 Script/automation/editor/run_widgettree_editor_smoke.py` (9A progress: ping, viewport, camera, frames, presentation PNG, clean quit) | Pass (macOS) |
| 9B | Editor-scale baseline | `xmake r ya-gui-closure-test --gtest_filter='EditorScaleBaselineTest.*'` (3/3); full closure 395/395 after 9D | Pass |
| 9C | Attach/detach + theme + deferred texture soak | `EditorLongRunSoakTest.*` (4/4) | Pass |
| 9D | DPI / CJK IME / clipboard / text editing | `EditorInputContractTest.*` (4/4); CJK fallback also `WidgetLayoutTest.ScaledViewScalesFallbackGlyphsByOwnDesignSize` | Pass |
| 9E | Snapshot digest + route trace + GPU/offscreen parity | `python3 Script/automation/gui/run_workbench_gpu_parity.py --skip-build` exit 0; log `pass=true differing=0 ratio=0.0000` (1280×800 Widgets page) | Pass (macOS/Vulkan) |
| XP-MAC | macOS/Clang editor + GUI closure | `xmake b ya-game-editor`; `xmake r ya-gui-closure-test` 395/395 | Pass |
| XP-WIN | Windows/MSVC compile + same tests | No Windows host in this session; designated-initializer / `YA_*_API` rules in `.agent/skills/cross-platform/SKILL.md` are the compile contract, not a substitute for a green MSVC run | **Blocker** |
| XP-OGL | OpenGL presentation | `GUIAppHost` asserts `VulkanSwapChain`; WidgetTree chrome compose is Vulkan-only. OpenGL 3D runtime is a separate matrix and was not run | **Blocker** |
| SOAK-HR | Multi-hour process soak | 9C is structural cycle soak (64/32), not a long-lived editor process | **Blocker** (descoped to structural soak) |
| IMGUI | `imgui-local` removed | Native viewport gizmo no longer uses `GuiSystem` / `ImGuizmo`; retained inspector now covers nested/composite + vec2/int core fields, but `imgui-local` still remains for FilePicker and legacy `TypeRenderer` / `ContainerPropertyRenderer` cleanup | **Blocker** for “no ImGui in process”, not for WidgetTree-only chrome |

## Ready claim

**Not ready.** WidgetTree is the only editor chrome host on the verified macOS/Vulkan path, but Windows/MSVC, OpenGL presentation, hour-scale soak, and full `imgui-local` removal remain open.

Do not advertise “retained editor ready” until XP-WIN is Pass and the remaining Blocker rows are either Pass or an explicit product descope recorded in `progress.md`.
