# GUI 多 OS Window / GameEditor 多 Editor TODO

> 更新时间：2026-09-11。状态：`[ ]` 未开始，`[-]` 进行中，`[x]` 完成，`[~]` 条件延后。C15 已完成。剩余条件延后 R-5 / MW-901/902/903。不得把 Gallery extra 冒充 GameEditor soak，也不得把 R-5 写成完成。

方向：[`plan.md`](plan.md)「冻结：device / present / camera」。Camera 链（`c2_view_model.md`）已冻结：C2 完成前不实现 N Camera / `FRenderViewDesc`。

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

- [x] `MW-201` 在 `RHI/Core` 增加 additive surface/presentation context，并在 Vulkan 复用 `VulkanSwapChain` 为每个 `INativeWindow` 创建独立 surface/swapchain/sync。验收：各自 acquire/submit/present；主 `IRender` facade 仍兼容；不把 `IRender` 改成数组。
- [x] `MW-201c` 冻结 present ≠ viewport：world 画离屏 `RenderTexture`；surface 只在 present 时选择 swapchain image 做 compose。验收：`PresentationGraphService` 注入 `IRenderSurfaceContext`；world cmdBuf 按 flight 不按 imageIndex；viewport 不 fallback swapchain extent。工件：[`c2_present_compose_model.md`](c2_present_compose_model.md)。
- [x] `MW-201d` 冻结 Camera 链：`graphics → UI → view compose` 写相机 RT；`display compose → present` 才碰窗口。验收：名词与 `renderFrame` 注释对齐；不实现 N Camera。工件：[`c2_view_model.md`](c2_view_model.md)。
- [x] `MW-202` 覆盖 resize/minimize/out-of-date/close/deferred deletion。验收：zero extent、安全销毁、GPU validation 无错误。
- [x] `MW-203` 实现 per-window Render2D pass/resource isolation：每窗口唯一 pass slot，静态 session 仅串行复用。验收：不同窗口 UI 不覆盖 vertex/descriptor/snapshot，offscreen parity 不退化；并行 recorder 不在本任务实现。
- [x] `MW-206` 不可上屏 surface：最小化/zero extent 只 skip 该窗 acquire/present，delay swapchain recreate；AppKernel 与其它窗继续。验收：最小化 extra 时 primary 仍 present；恢复后下一帧 recreate；去掉 GameRuntime `_bMinimized` sleep。工件：[`c2_unpresentable_surface.md`](c2_unpresentable_surface.md)。不做 extra `renderAll`、N Camera。
- [x] extra `GUIWindowManager::renderAll` present：共享 device 上每 extra 一扇 `IRenderSurfaceContext`；串行 compose；不可上屏 skip；resize 只 wait 该 surface。验收：`xmake r GUIWorkbench --extra-window --exit-after-frame=8` 双窗 swapchain present 后干净退出。
- [x] `MW-207` 多窗口 present 消费方收口到 `IRenderSurfaceContext` / `ISwapchain`：`GUIAppHost`、`GUIWindowPresent`、`PresentationGraphService`、`RHISurfaceContextTest` 禁止 `as<VulkanSwapChain>()`；imported images 走 `buildPresentationImages`。验收：source-scan + `ya-rhi-vulkan-smoke` RHISurfaceContext。不做 OpenGL 多窗实现、N Camera、两次 submit。

## C2G — FeatureGallery 多窗口实例（gui-framework）

C1/C2 完成后再做。本步只走 `GUIApp` window API 与 GUIWorkbench，不进入 GameEditor / EditorSurface / dock tear-off。

- [x] `MW-204` GUIApp 对 Feature Gallery 暴露 `openWindow` / `closeWindow`。验收：`FWorkbenchApp` 经 `GUIApp::openWindow` 创建 extra `GUIWindowManager` slot（**不是**第二份 `GUIWindowHost` / 第二套 loop）；共享 device；present 走 `renderAll`。
- [x] `MW-205` Composition 组 `Windows` 页：Open / Close extra OS window，独立 WidgetTree（`ExtraOsWindowDemo`，不共享 Gallery tree / `FDemoState`）。验收：`--start-page Windows`；headless scenario 锁页控件；windowed `--smoke-actions` 点 Open、点 extra、resize extra、Close；关副窗主窗仍在。Golden BMP 延后（双 swapchain 不能用主窗一张 capture 冒充）。禁止第二份 Gallery shell、`FDockContext` floating、或 GameEditor 依赖。

## C2R — Render boundary follow-up（依赖 C2/C2G，不进入 N Camera）

- [x] R-1 将 RenderRuntime::FrameInput 收口为 CameraFrameInput、ViewComposeInput、DisplayComposeInput、PresentFrameInput；只做 typed additive migration，不改变 submit 次数。
- [x] R-2 让 Camera owner 在 graph build 前生成 view/projection/viewProjection/extent；BaseRenderPipeline、Forward/Deferred、debug/overlay 只消费 CameraFrameInput，不从 swapchain/window 猜尺寸。
- [x] R-3 将 view compose 与 display compose 落成分离接口/测试：前者写 Camera 离屏 RT，后者写 surface swapchain image；GUIRenderSurface 不 acquire/present。
- [x] R-4 收口 acquire/present/recreate/zero-extent 到 IRenderSurfaceContext/present coordinator；PresentationGraphService 只服务主 world surface，extra GUI 使用 window-local display compose。
- [~] R-5 以 trace/性能证据决定是否两次 submit；当前无 N Camera、无异步 world、无测得的多 surface 同步压力，保持统一 loop/单 cmdBuf。不得用文档闭环冒充完成。
- [~] R-6 FRenderViewDesc、N Camera、ViewId/PreviewTarget 绑定另开计划；不得并入当前多 OS window 基础线。

## C3 — GUI 跨窗口 drag primitive

- [x] `MW-301` 提供 source/target window id、boundary enter/leave、drag keep-alive、延迟 create/destroy；不包含 tab 语义。依赖 C2G：两扇窗必须已是 Feature Gallery 打开的真实 OS window。

## C4 — GameEditor session/tab 解耦（EditorSurface 迁移）

C4 内部顺序：ES-1 → ES-5、MW-401、MW-501、MW-502、MW-601、MW-602、C7、C8、C9-P、C9 soak 已完成。不得把 `EditorSurface` 改造成 window manager，也不得把 `EditorWindowSession` 做成更大的 god object。不得为第二扇窗复制 `IRender::create`。默认窗仍走 primary `IRenderSurfaceContext`；extra 走 `onAfterPresent` + `GUIWindowManager::renderAll`。

- [x] `ES-1` 把 `tick(App&)` / `applyWindowMetrics(App&)` 改为消费 `FEditorSurfaceContext` 与 `EditorWindowMetrics`。验收：Surface 不再读 `App`/`IRender` window API；viewport frame state 由 context 注入；单窗口行为不变；保留 `tick(App&)` 仅作过渡 forwarding。
- [x] `ES-2` 引入单元素 `EditorWindowRegistry` + `EditorWindowSession` 持有 Surface、window-root dock、tree/viewport binding。验收：`EditorModule` 不再直接持有 `_editorSurface`；事件/tick/snapshot 经 default window id 路由；仍只有一个 native window。
- [x] `ES-3` 落地 root/nested ownership：`WindowRootEditor` / `EditorOwnedTool` / `WindowTool`；`_selection`/`_actions`/`_undo` 迁出 Surface，归属 editor/document session。验收：窗口只引用当前激活 root editor；owned tool 带 `ownerEditorId`；不能跨 root editor dock。
- [x] `ES-4` / `MW-402` 解耦 `FEditorTabSpawnContext`：增加 windowId、scope、optional ownerEditorId、document key、placement/detach policy。验收：factory 只创建 UI content，不持有 Surface、不拥有 tree、不执行 tick。
- [x] `WT-IME` 补齐 `WidgetTree::wantsTextInput()`（焦点路径 + focused widget capability）；删除 `EditorSurface` / `EditorInputNode` 对 `dynamic_cast<EditorInspectorTab*>`。验收：TextField/DragFloat/SpinBox/Inspector 走同一 tree API；GUI 不认识 Editor tab 类型。
- [x] `DS-1` dock placement policy 从「owner root id」升级为「目标 dock scope + owner」。验收：owned tool 拒绝挂到错误 scope 的 dock，不只比较 root id；GUI `FDockContext` 仍不认识 editor root。C5 落地 nested dock 前，window-root host 仍可物化同 owner 的 owned tool（扁平兼容）。
- [x] `ES-5` 删除 `EditorSurface::tick(App&)` forwarding；事件、snapshot、viewport、dialogs 全部按 window id 路由。前置：WT-IME、MW-207；host/Surface 不再经 `primarySwapchain()` 读窗。验收：`EditorInputNode` 经 registry `find(windowId)` 读 session/tree capability。完成后才允许第二扇 `EditorWindowSession`。
- [x] `MW-401` 在 ES-5 完成且 GUI C1/C2 可用后，才扩展为两个 editor session 各自拥有 tree、window-root dock、nested editor-owned dock 和 viewport context。验收：owned tool 不能 dock 到其他 root editor/tab，但可按 policy 成为独立 editor window 并保留 ownerEditorId。产品路径仍只 present 默认 native window；nested dock 尚无 DockSpace chrome（C5）。

## C5 — 主窗口和旧 tab 迁移

- [x] `MW-501` main window bootstrap：Level Editor 常驻、不可关闭、旧 `editor.dockLayout` 兼容。
- [x] `MW-502` 迁移 Viewport/Hierarchy/Inspector/Content Browser/Runtime Tools 到 session-owned 路径。

## C6 — Document-backed editors

- [x] `MW-601` 定义 document identity、dirty、undo、close、singleton、preview ownership。
- [x] `MW-602` Material/UI/Script 作为 `WindowRootEditor` 主窗口 Dock tabs；各自 Preview/Parameters/Hierarchy/Inspector 作为 `EditorOwnedTool` nested leaves。

## C7 — 真实 tear-off/re-dock

- [x] `MW-701` 先冻结 `FDockFloatingPlacement`（不要把 `UIDockFloatingHost` 当 OS window）：显式区分 `InProcessOverlay` 与 `NativeWindow` projection，并记录 source dock scope / target window / ownerEditorId / document key。
- [x] `MW-702` GUI Framework 实现 `IGUIWindowCoordinator` / `IGUIWindowSession`，并把 DockSpace 通用 orchestration 收回 Framework：每个 native window 一套 NativeWindow + RenderSurfaceContext + WidgetTree + snapshot/input/focus/presentation；共享 device 和 service，不共享 live WidgetTree，不复制 `IRender::create`。
- [x] `MW-703` GUI Framework 完成 generic cross-window drag router：source/target window 路由、boundary enter/leave、keep-alive、deferred close、drop preview 与 source detach/target attach 事务；不得包含 EditorRootId/DocumentKey。
- [x] `MW-704` GameEditor 接入 typed editor drag payload 与 placement policy：root editor / detachable owned tool detach 到 native window，保留 owner/document；跨 tree 迁移不得双挂载。
- [x] `MW-705` cross-window re-dock、owned tool 禁止挂到其他 root editor/tab、owner-aware close policy、空窗回收和 Level Editor 保护。
- [x] `MW-706` 增加 window chrome capability：macOS 默认 Hybrid（保留 traffic lights/safe-area），Windows 可选 ClientDrawn；GUI Framework 只使用 capability API，不把平台 non-client 逻辑泄漏到 Editor。
- [x] `MW-707` persistence 保存 window topology + dock topology + placement mode；旧 in-process floating JSON 必须可迁移，不能把旧 Popup 坐标解释成屏幕坐标。

## C8 — 持久化与恢复

- [x] `MW-801` versioned `windows[]` envelope：bounds/monitor/maximized/role、root dock、root editor/document、nested owned dock、window tools、active/focus。
- [x] `MW-802` 坏 monitor、缺失 asset、窗口关闭中断时的安全恢复。

## C9 — Release soak

- [x] `C9-P` 产品 extra window present/input：`IRuntimeModule::onAfterPresent` 在 primary submit 之后（acquire 失败也调用）；`EditorModule` 持有 `GUIWindowManager`，启动 restore extra，OS close 先 `closeEditorWindow` 再 `tickTrees`/`renderAll`；extra 输入按 window id 路由，不进 default camera。DockSpace NoTarget 仍 overlay。不得把本步写成 C9 soak。
- [x] 双窗口 GPU present / resize / close soak：共享 device 上 extra+primary 32 帧 present 并中途 recreate；`GUIWindowManager` 64 帧 `tickTrees`+resize/minimize/close 保 sibling；Editor tear-off 按产品 `onAfterPresent` 顺序 closeRequested → `closeEditorWindow`。Workbench `--extra-window --exit-after-frame=32` 干净退出。DockSpace NoTarget 仍 overlay；双 swapchain golden BMP 仍延后。

## C10 — 产品 DockSpace NoTarget native tear-off

- [x] `C10` DockSpace NoTarget：可拆 tab 经 `realizeNoTargetTearOff` → coordinator 开真实 OS window；Locked/Level 拒绝且不 overlay；未接线（Gallery / WidgetTree 测试）仍 InProcessOverlay。`FDockContext` / `EditorDockWorkspace` 不创建 native window；`EditorSurface` 不 include coordinator。Screen geometry 才作为 OS origin。不实现完整 extra menu chrome、R-5、N Camera。

## C11 — Hybrid chrome safe-zone 与 DnD 回归

- [x] `C11` macOS Hybrid：SDL safe-area 为 0 时仍保留 traffic-light 最小宽/高；`titleContent` 为 Client（菜单），仅 trailing gutter 为 Drag。Editor/Workbench 菜单避开红绿灯；extra hosted dock 吃 `contentInsets`。`UIDragSourceBehavior` 在 capture-on-press 时从 captured move 开 drag。不实现完整 extra editor menu chrome、R-5、N Camera。

## C12 — Host 唯一 drag session

- [x] `C12` 指针拖拽 session 由 `GUIDragRouter` 在 host 层唯一持有（GUIApp 与 GameEditor 共用）。WidgetTree 只保留 source-local payload/ghost/observer。跨窗 drop 走 router；唯一 tab extra NoTarget 移动现有窗或 reclaim 空源窗。不实现完整 extra editor menu chrome、R-5、N Camera。

## C13 — Host 指针宇宙收口

- [x] `C13` 同一 `GUIDragRouter` 持有 capture 所属窗、OS cursor、IME 窗、extra OS clipboard、树内 modal 的 app-modal 阻挡。不新造 router。不实现完整 extra editor menu chrome、R-5、N Camera、FontManager per-window DPI。

## C14 — Dock drop target seam

- [x] `C14` 把 `UIDockSpace::FDropPreview` 的 bool soup 收口为 `EDockDropTargetKind` / `FDockDropTarget`；`resolveDropPreview` 拆成 floating well / tab well / tab stack。不公开 `UIDockTabWell`/`UIDockTabStack` 控件。视觉语义不变。不实现 `FDockContext::commitDrop`、GameEditor policy 改签名、R-5、N Camera。

## C15 — Stack projection + TabRegistry / DockLayout seam

- [x] `C15` `FLeafView` 改名为 `FDockStackView`；`EDockNodeKind::Stack` 取代公开 `Leaf`；`FDockContext` 内部分成 `FTabRegistry` + `layout()`；drop 提交收到 `commitDrop`；overlay floating 自己产生 `FloatingTabWell`。不公开 `UIDockTabWell`/`UIDockTabStack` 控件。JSON 仍可读 `"leaf"`，写出 `"stack"`。不改 GameEditor policy 签名、R-5、N Camera。

## 条件延后

- [~] `MW-901` 只有并行/嵌套 recording 真实存在时才实例化 Render2D session。
- [~] `MW-902` 只有独立 world preview 真实需要时才扩展 RenderRuntime/PresentationGraphService；GUI-only 辅助窗不得提前复制 world graph。
- [~] `MW-903` 只有跨平台构建暴露缺口时才增加平台特化 window backend。
