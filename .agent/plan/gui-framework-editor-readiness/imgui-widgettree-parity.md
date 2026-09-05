# ImGui → WidgetTree Parity Checklist

> **As of:** 2026-09-06 (gui-kernel-ux-parity K4: dock tab close + same-leaf reorder)  
> **Default chrome:** WidgetTree only (`EditorSurface`)  
> **Legacy chrome:** `--editor-chrome=imgui` is ignored (WARN); `onImGuiRender` deleted  
> **Purpose:** Gate remaining `imgui-local` removal — viewport gizmo is native, but leftover helpers still require it. **Hand-feel** is a separate gate: see `.agent/plan/gui-kernel-ux-parity/`.

## How to read

| Symbol | Meaning |
|--------|---------|
| ✅ | Widgettree default path has functional parity for the **core workflow**; safe to delete the ImGui-only *render entry* once verified. **Does not mean** daily ImGui hand-feel (selection, picker, tab close, tree CRUD). |
| 🟡 | Retained path exists but **known gaps** vs legacy ImGui (listed in Notes). Often a feel gap, not a missing host. |
| 🔴 | **No equivalent UI** on the retained path (or still ImGui-only leftover). |
| ⚫ | Old ImGui implementation **already removed**; capability may be partial or replaced elsewhere. |
| ➖ | Not applicable / intentionally out of scope for this migration. |

**Path vs feel:** a row can be ✅ (you can finish the job) and still fail daily editor muscle memory. Kernel feel work lives in `gui-kernel-ux-parity` (K2–K4, E1–E3). Closure dump tests are not a feel gate.

**Deletion rule (per feature row):**

1. Row is ✅ on widgettree default path **or** explicitly marked ➖.  
2. `feature_matrix.json` evidence updated; `Remaining` cleared or accepted.  
3. Targeted test or editor smoke covers the workflow.  
4. Then: remove ImGui **render entry** → later remove **uncalled dead code** (8N–8P pattern).

---

## Chrome & routing

| Area | Legacy ImGui | WidgetTree (`EditorSurface`) | Status | Notes |
|------|--------------|------------------------------|--------|-------|
| Default host | `EditorModule::onBeforePresentation` → `onImGuiRender` | `onPresentation` → `EditorSurface::tick` + snapshot replay | ⚫ / ✅ | ImGui chrome host removed in 8W |
| Project browser (no project) | `EditorLayer::projectBrowserWindow` | `EditorSurface::buildProjectBrowser` | ⚫ / ✅ | ImGui window deleted 8W |
| Main menu | `EditorLayer::menuBar` (ImGui) | `UIMenuBar` + `ActionMap` | ⚫ / 🟡 | ImGui menu deleted 8W; see File menu gaps below |
| Toolbar | `EditorLayer::toolbar` (icon `ImageButton`) | Text `UIButton` row | ⚫ / 🟡 | ImGui toolbar deleted 8W; **feel:** no icons (E2) |
| Dock layout | ImGui `DockSpace` | `FDockContext` + `UIDockSpace` | 🟡 | Path: compact strip, hide-tab-bar, persist `editor.dockLayout`. **Feel (K4):** leaf tab close + same-leaf reorder; Viewport/Hierarchy/Inspector hide close |
| Editor Settings window | `EditorLayer::editorSettings` | `EditorSettingsDialog` hosted by `EditorSurface` | ⚫ / ✅ | ImGui window deleted 8W; 10E owner extract |
| Debug images window | `EditorLayer::debugWindow` | `EditorDebugImagesTab` dock tab | ⚫ / ✅ | ImGui window deleted 8W; cube-face button grid not retained |
| Auxiliary modals | `renderAuxiliaryUi` → `FilePicker::render` | `EditorFilePickerDialog` hosted by `EditorSurface` | ⚫ / ✅ | ImGui FilePicker modal chrome deleted 8W; `FilePicker` type remains for fallback APIs |
| Viewport display | `viewportWindow` + `ImGui::Image` | `UIImage` samples offscreen compose | ⚫ / ✅ | `viewportWindow` deleted 8W |
| Viewport input / pick / gizmo | `EditorLayer::onEvent` + ImGuizmo | Same `onEvent` + `EditorViewportGizmoOverlay` | ✅ | Native gizmo math + `Render2D` compose draw; overlay contract retained |
| Viewport context menu | `viewportWindow` → `ContextMenu` (ImGui) | `EditorSurface::openViewportContextMenu` (`UIMenu`) | ✅ | Uses `NodeCreateRegistry` presets + `EditorLayer` cmds |
| Viewport Delete / Duplicate | Context menu only (ImGui path) | `cmdDeleteSelection` / `cmdDuplicateSelection` + Delete / Ctrl+D | ✅ | Works on widgettree via `onEvent` + Edit menu actions |
| ImGui texture bridge | `getOrCreateImGuiTextureID` | Not used by widgettree chrome | ➖ | Still needed by legacy helper paths |

---

## File / project / scene

| Feature | Legacy ImGui | WidgetTree | Status | Notes |
|---------|--------------|------------|--------|-------|
| New Scene | File menu | `scene.new` action | ✅ | |
| Open Scene | File menu (**TODO stub**) | — | ➖ | Neither path implements real open-scene dialog yet |
| Save Scene | File menu | `scene.save` | ✅ | |
| Save Scene As | File menu → `FilePicker` | `EditorSurface::openSceneSaveDialog` → `EditorFilePickerDialog` | ✅ | 8L; 10E owner extract |
| Exit | File menu | `app.exit` | ✅ | |
| Open project | Project browser | Project browser (retained) | ✅ | |
| Content: open `.scene.json` | ImGui Content Browser (⚫) | `activateContentItem` → `loadScene` | ✅ | Retained only since 8A |
| Default startup scene config | Editor Settings + `FilePicker` | Editor Settings + `openFilePickerDialog` | ✅ | Phase 8U |

---

## Edit / selection / undo

| Feature | Legacy ImGui | WidgetTree | Status | Notes |
|---------|--------------|------------|--------|-------|
| Undo / Redo | — (not in ImGui menu) | Edit menu + `UndoStack` | ✅ | Widgettree **ahead** of legacy menu |
| Multi-select hierarchy | `SceneHierarchyPanel` bus | Same bus + `EditorSurface` tree | ✅ | Ctrl/Cmd + click; Shift range |
| Hierarchy filter | ⚫ ImGui tree | `Reactive` + `bindFilter` | ✅ | |
| Hierarchy reorder (DnD) | ⚫ ImGui tree | `UITreeView` + `EditorHierarchyOps` | ✅ | |
| Viewport pick | `onEvent` | `onEvent` (via `EditorInputNode`) | ✅ | |
| Gizmo translate/rotate/scale | W/E/R + ImGuizmo | Same | 🟡 | Bridge retained; not pure WidgetTree draw |
| Gizmo undo session | `EditorTransformUndo` | Same | ✅ | |
| Hierarchy delete | Viewport context menu / shortcuts | `cmdDeleteSelection` + Delete key + Hierarchy tree menu | ✅ | Viewport/menu/shortcut + **feel (E1):** tree right-click uses `selection.delete` |
| Duplicate selection | Viewport context menu / shortcuts | `cmdDuplicateSelection` + Ctrl/Cmd+D | ✅ | |

---

## Panels (editor tabs)

### Content Browser

| Feature | Legacy ImGui | WidgetTree | Status | Notes |
|---------|--------------|------------|--------|-------|
| Panel render | `ContentBrowserPanel::onImGuiRender` | `EditorSurface::buildContentBrowser` | ⚫ / ✅ | ImGui path removed 8A |
| Mount list + entries | `FileExplorer::render` | Keyed reconciler + visible window | ✅ | |
| Search / filter | ImGui | `UITextField` + fingerprint | ✅ | |
| Texture inspect | Panel callback | `inspectAsset` → Asset Inspector tab | ✅ | |
| Icons | `ContentBrowserPanel::init` (ImGui tex) | Retained rows (no ImGui icons in list) | 🟡 | Cosmetic; textures already loaded in `EditorLayer::onAttach` (E2) |

### Hierarchy

| Feature | Legacy ImGui | WidgetTree | Status | Notes |
|---------|--------------|------------|--------|-------|
| Panel render | `sceneTree` draw | `UITreeView` in dock panel | ⚫ / ✅ | ImGui draw removed 8O |
| Selection sync | `SceneHierarchyPanel` | Same panel as selection bus | ✅ | |
| Entity CRUD from tree UI | Context / ImGui menus | Hierarchy `setOnContextMenu` → `ActionMap` | ✅ | Create Empty / Duplicate / Delete share viewport actions (E1) |

### Inspector (entity / component)

| Feature | Legacy ImGui | WidgetTree | Status | Notes |
|---------|--------------|------------|--------|-------|
| DetailsView stack | `DetailsView` + `TypeRenderer` tree | — | ⚫ | Deleted 8N; was uncalled after 8C |
| Entity/component fields | `TypeRenderer::renderReflectedType` | `PropertyGraph` + `EditorAutoPropertySection` | ✅ | Leaf display names + nested group headers; `editor_density` label column; retained path covers scalars/vectors/enum/color/asset-ref, nested flatten, sequence/map mutation, TextureRef preview. **Feel (K3):** `UIColorEdit` swatch opens SV/hue/hex picker (channel strip secondary) |
| Multi-selection mixed values | DetailsView | `PropertyGraph` intersection + em-dash | ✅ | |
| Asset path Browse | `FilePicker` | `EditorSurface` asset picker popup | ✅ | Phase 8M |
| Game UI Entry summary | DetailsView | `EditorInspectorTab` widget entry block | ✅ | Open in UI Designer button |
| `TypeRenderer` registry | Compiled, `registerBuiltinTypeRenderers` | **No caller** on widgettree | ⚫ | Dead stack; UIDesigner ImGui inspector removed 8P |

### Asset Inspector

| Feature | Legacy ImGui | WidgetTree | Status | Notes |
|---------|--------------|------------|--------|-------|
| Panel render | `AssetInspectorPanel` ImGui | Asset Inspector dock tab | ⚫ / ✅ | 8E / 8N |
| Texture preview | ImGui image | `UIImage` + path status | ✅ | |

### Frame Stats

| Feature | Legacy ImGui | WidgetTree | Status | Notes |
|---------|--------------|------------|--------|-------|
| Panel render | `FrameStatsPanel` ImGui hook | `EditorSurface` `_statsText` sync | ⚫ / ✅ | 8D; inline text stats |
| GPU/CPU deep metrics | `FrameStatsPanel` own tree | Basic frame/delta/FPS/viewport size only | 🟡 | |

### Runtime Tools

| Feature | Legacy ImGui | WidgetTree | Status | Notes |
|---------|--------------|------------|--------|-------|
| Panel render | `RuntimeToolsPanel` sections | `EditorSurface` tab + `Runtime*Section` | ⚫ / ✅ | 8F / 8O |
| Play / Simulate / Stop | ImGui toolbar + panel | Toolbar + Runtime Tools tab | ✅ | |
| Diagnostics / profiling / render settings / RG / RT / debug prims | ImGui helpers (⚫) | Retained sections | ✅ | Helpers deleted 8O |

### UI Designer

| Feature | Legacy ImGui | WidgetTree | Status | Notes |
|---------|--------------|------------|--------|-------|
| Panel render | `UIDesignerPanel::onImGuiRender` | Retained tab: palette + tree + inspector | ⚫ / 🟡 | 8G shell; 8T palette/inspector |
| Document open/save | Data layer | Same `UIDesignerPanel` APIs | ✅ | |
| Widget palette | `drawPalette` (⚫) | `UITypeRegistry` button list + `addPaletteWidget` | ✅ | Phase 8T |
| Widget tree authoring | `drawWidgetTree` (⚫) | `UITreeView` + canvas pick sync | 🟡 | **Feel:** tree DnD not retained (E3 / K1 typed op) |
| Field inspector | `drawInspector` + `TypeRenderer` (⚫) | `PropertyGraph` + `EditorAutoPropertySection` | ✅ | Phase 8T |
| Preview canvas / pick / drop | Data layer (`pickAt`, `applyWidgetDrop`) | 2D viewport mode + `EditorModule` compose | 🟡 | Canvas overlay works in 2D mode; tree DnD not retained |
| Delete widget | Keyboard in 2D canvas mode | `UIDesignerPanel::deleteWidget` via `onEvent` | 🟡 | Works in 2D mode only |

### GUI Workbench

| Feature | Legacy ImGui | WidgetTree | Status | Notes |
|---------|--------------|------------|--------|-------|
| Panel render | `GUIWorkbenchPanel` ImGui | `FWorkbenchSurface` host in dock | ⚫ / ✅ | 8H |
| Demo pages | ImGui compositor | `WorkbenchSurface::buildUI` | ✅ | |

### Render Graph debug

| Feature | Legacy ImGui | WidgetTree | Status | Notes |
|---------|--------------|------------|--------|-------|
| Debug window | `renderGraphWindow` (⚫) | `RuntimeRenderGraphSection` summary in Runtime Tools | ⚫ / 🟡 | 8J; summary only, not full debug UI |

---

## Modals & pickers (`FilePicker`)

| Mode | Legacy ImGui | WidgetTree | Status | Notes |
|------|--------------|------------|--------|-------|
| Scene save | `openSceneSavePicker` | `makeSceneSavePickerRequest` → `EditorFilePickerDialog` | ✅ | 8L; 10E collapsed three overlays into one owner |
| Texture / model asset | `openTexturePicker` / `openModelPicker` | `makeAssetPickerRequest` → same dialog | ✅ | 8M; 10E |
| Script / material / directory / generic | `FilePicker::*` | `openFilePickerDialog` + `FEditorFilePickerRequest` factories | ✅ | 8S；legacy `FilePicker` type remains |
| Editor Settings browse | `FilePicker` in `editorSettings` | `makeSceneJsonFilePickerRequest` → same dialog | ✅ | 8U |

---

## View menu & shell chrome

| Feature | Legacy ImGui | WidgetTree | Status | Notes |
|---------|--------------|------------|--------|-------|
| Viewport 3D / 2D | View menu | View menu + toolbar | ✅ | |
| Editor Settings | — (ImGui window in legacy shell) | View → Editor Settings → `EditorSettingsDialog` | ✅ | 8U; 10E owner extract |
| Fullscreen | View menu checkbox | — | ➖ | OS/window fullscreen; ImGui chrome item deleted 8W |
| Dock padding / dock flags | View menu | — | ➖ | Dev-only ImGui dock tuning; deleted with chrome shell |
| ImGui demo window | View menu (⚫) | — | ⚫ | Removed 8K |

---

## Infrastructure dependencies

| Dependency | Still required for | Safe to remove when |
|------------|-------------------|---------------------|
| `imgui-local` | leftover FilePicker/TypeRenderer, editor-internal texture bridge | FilePicker/TypeRenderer have no callers |
| `TypeRenderer` + `ContainerPropertyRenderer` | **Nothing** (no live caller) | Retained container/map/preview parity landed; safe to delete when `imgui-local` FilePicker is also gone |
| `FileExplorer::render` | **Nothing** (⚫) | Already removed from Content Browser path |
| `ImGuiImageEntry` / texture bridge | legacy helper paths | Helper callers are removed or migrated |

---

## Summary counts (widgettree default path)

| Status | Count (feature rows above) | Interpretation |
|--------|---------------------------|----------------|
| ✅ | ~25 | Core shell + major panels usable |
| 🟡 | ~12 | Usable but incomplete vs legacy or vs ideal |
| 🔴 | ~14 | **Blockers** for full ImGui removal |
| ⚫ | ~10 | Already deleted on ImGui side |

**Critical remaining before removing `imgui-local`:**

1. `FilePicker` / `TypeRenderer` still compile without a live widgettree chrome caller

---

## Suggested migration order (next checkpoints)

Kernel feel (not another `EditorSurface` split) — `.agent/plan/gui-kernel-ux-parity/`:

1. **K0** — Freeze path vs feel (this file + skill)  
2. **K1** — Typed `UIDragDropOperation` only payload ✅ (`1c66af41`; this checkpoint records evidence)  
3. **K2** — `UITextField` selection + DragFloat/SpinBox edit reuse + I-beam ✅  
4. **K3** — ColorEdit SV/hue/hex picker ✅  
5. **K4** — Dock leaf tab close + same-leaf reorder ✅  
6. **E1** — Hierarchy tree right-click CRUD via existing `ActionMap` ✅  
7. **E2** — Toolbar / Content Browser icons  
8. **E3** — UI Designer tree DnD; delete dead `TypeRenderer` / ImGui `FilePicker::render`

Release blockers still outside this line: XP-WIN, XP-OGL, SOAK-HR, remaining `imgui-local` after E3.  

---

## Maintenance

- Update this file **in the same commit** as any ImGui deletion checkpoint.  
- Cross-link row IDs to `feature_matrix.json` (`retained_*`, `imgui_removal`, `dock_layout_persistence`).  
- A row may move to ✅ only after **manual or automated** verification on `--editor-chrome=widgettree` (default).
