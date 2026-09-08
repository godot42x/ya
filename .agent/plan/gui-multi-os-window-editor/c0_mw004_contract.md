# MW-004 GUI Framework / GameEditor 契约冻结

> 2026-09-08。不改代码。证据来自 include 扫描与 MW-001..003。

## 冻结不变量

1. 一个 native window = 一个 `GUIWindowHost` = 一个 `WidgetTree` = 一套 snapshot/presentation。所有窗由一个 `AppKernel` 调度。
2. GUI Framework **不** include GameEditor / `EditorLayer` / Scene / ECS。`Framework/GUI` 对 `GameEditor` 符号的命中仅 `Style.h` 注释。RHI **不** 认识 tab / `FDockContext`。
3. `FDockContext` 只是 dock model；`UIDockFloatingHost` 是同 tree 的 Popup 投影，**不**创建 OS window。
4. `EditorTabSpawnerRegistry` 只注册 factory，不拥有 tree/widget。
5. GameEditor 只经 GUI window/host/widget/dock API 使用框架；新代码不得再让 `EditorSurface` 读 `IRender` window/swapchain API（ES-1）。
6. 不新增 `EditorPanel`、中心 event bus、每窗独立 while-loop、`IRender[]`。
7. 共享一个 Vulkan device；每窗独立 surface/swapchain/sync/imported present image。create/rebuild/destroy 只在 frame boundary。
8. GUI 辅助窗不得复用 `PresentationGraphService` / 完整 world graph。主 `ya::App` 窗继续走 `IRender` main-facade。
9. Feature Gallery `Windows` 页（C2G）是框架层第一份真实多窗消费者，先于 GameEditor 第二窗。
10. `Render2D::session` 静态串行复用；每窗唯一 pass slot。第二套 Render2D owner 仅 MW-901。
11. `SelectionModel` / `ActionMap` / `UndoStack` 归属 editor/document session，不按 native window 复制。
12. `EditorSurface` 保持单窗 UI 编排 facade；`EditorWindowSession` 不吸收全部字段变成 god object。

## 允许的依赖方向

```text
RHI device + surface-context
  -> GUIWindowHost / GUIWindowManager / WidgetTree / Render2D compose
       -> GUIWorkbench FeatureGallery（C2G）
       -> GameEditor EditorWindowSession / EditorSurface / tabs
```

禁止反向：GUI → GameEditor；RHI → tab；DockContext → NativeWindow。

## 迁移期例外（必须有删除点）

- `EditorSurface::tick(App&)` forwarding：ES-5 删除。
- `PresentationGraphService` + `IRender::begin/end`：仅主 GameRuntime 窗。
- 旧 `editor.dockLayout`：只解释为 main window；新拓扑 `windows[]` 在 C8。

## 本 checkpoint 边界

- 保留：现有单窗产品路径。
- 未完成：C1 起的实现。C0 审计闭环后才允许 `GUIWindowManager` 编码。
- 偏离：无。
