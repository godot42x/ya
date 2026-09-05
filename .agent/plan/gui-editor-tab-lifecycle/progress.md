# Progress

## P0 当前 checkpoint

- `WidgetTreeTest` 覆盖：只 tick attached/visible 且 opt-in 的节点；hidden/detached 跳过；`UICompoundWidget` 不驱动 child tick，tree 递归驱动。
- `EditorSurface::tick` 在 window metrics 之后调用 `_tree->tick(dt)`。
- skill 产品循环写明 Editor 与 GUIApp 一样先 `WidgetTree::tick`。

## P1 当前 checkpoint

- `EditorTabSpawnerRegistry` 注册 Inspector / Content / Runtime / Designer / Asset / Debug / Stats / Workbench；Surface `invokeTab` 先 `activatePanel`，否则 spawn + `addPanel`。
- `FDockContext` 增加 `findPanelByStableKey` / `hasPanel` / `activatePanel` / `collectLayoutPanelKeys`；未选中 tab 经 Dock 投影 detach 后不再 tick。
- Tools 菜单与 `--editor-tab` 走同一 invoke；无有效 layout 时 spawn 默认 workspace ids。
- 上述 Tab 改为 compound/widget root，`sync()` 离开 Surface；`syncPresentation` 只留 viewport / hierarchy / selection / toolbar / dialogs。
- 验证：`DockNodeTest`（含 activate/detach tick）、`EditorTabSpawnerRegistryTest`、`EditorListRowsTest`、`EditorFilePickerDialogTest` 通过；`ya-game-editor` 构建通过。

### P1 保留项

- Hierarchy / Viewport 仍由 Surface 物化，尚未成为 spawner tab。
- Selection / hierarchy 仍靠 Surface fingerprint 轮询。
- Surface 仍有 `syncPresentation`。

## P2 当前 checkpoint

- `EditorLayer` 在 `++_selectionGeneration` 处广播 `onSelectionChanged`；`notifyHierarchyChanged()` 覆盖 scene context、create/delete/duplicate、Inspector rename、Hierarchy reorder。
- `EditorHierarchyTab` 为 compound root：`onAttached` 拉 scene 并订两个 delegate；Surface 不再持 Hierarchy 控件或 fingerprint 轮询。
- Project browser 用独立 `_projectList` / `_projectRoots`，刷新走 `ReactiveList::replace`。
- Inspector 结构跟 selection/hierarchy delegate；`tick` 只拉已投影属性值。
- 验证：`ya-game-editor` 构建通过；`EditorHierarchyOpsTest`、`EditorTabSpawnerRegistryTest`（含 builtin hierarchy）通过。

### P2 保留项

- Viewport 仍由 Surface `buildViewportBody` 物化。
- Surface 仍有 `syncPresentation`（viewport 纹理 / toolbar / dialogs / project browser）。

## P3 当前 checkpoint

- Viewport 由 `EditorViewportTab` spawn；`IEditorViewportHost` / `IEditorViewportHostSink` 让 Surface 推 display image、读 rect/hover/focus，不再持有 `_viewportImage`。
- `EditorSurface::tick`：window metrics → `WidgetTree::tick` → shell chrome → push viewport display → snapshot → overlay bridge。没有 `syncPresentation`。
- 验证：`ya-game-editor` 构建通过；`EditorViewportTabTest`、`EditorViewportOverlayHostTest`、`EditorInputContractTest` 通过。

### P3 保留项

- `syncShellChrome` 仍每帧刷 toolbar / dialogs / project browser；P4 把 toolbar 改 `onAppStateChanged` 并清残留 helper。

## P4 当前 checkpoint

- Surface 只剩 dialog `syncShellDialogs`、viewport wrap/bridge、project browser 的 `ReactiveList::replace`（refresh/open 时，不再每帧扫）。
- Toolbar 模式字订 `App::onAppStateChanged`。
- skill GameEditor chrome：禁止 `tab->sync` / Surface 持 Tab 指针；tick 为 metrics → tree.tick → dialogs → push display → snapshot → overlay bridge。
- 验证：`ya-game-editor` 构建通过；`ya-gui-closure-test` `WidgetTreeTest.*:DockNodeTest.*` 93 passed；既有 editor 测试集 17 passed。
