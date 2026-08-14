# GUIWorkbench feature-gallery regression matrix

> Phase D contract: GUIWorkbench is an example-layer regression application.
> Framework owns reusable widgets, layout, routing and tooling; this matrix
> owns demo-page coverage and golden/scenario bookkeeping.

## Page taxonomy

| Group | Page | Primary contract | Current evidence | Next Phase D artifact |
| --- | --- | --- | --- | --- |
| Render/debug | `Render` | baseline text/image/button and coordinate overlay | `render_probe_interaction.jsonl` | scenario + zero-diff BMP baseline |
| Basic interaction | `Widgets` | button, slider, checkbox, combo/focus | `widgets_interaction.jsonl` | scenario + zero-diff BMP baseline |
| Layout | `Layout` | nested box/slot composition | `layout_spacing_interaction.jsonl` | scenario + zero-diff BMP baseline |
| Basic interaction | `Menus` | menu bar, popup shield, hover switch | `menus_popup_interaction.jsonl` | scenario + zero-diff BMP baseline |
| Basic interaction | `DragDrop` | drag session + accepting target | `dragdrop_interaction.jsonl` | scenario + zero-diff BMP baseline |
| Overlay/debug | `Modal` | modal popup focus and Esc/shield dismissal | `modal_interaction.jsonl` | scenario + zero-diff BMP baseline |
| Layout | `ScrollSplit` | split capture, wheel bubble, resize safety | `resize_scrollsplit_interaction_stress.jsonl` | scenario + zero-diff BMP baseline |
| Tree/property reference | `Editor` | reusable workbench/editor reference surface | `editor_inspector_interaction.jsonl` | scenario + zero-diff BMP baseline |

## Rules

1. A page scenario targets one named start page and must make its state
   transition explicit with checkpoint dumps.
2. A golden is paired with structure: screenshot pass alone is never enough.
   The checkpoint JSON must prove page identity and its relevant widget/route
   state.
3. Page scenarios live in `Example/GUIWorkbench/Scenarios/`; generated BMP
   captures/diffs remain ignored under `build/gui-convergence-diagnostics/`.
4. Do not move page-specific builders or demo state into `Framework/GUI`.
5. The existing `resize_scrollsplit_interaction_stress.jsonl` remains the
   high-value layout/interaction gate rather than being reduced to a static
   screenshot.
6. Scenarios must use host-side `assert` packets for their semantic contract;
   checkpoint JSON and BMP golden capture remain complementary evidence.

## Current matrix verification

On August 13, 2026, all eight page scenarios completed with host assertions,
checkpoint dumps, a generated baseline BMP, and a subsequent zero-tolerance
`--scenario-golden` / `--scenario-diff` pass. The generated captures and diff
images live under ignored `build/gui-convergence-diagnostics/phase-d-page-matrix/`;
the versioned JSONL scenario is the reproducible source contract.
