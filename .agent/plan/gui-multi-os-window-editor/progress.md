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
