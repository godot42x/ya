# GUI 多 OS Window / GameEditor 多 Editor 进度

> 建立日期：2026-09-08。只记录实际审计、实现、测试、偏离和阻塞。

## 2026-09-08 — 计划建立

### 已确认事实

- `GUIWindowHost` 已有 SDL/Vulkan host、WidgetTree、snapshot、present；当前产品使用仍以单窗口为中心。
- WidgetTree 的 pointer/focus/capture/popup/drag 状态适合 window-local 生命周期。
- GUIRenderSurface 已区分 offscreen/imported swapchain image，compose 不负责 acquire/present。
- Render2DPassSlot 支持串行多 pass 隔离，但不等于多 native swapchain。
- EditorSurface 当前拥有单一 tree、dock context、selection、undo、action、viewport host bridge；EditorModule 只有一个 surface 实例。
- FDockContext floating 当前是同一 tree 的 Popup projection，需要在 GUI 多窗口能力之上实现 native window tear-off。
- EditorTabSpawnerRegistry 是现有 factory 入口，但 spawn context 绑定 WidgetTree、EditorLayer、viewport sink，需要迁移到 window/session context。

### 本计划决策

- 一个 native window 对应一个 WidgetTree；所有窗口由一个 GUI application loop 统一调度。
- GUI Framework 负责 native window、surface/swapchain、per-window tree/snapshot/presentation；GameEditor 负责 editor session、dock/tab/document 和跨窗迁移。
- 默认产品形态是主窗口聚合 Level Editor，其他 editor 可作为 tab 或独立 window。
- 第一阶段不复制完整 RenderRuntime world frame 到每个 editor window；Material/UI preview 优先 offscreen/preview target。
- 不新增 EditorPanel、中心万能 bus、每窗口独立 loop，或把 FDockContext 变成 native window manager。

### 尚未开始

- 未修改 Engine/Source；
- 未实施 native window manager；
- 未进行 RHI 多 surface API 改造；
- 未迁移旧 GameEditor tabs；
- 未运行本计划相关构建或运行时测试。

### 本轮底层调研补充

- 已确认 `NativeWindowManager` 已是多 native-window 容器，但不承担 RHI surface/swapchain/present。
- 已确认 `IRender` 和 `VulkanRender` 把 device 与唯一 surface/swapchain、window handle、frame sync 混在同一 owner 中。
- 已确认 `VulkanRender::begin/end` 的 acquire/present/fence/semaphore 目前都是单 surface 语义。
- 已确认 `PresentationGraphService` 只能作为主 world window 的 presentation service，不能直接共享给辅助 GUI windows。
- 已确认 `GUIAppHost` 不能被复制初始化来实现多窗，否则会复制 Vulkan device；需要共享 device + per-window surface context。
- 计划已从方向性描述收敛为具体 RHI 目标：新增 additive surface/presentation context，`VulkanRender` 继续作为共享 device owner，`VulkanSwapChain` 每窗口实例化，主 `IRender` facade 兼容保留。
- 已明确 C2 的实现顺序：先 RHI context factory/per-surface sync，再 GUI host；禁止复制 `GUIAppHost::init`。
- 已明确 `PresentationGraphService` 暂不多窗口化，辅助 GUI window 走 window-local imported target + GUI compose；多 world window 另开 checkpoint。

## 2026-09-08 — 计划复审：补齐 tab 层级

### 本轮修订

- 明确 `WindowRootEditor`、`EditorOwnedTool`、`WindowTool` 三种 scope。
- 初版规则曾将 owned tool 限制为只能 nested；本轮已修正为：owned tool 也可成为独立 native/editor window，但不能 dock 到其他 root editor/tab。
- 明确每个 root editor 拥有独立 nested `FDockContext` 投影，不能把所有 tab 扁平塞入 window-root context。
- 明确 descriptor、instance、persistence record 必须携带 scope、ownerEditorId、document key 和 detach/close policy。
- 补上恢复顺序和 owner 缺失时的降级规则，避免产生 orphan leaf 或错误提升为 root。

### 仍待 C0 证据确认的部分

- RHI 是否能在共享 device 上安全持有多个 presentation surface/swapchain；
- 当前 GUI host 是否能抽出 per-window frame resources 而不改变已有 single-window 主链；
- 哪些旧 tab 实际是 owned tool，哪些应提升为 WindowTool。

## 2026-09-08 — 计划复审修正：owned tool 可独立成窗

- 修正 `EditorOwnedTool` 语义：限制是不能 dock 到其他 root editor/tab，而不是不能成为 native/editor window。
- 独立窗口中的 owned tool 必须保留 `ownerEditorId`、document 关联和 owner-aware close/re-dock policy。
- `WindowTool` 仅表示完全不携带 editor/document ownership 的窗口级工具。
- 持久化新增 owned-tool 独立窗口 placement，恢复时禁止静默改绑到其他 root editor。

### 工作区边界

建立计划时仓库存在大量用户未提交 GUI、Editor、GameRuntime、Workbench、测试和既有计划改动。本计划当前只新增本目录工件。

## 2026-09-08 — EditorSurface 迁移方案收口

### 本轮修订

- 明确不删除 `EditorSurface`，也不把它改造成 `EditorWindowManager` 或把字段整体倒进更大的 `EditorWindowSession` god object。
- 按 window-local / editor-root / app-global / legacy bridge 拆当前 Surface 字段；`SelectionModel`/`ActionMap`/`UndoStack` 归属 root editor/document session，禁止按 native window 复制。
- 第一刀是 `tick(App&)` → `FEditorSurfaceContext` + `EditorWindowMetrics`；`applyWindowMetrics` 不再读 `IRender` window/swapchain API。
- `EditorModule` 先引入单元素 `EditorWindowRegistry`；所有 `_editorSurface.xxx()` 改经 default window session。确认单窗口行为不变后，才允许第二个 session。
- `wantsTextInput()` 改为 `WidgetTree` focus capability，删除 `dynamic_cast<EditorInspectorTab*>`。
- C4 内部顺序固定为 ES-1 → ES-5；`MW-401` 的双 session 扩展排在 ES-5 之后，并依赖 GUI C1/C2。

### 已核对的当前切口

- [`EditorSurface.h`](Engine/Source/Applications/GameEditor/include/GameEditor/UI/EditorSurface.h)：`tick(App&)`、`_app`/`_appStateHandle`、window-local tree/dock/viewport 与全局 selection/undo 混在同一类。
- [`EditorSurface.cpp`](Engine/Source/Applications/GameEditor/UI/EditorSurface.cpp)：`applyWindowMetrics` 读 `getWindowSize`/`getNativeWindow`/`getSwapchainWidth`；`wantsTextInput` 特判 Inspector tab。
- [`EditorModule.cpp`](Engine/Source/Applications/GameEditor/EditorModule.cpp)：直接持有 `_editorSurface`，`onPresentation` 对其 `tick`/`snapshot`/`tree`。
- [`EditorTabSpawnerRegistry.h`](Engine/Source/Applications/GameEditor/include/GameEditor/UI/EditorTabSpawnerRegistry.h)：`FEditorTabSpawnContext` 绑定单窗口 tree/selection/actions/undo。

### 下一接力点

执行 `MW-001`，先完成 RHI/surface/swapchain 单实例事实矩阵；C0 完成前不实现 `GUIWindowManager`。GameEditor 侧 C0 只做 `MW-003` 登记，不开始 ES-1 编码。

`feature_matrix.json` 已包含 ES-1..ES-5 与 MW-204/205。

## 2026-09-08 — FeatureGallery 多窗口实例（gui-framework）

### 本轮修订

- 在 C2 与 C3 之间增加 C2G：GUI Framework 第一份真实多窗消费者是 GUIWorkbench Feature Gallery，不是 GameEditor。
- `GUIApp` 暴露 `openWindow` / `closeWindow`；`FWorkbenchApp` 只调用，不把 `FWorkbenchSurface` 改成 window manager。
- Composition 组新增 `Windows` 页（MW-204/205）：Open/Close 额外 OS window；每窗独立 WidgetTree 与 retained UI 实例。
- 明确不是第二份完整 Gallery shell，也不是 `FDockContext` floating / editor tear-off。
- C3 drag 与 GameEditor 第二窗都依赖 C2G 手测路径存在。

### 下一接力点

仍先执行 `MW-001`。C2G 排在 C1/C2 之后、C3 之前。

## 2026-09-08 — C0 闭环（MW-001..004）

### 完成

- MW-001：[`c0_mw001_irender_ownership.md`](c0_mw001_irender_ownership.md)。`IRender` API 分为 device / surface / main-facade。未改 RHI。
- MW-002：[`c0_mw002_frame_boundary.md`](c0_mw002_frame_boundary.md)。双窗应串行 acquire/record/present；每窗 pass slot；GUI 未 init `DeferredDeletionQueue`；`rebuildPresentationResources(waitIdle)` 多窗不安全。
- MW-003：[`c0_mw003_tab_scope.md`](c0_mw003_tab_scope.md)。9 个 builtin tab 分类；无 Level/Material/Script spawner；`ui-designer` 应升为 `WindowRootEditor`。
- MW-004：[`c0_mw004_contract.md`](c0_mw004_contract.md)。GUI 不依赖 GameEditor（仅 Style.h 注释）。

### 保留 / 未完成 / 偏离

- 保留：单窗 `ya::App` / `GUIAppHost` 行为、`IRender` main-facade。
- 未完成：C1 `MW-101` 起才写代码。
- 偏离：无 Engine 改动。

### 下一接力点

`MW-101`：`GUIWindowManager` 最小生命周期，消费共享 device + surface-context provider，禁止 `IRender::create` 复制 backend。

## 2026-09-09 — C1 MW-101（GUIWindowManager + 双窗 tree/input）

### 完成

- `GUIWindowManager`：额外 OS window 各持一个 `INativeWindow` + `WidgetTree`；create/requestClose/destroy/find/dispatch/tickAll；`renderAll` 空实现。
- `GUIApp` 成为 `IAppLoopDelegate`：统一 `AppKernel`；`openWindow` / `closeWindow` / `findTree`；按 `guiEventWindowId` 路由；关副窗不退主循环。
- 额外窗不调用 `IRender::create`，不复制 `GUIAppHost::init`，不 present。
- 鼠标/键盘/窗口事件携带 window id；`SdlEventSource` 可接受全部 OS 窗口事件。
- `NativeWindowManager::shutdown` 不再 `SDL_Quit`（与主窗共享 SDL）。
- 测试：`ya-gui-headless-host-test` 中 `GUIWindowManagerTest` / `GUIAppExtraWindowTest`（SDL 创建失败则 skip）。

### 保留 / 未完成 / 偏离

- 保留：单窗 `GUIApp::init/run`、主窗 `IRender` facade、关主窗才 `shouldClose`。
- 未完成：extra present / surface-context（C2）；MW-102 更完整的 capture/tooltip/clipboard 隔离；C2G Feature Gallery `Windows` 页。
- 偏离：MW-101 未接入 surface-context provider——额外窗本切片无 GPU，C2 再接。`GUIApp` 用 `unique_ptr<GUIWindowManager>` 避免与 `GUIWindowHost` 头循环。

### 下一接力点

`MW-102`：per-window input/tick/snapshot 不泄漏。不要开始 extra swapchain。

## 2026-09-09 — C1 MW-102（per-window input/tick/snapshot 隔离）

### 完成

- Extra 窗 `WindowFocusLost` 清 hover/tooltip，保留 tree-local focus/capture；manager `_focusedId` 在失焦后不再把无 window id 的键送给该副窗。
- `tickAll` 在每窗 `buildSnapshot` 前设置 `FontManager` active DPI；每槽独立 `UIFrameSnapshot`（`findSnapshot`）。
- Extra 窗使用 tree-local clipboard，不绑定 OS clipboard，避免互相覆盖 paste buffer。
- Tooltip dwell 的 `_frameCounter` 每帧递增（不再只在 debug validation 里 ++）。
- 测试：`GUIWindowManagerTest.IsolatesPointerFocusCaptureTooltipClipboardDpiAndSnapshot`。

### 保留 / 未完成 / 偏离

- 保留：单窗 `GUIApp::init/run`、主窗 OS clipboard、主窗 `IRender` present。
- 未完成：C2 extra surface/swapchain/present（MW-201..203）；C2G Feature Gallery `Windows` 页。
- 偏离：OS clipboard 仍只绑主窗；C2G 产品副窗若需要系统剪贴板再绑，且须保持 tree hook 可替换。SDL 光标对象为 process-static，由当前 pointer 窗更新。

### 下一接力点

`MW-201`：RHI additive surface/presentation context。不要把 `IRender` 改成数组，不要复制 `GUIAppHost::init`。

## 2026-09-09 — C2 MW-201（additive surface/presentation context）

### 完成

- `IRenderSurfaceContext`：per-surface begin/end、swapchain、acquire/present sync。
- `IRender::createSurfaceContext(INativeWindow&)`：共享当前 device，不调用 `IRender::create`。
- `VulkanRenderSurfaceContext`：每窗 `VkSurfaceKHR` + `VulkanSwapChain` + flight fence/semaphore。
- `VulkanSwapChain` 显式持有 surface/window，不再读 `VulkanRender::getSurface()`。
- 当时主 `IRender::begin/end` 仍驱动第一扇窗；GUI extra 仍不 present。（后续 follow-up / MW-201c 已删掉这套 facade，acquire 在 `IRenderSurfaceContext`。）
- 测试：`ya-rhi-vulkan-smoke` `RHISurfaceContext.ExtraWindowAcquireSubmitPresentIndependentOfPrimary`。

### 保留 / 未完成 / 偏离

- 保留：单窗 `IRender` facade、`GUIAppHost` present、`PresentationGraphService` 主窗、`GUIWindowManager::renderAll` 空实现。
- 未完成：MW-202 resize/minimize/out-of-date/close（`VulkanSwapChain::recreate` 仍 `vkDeviceWaitIdle`）；MW-203 per-window Render2D slot；extra GUI present。
- 偏离：extra context 析构会 `vkQueueWaitIdle` graphics+present（共享 queue，会排空主窗 in-flight）。

## 2026-09-09 — MW-201 follow-up（primary swapchain into SurfaceContext）

### 完成

- 主窗 swapchain + flight fence/semaphore 从 `VulkanRender` 迁到 `_primarySurface`（`attachExistingSurface`，不销毁 device-pick 的 `VkSurfaceKHR`）。
- 当时 `IRender::begin/end` / `getSwapchain()` 曾是 primary context 的 facade；MW-201c 起这些 API 已删，调用方持有 `IRenderSurfaceContext*`。device 仍负责 GPU timing 与 `DeferredDeletionQueue` flush。
- 删除 `VulkanRender` 上重复的 `_swapChain`、`frameFences`、`createSyncResources` 与未使用的 `m_imageAvailableSemaphore` 三件套。
- 主窗 suboptimal 改为 `requestRecreate`（不再 `vkDeviceWaitIdle` 整 device 重建），与 extra 同一条 present 路径。

### 保留 / 未完成 / 偏离

- 保留：`createSurface()` 仍在 `findPhysicalDevice` 之前；`IRender` 仍是单窗 facade，不是 context 数组。
- 未完成：MW-202（`VulkanSwapChain::recreate` 仍 `vkDeviceWaitIdle`）；MW-203；extra GUI present。
- 偏离：主 `end` 空 cmdbuf 仍 skip submit（兼容 “App 已自行 submit”）；extra `end` 仍补 scratch present barrier。

## 2026-09-09 — MW-201c（present ≠ viewport / compose model）

### 完成

- 冻结三层：`IRender` device、离屏 viewport `RenderTexture`、`IRenderSurfaceContext` 只在 present 时选择 swapchain image。工件：[`c2_present_compose_model.md`](c2_present_compose_model.md)。
- `PresentationGraphService` 注入 `IRenderSurfaceContext*`；`onRecreate` 只重建该 surface 的 imported images。
- World command buffer 按 `MAX_FLIGHTS_IN_FLIGHT` / `flightIndex` 分配，不再按 swapchain `imageIndex`。
- Viewport 不再 fallback 到 swapchain extent；host 在 rect 为空时用窗口尺寸；pipeline 初始尺寸来自 host viewport。
- World postprocess sRGB 跟离屏 format；swapchain sRGB 只留在 presentation compose。
- `GUIAppHost` 经自己持有的 present context 取 swapchain。

### 保留 / 未完成 / 偏离

- 保留：单窗仍在 `RenderRuntime::beginFrameCommandBuffer` 里 acquire（world+present 共用一条 cmdBuf，acquire 必须在录制前）；extra GUI 仍不 present。
- 未完成：Render2D / debug primitives 仍读 `primaryFrameIndex()`（MW-203）；EditorSurface 仍读 `primaryWindow/Swapchain`（ES-1）；pipeline 未把 world 与 present 拆成两次 submit。
- 偏离：无。

## 2026-09-09 — MW-201d（Camera 链 graphics → UI → compose → present）

### 完成

- 冻结渲染单位为 Camera，不是窗口：`graphics → UI → view compose` 写该相机离屏 RT；`display compose → present` 才碰 swapchain。工件：[`c2_view_model.md`](c2_view_model.md)。
- `RenderRuntime::renderFrame` / `FrameInput` / `ViewportStateService` / `IEditorViewportHost` / `bPrimary` 按这条链标注。
- `syncRuntimeCameraAspect` 标明会污染未绑定相机，多 Camera 前必须按 view 绑定改。

### 保留 / 未完成 / 偏离

- 保留：仍只执行一条 Camera 链；acquire 仍在 `RenderRuntime` 内。
- 未完成：`FRenderViewDesc`、N Camera 串行、Material/UI PreviewTarget 分窗。
- 偏离：无。

### 下一接力点

`MW-202`：per-surface recreate/minimize/close，禁止用 device waitIdle 卡住其他窗。不要开始 Feature Gallery `Windows` 页。Camera 链（MW-201d）已冻结，不在 C2 铺 N 视图。

## 2026-09-09 — 计划收口（方向写入 plan.md）

把 device / present / Camera 结论写进 [`plan.md`](plan.md) 开头冻结节，并同步 `session_checklist.md`、`c0_mw004_contract.md`、`feature_matrix.json`。避免只记在 progress：下一会话先读 plan 冻结节。

## 2026-09-09 — C2 MW-202（per-surface recreate / close）

### 完成

- `VulkanSwapChain::recreate` 不再 `vkDeviceWaitIdle`。调用方先 wait 该 surface。
- `IRenderSurfaceContext::waitInFlight()`：wait 该窗 graphics fence + present-complete fence（present 后 empty submit 到 present queue，不 `vkQueueWaitIdle`）。
- extra context 析构不再 graphics/present `queue waitIdle`。
- GUI：最小化跳过 present；rebuild imported targets 不再整 device waitIdle；截图 wait 该 surface。
- 测试：`RHISurfaceContext.ExtraWindowResizeAndCloseDoesNotDeviceWaitIdlePrimary`；关 extra 后主窗立刻 present。

### 保留 / 未完成 / 偏离

- 保留：进程退出仍 `IRender::waitIdle()`；`RenderRuntime` 仍内部 acquire；extra GUI 仍不 present。
- 未完成：MW-203 per-window Render2D slot；extra `renderAll` present。
- 偏离：无。macOS 上未做 0x0 SDL 最小化专项；zero extent 走 begin `imageIndex=-1` / GUI skip present。

### 下一接力点

`MW-203`：每窗唯一 Render2D pass slot。不要开始 Feature Gallery `Windows` 页，不要铺 N Camera。最小化重构是 MW-206，不要塞进本任务。

## 2026-09-09 — 不可上屏 / 最小化写入计划

工件：[`c2_unpresentable_surface.md`](c2_unpresentable_surface.md)。最小化只 delay 该 PresentSurface 的 recreate/present，不 pause AppKernel。编码 **MW-206**，排在 MW-203 之后、extra present 之前。本轮不改代码。

## 2026-09-09 — C2 MW-203（per-window Render2D pass slot）

### 完成

- `Render2D::acquirePassSlot` / `releasePassSlot` 共用一个 pool；`kMaxPassSlots` 16。
- `FRender2DComposePassDesc::passSlot`；GUI host / extra slots 各持 present + offscreen slot。
- 测试：`Render2DPassSlotTest.AcquireReturnsDistinctSlotsAndReleaseRecycles`。

### 保留 / 未完成 / 偏离

- 保留：静态 session 串行；kind pool 仍给单窗 editor/runtime。
- 未完成：当时下一任务是 MW-206。
- 偏离：无。flight 仍可用 `primaryFrameIndex()`；不作为本任务范围。

### 下一接力点

`MW-206`：不可上屏 skip present / delay recreate。

## 2026-09-09 — C2 MW-206（不可上屏 skip present / delay recreate）

### 完成

- `IRenderSurfaceContext::isPresentable()`：native minimized 或 window/surface extent 0。
- `begin` 不可上屏时不 reset fence、返回 `imageIndex == -1`；delay recreate 保持 dirty，且不覆盖上次成功的 swapchain extent。
- GUI host：不可上屏仍 `updateUI` + snapshot，只跳过 compose/present。`GUIWindowManager::tickAll` 最小化 extra 仍 tick。
- GameRuntime：去掉 `_bMinimized` + `sleep(100)`；logic 照常。`RenderRuntime` 在 `begin` 返回 -1 时 skip GPU（单 cmdBuf 迁移注释）。
- 测试：`GUIWindowManagerTest.MinimizedExtraStillTicksAndSnapshots` 通过；`RHISurfaceContext.*` 2 passed。macOS 测试窗 `SDL_MinimizeWindow` 往往不置 MINIMIZED，`ExtraWindowUnpresentableDoesNotBlockPrimaryPresent` skip。

### 保留 / 未完成 / 偏离

- 保留：进程退出仍 `IRender::waitIdle()`；`RenderRuntime` 仍内部 acquire；extra GUI 仍不 present。
- 未完成：extra `GUIWindowManager::renderAll` present。
- 偏离：无。OS 最小化在无焦点测试窗上不可靠，host 以 WindowMinimize 事件 + `isPresentable()` 为准。

### 下一接力点

extra `GUIWindowManager::renderAll` present。不要开始 Feature Gallery `Windows` 页，不要铺 N Camera。

## 2026-09-09 — C2 extra present + C2G 最小闭环

目标：尽快跑通双窗 present，再补 MW-205 scenario/smoke。

### 完成

- `GUIWindowPresent`：每 extra 一扇 `IRenderSurfaceContext` 上 acquire → compose snapshot → present；不可上屏 skip；resize 只 `waitInFlight` 该 surface。
- `GUIWindowManager::create(..., IRender*)` 在共享 device 上建 surface；`renderAll` 真正 present。无 device 时（现有测试）仍 no-op。
- `GUIApp::onTick`：primary present 之后 extra `tickAll` + `renderAll`。`openWindow` 把 primary `IRender*` 传给 manager。
- Feature Gallery：Composition/`Windows` 页 + `--extra-window`；extra 是独立 `ExtraOsWindowDemo` WidgetTree，不是第二份 `GUIWindowHost`。
- 验证：`xmake b ya-gui-host` / `GUIWorkbench`；`ya-gui-headless-host-test` 8/8；`xmake r GUIWorkbench --extra-window --exit-after-frame=8` 双窗 swapchain present 后 `GUIWorkbench finished`；`--start-page=Windows --exit-after-frame=5` 进入该页。validation 日志无 extra 生命周期错误（MAILBOX 不可用是既有 warn）。

### 保留 / 未完成 / 偏离

- 保留：进程退出仍 `IRender::waitIdle()`；`RenderRuntime` 仍内部 acquire；extra 无 offscreen parity / inspector overlay。
- 未完成：MW-205 点 Open 的独立 click/resize/focus scenario、smoke、golden。
- 偏离：无。extra 走 manager slot，没有复制 `GUIAppHost::init`。

### 下一接力点

`MW-205` 剩余：Windows 页 scenario/smoke/golden。不要铺 N Camera、GameEditor、dock tear-off。

## 2026-09-09 — C2G MW-205（Windows 页 scenario/smoke）

### 完成

- Headless scenario：`Example/GUIWorkbench/Scenarios/windows_extra_os.jsonl` 锁 `WindowsDemo` / Open / Close；`!extra-click` 证明 extra 树不在 Gallery。
- Windowed smoke frame 19–20：点 `windows-open` 建真实 extra surface；点 `extra-click`；resize extra 400×300 不改 Gallery extent；Close extra；随后 Editor 自动化仍 PASS。
- 验证：`xmake r GUIWorkbench --headless --start-page=Windows --scenario Example/GUIWorkbench/Scenarios/windows_extra_os.jsonl`；`--smoke-actions --exit-after-frame=60` → `GUIWorkbench smoke result: PASS`。

### 保留 / 未完成 / 偏离

- 保留：extra 无 offscreen parity / inspector；scenario-capture 只拍主窗。
- 未完成：golden BMP（双 swapchain 不能用主窗一张图冒充完成，延后）。
- 偏离：无。headless 不调用 `openWindow`（无 device）；真实 Open 只走 windowed smoke。

### 下一接力点

`MW-301`：跨窗口 drag primitive。不要铺 N Camera、GameEditor tear-off。

## 2026-09-09 — C3 MW-301（跨窗口 drag primitive）

### 完成

- `WidgetTree`：`setExternalDropHover` / `clearExternalDropHover` / `dropExternal` / `finishDrag`。目标树不设本地 `_dragOperation`，`isDragging()` 保持 false。
- `GUIApp` 拦截跨窗 move/release/leave/Escape：记录 source/hover window id 与 enter/leave 计数；source 会话 keep-alive。
- 延迟销毁：drag 期间不 flush source/hover 的 `requestClose`；`runAfterDrag` 作为延迟 create 钩子。
- 验收：`WidgetTreeTest.ExternalDropHoverDoesNotStartLocalDrag`；`GUIAppCrossWindowDragTest` A→B drop、leave keep-alive、deferred close。`ya-gui-headless-host-test` 11/11。
- 消费者：Windows 页 `windows-drag`，extra 窗 `extra-drop`。不含 tab/editor 语义。

### 保留 / 未完成 / 偏离

- 保留：ghost 停在源窗最后一点；坐标不映射到源树。
- 未完成：C7 tear-off 用此 primitive 迁 tab；跨窗 drag 的 Workbench scenario/smoke 坐标未加（gtest 为门禁）。
- 偏离：`openWindow` 在 drag 中仍立即创建（第三扇窗安全）；延迟 create 走 `runAfterDrag`，不阻塞 Open。

### 下一接力点

`ES-1`：EditorSurface 改为消费 `FEditorSurfaceContext` / `EditorWindowMetrics`。不要铺第二扇 editor window、N Camera、C2R R-1。

## 2026-09-09 — C4 ES-1（EditorSurface context 解耦）

### 完成

- `EditorWindowMetrics` / `FEditorSurfaceContext`：logical/framebuffer extent、dpi、viewport view/projection。
- `makeEditorSurfaceContext(App&)` 是唯一从 primary present surface 读窗口/swapchain 的适配器；`EditorSurface.cpp` 不再调用 `primaryWindow` / `primarySwapchain`。
- `tick(const FEditorSurfaceContext&)` 消费 metrics + camera matrices；`tick(App&)` 仅 rebuild chrome 后转发。
- `applyEditorWindowMetrics` 只写 WidgetTree extent/dpi。
- 验收：`EditorSurfaceContextTest` 4/4；`ya-game-editor` / `ya-game-runtime` 构建通过。

### 保留 / 未完成 / 偏离

- 保留：rebuild/chrome 仍用 `App&`（editorTab、play/sim 标签、`bindAppState`），不是 window API。
- 未完成：`EditorWindowSession` / registry（ES-2）；`tick(App&)` 删除（ES-5）；`EditorApplicationServices` 未发明。
- 偏离：context 比计划草稿更窄（不含 layer/tree/spawners/services），避免 ES-1 提前做 session 所有权迁移。

### 下一接力点

`ES-2`：单元素 `EditorWindowRegistry` + `EditorWindowSession` 持有 Surface。禁止第二扇 editor window、N Camera。C2R 已切到 R-1 先收口底层。

## 2026-09-09 — 渲染边界计划迭代

### 本轮新增决策

- 明确 NativeWindow 只表示 OS 生命周期与 metrics；IRenderSurfaceContext/PresentSurface 只负责 surface、swapchain、acquire/present/recreate。
- 明确 Camera/WorldView 负责 view/projection/viewProjection、离屏 extent 和 Camera RT；BaseRenderPipeline 只消费 immutable camera frame data，不从 swapchain 猜尺寸。
- 明确 view compose（写回 Camera RT）与 display compose（写 surface swapchain image）是两个不同阶段；Present 不属于 graphics/base pipeline。
- 增加 R-1～R-5 的执行顺序：先 typed frame inputs，再解耦矩阵与 pipeline，再拆 compose owner，再收口 present，最后以证据决定是否拆 submit。
- FRenderViewDesc、N Camera、第二次 submit、独立 world preview 仍为后续条件项，不得塞入当前多 OS window 基础线。

### 本轮未完成

- R-1 / R-2 已落地；R-3～R-5 仍为计划任务。
- 当前实现仍保留单 Camera / 迁移期 world+display 共用 command buffer 的路径；该现状与计划一致。

## 2026-09-09 — C2R R-1（FrameInput 四组收口）

### 完成

- `CameraFrameInput` / `ViewComposeInput` / `DisplayComposeInput` / `PresentFrameInput`：`RenderRuntime::FrameInput` 由这四组构成。
- `renderFrame` 仍一次 `prepareFrame` + 一次 `submitFrame`；acquire 仍在 `beginFrameCommandBuffer`。
- Host（`GameRuntimeFrameOrchestrator`）按组填充；`PresentFrameInput.surface` 写入 primary，null 仍 fallback 到 `getPrimarySurfaceContext()`。
- `submitFrame` 对该 surface `end`，不再二次查找 primary。
- 验收：`RenderRuntimeSnapshotTest` 分组 static_assert + 单 prepare/submit 源扫描。

### 保留 / 未完成 / 偏离

- 保留：pipeline 仍吃 `RenderPipelineFrameContext`（R-2 再让 pipeline 只消费 CameraFrameInput）；world+display 共用一条 cmdBuf。
- 未完成：R-2 矩阵/extent 解耦；R-3 compose 接口拆分；R-4 acquire 移出 RenderRuntime；R-5 第二次 submit。
- 偏离：无。UI snapshot 留在 `CameraFrameInput`（Camera 链 UI pass），view compose 仍是 editor `recordCompose`。

### 下一接力点

`R-2`：Camera owner 在 graph build 前生成 view/projection/extent；Forward/Deferred/debug/overlay 只消费 CameraFrameInput，不从 swapchain/window 猜尺寸。禁止 N Camera、第二次 submit、ES-2。

## 2026-09-09 — C2R R-2（CameraFrameInput 矩阵/extent）

### 完成

- `CameraFrameInput` 成为 owner 在 graph build 前填好的 camera 包：view / projection / viewProjection / offscreen extent。
- `RenderPipelineFrameContext` 只保留 `cmdBuf` + `camera` + overlay snapshot；tick 不再用 `ViewportState` 覆盖 extent。
- Host 用 `makeCameraViewProjection` 同时喂 extractor 与 `CameraFrameInput`；extract 的 extent 来自 camera rect，不读 pipeline cache。
- Overlay / billboard 消费 `viewProjection`；`DebugPrimitives` 用 recording flight，不再读 `primaryFrameIndex`。
- 验收：`RenderRuntimeSnapshotTest` 含 viewProjection 与 pipeline 不猜 present surface 的源扫描。

### 保留 / 未完成 / 偏离

- 保留：tick 仍走 `RenderPipelineFrameContext` 包一层 camera（cmdBuf 不是 camera 状态）；world+display 共用一条 cmdBuf；acquire 仍在 RenderRuntime。
- 未完成：R-3 compose 接口拆分；R-4 acquire 移出 RenderRuntime；R-5 第二次 submit。
- 偏离：项目没有独立 `BaseRenderPipeline` 类型；契约落在 `IRenderPipeline` + Forward/Deferred。

### 下一接力点

`R-3`：view compose 写 Camera 离屏 RT，display compose 写 surface swapchain image；GUIRenderSurface 不 acquire/present。禁止 N Camera、第二次 submit、ES-2。

## 2026-09-09 — C2R R-3（view compose vs display compose）

### 完成

- `recordCameraViewCompose` 把 Camera UI pass + `ViewComposeInput` 录到 `getViewportDisplayImageShared()`（离屏 Camera RT），不写 swapchain。
- `PresentationGraphService::recordDisplayCompose` 取代 `render`，只把 Camera display RT composite 到 `swapchain[imageIndex]`。
- `GUIRenderSurface::isDisplayComposeTarget()` 以 `PresentSrcKHR` 区分 display target；surface 仍不 acquire/present/读 live WidgetTree。
- `renderFrame` 仍一次 `prepareFrame` + 一次 `submitFrame`。
- 验收：`RenderRuntimeSnapshotTest` 锁 view-then-display 调用顺序与目标；`GUIRenderSurfaceTest` 源扫描 compose 不碰 surface/WidgetTree。

### 保留 / 未完成 / 偏离

- 保留：acquire 仍在 `beginFrameCommandBuffer`；world+display 共用一条 cmdBuf；UI snapshot 仍在 `CameraFrameInput`。
- 未完成：R-4 acquire 移出 RenderRuntime；R-5 第二次 submit。
- 偏离：无。没有新建平行 compose 类型；display 入口就是 `PresentationGraphService`。

### 下一接力点

`R-4`：acquire/present/recreate/zero-extent 只出现在 `IRenderSurfaceContext` / present coordinator；`PresentationGraphService` 继续只服务主 world surface。禁止 N Camera、第二次 submit、ES-2。

## 2026-09-09 — C2R R-4（present coordinator）

### 完成

- `FPresentFrame` + `acquirePresentFrame` / `submitPresentFrame`：recreate/zero-extent/minimize 仍在 `IRenderSurfaceContext::begin`；host 只做 acquire→record→present 配对。
- `GameRuntimeFrameOrchestrator`、`presentGuiSnapshot`、`GUIAppHost` 经 coordinator acquire/present；`RenderRuntime::renderFrame` 只 begin/end 飞行 cmdBuf 并返回 handle。
- `PresentFrameInput` 携带已 acquire 的 `surface` + `imageIndex`；去掉 `getPrimarySurfaceContext` fallback。
- `PresentationGraphService` 仍只绑主 world surface 做 display compose；extra GUI 继续 window-local `GUIRenderSurface`。
- 验收：`RenderRuntimeSnapshotTest` 锁 RenderRuntime 无 `present->begin/end`；host/extra 使用 coordinator；`PresentFrameTest.AcquiredRequiresSurfaceAndNonNegativeImage`。

### 保留 / 未完成 / 偏离

- 保留：world+display 共用一条 cmdBuf（acquire 必须在录制前）；不可上屏时 host 跳过整段 GPU 录制（单 cmdBuf 仍把 world 绑在 present 上）。
- 未完成：R-5 第二次 submit（有证据才拆 world 与 display）。
- 偏离：无新平行 present 类型；coordinator 是 `IRenderSurfaceContext::begin/end` 的 host 配对，不是第二套 sync。

### 下一接力点

`R-5`：以 trace/性能证据决定是否把 world/view compose 与 display compose 拆成两次 submit。未满足证据前保持统一 loop / 单 cmdBuf。禁止 N Camera、ES-2。

## 2026-09-09 — C4 ES-2（单元素 EditorWindowSession）

### 完成

- `EditorWindowId` / `kDefaultEditorWindowId` 落在 GameEditor，不 include GUI host。
- `EditorWindowSession` 持有 `EditorSurface`，转发 tick / dispatchEvent / snapshot / viewport query；不吸收 selection/undo/actions。
- `EditorWindowRegistry` 是单元素表：`find(default) == &defaultSession()`，其它 id 为 nullptr。不是 map，避免 ES-5 占位。
- `EditorModule` 去掉 `_editorSurface`；attach / detach / presentation / dialogs 经 `find(kDefaultEditorWindowId)`。
- `EditorInputNode` 绑 `EditorWindowSession*`。
- 验收：`EditorWindowSessionTest`；`EditorModule.cpp` 无 `_editorSurface`；`EditorSurfaceContextTest` 仍适用。

### 保留 / 未完成 / 偏离

- 保留：仍一扇 native window；`EditorSurface::tick(App&)` forwarding（ES-5 删）；dock/tree/viewport host 仍在 Surface 内；world+display 仍一条 cmdBuf。
- 未完成：ES-3 把 selection/undo/actions 迁出 Surface；ES-4 spawn context；ES-5 删 `tick(App&)` 与第二扇 session。
- 偏离：R-5 未做。没有 N Camera、异步 world、或测得的多 surface 同步压力，保持单 cmdBuf；不把两次 submit 用文档标成完成。

### 下一接力点

`ES-3`：落地 `WindowRootEditor` / `EditorOwnedTool` / `WindowTool`；`_selection` / `_actions` / `_undo` 迁出 Surface。禁止第二扇 editor window、N Camera、两次 submit。

## 2026-09-09 — C4 ES-3（EditorRootSession ownership）

### 完成

- `EditorRootSession` 持有 Level Editor 的 selection / actions / undo；`EditorWindowSession::activeRoot()` 是窗口对当前 root 的唯一引用。
- `EditorSurface` 不再拥有这三份模型；chrome 经 `_rootSession` 引用；project browser 用窗口局部 `_projectSelection`。rebuild 不再重建 undo/selection。
- Builtin spawners 按 MW-003 标注 `EEditorTabScope` + `ownerEditorId`（viewport/hierarchy/inspector → Level owned tools）。
- `canDockEditorTab`：owned tool 只能落到其 owner root；`EditorDockWorkspace::materializeTab` 拒绝其他 root。
- 验收：`EditorRootSessionTest`；`EditorTabSpawnerRegistryTest.BuiltinOwnedToolsBindToLevelEditor`；Surface.h 无 `_selection`/`_actions`/`_undo`。

### 保留 / 未完成 / 偏离

- 保留：仍一扇 native window、一个 Level `EditorRootSession`；ui-designer 标成 WindowRootEditor 但没有第二份 root session（C6）；扁平 window-root dock，没有 nested editor-owned dock（C5）；`FEditorTabSpawnContext` 仍是必选 selection/actions/undo 引用（ES-4）；`tick(App&)`（ES-5）。
- 未完成：ES-4 spawn context 字段；ES-5 删 `tick(App&)`；C5 Level 常驻 nested dock。
- 偏离：live dock drag 仍走 GUI `FDockContext`（框架不认识 editor root）；跨 root 拒绝发生在 materialize/invoke。没有第二扇 editor window。

### 下一接力点

`ES-4`：`FEditorTabSpawnContext` 增加 windowId、scope、optional ownerEditorId、document key、placement/detach；factory 只创建 UI，不持有 Surface。禁止第二扇 editor window、N Camera、两次 submit。

## 2026-09-09 — C4 ES-4（spawn context identity + optional owner pointers）

### 完成

- `FEditorTabSpawnContext` 携带 `windowId` / `scope` / optional `ownerEditorId` / `documentKey` / `placement` / `detachPolicy`；`tree` / `layer` / `selection` / `actions` / `undo` / `viewportHost` 改为可空指针。
- `FEditorTabSpawner` 同步带 placement/detach；`EditorDockWorkspace::makeSpawnContext(spawner)` 从 host + spawner 填 identity，不解引用空 host。
- Builtin：viewport Locked nested；hierarchy/inspector TearOffKeepOwner nested；window tools / ui-designer IndependentWindow。owned factory 缺 owner 状态返回 nullptr；`runtime-tools` 可在全空 context 下 spawn。
- factory 不持有 `EditorSurface*`、不拥有 tree、不调用 `tick()`。
- 验收：`SpawnContextAllowsNullPointersAndNoSurface`；`RuntimeToolsSpawnWithNullTreeAndLayer`；`OwnedToolSpawnReturnsNullWithoutOwnerState`；`MakeSpawnContextCopiesWindowAndSpawnerIdentity`。

### 保留 / 未完成 / 偏离

- 保留：仍一扇 native window、一个 Level `EditorRootSession`；扁平 window-root dock（C5）；`tick(App&)`（ES-5）；ui-designer 无第二份 root session（C6）。
- 未完成：ES-5 删 `tick(App&)` 与 `dynamic_cast<EditorInspectorTab*>` IME；C5 nested owned dock；第二扇 session。
- 偏离：placement `EditorOwnedNested` 仍物化进同一扁平 dock。没有第二扇 editor window。

### 下一接力点

`ES-5`：删除 `EditorSurface::tick(App&)` forwarding；`wantsTextInput()` 走 `WidgetTree` capability。完成后才允许第二扇 `EditorWindowSession`。禁止 N Camera、两次 submit。

## 2026-09-09 — C2 MW-207（present 消费方去掉 VulkanSwapChain hard-code）

### 完成

- `GUIAppHost` 用 `ISwapchain` 读 format/extent/handle，recreate 走 `IRenderSurfaceContext::requestRecreate()`。
- `PresentationGraphService::rebuildImages` 走 `IRenderSurfaceContext::buildPresentationImages`，不再 `as<VulkanSwapChain>()` / `getVkImages()`。
- `RHISurfaceContextTest` present/resize/minimize 只调 surface `begin`/`end`/`requestRecreate`。
- 验收：`RenderRuntimeSnapshotTest.HostPresentCoordinatorOwnsAcquireAndPresent` source-scan；`ya-rhi-vulkan-smoke` `RHISurfaceContext.*`。

### 保留 / 未完成 / 偏离

- 保留：GUI host 产品后端仍选 `ERenderAPI::Vulkan`；`VulkanRenderSurfaceContext` 仍是 Vulkan 实现；OpenGL 无 `createSurfaceContext`；`IRender::primarySwapchain()` 仍作 bootstrap/兼容。
- 未完成：WT-IME、DS-1、ES-5（删 `tick(App&)` 与剩余 `primarySwapchain` 读窗）、OpenGL 多窗。
- 偏离：无。未做 N Camera、两次 submit。

### 下一接力点

`WT-IME`：`WidgetTree::wantsTextInput()` + 删除 Inspector `dynamic_cast`。禁止第二扇 editor window、N Camera、两次 submit。

## 2026-09-09 — C4 WT-IME（WidgetTree text-input capability）

### 完成

- `WidgetTree::wantsTextInput()` 沿 `getFocusPath()` 问 `UIElement::wantsTextInput()`。
- `UITextField` 始终；`UIDragFloat` / `UISpinBox` 仅 `_bEditing`；ColorEdit picker 仅 hex 编辑。
- `EditorSurface` / `EditorInputNode` 转发 tree；删除 Inspector / AutoPropertySection 的 IME 特判和 Surface 对 `EditorInspectorTab.h` 的 include。
- 验收：`WidgetTreeTest.WantsTextInput*`；`ToolControlsTest` DragFloat/SpinBox/ColorEdit hex；`EditorWindowSessionTest.ImeUsesTreeCapabilityNotInspectorCast`。

### 保留 / 未完成 / 偏离

- 保留：仍一扇 native window；`tick(App&)`；扁平 dock；`primarySwapchain()` bootstrap。
- 未完成：ES-5 删 `tick(App&)` 与剩余 `primarySwapchain` 读窗。
- 偏离：无。没有第二扇 editor window。

### 下一接力点

`ES-5`：删除 `EditorSurface::tick(App&)` forwarding；事件、snapshot、viewport、dialogs 按 window id 路由；host/Surface 不经 `primarySwapchain()` 读窗。完成后才允许第二扇 `EditorWindowSession`。禁止 N Camera、两次 submit。

## 2026-09-09 — C4 DS-1（dock placement = 目标 dock scope + owner）

### 完成

- `canDockEditorTab(tab, targetPlacement, targetRootId)`：WindowTool / WindowRootEditor 只能进 window-root dock；owned tool 只能进同 owner 的 nested dock，C5 前也可进同 owner 的扁平 window-root dock。
- `EditorDockWorkspace::FHost.targetPlacement` 是目标 dock scope；`materializeTab` / layout import 拒绝错误 scope，不只比较 `activeRootId`。
- 当前 chrome host 显式声明 `WindowRootDock`。GUI `FDockContext` 不包含 editor root / scope 类型。
- 验收：`EditorRootSessionTest` 政策；`EditorDockWorkspaceTest` WindowTool-in-nested、other-root nested、layout 剔除 WindowTool；`EditorWindowSessionTest` host scope + DockContext source-scan。

### 保留 / 未完成 / 偏离

- 保留：仍一扇 native window、一个 Level `EditorRootSession`；扁平 window-root dock（C5）；`tick(App&)` 转发已在 Surface 删除但仍需 ES-5 收口路由与 `primarySwapchain` 读窗。
- 未完成：ES-5；C5 nested owned dock；第二扇 session。
- 偏离：placement `EditorOwnedNested` 仍可物化进 window-root dock（C5 扁平兼容）。没有第二扇 editor window。

### 下一接力点

`ES-5`：删除 `tick(App&)` 残留、按 window id 路由事件/snapshot/viewport/dialogs；host/Surface 不经 `primarySwapchain()` 读窗。禁止第二扇 editor window、N Camera、两次 submit。

## 2026-09-09 — C4 ES-5（按 window id 路由；删 tick(App&)）

### 完成

- `EditorSurface::tick(App&)` / `EditorWindowSession::tick(App&)` 已不存在；只走 `tick(const FEditorSurfaceContext&)`。
- `EditorInputNode` 持有 `EditorWindowRegistry*` + `EditorWindowId`，经 `find(windowId)` 取 session；dialogs / snapshot / viewport 同样按 window id。
- host/Surface/`EditorModule` 不再经 `primarySwapchain()` / `primaryWindow()` 读窗；metrics 来自注入的 `IRenderSurfaceContext`。
- 验收：`EditorWindowSessionTest.TickAppForwardingIsGone`、`HostAndSurfaceDoNotReadPrimarySwapchain`、`InputRoutesByWindowId`；`EditorInputContractTest` IME。

### 保留 / 未完成 / 偏离

- 保留：仍一扇 native present surface（`getPrimarySurfaceContext`）；`RuntimeRenderSettingsSection` 仍经 `primarySwapchain()` 改 vsync（WindowTool，不是 host/Surface）；扁平 window-root dock（C5）。
- 未完成：MW-401 第二扇 session；C5 nested owned dock chrome。
- 偏离：无第二扇 editor native window。未做 N Camera、两次 submit。

### 下一接力点

`MW-401`：两个 editor session 各自拥有 tree、window-root dock、nested owned dock、viewport context。禁止两棵树画进同一 native window，禁止每窗 `IRender::create`。

## 2026-09-09 — C4 MW-401（两个 editor session）

### 完成

- `EditorWindowRegistry` 可 `create` / `destroy` extra session；默认窗不能销毁。selection / Surface / window-root dock / nested owned dock / viewport overlay 隔离。
- 每个 `EditorSurface` 持有 window-root `FDockContext` + owned nested `FDockContext`；rebuild 分别 bind `WindowRootDock` / `EditorOwnedNested` workspace。
- `EditorModule::onDetach` 对全部 session `shutdown`。产品路径仍只 tick/present 默认 native window。
- 验收：`EditorWindowSessionTest.RegistryCreatesIsolatedSecondSession`；`EditorDockWorkspaceTest.TwoSessionsOwnIndependentRootAndNestedDocks`。

### 保留 / 未完成 / 偏离

- 保留：产品仍一扇 native present surface；owned tools 仍扁平进 window-root layout；nested dock 没有 `UIDockSpace` chrome。
- 未完成：C5 MW-501 Level 常驻 nested dock chrome；MW-502 旧 tab 迁到 session-owned 路径；C7 真实 tear-off。
- 偏离：没有第二扇 editor OS window。未做 N Camera、两次 submit、每窗 `IRender::create`。

### 下一接力点

`MW-501`：主窗口 Level Editor 常驻、不可关闭，owned tools 进 nested dock chrome，兼容旧 `editor.dockLayout`。禁止两树一窗、N Camera、两次 submit。

## 2026-09-10 — C5 MW-501（Level Editor 常驻 nested dock）

### 完成

- Builtin `level-editor`：Locked `WindowRootEditor`，spawn 出 `EditorLevelEditorTab`（nested `UIDockSpace` 投影 session 的 owned `FDockContext`）。
- `canDockEditorTab` 不再允许 owned tool 进 window-root；`invokeTab` 从 window-root 转发到 nested workspace。
- 两个工厂布局：`DefaultEditorDockLayout.json`（level + window tools）与 `DefaultEditorOwnedDockLayout.json`（viewport/hierarchy/inspector）。Locked tab `setPanelClosable(false)`；reset 先强制 closable 再关。
- `editor.dockLayout` v2 `{version:2, windowRoot, ownedNested}`；v1 扁平文档 remap 到两个工厂（自定义 split 丢失）。
- 验收：`FactoryLayoutPlacesDefaultTabs`、`FactoryOwnedNestedLayoutPlacesOwnedTools`、`LayoutDocumentForPlacement*`、`OwnedToolDocksOnlyUnderItsRoot`、`MaterializeRejectsOwnedToolInWindowRootEvenMatchingOwner`、`InvokeTabForwardsOwnedToolToNestedWorkspace`、`LockedTabRejectsClose`、`BuiltinLevelEditorSpawnRequiresNestedDock`。

### 保留 / 未完成 / 偏离

- 保留：产品仍一扇 native present surface；extras 可在 registry 创建但不 tick/present；ui-designer 仍是 WindowRootEditor 但无独立 root session（C6）；in-window floating，不是 native tear-off（C7）。
- 未完成：MW-502 把 Viewport/Hierarchy/Inspector/Content/Runtime 从「仍像整窗工具」收口到明确的 session-owned 路径；C6 Material/Script nested；C7 真实 OS tear-off。
- 偏离：v1 自定义 dock split 不保留，只保 tab 集合语义。未做 N Camera、两次 submit、每窗 `IRender::create`、两树一窗。

### 下一接力点

`MW-502`：迁移 Viewport/Hierarchy/Inspector/Content Browser/Runtime Tools 到 session-owned 路径。禁止把 Surface 改成 window manager。

## 2026-09-10 — C5 MW-502（旧 tab session-owned 路径）

### 完成

- `FEditorSurfaceContext.presentSurface` 与 spawn context `app` / `presentSurface` 由窗口 session 注入；Surface rebuild 消费完整 context。
- Viewport spawn 无 host 返回 nullptr。Inspector 必选 owner `SelectionModel`，目标实体经 `editorSelectionEntities` 解析（session ids，否则 Layer fallback）。
- Content Browser 经 `EditorLayer::cmdLoadScene`，不再 `App::get()`。Runtime Tools play/stop 走 ActionMap；vsync/present 走 `IRenderSurfaceContext::getSwapchain()`，不再 `primarySwapchain()`。
- 验收：`BuiltinViewportSpawnRequiresHost`、`OwnedToolSpawnReturnsNullWithoutOwnerState`（inspector 仅 selection 仍拒绝）、`MakeSpawnContextCopiesWindowAndSpawnerIdentity` 复制 app/presentSurface、`SessionOwnedTabsDoNotUseAppGetOrPrimarySwapchain`。

### 保留 / 未完成 / 偏离

- 保留：产品仍一扇 native present surface；Runtime Diagnostics/DebugPrimitives 段仍用过程级 `App::get()`（非 present）；ActionMap 的 play/exit 内部仍调 App（session 边界在 ActionMap）；EditorLayer 仍是共享 document。
- 未完成：C6 MW-601 document identity；MW-602 Material/Script nested；C7 真实 OS tear-off。
- 偏离：未做 N Camera、两次 submit、每窗 `IRender::create`、两树一窗。未把 App 从 ActionMap/Layer 命令里删掉。

### 下一接力点

`MW-601`：定义 document identity、dirty、undo、close、singleton、preview ownership。禁止把 Surface 改成 window manager。

## 2026-09-10 — C6 MW-601（document identity）

### 完成

- `FEditorDocumentId` / `EditorDocumentSession` / `EditorDocumentRegistry`：identity、dirty、undo、close policy、bindCount、per-kind preview claim。Registry 归 `EditorModule`，不归 Surface / WindowSession。
- Level scene：window `bindSceneDocument` 按路径单例；`EditorRootSession::undo()` 转发到 document。两窗同 key 共享 undo；切路径后 unused session discard。
- UI Designer：`RejectIfDirty` close、save 清 dirty、structural edit / drag `markDirty`、Close 按钮走 `closeDocument()`；scene-entry 按 `sceneName#entryId` 单例；preview claim 按 kind 互斥（不是 Camera）。
- 验收：`EditorDocumentSessionTest` singleton/dirty/Locked/bindCount/preview/shared undo/source-scan；`UIDesignerPanelTest.DocumentRegistryTracksDirtyCloseAndSingleton`、`OpenSceneEntrySharesDocumentSession`。

### 保留 / 未完成 / 偏离

- 保留：产品仍一扇 native present surface；Level 仍共享 `EditorLayer` scene；ui-designer 仍无独立 `EditorRootSession`（MW-602）；in-window floating，不是 native tear-off（C7）。
- 未完成：MW-602 Material/Script nested owned tools；UI Designer inspector 字段编辑不自动 markDirty；无 save/discard 对话框（脏 Close 直接拒绝）。
- 偏离：未做 N Camera、两次 submit、每窗 `IRender::create`、两树一窗。未把 Material/Script spawners 塞进本 checkpoint。

### 下一接力点

`MW-602`：Material/UI/Script 作为 `WindowRootEditor` 主窗口 Dock tabs，各自 Preview/Parameters/Hierarchy/Inspector 作为 `EditorOwnedTool` nested leaves。禁止把 Surface 改成 window manager。

## 2026-09-10 — Floating placement / native window 语义冻结

### 决策

- 明确区分 `DockNode`、`FDockFloatingPlacement`、`UIDockFloatingHost` 和 native `GUIWindowSession`。现有 floating host 是同一 OS window / WidgetTree 内的 `InProcessOverlay`，不是 native multi-window。
- 一个 native window 只拥有一棵 WidgetTree、一个 snapshot/input/focus 域和一套 surface/presentation；共享的是 device、document/editor session 和服务，不是 live widget tree。
- 跨窗移动采用 source detach → placement/session ownership transfer → target attach/rebuild；禁止同一 live widget 双挂载。
- `FDockContext` 继续只管理 dock model、placement 和 policy，不创建 native window；GUI coordinator 负责窗口生命周期，GameEditor 负责 owner/document/tab scope/close policy。
- Window chrome 作为 GUI Framework platform capability：macOS 默认 Hybrid（保留 traffic lights、safe-area 和 AppKit 行为），Windows 可选 ClientDrawn 但必须保留 resize/snap/system/accessibility 等语义；Editor/Dock 不直接依赖平台 non-client API。

### 影响

- C7 拆成 MW-701～MW-706：先冻结 placement/projection 数据模型，再实现 coordinator/session、跨 tree tear-off、re-dock/close、chrome capability 和 topology persistence。
- 现有 `UIDockFloatingHost`、Popup 坐标和 layout JSON 只能作为 in-process floating 兼容路径；不能直接解释为屏幕坐标或 OS window geometry。

### 保留 / 未完成 / 偏离

- 保留：C1-C6 已完成的每窗 WidgetTree、共享 device、per-surface presentation、Level root/owned-tool scope 和 ES-5 Surface 解耦。
- 未完成：真实 native tear-off、window topology persistence、跨平台 chrome backend 和 OpenGL/其他 backend 的窗口能力实现。
- 偏离：无代码实现；本轮只更新计划语义和验收边界。

### 下一接力点

`MW-602` 继续 Material/UI/Script root editor 迁移；C7 开始前先完成 `MW-701` placement/projection contract，禁止把现有 in-process floating 当作 native window 完成证据。

## 2026-09-10 — C6 MW-602（Material/UI/Script WindowRootEditor + nested owned tools）

### 完成

- `EditorWindowSession` 持有 Level / UI / Material / Script 四个 `EditorRootSession`；Surface 只拿 `FEditorRootSessions` 指针袋。UI/Material/Script 的 nested `FDockContext` 由 `EditorNestedDockHost` 持有（关闭 floating/tear-off），不把 Surface 做成 dock manager。
- `ui-designer` 是 `kUIEditorRootId` WindowRootEditor；Preview / Palette / Tree / Inspector 是 nested owned tools，UI 树与 Level Hierarchy 隔离。Material/Script 同样是 WindowRootEditor + Preview/Parameters/Hierarchy/Inspector document tools（identity / dirty / undo / preview claim chrome）。
- `invokeTab` 从 window-root 打开 owned tool 时先物化对应 root tab 再进其 nested。Content Browser `.lua` / `.mat` 经 Layer `openDocumentEditor` 绑定 document、claim preview、打开 root tab。
- 验收：`BuiltinOwnedToolsBindToLevelEditor`（ui-designer owner=UI；material/script spawners）；`UIOwnedToolCannotDockInLevelNested` / `UIOwnedToolCannotMaterializeInLevelNested`；`InvokeMaterialOwnedToolOpensRootThenNested`；`FactoryOwnedNestedLayoutForUIDoesNotUseLevelTree`；`MaterialAndScriptRootsHaveIsolatedUndoAndSelection`；`SessionOwnedTabsDoNotUseAppGetOrPrimarySwapchain` 覆盖新 tab 源文件。57 相关测试通过。

### 保留 / 未完成 / 偏离

- 保留：产品仍一扇 native present surface；Level 仍共享 `EditorLayer` scene；UI 画布仍是 Level 2D viewport（PreviewTarget，不是 Camera）；`editor.dockLayout` v2 `ownedNested` 只持久化 Level；in-window floating，不是 native tear-off（C7）。
- 未完成：Material/Script 还不是 material graph / script AST 编辑器；UI Designer inspector 字段编辑不自动 markDirty；无 save/discard 对话框（脏 Close 直接拒绝）；UI/Material/Script nested layout 不写入 `windows[]`（C8）。
- 偏离：未做 N Camera、两次 submit、每窗 `IRender::create`、两树一窗。未开始 C7 tear-off。

### 下一接力点

`MW-701`：root editor 或 detachable owned tool detach 到 native window。禁止把 Surface 改成 window manager。

## 2026-09-10 — C7 MW-701（FDockFloatingPlacement / overlay vs native projection）

### 完成

- `FDockFloatingPlacement` 取代 floating 记录语义：`EDockFloatingProjection::{InProcessOverlay, NativeWindow}`、`EDockSourceScope`、`targetWindowId`、opaque `ownerEditorId`、`documentKey`。Geometry 对 overlay 仍是 tree-local，不是屏幕坐标。
- `tearOffPanel` 默认 `InProcessOverlay`，从 host `sourceScope`/`hostWindowId` 和 panel identity 填 placement。`NativeWindow` 只记账，不创建 OS window。`UIDockFloatingHost` 只物化 overlay；NativeWindow 记录被跳过。
- 旧 `floating[]` JSON 缺 `projection` 时按 overlay 导入。Editor window-root / owned nested dock 写入 host scope + window id；`materializeTab` 把 spawner owner / document key 写到 panel。
- 验收：`DockNodeTest.TearOffCopiesHostAndPanelIdentityIntoPlacement`、`LegacyFloatingJsonImportsAsInProcessOverlay`、`OverlayHostSkipsNativeWindowPlacement`（32/32 DockNodeTest）；`EditorDockWorkspaceTest.MaterializeCopiesSpawnerIdentityOntoDockPanel`；`EditorWindowSessionTest.DockContextDoesNotKnowEditorRoots`（无 `EditorRootId` / `GameEditor`）。

### 保留 / 未完成 / 偏离

- 保留：产品仍一扇 native present surface；现有拖出仍是 Popup overlay；`FFloatingWindow` 作为 placement 别名保留。
- 未完成：MW-702 coordinator/session；MW-703 跨 tree native tear-off；MW-704 re-dock/close；MW-705 chrome capability；MW-706 / C8 `windows[]`。
- 偏离：未把 overlay host 改名冒充 OS window；未做 N Camera、两次 submit、每窗 `IRender::create`。

### 下一接力点

`MW-702`：GUI Framework `IGUIWindowCoordinator` / `IGUIWindowSession`。禁止把 `EditorSurface` 改成 window manager，禁止复制 `IRender::create`。

## 2026-09-10 — C7 边界再收口：Dock orchestration 归 GUI Framework

### 决策

- DockSpace 的 split/leaf/tab projection、hit-test、drop preview、tab reorder、floating projection、generic drag session 和跨 OS window drag router 全部归 GUI Framework。
- GUI Framework 负责 source/target window 路由、boundary enter/leave、keep-alive、deferred close 以及 detach → target accept → attach/rebuild 事务；这些接口不得包含 EditorRootId、DocumentKey、EditorOwnedTool 等 GameEditor 类型。
- GameEditor 只提供 tab scope、ownerEditorId、document identity、typed editor drag payload、placement policy 和 owner close policy。
- GameEditor 不得重新监听 SDL/平台事件实现跨窗拖拽，也不得由 EditorDockWorkspace 直接创建 native window。
- `IGUIWindowCoordinator` / `IGUIWindowSession` 是 GUI Framework 的窗口机制；`EditorDockWorkspace` 只是调用方和策略适配层。

### C7 执行顺序

1. MW-702：GUI window coordinator/session + per-window WidgetTree/surface/presentation。
2. MW-703：generic cross-window drag router 与 DockSpace 通用迁移事务。
3. MW-704：GameEditor typed payload/policy 接入 root editor 与 owned tool tear-off。
4. MW-705：owner-aware re-dock、close policy、空窗回收和 Level Editor 保护。
5. MW-706：window chrome capability backend。
6. MW-707：window topology 与 dock topology 分离持久化和兼容迁移。

### 保留 / 未完成 / 偏离

- 保留：MW-701 已冻结的 `FDockFloatingPlacement`、`InProcessOverlay` / `NativeWindow` projection 区分，以及一个 native window 一棵 WidgetTree。
- 未完成：MW-703～MW-707；当前 C3 的 GUI drag primitive 只证明 generic window routing，尚未完成 Dock projection 与 editor payload 的闭环。
- 偏离：不把 `UIDockFloatingHost` 改造成 window manager，不在 GameEditor 复制 GUI drag/drop 或 native window 实现。

### 下一接力点

`MW-703`：generic cross-window drag router 与 DockSpace 通用迁移事务。不得包含 EditorRootId / DocumentKey。

## 2026-09-10 — C7 MW-702（IGUIWindowCoordinator / GUIWindowSession）

### 完成

- `GUIWindowManager` 实现 `IGUIWindowCoordinator`；内部 `FSlot` 收成 `GUIWindowSession`：一扇 extra 窗拥有 NativeWindow + WidgetTree + snapshot + 可选 `IRenderSurfaceContext`。共享 process `IRender`，不调用 `IRender::create`。主窗仍是 `GUIWindowHost`，不是 session。
- `GUIApp::windowCoordinator()` / `findSession()` 暴露 coordinator。`realizeNativeDockPlacement` 为 NativeWindow placement 创建 session 并 `bindFloatingTargetWindow`；不跨树迁移 live widget。
- `FDockContext`：NativeWindow tear-off 的 `targetWindowId` 为 0 直到 bind；overlay 仍写 host window。`bindFloatingTargetWindow` 拒绝 overlay。缺 `targetWindowId` 的 native JSON 导入为 0。DockContext / EditorDockWorkspace 不创建 OS window。
- 验收：`GUIWindowManagerTest.SessionOwnsIsolatedTreeSnapshotAndNullSurfaceWithoutDevice`、`RealizeNativeDockPlacementBindsSessionWithoutMigratingWidget`；`GUIAppExtraWindowTest.ExposesCoordinatorSessions`；`DockNodeTest.NativeWindowTearOffLeavesTargetUnbound`、`NativeWindowJsonWithoutTargetStaysUnbound`（34/34 DockNodeTest）；`EditorWindowSessionTest.DockContextDoesNotKnowEditorRoots`。

### 保留 / 未完成 / 偏离

- 保留：产品仍只 present 默认 native window；主窗不是 `IGUIWindowSession`；realize 出的 extra 窗在无 device 测试里 surface 为 null；现有拖出仍是 overlay。
- 未完成：MW-703 跨 tree detach/attach 与 drag router；MW-704 editor tear-off；MW-705 re-dock/close；MW-706 chrome；MW-707 / C8 `windows[]`。
- 偏离：未另起一套 window manager；未把 `EditorSurface` 改成 window manager；未做 N Camera、两次 submit、每窗 `IRender::create`、两树一窗。

### 下一接力点

`MW-703`：GUI-owned cross-window drag router 与 DockSpace 通用迁移事务（source detach → target attach，禁止双挂载）。不得包含 editor 类型。

## 2026-09-10 — C7 MW-703（Dock 跨树迁移事务）

### 完成

- `FDockContext::extractPanel` / `adoptPanel` / `transferPanelTo` / `transferNativePlacementTo`：source detach → target adopt → leaf 挂载，禁止 live widget 双挂载。重复 `stableKey` 拒绝且保留 source。
- `FDockPanelDragDropOp` 携带 `sourceContext`；目标 `UIDockSpace` 把 foreign drop 当 import，先 resolve preview 再 extract，避免拒绝路径抽空面板。
- GUI 跨窗 drag router 仍是 MW-301 的那条（不另起 SDL listener）；dock 迁移发生在 DockSpace drop handler。
- 验收：`DockNodeTest.TransferPanelMovesWidgetWithoutDualMount`、`TransferRejectsDuplicateStableKeyAndPreservesSource`、`TransferNativePlacementMovesTornPanel`、`ForeignDockDropTransfersPanelWithoutDualMount`（39 含后续 canAdopt）；`GUIAppCrossWindowDragTest.DockPanelDropTransfersWithoutDualMount`；`EditorWindowSessionTest.DockContextDoesNotKnowEditorRoots`。

### 保留 / 未完成 / 偏离

- 保留：产品仍只 present 默认 native window；`realizeNativeDockPlacement` 仍不迁移 widget。
- 未完成：当时下一刀为 MW-704 editor tear-off。
- 偏离：未把 `EditorSurface` 改成 window manager；未做 N Camera、两次 submit、每窗 `IRender::create`。

### 下一接力点

`MW-704`：GameEditor typed payload / placement policy 与 native tear-off。

## 2026-09-10 — C7 MW-704（GameEditor native tear-off）

### 完成

- `FEditorTabDragPayload` + `canTearOffEditorTab` / `canAcceptEditorDrop`。`EditorDockWorkspace::bind` 把 `FDockContext::canAdoptPanel` 接到该政策（opaque stableKey / ownerEditorId / documentKey）；GUI 仍不认识 `EditorRootId`。
- `canAdoptPanel` 在 `transferPanelTo` / foreign drop 的 extract 之前过滤，拒绝时 source 面板仍在。
- `tearOffEditorPanelToNativeWindow`：GameEditor 只调用 `IGUIWindowCoordinator::realizeNativeDockPlacement` + `transferNativePlacementTo`。Locked 拒绝且不创建 OS window。Root editor 进 extra window-root dock；owned tool 进 extra owned-nested dock，保留 owner/document（同一 `EditorDocumentSession` bindCount+1）。Extra session `adoptHostTree` 使用 coordinator 的 WidgetTree，不另造第二棵树。
- `EditorDockWorkspace` 仍不 include `IGUIWindowCoordinator` / `GUIWindowManager`。
- 验收：`EditorRootSessionTest.TearOffAndDropPolicyFollowDetachAndScope`；`EditorDockWorkspaceTest.WindowRootAdoptPolicyRejectsOwnedToolWithoutExtracting`；`DockNodeTest.CanAdoptPanelRejectsTransferWithoutExtracting`；`EditorNativeTearOffTest.LockedTabDoesNotOpenNativeWindow`、`RootEditorMovesToNativeWindowWithoutDualMount`、`OwnedToolKeepsOwnerAndDocumentOnNativeWindow`；`EditorWindowSessionTest.DockContextDoesNotKnowEditorRoots`。

### 保留 / 未完成 / 偏离

- 保留：产品 `EditorModule` 仍只 tick/present 默认 native window；拖出 DockSpace 默认仍是 `InProcessOverlay`。Level / Viewport 仍 Locked。
- 未完成：MW-705 re-dock、owner-aware close、空窗回收、Level 保护；MW-706 chrome；MW-707 / C8 `windows[]`；产品路径把 extra editor window 纳入 present/input。
- 偏离：未把 `EditorSurface` 改成 window manager；未在 GameEditor 监听 SDL 或直接创建 native window；未做 N Camera、两次 submit、每窗 `IRender::create`、两树一窗。

### 下一接力点

`MW-705`：cross-window re-dock、owned tool 禁止挂到其他 root editor/tab（drop 路径已有 canAdopt，补 close/reclaim/Level 保护）。

## 2026-09-10 — C7 MW-705（re-dock / close / empty reclaim / Level 保护）

### 完成

- `canRedockEditorTab` / `canCloseEditorWindow`：Locked 不能迁；默认窗不能关。`canAdoptPanel` 拒绝 Locked import，owned tool 仍只能进 owner nested。
- `redockEditorPanelToOwner` 把面板迁回 owner home dock（owned tool → owner nested；root/window tool → default window-root），禁止双挂载。空 extra 经 `reclaimEditorWindowIfEmpty`：`EditorWindowRegistry::destroy` + `IGUIWindowCoordinator::destroySession`。
- `closeEditorWindow` 先预检再全部迁回再回收。GameEditor 不听 SDL、不自己建窗。
- 验收：`EditorNativeTearOffTest.OwnedToolRedocksHomeAndReclaimsEmptyWindow`、`CloseExtraWindowRedocksRootEditorHome`、`CloseDefaultWindowIsRejected`、`LockedLevelCannotRedockAwayFromDefault`；`EditorDockWorkspaceTest.LockedTabCannotImportIntoAnotherWindowRoot`；`EditorRootSessionTest.TearOffAndDropPolicyFollowDetachAndScope`。

### 保留 / 未完成 / 偏离

- 保留：产品 `EditorModule` 仍只 tick/present 默认 native window；拖出默认仍是 overlay。
- 未完成：MW-706 chrome capability；MW-707 / C8 `windows[]`；产品路径 extra window present/input；关 tab 后自动 reclaim 尚未接到 Surface。
- 偏离：未把 `EditorSurface` 改成 window manager；未做 N Camera、两次 submit、每窗 `IRender::create`。

### 下一接力点

`MW-706`：window chrome capability（macOS Hybrid，Windows 可选 ClientDrawn）。GUI Framework 只走 capability API。

## 2026-09-10 — C7 MW-706（window chrome capability）

### 完成

- `EWindowChromeMode` { Native, Hybrid, ClientDrawn } 与 `queryWindowChromeCapabilities` / `resolveWindowChromeMode` / `applyWindowChrome` 落在 GUI Host。macOS 默认 Hybrid（full-size content + 保留 traffic lights）；ClientDrawn 为显式请求（borderless + SDL hit-test）。
- 平台 non-client 只在 `GUIWindowChromeCocoa.mm` / `applyWindowChrome`。Dock、EditorSurface、tab spawner 只消费 `queryWindowChromeInsets` / metrics。
- `GUIWindowHost`、`GUIWindowManager`、`App::getOrCreateMainNativeWindow` 在建窗后 apply。Editor chrome 用 `chromeInsetTop` 避开 safe-area。
- 验收：`GUIWindowChromeTest.*`（6）；`EditorNativeTearOffTest.*`；`EditorWindowSessionTest.DockContextDoesNotKnowEditorRoots`。

### 保留 / 未完成 / 偏离

- 保留：产品仍只 present 默认 native window；拖出默认仍是 overlay。Windows ClientDrawn 走 SDL hit-test 做 resize/drag，未重建 Win32 snap / system menu / DWM shadow。
- 未完成：MW-707 topology persistence；C8 `windows[]`；产品 extra window present/input；titlebar 内嵌 tab 仍只用 titleContent 矩形，Editor 目前把菜单叠在 safe-area 下方。
- 偏离：未把 `EditorSurface` 改成 window manager；未做 N Camera、两次 submit、每窗 `IRender::create`。

### 下一接力点

`MW-707`：persistence 保存 window topology + dock topology + placement mode；旧 in-process floating JSON 必须可迁移。

## 2026-09-10 — C7 MW-707（dock floating[] vs windows[] persistence）

### 完成

- `FDockContext` export 把 `InProcessOverlay` 写入 `floating[]`（`geometrySpace: treeLocal`），把 `NativeWindow` 写入 sibling `windows[]`。
- 旧 `floating` 无 `projection` → overlay + TreeLocal，pos 不升格为屏幕坐标，也不进入 `windows[]`。legacy `floating` + `projection: nativeWindow` 无 geometrySpace 仍 TreeLocal；下次 export 迁到 `windows[]`。
- `collectLayoutPanelKeys` / `sanitizeLayoutJson` 同时走 `windows[]`；跨数组重复 panel key 导入失败。
- Editor persist envelope bump 到 v3，仍用 `windowRoot` / `ownedNested`（不是 C8 顶层 OS `windows[]`）。
- 验收：`DockNodeTest.LegacyFloatingJsonImportsAsInProcessOverlay`、`NativeWindowJsonWithoutTargetStaysUnbound`、`OverlayExportNeverCopiesPosIntoWindows`、`CollectLayoutPanelKeysWalksNativeWindows`、`EditorDockWorkspaceTest.LayoutDocumentForPlacementV3SelectsFields`。

### 保留 / 未完成 / 偏离

- 保留：产品仍只 present 默认 native window；拖出默认仍是 overlay；NativeWindow `pos` 本步仍 TreeLocal（不写 OS `setPosition`）。
- 未完成：MW-801 editor `windows[]` envelope（bounds/monitor/maximized/role、restore extra OS windows）；MW-802 坏 monitor 恢复；产品 extra window present/input。
- 偏离：未把 `EditorSurface` 改成 window manager；未在产品路径 realize extra OS windows；未做 N Camera、两次 submit、每窗 `IRender::create`。

### 下一接力点

`MW-801`：versioned editor `windows[]` envelope（bounds/monitor/maximized/role、root dock、document、nested owned dock）。

## 2026-09-10 — C8 MW-801（editor windows[] envelope）

### 完成

- `editor.dockLayout` v4：`windows[]` 每项含 `role` / `bounds` / `monitor` / `maximized` / `windowRoot` / `ownedNested` / `activeRootId` / `documentKey`。v1–v3 仍映射为 main window。
- `INativeWindow` 增加 position / display / maximized；GUI `queryWindowScreenPlacement` / `applyWindowScreenPlacement`。缺失 monitor 跳过 origin，只应用 size。
- `restoreEditorExtraWindows` 经 `IGUIWindowCoordinator::createSession` 恢复 extra OS window + `EditorWindowSession`；GameEditor 不创建 SDL 窗。`EditorDockWorkspace` 不 include coordinator。
- overlay `floating` pos 恢复后仍是 TreeLocal，不会写成 OS origin。
- 验收：`EditorDockWorkspaceTest.LayoutDocumentForPlacementV4SelectsMainWindowFields`；`EditorWindowLayoutTest.*`；`GUIWindowChromeTest.QueryAndApplyScreenPlacementUsesSizeNotOverlayCoords`。

### 保留 / 未完成 / 偏离

- 保留：产品 `EditorModule` 仍只 tick/present 默认 native window；App 仍无 coordinator，因此产品启动不会 restore extra OS window。拖出默认仍是 overlay。
- 未完成：产品 extra window present/input。
- 偏离：未把 `EditorSurface` 改成 window manager；未做 N Camera、两次 submit、每窗 `IRender::create`。

### 下一接力点

`MW-802`：坏 monitor、缺失 asset、窗口关闭中断时的安全恢复。

## 2026-09-10 — C8 MW-802（bad monitor / missing asset / interrupted close）

### 完成

- `recoverWindowScreenPlacement`：有效 monitor 应用 origin；缺失 monitor 按 name 或 primary 可用区重定位；`monitorIndex < 0` 且无名只改 size。Editor/Dock 不调 SDL。
- `restoreEditorExtraWindows` 跳过 `closing: true`、未知 `ownerEditorId`、registry 在场但找不到 `documentKey`；不把 Level/其他 root 的 document 改绑上去。空 torn-off extra 不 export、不 restore。
- 产品 `EditorModule` 对 main native window 做一次 placement recover；仍无 coordinator，不 restore extra OS window，也不 tick extra。
- 验收：`EditorWindowLayoutTest.*`（含 skip closing/missing document/unknown owner、export omit empty、relocate missing monitor）；`GUIWindowChromeTest.QueryAndApplyScreenPlacementUsesSizeNotOverlayCoords`；`EditorDockWorkspaceTest.LayoutDocumentForPlacement*`。

### 保留 / 未完成 / 偏离

- 保留：产品仍只 tick/present 默认 native window；App 仍无 coordinator；拖出默认仍是 overlay。
- 未完成：产品 extra window present/input；C9 soak；R-5；MW-901/902/903。
- 偏离：未把 `EditorSurface` 改成 window manager；未做 N Camera、两次 submit、每窗 `IRender::create`。

### 下一接力点

`C9`：双窗口、GPU parity、长时 resize/close/drag soak。产品 extra present/input 仍未接线，编辑器双窗 soak 不能假装完成。

## 2026-09-10 — C9-P（产品 extra present / input）

### 完成

- `IRuntimeModule::onAfterPresent`：primary `submitPresentFrame` 之后调用；primary acquire 失败 / 未 acquired 也调用（MW-206：不可上屏主窗不得跳过 extra）。
- `EditorModule` 持有 `GUIWindowManager`：`onAttach` init + `restoreEditorExtraWindows` + persist 带 coordinator；`onAfterPresent` 对 `closeRequested` extra 先 `closeEditorWindow`，再 `tickTrees` / `renderAll`。不 `tickAll`（flush 会在 redock 前销毁）。
- extra 不 `EditorSurface::tick`；内容在 coordinator `WidgetTree`（`adoptHostTree` + `hostDockOnTree`）。默认窗仍 `onPresentation` + primary cmdBuf + `replayUIFrameSnapshot(..., EditorToolSurface)`。
- 输入：`EditorInputNode` extra `dispatchEvent`；`onEvent` 路由 extra window 事件；主窗 `WindowFocus` 清 extra `_focusedId`；extra 鼠标不写 `App::_lastMousePos`。
- GameEditor 不 `IRender::create`、不 `SDL_CreateWindow`。`EditorDockWorkspace` 仍不 include coordinator。
- 验收：`EditorWindowSessionTest.*`（含 InputRoutesByWindowId / onAfterPresent / tickTrees）；`EditorWindowLayoutTest.*`；`EditorNativeTearOffTest.*`；`EditorDockWorkspaceTest.LayoutDocumentForPlacement*`；`GUIWindowManagerTest.*`（含 `setFocusedWindow(0)` 后未标记键盘不再进 extra）。

### 保留 / 未完成 / 偏离

- 保留：单 cmdBuf；N Camera / 两次 submit 不做；DockSpace NoTarget 仍 overlay；extra 窗只有 hosted dock，没有完整 editor menu chrome。
- 未完成：C9 soak（GPU parity / 长时 resize/close/drag）；R-5；MW-901/902/903。
- 偏离：未把 `EditorSurface` 改成 window manager；未把 Gallery extra 冒充 GameEditor soak。工作区 `WorkbenchSurface.cpp` 有未完成 DSL 草稿挡住 `ya-gui-tooling`，本步把它恢复成 HEAD 的 imperative `assembleChrome` 以便编过 editor，未做 Workbench 重构。

### 下一接力点

`C9 soak`：双窗口 GPU parity、长时 resize/close/drag。产品 extra present/input 已接线，但仍不能把单次 restore/present 冒充 soak 完成。

## 2026-09-10 — C9 soak（dual-window present / resize / close）

### 完成

- `RHISurfaceContext.ExtraWindowPresentResizeCloseSoak`：共享 `IRender` device 上 primary+extra 各 present 32 帧，中途 extra 两次 `requestRecreate`，关 extra 后 primary 再 present 8 帧。
- `GUIWindowManagerTest.TickTreesResizeMinimizeCloseSoakKeepsSibling`：64 帧 `tickTrees`+`renderAll`；resize A、minimize/restore B、`WindowClose` 后 `destroySession(A)`，B 仍在。
- `EditorNativeTearOffTest.ExtraTickTreesResizeCloseSoakMatchesProductPath`：tear-off 后按 `EditorModule::onAfterPresent` 顺序（closeRequested → `closeEditorWindow`，否则 `tickTrees`/`renderAll`）；resize/minimize/restore extra；关窗 redock，无双挂载。
- `GUIWorkbench --extra-window --exit-after-frame=32` 干净 `GUIWorkbench finished`；headless `windows_extra_os.jsonl` 通过。

### 保留 / 未完成 / 偏离

- 保留：单 cmdBuf；N Camera / 两次 submit 不做；DockSpace NoTarget 仍 overlay；extra 窗只有 hosted dock；双 swapchain golden BMP 仍延后（C2G）。
- 未完成：R-5；MW-901/902/903。Widgets `--gpu-shot`/`--offscreen-diff` 本步未作为 C9 门禁：工作区 `WorkbenchSurface` DSL 与 `widgets_interaction.jsonl` 的 focusPath 已分叉，且 HEAD `attachSlot` 与当前 builder API 对不上，不能把 Gallery Widgets golden 冒充双窗 soak。
- 偏离：未开启 DockSpace native tear-off 默认；未把 Gallery extra 冒充 GameEditor soak；未做 N Camera、每窗 `IRender::create`。

### 下一接力点

本计划编码轨已收口。剩余只有条件延后：R-5（需 trace/性能证据才拆第二次 submit）、MW-901/902/903。DockSpace 拖出仍 overlay，若要产品默认 native tear-off 需另开目标。

## 2026-09-11 — C10（产品 DockSpace NoTarget native tear-off）

### 完成

- `FDockContext::realizeNoTargetTearOff`：DockSpace NoTarget 先调回调，true 则跳过 overlay；未接线或 false 仍 `InProcessOverlay`（Gallery / WidgetTree 测试不变）。
- `realizeNativeDockPlacement` 只把 `geometrySpace == Screen` 的 pos 写入 `FGUIWindowHostConfig` origin。`createSession` 不再用 `monitorIndex = -1` 覆盖已查询的 monitor，否则 recover 会丢掉 origin。
- GameEditor：`EditorSurface::setOnDockNoTargetTearOff` 装到 window-root / owned-nested dock；`EditorModule` 调 `handleDockNoTargetTearOff` → coordinator。Locked/Level 返回 handled、不开窗、不 overlay。Live tear-off extra 补 `hookExtraPersist` + 同样的 NoTarget 回调。
- `EditorSurface` 不 include coordinator；`EditorDockWorkspace` / `FDockContext` 不创建 OS window。
- 验收：`WidgetTreeTest.DockSpaceTabDragBehaviorStartsSessionAndTearsOffOnNoTarget` overlay；`DockSpaceNoTargetCallback*`；`GUIWindowManagerTest.RealizeNativeDockPlacement*`；`EditorNativeTearOffTest.DockSpaceNoTarget*`；`EditorWindowSessionTest` source-scan。

### 保留 / 未完成 / 偏离

- 保留：单 cmdBuf；N Camera / 两次 submit 不做；extra 窗只有 hosted dock，没有完整 editor menu chrome；Gallery NoTarget 仍 overlay；双 swapchain golden BMP 仍延后。
- 未完成：R-5；MW-901/902/903。
- 偏离：未把 `EditorSurface` 改成 window manager；未做每窗 `IRender::create`；未要求 OS 像素级 origin 与 Screen 坐标逐点相等（窗口管理器可夹紧）。

### 下一接力点

C11：Hybrid chrome safe-zone（菜单不画在红绿灯下）、titleContent Client vs trailing Drag、Workbench/Editor DnD 与顶部 panel 命中。不实现完整 extra editor menu chrome、R-5、N Camera。

## 2026-09-11 — C11（Hybrid chrome safe-zone + DnD）

### 完成

- `makeWindowChromeLayout` Hybrid：SDL safe-area 为 0 仍 floor traffic-light 78×28；`titleContent` 是 Client（菜单），仅 trailing gutter 是 Drag。新增 `queryWindowChromeLayout`。
- Editor 菜单放进 title 行（左 inset + 右 gutter）；toolbar/dock 在 `contentInsets.top` 之下。Workbench `applyChromeSafeZone`。extra hosted dock `contentInsets`。GameRuntime resize/move 重 apply chrome hit-test。
- `UIDragSourceBehavior`：capture-on-press 即从 captured move 开 drag。Gallery tile 同时设 `bBeginDragFromCapturedMove`。
- 验收：`GUIWindowChromeTest.HybridLayout*` / `ClientDrawnLayout*` / source-scan `queryWindowChromeLayout`；`GUIWindowManagerTest.DragDropTileCaptureStartsSessionAndDrops`。`ya-game-editor` / `GUIWorkbench` / `ya-game-runtime` 编译通过。

### 保留 / 未完成 / 偏离

- 保留：单 cmdBuf；N Camera / 两次 submit 不做；extra 窗仍无完整 editor menu chrome。
- 未完成：R-5；MW-901/902/903；windowed 手感（GameEditor File 菜单、Workbench DragDrop 页、tear-off extra tab）需用户确认。
- 偏离：未把 `EditorSurface` 改成 window manager；未改 NSWindow/AppKit 到 Editor/Dock。

### 下一接力点

本计划编码轨已收口。剩余只有条件延后：R-5（需 trace/性能证据才拆第二次 submit）、MW-901/902/903。extra 完整 editor menu chrome 不是本步范围。

## 2026-09-11 — C12（host 唯一 drag session）

### 完成

- 跨窗 tab drop 失效、唯一 tab 拖出 N-1 空叶窗：每棵 `WidgetTree` 各养一份 drag session，GameEditor 不走 `GUIApp` 的 router。
- `GUIDragRouter` 是每个 input universe（`GUIApp` 或 GameEditor `AppKernel`）的唯一 source/hover window 身份。`WidgetTree` 只持有 payload/ghost/observer。
- GameEditor：`EditorInputNode` 处理 Input 事件，`EditorModule::onEvent` 处理 `WindowMouseLeave`；二者 bind 同一 router。主窗 id 来自 `InputRouter::getWindow()`，不读 `App` 私有 `NativeWindowManager`。`WindowFocusLost` 不 cancel 进行中的 drag。
- 唯一 extra tab NoTarget 移动现有 OS 窗；tear-off 后 reclaim 空源窗。远指针不 tear-off。
- 验收：`GUIDragRouterTest.*`、`GUIAppCrossWindowDragTest.*`、`EditorNativeTearOffTest.UniqueExtraTabTearOffReclaimsEmptySource`、`EditorWindowSessionTest.InputRoutesByWindowId`。`ya-game-editor` / `ya-gui-headless-host-test` 编译通过。

### 保留 / 未完成 / 偏离

- 保留：单 cmdBuf；N Camera / 两次 submit 不做；extra 窗仍无完整 editor menu chrome。
- 未完成：R-5；MW-901/902/903；windowed 手感（Hierarchy 跨窗 drop、唯一 tab extra 不产空叶窗、extra 不再卡在 5 格 chooser）需用户确认。
- 偏离：未把 session 做成进程单例（gtest 多树）；未把 `GUIApp` 当 GameEditor context。

### 下一接力点

C13 收口同一 router 上的 capture / cursor / IME / clipboard / app-modal。

## 2026-09-11 — C13（host 指针宇宙收口）

### 完成

- 同一 `GUIDragRouter` 增加 capture 所属窗、OS cursor、IME 窗、树内 modal 的 app-modal 阻挡。外窗指针事件 remap 到 capture 树；modal 打开时吞掉其它 OS 窗的 pointer/key。
- extra `GUIWindowManager` session `bindSdlClipboard`；`WindowFocusLost` 不对正在 capture/drag 的树注入远指针。
- GameEditor `EditorInputNode::getCursor()` 问 router；GUIApp `applyPointerUniverse` 在事件后 `OsCursor::set` + `syncTextInput`。
- 验收：`GUIDragRouterTest.RoutesCapturedMoveFromForeignTreeWithoutSdl` / `ModalBlocksForeignWindowPointerWithoutSdl` / `TextInputWindowFollowsFocusedFieldWithoutSdl` / `CursorReadsCaptureWidgetWithoutSdl`；既有 `GUIAppCrossWindowDragTest.*`；`EditorWindowSessionTest.InputRoutesByWindowId` source-scan clipboard + cursor。`ya-game-editor` 编译通过。

### 保留 / 未完成 / 偏离

- 保留：单 cmdBuf；N Camera / 两次 submit 不做；extra 窗仍无完整 editor menu chrome。
- 未完成：R-5；MW-901/902/903；FontManager `setActiveDpiScale` 仍是进程全局（方向相反，未纳入本步）；windowed 手感（extra 分栏拖出窗外、extra TextField IME、file picker modal 挡住其它 OS 窗）需用户确认。
- 偏离：未新造 `GUIPointerRouter`；未把 dialog 做成进程单例。

### 下一接力点

本计划编码轨已收口。剩余只有条件延后：R-5（需 trace/性能证据才拆第二次 submit）、MW-901/902/903。extra 完整 editor menu chrome 不是本步范围。

## 2026-09-11 — Chrome page tabs / leaf roles / UE drag overlay

### 目标

Hybrid 窗体自上而下：traffic lights + 页面 TabBar → MenuBar → toolbar/dock。WindowRootEditor 只进 chrome 页签 / page leaf；WindowTool 只进 inner tools leaf。跨窗拖出 OS 窗口后，GUI Host 动态创建 click-through 透明置顶幽灵窗，只负责预览。

### 已落地

- `EDockLeafRole` Page/Tools + factory JSON `hideTabBar` page leaf；`canAdoptOntoLeaf` / `chooseAdoptLeaf`。
- `EditorSurface` 把 `UIMenuBar` 移出 titleContent，页签进 Hybrid title row。
- `GUIDragRouter`：窗口矩形命中 + `SDL_CaptureMouse`；鼠标离开全部 bound window 时 spawn `bDragOverlay` session（不进 EditorWindowRegistry / orphan reap）。
- 未使用系统 OLE；payload/hit-test 仍在 router + WidgetTree。

### 保留 / 未完成

- extra 窗仍走同一套 EditorSurface chrome（含 menu），没有单独做成“无 menu 的 tool-only 窗”。
- 透明 Vulkan swapchain 在 macOS 上可能不透明；overlay 仍是跟随光标的小窗，不是全屏桌宠层。
- 未提交。

## 2026-09-11 — C14 Dock drop target seam

- 新增 `EDockDropTargetKind` / `FDockDropTarget`。`UIDockSpace::FDropPreview` 不再用 `bMerge`/`bTabBar`/`bChooser`/`targetFloatingId`。
- `resolveDropPreview` 拆成 `resolveFloatingWell` / `resolveTabWell` / `resolveTabStack`；Area 只做 leaf focus。
- 未公开 `UIDockTabWell` / `UIDockTabStack`；未把 `commitDrop` 收到 `FDockContext`。
- 验收：`DockNodeTest.DropTargetKindDistinguishesWellStackSplitAndNoTarget` 区分 Well / same-leaf center / foreign chooser / split / NoTarget。

## 2026-09-11 — C15 DockStackView + TabRegistry / commitDrop

- `FLeafView` 改为 `FDockStackView`（`stackId` / `well` / content）。物化控件名 `DockStack*`。
- `EDockNodeKind::Stack` 取代公开 `Leaf`；JSON 写出 `"stack"`，仍可读 `"leaf"`。`findStackForPanel` / `splitStack` / `stackIds` 为正式名，旧 `findLeafForPanel` / `splitLeaf` / `leafIds` 保留为别名。
- `FDockContext` 内部分成 `FTabRegistry _tabs` 与 `FDockTreeModel _layout`；`layout()` / `tabs()` 与 `dockModel()` 并存。
- `FDockContext::commitDrop` 接收 `FDockDropTarget`；`UIDockSpace` drop 只做 import + commit + 投影刷新。
- `UIDockFloatingWindow::dropTargetAt` 自己产生 `FloatingTabWell`，不再把 accept/commit 转给 `UIDockSpace::onDrop`。
- 未公开 `UIDockTabWell` / `UIDockTabStack`；GameEditor policy 签名未改。
- 验收：`CommitDropMovesPanelBetweenStacks`、`FloatingWindowProducesDropTargetWithoutDockSpace`、`LayoutJsonExportsStackKindAndImportsLegacyLeaf`。
