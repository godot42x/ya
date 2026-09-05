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
