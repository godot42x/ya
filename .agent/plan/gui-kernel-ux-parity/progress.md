# Progress

## K2 当前 checkpoint（2026-09-06）

- `FTextEditState` 是 TextField / DragFloat 编辑态 / SpinBox 编辑态的唯一选区+插入实现；无第三套迷你编辑器。
- `UITextField`：Shift+方向、拖选、primary+A、拷切贴作用在选区上；hover I-beam。
- `ECursorType::IBeam` 接到 GUI host 与 `InputRouter`（SDL_SYSTEM_CURSOR_TEXT）。
- 验证：`ToolControlsTest.TextField*` / `DragFloatEditReusesTextSelection` / `SpinBoxEditReusesTextSelection` / `EditorInputContractTest.TextField*`；headless `widgets_text_selection.jsonl` + `widgets_interaction.jsonl`。

### K2 保留项

- K3–E3 未做。
- Shift+click 扩展选区未做（鼠标事件无 modifier；拖选 + Shift+方向已覆盖）。
- 不合并 `GUIApp` 与 `ya::App`；不拆 WidgetTree。
- 未宣称 retained editor ready。

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
