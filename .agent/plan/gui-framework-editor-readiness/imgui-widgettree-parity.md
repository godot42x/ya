# ImGui → WidgetTree Parity Checklist

> **As of:** 2026-09-04 (after Phase 8T)  
> **Default chrome:** `editor.chrome.host = widgettree`  
> **Legacy chrome:** `--editor-chrome=imgui` still runs `EditorLayer::onImGuiRender()`  
> **Purpose:** Gate deletions — do **not** remove ImGui / `imgui-local` until a row is ✅ for widgettree default path.

## How to read

| Symbol | Meaning |
|--------|---------|
| ✅ | Widgettree default path has functional parity for the **core workflow**; safe to delete the ImGui-only *render entry* once verified. |
| 🟡 | Retained path exists but **known gaps** vs legacy ImGui (listed in Notes). |
| 🔴 | **ImGui-only** today (`onImGuiRender` / `FilePicker` / ImGuizmo bridge). Widgettree has no equivalent UI. |
| ⚫ | Old ImGui implementation **already removed**; capability may be partial or replaced elsewhere. |
| ➖ | Not applicable / intentionally out of scope for this migration. |

**Deletion rule (per feature row):**

1. Row is ✅ on widgettree default path **or** explicitly marked ➖.  
2. `feature_matrix.json` evidence updated; `Remaining` cleared or accepted.  
3. Targeted test or editor smoke covers the workflow.  
4. Then: remove ImGui **render entry** → later remove **uncalled dead code** (8N–8P pattern).

---

## Chrome & routing

| Area | Legacy ImGui | WidgetTree (`EditorSurface`) | Status | Notes |
|------|--------------|------------------------------|--------|-------|
| Default host | `EditorModule::onBeforePresentation` → `onImGuiRender` | `onPresentation` → `EditorSurface::tick` + snapshot replay | ✅ | Default since Phase 8I |
| Project browser (no project) | `EditorLayer::projectBrowserWindow` | `EditorSurface::buildProjectBrowser` | ✅ | Both use same `EditorLayer` project APIs |
| Main menu | `EditorLayer::menuBar` (ImGui) | `UIMenuBar` + `ActionMap` | 🟡 | See File menu gaps below |
| Toolbar | `EditorLayer::toolbar` (icon `ImageButton`) | Text `UIButton` row | 🟡 | Visual parity only; actions wired |
| Dock layout | ImGui `DockSpace` | `UIDockWorkspace` + `UIDockSpace` | 🟡 | Docked tree persists (8Q); no floating geometry |
| Editor Settings window | `EditorLayer::editorSettings` | `EditorSurface::openEditorSettingsDialog` | ✅ | View 菜单；sampler/overlay/startup scene |
| Debug images window | `EditorLayer::debugWindow` | — | 🔴 | Viewport debug catalog, channel masks, group viewers |
| Auxiliary modals | `renderAuxiliaryUi` → `FilePicker::render` | Retained popups on `EditorSurface` | 🟡 | Scene save + asset browse + generic file picker migrated |
| Viewport display | `viewportWindow` + `ImGui::Image` | `UIImage` samples offscreen compose | ✅ | Widgettree does not call `viewportWindow` |
| Viewport input / pick / gizmo | `EditorLayer::onEvent` + ImGuizmo | Same `onEvent` + `EditorViewportGizmoOverlay` | 🟡 | Gizmo draw/IO still ImGuizmo; overlay contract retained |
| Viewport context menu | `viewportWindow` → `ContextMenu` (ImGui) | `EditorSurface::openViewportContextMenu` (`UIMenu`) | ✅ | Uses `NodeCreateRegistry` presets + `EditorLayer` cmds |
| Viewport Delete / Duplicate | Context menu only (ImGui path) | `cmdDeleteSelection` / `cmdDuplicateSelection` + Delete / Ctrl+D | ✅ | Works on widgettree via `onEvent` + Edit menu actions |
| ImGui texture bridge | `getOrCreateImGuiTextureID` | Not used by widgettree chrome | ➖ | Still needed for legacy chrome + gizmo |

---

## File / project / scene

| Feature | Legacy ImGui | WidgetTree | Status | Notes |
|---------|--------------|------------|--------|-------|
| New Scene | File menu | `scene.new` action | ✅ | |
| Open Scene | File menu (**TODO stub**) | — | ➖ | Neither path implements real open-scene dialog yet |
| Save Scene | File menu | `scene.save` | ✅ | |
| Save Scene As | File menu → `FilePicker` | `EditorSurface::openSceneSaveDialog` (retained) | ✅ | Phase 8L |
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
| Hierarchy delete | Viewport context menu / shortcuts | `cmdDeleteSelection` + Delete key | ✅ | No hierarchy-tree UI; viewport/menu/shortcut path |
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
| Icons | `ContentBrowserPanel::init` (ImGui tex) | Retained rows (no ImGui icons in list) | 🟡 | Cosmetic |

### Hierarchy

| Feature | Legacy ImGui | WidgetTree | Status | Notes |
|---------|--------------|------------|--------|-------|
| Panel render | `sceneTree` draw | `UITreeView` in dock panel | ⚫ / ✅ | ImGui draw removed 8O |
| Selection sync | `SceneHierarchyPanel` | Same panel as selection bus | ✅ | |
| Entity CRUD from tree UI | Context / ImGui menus | — | 🔴 | Create/delete not in retained hierarchy |

### Inspector (entity / component)

| Feature | Legacy ImGui | WidgetTree | Status | Notes |
|---------|--------------|------------|--------|-------|
| DetailsView stack | `DetailsView` + `TypeRenderer` tree | — | ⚫ | Deleted 8N; was uncalled after 8C |
| Entity/component fields | `TypeRenderer::renderReflectedType` | `PropertyGraph` + `EditorAutoPropertySection` | 🟡 | Reflection path; special editors TBD per component |
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
| Widget tree authoring | `drawWidgetTree` (⚫) | `UITreeView` + canvas pick sync | 🟡 | DnD reorder UI still missing |
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
| Scene save | `openSceneSavePicker` | `EditorSurface` scene-save popup | ✅ | 8L |
| Texture / model asset | `openTexturePicker` / `openModelPicker` | `openAssetPickerDialog` | ✅ | 8M |
| Script / material / directory / generic | `FilePicker::*` | `openFilePickerDialog` + `FEditorFilePickerRequest` factories | ✅ | 8S；legacy imgui chrome 仍可用 ImGui `FilePicker` |
| Editor Settings browse | `FilePicker` in `editorSettings` | `makeSceneJsonFilePickerRequest` → retained picker | ✅ | 8U |

---

## View menu & shell chrome

| Feature | Legacy ImGui | WidgetTree | Status | Notes |
|---------|--------------|------------|--------|-------|
| Viewport 3D / 2D | View menu | View menu + toolbar | ✅ | |
| Editor Settings | — (ImGui window in legacy shell) | View → Editor Settings | ✅ | Phase 8U |
| Fullscreen | View menu checkbox | — | 🔴 | |
| Dock padding / dock flags | View menu | — | 🔴 | Dev-only ImGui dock tuning |
| ImGui demo window | View menu (⚫) | — | ⚫ | Removed 8K |

---

## Infrastructure dependencies

| Dependency | Still required for | Safe to remove when |
|------------|-------------------|---------------------|
| `imgui-local` | Legacy chrome, `FilePicker`, `ContextMenu`, ImGuizmo overlay, debug window | All rows above 🔴→✅ or ➖; gizmo has retained draw path or accepted bridge |
| `TypeRenderer` + `ContainerPropertyRenderer` | **Nothing** (no live caller) | After audit confirms no dynamic load; UI Designer retained inspector lands |
| `FileExplorer::render` | **Nothing** (⚫) | Already removed from Content Browser path |
| `ImGuiImageEntry` / texture bridge | Legacy chrome, gizmo, debug | Legacy chrome removed |

---

## Summary counts (widgettree default path)

| Status | Count (feature rows above) | Interpretation |
|--------|---------------------------|----------------|
| ✅ | ~25 | Core shell + major panels usable |
| 🟡 | ~12 | Usable but incomplete vs legacy or vs ideal |
| 🔴 | ~14 | **Blockers** for full ImGui removal |
| ⚫ | ~10 | Already deleted on ImGui side |

**Critical 🔴 blockers before deleting legacy ImGui chrome:**

1. ~~Viewport context menu (create / duplicate / delete entities)~~ ✅ Phase 8R  
2. ~~Editor Settings (or move settings into retained UI)~~ ✅ Phase 8U  
3. Debug images window (or drop scope)  
4. ~~UI Designer palette + inspector~~ ✅ Phase 8T（tree DnD 仍 🟡）  
5. ~~Remaining `FilePicker` modes~~ ✅ Phase 8S  
6. Floating dock persistence (if tear-off is enabled later)

---

## Suggested migration order (next checkpoints)

1. ~~**8R** — Viewport authoring parity: retained context menu + Delete/Duplicate actions (`ActionMap`)~~  
2. **8S** — ~~Generalize retained file picker~~ ✅  
3. **8T** — ~~UI Designer palette + inspector~~ ✅  
4. **8V** — Debug window: retained panel or descope  
5. **8W** — Remove `onImGuiRender` shell + `imgui-local` after matrix rows ✅  

---

## Maintenance

- Update this file **in the same commit** as any ImGui deletion checkpoint.  
- Cross-link row IDs to `feature_matrix.json` (`retained_*`, `imgui_removal`, `dock_layout_persistence`).  
- A row may move to ✅ only after **manual or automated** verification on `--editor-chrome=widgettree` (default).
