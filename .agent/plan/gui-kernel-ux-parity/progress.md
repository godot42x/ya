# Progress

## E2 当前 checkpoint（2026-09-06）

- Toolbar Play / Simulate / Stop 用 `UIImage` + `editor_icons` 路径，不再纯文字块。
- Content Browser / FilePicker 行带 folder/file 图标；`editor_density` 控制图标尺寸。
- `EditorSurface` snapshot 接 `resolveGameUITexture`，与 `EditorLayer::onAttach` 已加载的纹理同一 cache。
- 验证：`ToolControlsTest.ImageDumpReportsAssetPath`；`EditorListRowsTest.*`。Editor toolbar 接线已在 `fe7bf44e` 的 `EditorSurface` 中。

### E2 保留项

- E3 未做。
- Mode3D / Mode2D 仍是文字按钮（没有对应图标资产）。
- 不合并 `GUIApp` 与 `ya::App`；不拆 WidgetTree。
- 未宣称 retained editor ready。

## E1 当前 checkpoint（2026-09-06）

- `UITreeView` 右键选中行（或空白处 `nodeId=""`）并回调 host；DSL `setOnContextMenu`。
- Editor：`selection.createEmpty` 与 duplicate/delete 同一 `ActionMap`；Hierarchy 与 viewport 菜单都 `fromAction`。
- Gallery：右键 `GalleryTree` 打开 Create/Duplicate/Delete 菜单，点 Create 后关闭。
- 验证：`ToolControlsTest.TreeViewRightClick*`；headless `gallery_tree_context.jsonl`。Editor 接线已在 `810170da` 的 `EditorSurface` 中。

### E1 保留项

- E2–E3 未做。
- Hierarchy 菜单没有 viewport 的 Create 3D Object / Light 子菜单（同一 ActionMap 三命令即可）。
- 不合并 `GUIApp` 与 `ya::App`；不拆 WidgetTree。
- 未宣称 retained editor ready。

## K4 当前 checkpoint（2026-09-06）

- `UITabButton` 可关 tab 有 close hit-zone；`FDockContext::closePanel` 走已有 `FDockTreeModel::removePanel`。
- 同 leaf 拖到 tab 条调用 `movePanel` 重排；`FDockPanelRecord.closable=false` 隐藏关闭钮。
- Editor：Viewport / Hierarchy / Inspector 不可关；Workbench Scene 不可关、Console 可关。
- 验证：`WidgetTreeTest.DockSpaceTabCloseRemovesClosablePanel` / `DockSpaceSameLeafTabDropReorders` / `DockNodeTest.SameLeafMoveReordersTabs`；headless `dock_tabs.jsonl`（关 Console + 西叶 Assets/Hierarchy 换序）。布局仍走已有 `editor.dockLayout` JSON。

### K4 保留项

- E1–E3 未做。
- 浮窗标题栏 X 仍是整窗 re-dock，不是关单个 floating tab 的唯一路径（叶内 × 对 floating tab 也接到 `closePanel`）。
- 不合并 `GUIApp` 与 `ya::App`；不拆 WidgetTree。
- 未宣称 retained editor ready。

## K3 当前 checkpoint（2026-09-06）

- `UIColorEdit` 色板打开 SV 方 + hue 条 + hex/rgba 选色器（`FColorPicker` 单 paint 面，不是 `UICompoundWidget`）。
- 点选 SV/hue 实时改色且不关 popup；靠近窗口底边时 picker 翻到色板上方。
- RGBA 通道条仍是次要路径。Inspector `EditorAutoPropertySection` 已用同一 `UIColorEdit`，零改。
- 验证：`ToolControlsTest.ColorEditSwatchOpensSvHuePicker`（含 hex 提交）；headless `gallery_color_picker.jsonl` + `gallery_acceptance.jsonl`。

### K3 保留项

- K4–E3 未做。
- 不合并 `GUIApp` 与 `ya::App`；不拆 WidgetTree。
- 未宣称 retained editor ready。

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
