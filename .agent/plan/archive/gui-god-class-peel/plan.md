# God class 收口：UIElement / Gizmo / EditorSurface

三个点不是同一类问题，不能按行数一刀切。大文件本身不是罪；主链路不要横切。

## Checkpoints

- **C1**：layout-only 枚举离开 `UIElement.h`，进入 `UILayout.h`；`UIElement.cpp` 的 layout→paint→dirty 仍同一翻译单元
- **C2**：抽出 `EditorViewportGizmoController`；Overlay 不再 friend Layer；compose / undo 语义不变
- **C3**：Surface rebuild 期 dock/workspace + actions 拆文件；`tick` 顺序不变；清理无用头转发

## 明确不做

- 多实例 document tab
- gizmo plane / uniform scale / mode UI
- 合并 GUIApp 与 `ya::App`
- 把 UIElement 按 layout/paint 拆 cpp
- Surface 再持 tab 指针 / `tab->sync` / 中心总线
- 拆 `EditorSurface::tick` 本体
