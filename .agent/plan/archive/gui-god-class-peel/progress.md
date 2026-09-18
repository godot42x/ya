# Progress

## C1 当前 checkpoint

- `EWidgetAlignH` / `EWidgetAlignV` / `EWidgetBoxLayout` / `EWidgetMainAxisAlignment` 从 `UIElement.h` 迁到 `UILayout.h`。
- 反射注册跟着枚举走，落在 `UILayout.cpp`。
- `UIElement.cpp` 的 layout / paint / dirty 仍在同一翻译单元，未拆 `UIElementLayout.cpp` / `UIElementPaint.cpp`。
- 只含 `UIElement.h` 就会断的控件/DSL 头补了 `UILayout.h`：`Text.h`、`TextEdit.h`、`UIFrameSnapshot.h`。
- 验证：`xmake b ya-gui-widgets` 通过；`xmake r ya-gui-closure-test -- --gtest_filter='WidgetTreeTest.*:WidgetLayoutTest.*'` 147 passed。

### C1 保留项

- `UIElement.h` 仍是宽内核门面（lifecycle / dirty / hit / focus）；本轮只搬家 layout 枚举。
- VisualFlag 仍贴在 `UIElement.h` 末尾。

## C2 当前 checkpoint

- 抽出 `EditorViewportGizmoController`（`GameEditor/UI/`）；Layer 只 bind 窄 sources 并在 selection / Mode2D 时 `cancelDrag()`。
- Overlay 持有 controller 指针，不再 friend Layer；W/E/R 只走 Overlay。
- compose 仍是 `EditorModule` 调一次 `layer.gizmo().recordOverlay()`；undo 仍是 begin 捕获、end `pushEditorTransformUndo`。
- `buildViewportGizmoFrame` 吃 `Entity*`，不再吃 `const EditorLayer&`。
- 验证：`xmake b ya-game-editor` 通过；`EditorInputContractTest` 4 passed；`EditorViewportOverlayHostTest` 3 passed。

### C2 保留项

- 未做 plane handle / uniform scale / mode UI。
- Viewport 两个 `.cpp` 未动。

## C3 当前 checkpoint

- rebuild 期 dock/workspace 政策迁到 `EditorDockWorkspace`（materialize/invoke/Tools/default layout/persist）。
- `registerEditorActions` 独立为 `EditorActionCatalog`；Surface rebuild 调一次。
- `EditorSurface::tick` 顺序未变：metrics → `tree.tick` → `syncShellDialogs` → `pushViewportDisplay` → snapshot → publish rect → overlay host。
- 公共头去掉 `TreeView.h`、未用转发（`ICommandBuffer` / `UIElement` / `UITextField`）和重复的 `EditorTabSpawnerRegistry` class 前向。
- 验证：`xmake b ya-game-editor` 通过；`WidgetTreeTest.*:DockNodeTest.*` 93 passed；既有 editor 滤镜 37 passed。

### C3 保留项

- `tick` 本体、viewport wrap、`syncShellDialogs` 未拆。
- Inspector `wantsTextInput` 仍 `dynamic_cast`，未做成 tab 自己的焦点查询。


