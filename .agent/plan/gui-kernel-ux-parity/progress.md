# Progress

## K1 当前 checkpoint（2026-09-06）

- 代码已在 `1c66af41`：`UIDragDropOperation` 是会话 payload；string `beginDrag` 只包 `UIStringDragDropOperation`；Dock 用 `FDockPanelDragDropOp`；Tree 用 `FTreeReorderDragDropOp`；无 `kDockPanelPayload` 前缀解析。
- 不停靠 tab 拖与浮窗标题拖抽 helper。
- 验证：`xmake r ya-gui-closure-test -- --gtest_filter='WidgetTreeTest.Dock*:WidgetTreeTest.*Drag*:ToolControlsTest.SelectableRowDraggable*:ToolControlsTest.TreeViewReorder*'`（14/14）；Gallery `gallery_drop.jsonl` + DragDrop `dragdrop_interaction.jsonl`。

### K1 保留项

- K2–E3 未做。
- 不合并 `GUIApp` 与 `ya::App`；不拆 WidgetTree / UILayout / GUIAppHost。
- 未宣称 retained editor ready。

## K0 当前 checkpoint（2026-09-06）

- 建立 `gui-kernel-ux-parity` 计划工件。
- `gui-framework` skill：closure dump 测试不是 UX 门禁；手感走 Gallery 状态组合 + editor 手测。
- `imgui-widgettree-parity.md` 区分「路径存在」与「手感等价」，并把本线 checkpoint 列为下一批。
- 验证：文档对照；本 checkpoint 不改 C++。

### K0 保留项

- K1–E3 未做（K1 代码已在 `1c66af41`，证据记录留在 K1 checkpoint）。
- 不合并 `GUIApp` 与 `ya::App`；不拆 WidgetTree / UILayout / GUIAppHost。
- 未宣称 retained editor ready。
