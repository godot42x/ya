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

