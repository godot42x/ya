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
