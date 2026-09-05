# Progress

## P0 当前 checkpoint

- `WidgetTreeTest` 覆盖：只 tick attached/visible 且 opt-in 的节点；hidden/detached 跳过；`UICompoundWidget` 不驱动 child tick，tree 递归驱动。
- `EditorSurface::tick` 在 window metrics 之后调用 `_tree->tick(dt)`；本步仍保留 `syncPresentation`（Tab 尚未 opt-in）。
- skill 产品循环写明 Editor 与 GUIApp 一样先 `WidgetTree::tick`。

### P0 保留项

- Tab 仍由 Surface `syncPresentation` 轮询。
- Hierarchy / Viewport 仍在 Surface。
- 未宣称 Tab Spawner 完成。
