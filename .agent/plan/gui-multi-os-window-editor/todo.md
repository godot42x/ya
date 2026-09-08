# GUI 多 OS Window / GameEditor 多 Editor TODO

> 更新时间：2026-09-08。状态：`[ ]` 未开始，`[-]` 进行中，`[x]` 完成，`[~]` 条件延后。

执行规则：同时最多一个任务为 `[-]`；GUI 与 GameEditor 两条 track 不跨层混提；RHI 改动必须对应 surface/swapchain/frame-resource 验收；计划文件与实现、测试同一 checkpoint 提交；共享工作区已有大量未提交改动，实施前逐文件确认归属。

## C0 — 事实矩阵

- [x] `MW-001` 审计 `IRender`、`VulkanRender`、`VulkanSwapChain`、surface、window handle、`PresentationGraphService`、`getSwapchain()` 的单窗口假设。验收：逐个 API 标记 device-owned、surface-owned 或 main-facade；列出可复用 API、必须 per-window 的状态、不能共享的资源；不改 RHI。
  - 工件：[`c0_mw001_irender_ownership.md`](c0_mw001_irender_ownership.md)。
- [x] `MW-002` 审计 flight index、UBO、vertex/descriptor、Render2D pass slot、DeferredDeletionQueue、resize/minimize/out-of-date/close 时序。验收：给出 instance→device→surface→swapchain→sync→imported image 时序、双窗口 acquire/submit/present 顺序、异常窗口对其他窗口的行为和共享/独占表；不实例化 Render2D session。
  - 工件：[`c0_mw002_frame_boundary.md`](c0_mw002_frame_boundary.md)。
- [x] `MW-003` 登记旧 Editor tabs 的 spawn/destroy/singleton/document/viewport/selection/undo/App 依赖，并分类为 `WindowRootEditor`、`EditorOwnedTool` 或 `WindowTool`。验收：每个 tab 有 owner editor/document、root/nested placement、迁移目标和 detachable 策略；不移动文件。
  - 工件：[`c0_mw003_tab_scope.md`](c0_mw003_tab_scope.md)。
- [x] `MW-004` 冻结 GUI Framework 与 GameEditor contract。验收：GUI 不依赖 Editor，Editor 只经 window/host API 使用 GUI。
  - 工件：[`c0_mw004_contract.md`](c0_mw004_contract.md)。

## C1 — GUI Framework 双窗口

- [x] `MW-101` 实现 `GUIWindowManager` 最小生命周期：create/requestClose/destroy/find/dispatch/tick/render。验收：依赖共享 device + surface-context provider，不调用 `IRender::create` 为每个窗口复制 backend；两个 native window、两个 WidgetTree、统一 loop、输入/resize/focus/close 隔离。
- [x] `MW-102` 实现 per-window input/tick/snapshot。验收：pointer/focus/capture/tooltip/clipboard/DPI/snapshot 不泄漏。

## C2 — GUI/RHI presentation

- [ ] `MW-201` 在 `RHI/Core` 增加 additive surface/presentation context，并在 Vulkan 复用 `VulkanSwapChain` 为每个 `INativeWindow` 创建独立 surface/swapchain/sync。验收：各自 acquire/submit/present；主 `IRender` facade 仍兼容；不把 `IRender` 改成数组。
- [ ] `MW-202` 覆盖 resize/minimize/out-of-date/close/deferred deletion。验收：zero extent、安全销毁、GPU validation 无错误。
- [ ] `MW-203` 实现 per-window Render2D pass/resource isolation：每窗口唯一 pass slot，静态 session 仅串行复用。验收：不同窗口 UI 不覆盖 vertex/descriptor/snapshot，offscreen parity 不退化；并行 recorder 不在本任务实现。

## C2G — FeatureGallery 多窗口实例（gui-framework）

C1/C2 完成后再做。本步只走 `GUIApp` window API 与 GUIWorkbench，不进入 GameEditor / EditorSurface / dock tear-off。

- [ ] `MW-204` GUIApp 对 Feature Gallery 暴露 `openWindow` / `closeWindow`。验收：`FWorkbenchApp` 能创建额外 `GUIWindowHost`；共享 device；不复制 `GUIAppHost::init`；不自建 while-loop。
- [ ] `MW-205` Composition 组增加 `Windows` 页：Open / Close 额外 OS window，每窗独立 WidgetTree 与 retained UI 实例（不共享主 Gallery tree / `FDemoState` handles）。验收：`--start-page Windows` 打开第二扇窗；独立 resize/focus/点击；关副窗主窗仍在；关主窗才退；配套 scenario/smoke。禁止第二份完整 Gallery shell、`FDockContext` floating、或 GameEditor 依赖。

## C3 — GUI 跨窗口 drag primitive

- [ ] `MW-301` 提供 source/target window id、boundary enter/leave、drag keep-alive、延迟 create/destroy；不包含 tab 语义。依赖 C2G：两扇窗必须已是 Feature Gallery 打开的真实 OS window。

## C4 — GameEditor session/tab 解耦（EditorSurface 迁移）

C4 内部顺序固定为 ES-1 → ES-5。不得把 `EditorSurface` 改造成 window manager，也不得把 `EditorWindowSession` 做成更大的 god object。ES-5 完成前禁止创建第二扇 editor window。

- [ ] `ES-1` 把 `tick(App&)` / `applyWindowMetrics(App&)` 改为消费 `FEditorSurfaceContext` 与 `EditorWindowMetrics`。验收：Surface 不再读 `App`/`IRender` window API；viewport frame state 由 context 注入；单窗口行为不变；保留 `tick(App&)` 仅作过渡 forwarding。
- [ ] `ES-2` 引入单元素 `EditorWindowRegistry` + `EditorWindowSession` 持有 Surface、window-root dock、tree/viewport binding。验收：`EditorModule` 不再直接持有 `_editorSurface`；事件/tick/snapshot 经 default window id 路由；仍只有一个 native window。
- [ ] `ES-3` 落地 root/nested ownership：`WindowRootEditor` / `EditorOwnedTool` / `WindowTool`；`_selection`/`_actions`/`_undo` 迁出 Surface，归属 editor/document session。验收：窗口只引用当前激活 root editor；owned tool 带 `ownerEditorId`；不能跨 root editor dock。
- [ ] `ES-4` / `MW-402` 解耦 `FEditorTabSpawnContext`：增加 windowId、scope、optional ownerEditorId、document key、placement/detach policy。验收：factory 只创建 UI content，不持有 Surface、不拥有 tree、不执行 tick。
- [ ] `ES-5` 删除 `EditorSurface::tick(App&)` forwarding；事件、snapshot、viewport、dialogs、`wantsTextInput()` 全部按 window id 路由。验收：`WidgetTree::wantsTextInput()` 取代 `dynamic_cast<EditorInspectorTab*>`；`EditorInputNode` 读 session/tree capability。完成后才允许第二扇 `EditorWindowSession`。
- [ ] `MW-401` 在 ES-5 完成且 GUI C1/C2 可用后，才扩展为两个 editor session 各自拥有 tree、window-root dock、nested editor-owned dock 和 viewport context。验收：owned tool 不能 dock 到其他 root editor/tab，但可按 policy 成为独立 editor window 并保留 ownerEditorId。

## C5 — 主窗口和旧 tab 迁移

- [ ] `MW-501` main window bootstrap：Level Editor 常驻、不可关闭、旧 `editor.dockLayout` 兼容。
- [ ] `MW-502` 迁移 Viewport/Hierarchy/Inspector/Content Browser/Runtime Tools 到 session-owned 路径。

## C6 — Document-backed editors

- [ ] `MW-601` 定义 document identity、dirty、undo、close、singleton、preview ownership。
- [ ] `MW-602` Material/UI/Script 作为 `WindowRootEditor` 主窗口 Dock tabs；各自 Preview/Parameters/Hierarchy/Inspector 作为 `EditorOwnedTool` nested leaves。

## C7 — 真实 tear-off/re-dock

- [ ] `MW-701` root editor 或 detachable owned tool detach 到 native window：source detach → keep-alive owner/document state → create target window → attach target tree。
- [ ] `MW-702` cross-window re-dock、owned tool 禁止挂到其他 root editor/tab、owner-aware close policy、空窗回收和 Level Editor 保护。

## C8 — 持久化与恢复

- [ ] `MW-801` versioned `windows[]` envelope：bounds/monitor/maximized/role、root dock、root editor/document、nested owned dock、window tools、active/focus。
- [ ] `MW-802` 坏 monitor、缺失 asset、窗口关闭中断时的安全恢复。

## 条件延后

- [~] `MW-901` 只有并行/嵌套 recording 真实存在时才实例化 Render2D session。
- [~] `MW-902` 只有独立 world preview 真实需要时才扩展 RenderRuntime/PresentationGraphService；GUI-only 辅助窗不得提前复制 world graph。
- [~] `MW-903` 只有跨平台构建暴露缺口时才增加平台特化 window backend。
