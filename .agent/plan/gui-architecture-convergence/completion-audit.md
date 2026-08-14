# GUI architecture convergence completion audit

> Audit date: August 14, 2026  
> Scope: `.agent/plan/gui-architecture-convergence/plan.md`, Phase 0 through
> Phase F. This records current-state evidence, not intended future work.

## Requirement-by-requirement evidence

| Plan area | Required outcome | Current evidence | Status |
| --- | --- | --- | --- |
| Phase 0 rendering | top-left coordinates, clip/scissor, resize, multi-flush, window/headless/offscreen parity | ScrollSplit structural/semantic parity, zero-diff window/offscreen BMP, resize+drag+wheel scenario, Render2D diagnostics recorded in `progress.md` | Pass on Windows Vulkan |
| Phase 0 interaction | stable pointer bootstrap, hover retirement, focus switching | `widgets_interaction.jsonl` asserts release state, hover retirement and focus-path transfer | Pass |
| Phase A owner chain | one `AppKernel` loop and clear App/Window/Tree ownership | `owner-model.md`; `GUIApp -> GUIWindowHost -> WidgetTree`; Product frame loop uses kernel adapter | Pass |
| Phase B layout/slot | parent-owned slots, Box layout host, reparent/invalidation/dump | `UILayout`, `UISlot`, `UIBoxLayout`, `UIBoxSlot`; closure tests and layout dump packets | Pass |
| Phase C routing | path discovery separated from delivery; preview/target/bubble; capture/popup/modal/drag policy; route diagnostics | `event-routing.md`, `WidgetRouteTrace`, `WidgetTreeDump`, route overlay, closure tests | Pass |
| Phase D Workbench | feature gallery, scenario+structural assertion+golden for each page | `workbench-regression-matrix.md`; 8 page scenarios; post-layout audit ran all eight against zero-tolerance BMP baseline | Pass on Windows Vulkan |
| Phase E specialized layouts | split/scroll/single child geometry extracted from widgets | `specialized-layouts.md`; `UISplitLayout`, `UIScrollLayout`, `UISingleChildLayout`; 103 closure tests and ScrollSplit post-refactor zero-diff | Pass |
| Phase F future multi-window | owner, active window, focus, modal and cross-window drag semantics written without premature docking | `multiwindow-semantics.md` | Pass as a design boundary |
| Cross-path smoke | headless/minimal host/product runtime/editor remain viable | `ya-gui-headless-host-test` 1/1; minimal host; `ya-host`, `ya-editor`; HelloMaterial runtime/editor 30-frame smoke | Pass on Windows Vulkan |
| Cross-platform validation | Vulkan **and MoltenVK** validation clean | Windows Vulkan evidence is current. This runner is Windows 10 with no macOS/Xcode/MoltenVK runtime, so no MoltenVK command can be honestly executed here. Required macOS commands are recorded in `progress.md`. | **Open external gate** |

## Commands executed in this audit

```powershell
xmake r ya-gui-closure-test                  # 104 / 104 PASS
xmake r ya-gui-headless-host-test            # 1 / 1 PASS
xmake r ya-gui-minimal-host --exit-after-frame=30
xmake b ya-host
xmake b ya-editor
python Script/ya.py run --project Example/HelloMaterial/HelloMaterial.yaproject -- `
  --exit-after-frame=30 --log-level=warn
python Script/ya.py run-editor --project Example/HelloMaterial/HelloMaterial.yaproject -- `
  --exit-after-frame=30 --log-level=warn
```

The Workbench page matrix was also re-run after the specialized-layout refactor:

```text
Render, Widgets, Layout, Menus, DragDrop, Modal, ScrollSplit, Editor
```

Every page completed host assertions and zero-tolerance
`--scenario-golden` / `--scenario-diff` comparison successfully.

After the specialized-layout refactor, ScrollSplit was also re-checked on both
cross-presentation paths:

```text
windowed snapshot JSON SHA-256 == headless snapshot JSON SHA-256
windowed GPU BMP / offscreen BMP zero-diff
```

## Remaining completion gate

This goal cannot be marked complete until the macOS/MoltenVK gate runs on a
macOS runner and records:

```bash
python3 Script/gui_convergence_macos_validation.py
```

The harness runs closure, exact windowed/headless snapshot identity, a
macOS-local ScrollSplit baseline capture, and a second
`--scenario-golden` / `--scenario-diff` pass. Completion evidence is its
exit 0 plus logs with zero VUID/error/assert.

Until then, the implementation and Windows gate are complete, but the
cross-platform validation requirement is intentionally unproven.
