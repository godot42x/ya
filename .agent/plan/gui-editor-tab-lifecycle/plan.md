# Editor Tab Spawner 与 WidgetTree 生命周期

## 目的

移除 `EditorSurface` 对各 Sub UI 的手写 `sync()` 调度。Surface 只做 editor shell：tree、Dock、菜单/工具入口、默认 workspace、snapshot 与 viewport render bridge。Tab 通过 UE 风格 Tab Spawner 注册和创建；Tab root 是普通 `UIElement` / `UICompoundWidget`，attach/detach/tick 由 `WidgetTree` 唯一驱动。

不新增 `EditorPanel`，不新增中心事件总线，不合并 `GUIApp` 与 `ya::App`。

## Checkpoints

- **P0**：Editor 路径补上 `WidgetTree::tick`；锁住 opt-in tick 契约
- **P1**：Tab Spawner registry、Dock 按 stable id 查询/激活、默认 workspace；迁移无 Viewport/Hierarchy 的 Tab 离开 Surface `sync`
- **P2**：Hierarchy 成为 Tab root；Selection / hierarchy 改 Layer 通知
- **P3**：Viewport 作为 shell-extension Dock Tab；Surface tick 收口为 metrics → tree.tick → snapshot → viewport bridge
- **P4**：删除 Surface 残留 Tab 成员与 helper；对齐 skill

前序结构面见 `.agent/plan/gui-editor-structure/`（C0–C3 已完成）。
