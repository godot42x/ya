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
