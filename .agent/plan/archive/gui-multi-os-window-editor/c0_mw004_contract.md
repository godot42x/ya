# MW-004 GUI Framework / GameEditor 契约冻结

> 2026-09-08；2026-09-11 补 Framework / GameEditor 职责三分。证据来自 include 扫描与 MW-001..003。

## 冻结不变量

1. 一个 native window = 一个 `GUIWindowHost` / `IGUIWindowSession` = 一个 `WidgetTree` = 一套 snapshot/presentation。所有窗由一个 `AppKernel` 调度。
2. GUI Framework **不** include GameEditor / `EditorLayer` / Scene / ECS。`Framework/GUI` 对 `GameEditor` 符号的命中仅 `Style.h` 注释。RHI **不** 认识 tab / `FDockContext`。
3. 责任三分：Framework = 窗口 / layout / 投影 / 拖拽机制；GameEditor = typed tab + dock policy；`EditorSurface` = 单窗 chrome 编排。禁止每个 TabWell 自持 dock session，也禁止删掉窗口级 `FDockTreeModel`。
4. `FDockContext` 是 DockSession（`FTabRegistry` + `layout()`/`FDockTreeModel` + opaque policy），不是 OS window。`UIDockSpace` 是 Area 投影（`FDockStackView`）。`UIDockFloatingHost` 是同 tree 的 Popup 投影，**不**创建 OS window。主窗与 detached 窗都是 WindowSession + DockLayout；overlay 只是另一种投影。模型节点是 Split / Stack。
5. `EditorTabSpawnerRegistry` 只注册 factory，不拥有 tree/widget。`EditorDockWorkspace` 不创建 native window、不实现 drag hit-test。
6. GameEditor 只经 GUI window/host/widget/dock API 使用框架；新代码不得再让 `EditorSurface` 读 `IRender` window/swapchain API（ES-1）。
7. 不新增 `EditorPanel`、中心 event bus、每窗独立 while-loop、`IRender[]`、全局 `DockingWorkspace`。
8. 共享一个 Vulkan device；每窗独立 surface/swapchain/sync/imported present image。create/rebuild/destroy 只在 frame boundary。swapchain 不是 Camera/viewport 目标。
9. GUI 辅助窗不得复用 `PresentationGraphService` / 完整 world graph。主窗 Camera 链 + 该窗 display compose；acquire 在 `IRenderSurfaceContext` / host `FPresentFrame` coordinator（R-4）。
10. Feature Gallery `Windows` 页（C2G）是框架层第一份真实多窗消费者，先于 GameEditor 第二窗。
11. `Render2D::session` 静态串行复用；每窗唯一 pass slot。第二套 Render2D owner 仅 MW-901。
12. `SelectionModel` / `ActionMap` / `UndoStack` 归属 editor/document session，不按 native window 复制。
13. `EditorSurface` 保持单窗 UI 编排 facade；`EditorWindowSession` 不吸收全部字段变成 god object。
14. Camera 链对象模型（`c2_view_model.md`）C2 完成前不实现 N 视图；不要为 Material/UI 窗复制 `RenderRuntime`。
15. 指针拖拽 session 对每个 input universe 唯一：`GUIDragRouter` 记录 source/hover window、capture 所属窗、IME 窗与 app-modal。`WidgetTree` 只持有 source-local payload/ghost/observer/capture widget。OS clipboard 所有窗 `bindSdlClipboard`。不是 `GUIApp` 单例，也不是进程单例。GameEditor 与 GUIApp 共用 router，不得再听 SDL。

## 允许的依赖方向

```text
RHI device + surface-context
  -> GUIWindowHost / GUIWindowManager / WidgetTree / Render2D compose
       -> GUIWorkbench FeatureGallery（C2G）
       -> GameEditor EditorWindowSession / EditorSurface / tabs
```

禁止反向：GUI → GameEditor；RHI → tab；DockContext → NativeWindow。

## 迁移期例外（必须有删除点）

- `EditorSurface::tick(App&)` forwarding：ES-5 已删除。
- 主窗 acquire/present 由 host `FPresentFrame` 配对 `IRenderSurfaceContext::begin/end`（R-4）；`RenderRuntime` 只录制。单窗仍一条 cmdBuf，拆第二次 submit 是 R-5。
- 旧 `editor.dockLayout`：v1/v2/v3 只解释为 main window。v4 `windows[]` 是 OS 窗 topology
  （MW-801）。Dock JSON 内 `windows[]` 仍是 NativeWindow placement（MW-707），不是屏幕 bounds。
  坏 monitor 迁到可用屏（MW-802），不得把 overlay `pos` 当 OS origin。
- 产品 extra OS window：restore + present/input 走 `onAfterPresent` / `GUIWindowManager`
  （C9-P）。主窗仍 `onPresentation` + primary surface。禁止 extra 录进 primary cmdBuf。
- 产品 DockSpace NoTarget（C10）：GameEditor 经 `realizeNoTargetTearOff` 回调调用
  coordinator；未接线仍 overlay。`FDockContext` 不创建 native window。TreeLocal 不得当 OS origin。

## 本 checkpoint 边界

- 保留：现有单窗产品路径。
- 未完成：C1 起的实现。C0 审计闭环后才允许 `GUIWindowManager` 编码。
- 偏离：无。
