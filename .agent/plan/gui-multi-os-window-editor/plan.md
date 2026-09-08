# GUI Framework 真正多 OS Window 与 GameEditor 多 Editor 计划

> 建立日期：2026-09-08；状态：待实施。

## 目标与结论

采用 GUI Framework 提供多 native window 基础、GameEditor 提供 editor/session/tab 语义的分层方案。主窗口默认聚合常驻 Level Editor；Material/UI/Script editor 可作为主窗口 tab，也可 tear-off 成真实 OS window，再 re-dock 到其他窗口。

### Tab scope 必须显式建模

不是所有 tab 都是同一级 editor：

- `WindowRootEditor`：window 根 DockContext 的顶层 editor（Level/Material/UI/Script），是 native window tear-off 的默认迁移单位。
- `EditorOwnedTool`：某个 root editor 内部的 nested dock leaf/tab（Hierarchy、Inspector、Material Preview、UI Tree），不能 dock 到另一个 root editor/tab 的内部，但可以 tear-off 成独立 editor window；独立窗口仍保留 owner editor/document 语义。
- `WindowTool`：window 级工具（Content Browser、Output、Runtime Tools），不属于 document editor，可按 window policy 停靠或独立。

每个 descriptor、instance 和 persistence record 必须包含 scope、`ownerEditorId`（owned tool 必填）、document key、dock-compatibility 和 detachable policy；不能把所有注册 tab 扁平放进同一个 root DockContext。

`WindowRootEditor` 与 `EditorOwnedTool` 的关系是 ownership/placement 关系，不是 OS window 限制：root editor 的根 widget 持有一个 editor-owned nested `FDockContext` 投影；owned tool 不能 dock 到另一个 root editor/tab，但可以独立成 editor window，并继续携带 `ownerEditorId`。若产品需要完全脱离 owner 语义的 Inspector/Output，才注册为独立 `WindowTool`。

推荐一个 native window 对应一个 `WidgetTree`，但所有窗口由一个 `GUIApplication`/`GUIWindowManager` 统一 loop 调度。共享 `IRender` device、shader/font/texture services；每个窗口独立拥有 WidgetTree、snapshot、surface/swapchain、focus/capture 和 Render2D pass slot。

不采用一个 WidgetTree 管多个 OS window：pointer/focus/capture、tooltip/popup、DPI、clip、snapshot 和销毁边界都会被迫增加 window 维度。也不为每个 window 建独立 while-loop。

## 当前仓库事实

GUI Framework 已有 `GUIWindowHost`、WidgetTree、UIFrameSnapshot、GUIRenderSurface、Render2DPassSlot；`UIDockSpace`/`UIDockFloatingHost` 仍是同一 tree 内的 dock/floating 投影。

GameEditor 的 `EditorSurface` 同时拥有一个 tree、DockContext、SelectionModel、ActionMap、UndoStack、viewport bridge；`EditorModule` 只有一个 surface；`FEditorTabSpawner` context 直接绑定 WidgetTree、EditorLayer 和 viewport sink。当前 floating 不是 native window，缺少 window registry、per-window presentation、跨 tree tab migration、nested editor-owned dock、window topology persistence。

## EditorSurface 迁移方案

不要把 `EditorSurface` 删除，也不要把它改造成多窗口管理器或更大的 `EditorWindowSession` god object。按 window-local / editor-root / app-global / legacy bridge 拆职责后，它只保留「当前窗口 UI 编排 facade」；窗口生命周期归 GUI Framework，editor/document 状态归 GameEditor session。

当前入口仍是 [`EditorSurface.h`](Engine/Source/Applications/GameEditor/include/GameEditor/UI/EditorSurface.h) 的 `tick(App&, float)`，以及 [`EditorModule.cpp`](Engine/Source/Applications/GameEditor/EditorModule.cpp) 直接持有 `_editorSurface`。`applyWindowMetrics()` 仍读 `IRender::getWindowSize/getNativeWindow/getSwapchainWidth`；`wantsTextInput()` 仍 `dynamic_cast<EditorInspectorTab*>`。这些是迁移的第一批切口，不是把 Surface 再堆成 window manager 的理由。

### 目标对象关系

```text
GUIWindowHost
  └── native window + WidgetTree + GUIRenderSurface
GameEditor
  └── EditorWindowRegistry
        └── EditorWindowSession          // 一个 native window 一份
              ├── EditorSurface          // 当前窗口 UI 编排 facade
              ├── root FDockContext
              ├── EditorRootSession[]    // Level/Material/UI/Script
              ├── EditorViewportBridge
              └── EditorWindowMetrics / focus
```

一个 native window 对应一个 WidgetTree。window-root `FDockContext` 可以包含多个 `WindowRootEditor`；每个 root editor 再拥有自己的 nested `FDockContext`。`EditorSurface` 不拥有全局 editor registry，也不决定哪个 root editor 应该打开。

`EditorWindowSession` 只持有窗口级生命周期与对 Surface / dock / viewport 的编排入口，不要把 selection、undo、action、tab 类型识别、RHI 或多窗 registry 继续塞进去。

### 最终职责

`EditorSurface` 保留：

- 构建当前窗口的 menu、toolbar、dock projection；
- 驱动当前窗口的 WidgetTree；
- 管理当前窗口的 dialog、viewport overlay；
- 生成当前窗口的 `UIFrameSnapshot`；
- 将窗口局部 viewport 状态发布给对应 editor session。

它不再负责：

- 读取 `App` / `IRender` / swapchain / native window；
- 管理多个 OS window；
- 决定哪个 root editor 应该打开；
- 保存全局 selection、undo、action；
- 识别具体 tab 类型（例如 `EditorInspectorTab`）。

内部只按稳定 owner 拆成员协作者，后续再按阅读困难拆文件，不得按行数机械拆分：

- WindowChrome（`_root` / `_menuBar` / `_toolbarModeText`）
- RootWorkspace（dock projection；model 本身归 session）
- ViewportBridge
- DialogCoordinator
- Snapshot orchestration

`EditorSurface::tick` 仍应是一条可读主流程，不要拆成多个并行生命周期。tab 不得持有 `EditorSurface*`、不得自己 `tick()`、不得管理 WidgetTree 生命周期、不得访问 native window 或 swapchain。

### 当前字段归属

按四类迁移，禁止「每个 native window 复制一份全局状态」：

- window-local：`_tree`、`_theme`、`_snapshot` → `EditorWindowSession` / GUI window host；`_root` / `_menuBar` / `_toolbarModeText` → Surface WindowChrome；`_dockContext` / `_dockSpace` / `_dockFloatingHost` → session 的 window-root workspace；`_viewport*` / overlay → `EditorViewportBridge`；file picker / settings → window-local DialogCoordinator。
- editor-root：`_selection`、`_actions`、`_undo` → `EditorRootSession` / document session。窗口只引用当前激活的 root editor session，不能按窗口盲目复制。
- app-global：`_workspace` → GameEditor `EditorWorkspaceController`，由 Surface 调用；`_layer` → GameEditor session/service，经窄 context 注入；`_tabSpawners` → application/workspace service。
- legacy bridge：`_app`、`_appStateHandle` 删除，改为 typed state/service subscription。

### API 与主时序

当前：

```cpp
void EditorSurface::tick(App& app, float dt);
void EditorSurface::applyWindowMetrics(App& app);
```

改为消费窄 context，Surface 不再调用 `IRender` 的 window/swapchain API：

```cpp
struct EditorWindowMetrics
{
    Extent2D logicalExtent;
    Extent2D framebufferExtent;
    float dpiScale = 1.0f;
};

struct FEditorSurfaceContext
{
    EditorLayer& layer;
    WidgetTree& tree;
    EditorWindowMetrics metrics;
    EditorTabSpawnerRegistry& tabSpawners;
    IEditorViewportHost* viewportHost = nullptr;
    EditorApplicationServices& services;
};

void EditorSurface::tick(FEditorSurfaceContext& context, float dt);
```

第一阶段保留现有实现，由 session 持有 Surface：

```cpp
class EditorWindowSession
{
    GUIWindowId windowId() const;
    void dispatchEvent(const Event&, glm::vec2);
    void tick(float dt);
    const UIFrameSnapshot& snapshot() const;

    EditorSurface _surface;
    FDockContext _rootDock;
    EditorWindowMetrics _metrics;
    EditorViewportBridge _viewport;
};
```

初始单窗口时 `EditorModule` 只持有单元素 `EditorWindowRegistry`，所有事件 / tick / snapshot / viewport rect 按 `EditorWindowId` 路由到 default session。确认单窗口行为不变后，才允许第二个 `EditorWindowSession`。

主入口：

```text
EditorWindowSession::tick
  -> surface.ensureBuilt()
  -> surface.updateMetrics(context.metrics)
  -> tree.dispatch maintenance
  -> tree.tick(dt)
  -> surface.updateDialogs()
  -> surface.updateViewportBridge()
  -> tree.buildSnapshot()
  -> publish viewport/focus state
```

迁移期可保留 `EditorSurface::tick(App&, float)` 作为过渡 forwarding，内部构造 `FEditorSurfaceContext`；新代码只能走 `EditorWindowSession::tick(dt)`。单窗口闭环后删除兼容入口。

兼容顺序固定：

1. `applyWindowMetrics` 改为消费 `EditorWindowMetrics`；
2. `pushViewportDisplay` 改为使用当前 window 的 viewport binding；
3. `syncViewportHostState` 改为消费 window-local frame state；
4. `EditorModule` 引入单元素 `EditorWindowRegistry`；
5. 所有 `_editorSurface.xxx()` 改为 default window session；
6. 删除 `EditorSurface::tick(App&)`；
7. 最后才允许创建第二个 `EditorWindowSession`。

### Tab spawn 与文本输入

当前 [`FEditorTabSpawnContext`](Engine/Source/Applications/GameEditor/include/GameEditor/UI/EditorTabSpawnerRegistry.h) 绑定单窗口的 `tree/layer/selection/actions/undo/viewportHost`。应增加 `windowId`、`EEditorTabScope`、`optional ownerEditorId`、document key 和 placement/detach policy；`selection/actions/undo/viewportHost` 改为可空指针，由 owner editor session 提供。

`spawn()` 只创建 widget/content。生命周期仍由 WidgetTree 负责 attach/detach/tick。

- `WindowRootEditor`（Level/Material/UI/Script）：挂在 window-root DockContext，可 tear-off 为 native window，持有 nested dock 与 document/selection/undo。
- `EditorOwnedTool`（Hierarchy/Inspector/Preview/UI Tree）：挂在所属 root editor 的 nested DockContext；可 tear-off，但必须保留 `ownerEditorId`，不能 dock 到其他 root editor/tab。
- `WindowTool`（Content Browser/Output/Runtime Tools）：不绑定 editor/document，按 window policy 停靠或独立。

`wantsTextInput()` 必须一起迁移。删除 `dynamic_cast<EditorInspectorTab*>`；改为 `WidgetTree::wantsTextInput()`，由 focus path 与 focused widget capability 判断。Inspector / Material / Script 都不需要被 Surface 特判。`EditorInputNode` 改为读 window session / tree capability。

### EditorSurface 迁移 checkpoint

这些是 C4 的内部顺序，不能跳步用 registry 占位或第二扇窗冒充完成：

- ES-1 Window context 解耦：metrics、viewport state、services 注入；Surface 不再读 `App` / RHI window API；单窗口行为不变。
- ES-2 Session 化：`EditorWindowSession` 持有 Surface、window-root dock、tree/viewport binding；`EditorModule` 通过单元素 registry 调度；仍只有一个 native window。
- ES-3 Root/nested ownership：root editor、owned tool、window tool 分类落地；selection/action/undo 归属 editor/document session。
- ES-4 Factory 解耦：spawn context 携带 scope/owner/document/placement；tab 不持有 Surface、不自行 tick。
- ES-5 Multi-window 路由：事件、snapshot、viewport、dialogs 按 window id 路由；旧 `tick(App&)` forwarding 删除。完成后才允许第二扇 editor window。

每个 checkpoint 必须有代码、定向测试和本计划工件更新；不能以重命名、拆文件、registry 占位或纯文档作为完成标志。

`feature_matrix.json` 的 `game_editor` track 应包含：

- ES-1：`FEditorSurfaceContext` + `EditorWindowMetrics`；Surface 不再读 App/IRender window API
- ES-2：单元素 `EditorWindowRegistry` + `EditorWindowSession` 持有 Surface；禁止 god object
- ES-3：selection/action/undo 归属 `EditorRootSession`；owned tool 带 `ownerEditorId`
- ES-4：spawn context 携带 windowId/scope/owner/document/placement；tab 不持有 Surface
- ES-5：删除 `tick(App&)`；`wantsTextInput` 走 WidgetTree capability；完成后才允许第二扇 editor window
- MW-401：ES-5 之后的双 session；window-root dock + nested editor-owned dock
- MW-402：与 ES-4 同一目标

## 层级契约

GUI Framework 负责 native window、SDL event 路由、window lifecycle、每窗口 WidgetTree/snapshot/surface/swapchain、resize/DPI/focus 和统一 loop。

GameEditor 负责 EditorWindowSession、window-root FDockContext、editor-owned nested dock context、EditorTabInstance、EditorDocumentSession、tab placement/ownership、tab spawner、dock/tear-off/re-dock 和 editor layout persistence。

`FDockContext` 不创建 native window；`EditorTabSpawnerRegistry` 不拥有 widget/tree；owned tool 可以成为独立 window，但不能 dock 到其他 root editor/tab；不新增 `EditorPanel`、中心万能 bus 或第二套生命周期。

## RHI/Render 影响面

C0 必须审计以下单实例假设后才能写代码：

1. `IRender`、`VulkanSwapChain`、surface、window handle 的 ownership；
2. `PresentationGraphService`、`getSwapchain()` 是否假定唯一窗口；
3. acquire/present、out-of-date、resize、minimized zero extent 的 per-window 状态；
4. `MAX_FLIGHTS_IN_FLIGHT` 下 UBO、vertex buffer、descriptor pool 的隔离；
5. `DeferredDeletionQueue` 是否能延迟回收关闭窗口资源；
6. `Render2DPassSlot` 与静态 `Render2D::session` 的串行/并行边界；
7. `GUIRenderSurface` imported swapchain image、layout 和 frame-boundary 替换；
8. OpenGL context/window 绑定；
9. GameRuntime world graph 是否把主窗口 swapchain 视为唯一 target。

首轮不把完整 GameRuntime world graph 复制到每个 editor window。主窗口继续消费 world/presentation；Material/UI preview 优先使用 offscreen/preview target。只有真实需求证明不足时，才另开独立 RenderRuntime world viewport 计划。

RHI 目标是共享一个 device、每窗口 surface/swapchain/frame resources、所有 create/rebuild/destroy 在 frame boundary 完成；command recording 只消费 immutable snapshot 和当前 surface。

### 已完成的底层调研结果

当前 `NativeWindowManager` 已能持有多个 `INativeWindow`，但只管理 SDL/native 对象，不创建 Vulkan surface、swapchain、同步对象，也不关联 WidgetTree。真正的单窗口耦合集中在 RHI：`IRender` 同时拥有 device 与 presentation API；`VulkanRender` 当前持有一个 `VkInstance`、一个 `VkSurfaceKHR`、一个 `_swapChain`、一个 native window、一套 graphics/present queue 和一套 frame sync；`VulkanRender::begin/end` 直接 acquire/present 这个唯一 swapchain。

`PresentationGraphService` 绑定 `render->getSwapchain()`，按唯一 swapchain image 数创建 presentation images/executors，并监听唯一 `onRecreate`。它应继续作为主 GameRuntime presentation service，不应被 GUI 辅助窗口复用。`GUIAppHost` 当前每个 host 初始化一个 `IRender`、一个 swapchain、一个 `Render2D::init` 和一个 WidgetTree；多窗口不能复制这段 init 以创建多个 device。

因此目标不是 `IRender[]`，而是长期分离 `IRenderDevice`（instance/device/queues/allocator/resource factory/descriptor/pipeline cache）与 `IRenderSurfaceContext`（INativeWindow + VkSurfaceKHR + ISwapchain + acquire/present + per-surface sync/recreate）。迁移期保留 `IRender` 作为主 surface compatibility facade；多窗口新代码不得依赖 `getSwapchain/getNativeWindow/getWindowSize/setVsync/begin/end` 这些主 surface 入口。

每个 surface context 独占 swapchain、current image、pending recreate、image-available semaphore、render-finished semaphore、frame fence 和 imported presentation images；共享的只能是 device 级资源。

`Render2D` 在统一 loop 串行录制时继续使用静态 session，但每窗口分配唯一 pass slot；只有并行 recording、嵌套 session 或第二个独立 Render2D owner 被真实证明时，才启动 `MW-901`。

`MW-001/002` 的输出必须具体包含：IRender API 的 device/surface/main-facade 分类；instance→device→surface→swapchain→sync→imported image 时序；双窗口 acquire/record/submit/present 顺序；minimized/out-of-date/close 对另一窗口的行为；flight/descriptor/vertex/pass-slot/deferred-deletion 共享表；RenderRuntime 仍只服务主 world window 的证据；OpenGL current-context 的支持结论。

`MW-001` 已落地为 [`c0_mw001_irender_ownership.md`](c0_mw001_irender_ownership.md)。`MW-002` 为 [`c0_mw002_frame_boundary.md`](c0_mw002_frame_boundary.md)。`MW-003` 为 [`c0_mw003_tab_scope.md`](c0_mw003_tab_scope.md)。`MW-004` 为 [`c0_mw004_contract.md`](c0_mw004_contract.md)。C0 闭环。下一编码入口是 `MW-101`（`GUIWindowManager` + 共享 device + surface-context provider），禁止复制 `GUIAppHost::init` / `IRender::create`。

## GUI Framework 实施轨道

### G0：事实与契约

冻结不变量：一个 `GUIWindowHost` 等于一个 native window、一个 WidgetTree 和一个 snapshot/presentation state；所有 window 由一个 loop 调度；window-local route 不泄漏；close/create/rebuild 延迟到安全边界。

### G1：NativeWindowManager

G1 必须消费共享 device + surface-context provider，禁止通过 `IRender::create` 为每个窗口创建完整 backend；先完成两个空白窗口的 tree/input/tick 生命周期，再接 presentation。

在 `Framework/GUI/Host` 增加最小 manager：create、requestClose、destroy、find、dispatch native event、tickAll、renderAll。保持一个 host 一个 tree；验收两个空白 window 同时运行，输入、resize、focus、close 互不影响。

### G2：per-window presentation

实现顺序固定为：先补 RHI surface-context factory 与 per-surface sync，再接 GUI host；不能复制 `GUIAppHost::init`。

接入 per-window surface/swapchain、frame resources、Render2D pass slot、resize/minimize/out-of-date 和 deferred destroy。验收两个窗口同时 present 不同 retained UI，关闭/resize 一个不污染另一个，GPU validation 无生命周期错误。

具体落点：优先在 `RHI/Core` 增加 additive 的 surface/presentation context 接口和 Vulkan 实现，复用现有 `VulkanSwapChain`；不要第一步重命名或删除 `IRender`。`VulkanRender` 继续作为共享 device owner，并通过 context factory 为每个 `INativeWindow` 创建 surface/swapchain/sync。`GUIWindowHost` 只持有 context handle 和 `GUIRenderSurface`，不直接访问 `VulkanRender` 私有成员。

`PresentationGraphService` 暂时继续只消费主 `IRender` facade；辅助 GUI window 使用 window-local imported target + GUI compose。未来若需要多个 world window，另开 `RenderRuntime multi-viewport` checkpoint。

### G2.5：FeatureGallery 多窗口实例

C1/C2 的空白双窗不能当作产品验收。GUI Framework 的第一份真实消费者是 [`Example/GUIWorkbench`](Example/GUIWorkbench) Feature Gallery，不是 GameEditor。

在 `GUIApp` 暴露 `openWindow` / `closeWindow` / `findWindow`（内部走 `GUIWindowManager`）。[`FWorkbenchApp`](Example/GUIWorkbench/Source/GUIWorkbench.h) 只调用这些 API，不自己持有第二套 loop，也不把 `FWorkbenchSurface` 改成 window manager。

落点：Composition 组新增 `Windows` 页（[`WorkbenchDemoPages.h`](Example/GUIWorkbench/Source/WorkbenchDemoPages.h) / `Pages/CompositionPages.cpp`，`surface.addPage("Composition", "Windows", ...)`）。页内可 Open / Close 额外 native window。每个额外窗口：

- 独立 `GUIWindowHost` + `WidgetTree` + snapshot + surface/swapchain；
- 独立 retained UI 实例（标题、计数按钮等足以证明隔离），不复用主 Gallery 的 tree、focus、`FDemoState` widget handles；
- 不是第二份完整 Gallery shell，也不是 `FDockContext` floating / tear-off；
- 关闭额外窗口不退出进程，不销毁主 Gallery tree；关主窗才退。

验收：`xmake b GUIWorkbench`；`--start-page Windows` 打开第二扇窗；两窗可独立 resize/focus/点击；输入与 tooltip 不串窗；关副窗主窗仍在。配套 scenario / smoke，禁止用截图或“窗口对象已创建”冒充完成。GameEditor、dock tab 语义、EditorSurface 均不进入本 checkpoint。

### G3：跨窗口 drag primitive

G3 排在 C2G 之后：Feature Gallery 已能打开第二扇真实 OS window，drag primitive 才能在两扇 present 着的窗之间验证。Framework 只提供 source/target window id、window boundary enter/leave、drag keep-alive 和延迟 create/destroy；不解释 tab/editor/document。

## GameEditor 实施轨道

### E0：旧 tab 迁移登记与 scope 分类

登记 `EditorViewportTab`、Hierarchy、Inspector、Content Browser、Runtime Tools、UIDesigner、Material、Script 的 spawn/destroy、singleton、document、viewport、selection、undo、App 依赖，并分类为 `WindowRootEditor`、`EditorOwnedTool` 或 `WindowTool`。验收必须明确 root/nested placement、owner editor/document、是否可独立 native window、owner 关闭策略以及 JSON identity。tab root 继续是 UIElement/UICompoundWidget；状态通过 editor/document/session service；tab 不保存 Surface/widget 指针、不自行 tick。

### E1：EditorWindowSession 与双层 DockContext

按 ES-1 → ES-2 落地，不要把 `EditorSurface` 直接改名为 window manager。先注入 `FEditorSurfaceContext` / `EditorWindowMetrics`，再让 `EditorWindowSession` 持有 Surface 与 window-root dock；`EditorModule` 先用单元素 `EditorWindowRegistry` 替换 `_editorSurface`。每个 root editor 可以拥有自己的 nested dock context；nested context 默认不接受其他 root editor 的 tab，但其中的 owned tool 可以 tear-off 为独立 editor window。project service 可共享；level/material/UI selection 和 undo 按 document/session 隔离；ActionMap 按 window/editor context 组合。Session 不得吸收 App/RHI、全局 selection 或多窗 create/destroy。

### E2：Tab factory 解耦

保留 `EditorTabSpawnerRegistry`，但 spawn context 改为 application + window session + optional document + tree/actions/undo。factory 不拥有 tree、不执行 loop/tick、不依赖固定 EditorSurface。

factory 还必须返回 placement metadata（scope、默认 owner editor 类型、singleton key、detachable/closable policy）；spawn 只创建内容，DockContext/WindowSession 决定它挂在哪个 root 或 nested leaf。

### E3：主窗口与旧 tab

启动顺序为 main window → EditorWindowSession → window-root DockContext → non-closable Level Editor → 其 nested owned tools → window tools → restore main dock layout。旧 `editor.dockLayout` 解释为 main window layout，并兼容映射旧扁平 panel；Level Editor 第一阶段不可关闭、不可 tear-off。

### E4：Material/UI/Script

editor tab 与 document session 分离：MaterialEditorTab(materialKey)、UIEditorTab(uiKey)、ScriptEditorTab(scriptKey) 都是 `WindowRootEditor`；各自 Preview/Parameters/Hierarchy/Inspector 是 `EditorOwnedTool`。定义 singleton、dirty、undo、close policy、preview ownership 和 nested dock layout。UI Editor 设计器 tree 与 Level Editor tree 分离。

### E5：真实 tear-off/re-dock

root editor、detachable owned tool 和 policy 允许的 WindowTool 都可以成为 native window root。owned tool 作为独立窗口时必须保留 `ownerEditorId`，不能被 dock 到其他 root editor/tab；它可以回到原 owner nested context，或按明确的同 owner window policy 重新挂载。不得生成无 owner 的孤儿 leaf。关闭辅助窗口先执行对应 owner/document close policy 或迁回 owner context；非 detachable tab 和 Level Editor 拒绝错误迁移。

### E6：窗口拓扑持久化

在旧 tree-only `editor.dockLayout` 之上增加 versioned `windows[]` envelope，保存 bounds/monitor/maximized/role、window-root dock、root editor/document、nested owned dock、独立 owned-tool windows、window tools、active/focus；owned tool 必须保存 `ownerEditorId` 与当前 placement。坏 monitor、缺失 asset、旧 JSON 或 owner 缺失必须安全降级。

恢复顺序必须是：先创建 window，再创建 root editors，再创建并绑定 nested/独立 owned tools，最后恢复 window tools、active tab 和 focus；任何 owned tool 找不到 owner 时丢弃该 placement 或回退到默认 owner，不允许静默改绑到其他 root editor。

## Checkpoint

| ID | 层级 | 单一可验收目标 |
|---|---|---|
| C0 | GUI/RHI/Editor | 完成 surface/swapchain/frame-resource 与旧 tab 事实矩阵（MW-001..004 工件已落地，不改 Engine） |
| C1 | GUI | 两个 native window、两个 WidgetTree、统一 loop |
| C2 | GUI/RHI | 多 surface/swapchain、resize/minimize/close teardown |
| C2G | GUI | FeatureGallery `Windows` 页打开/关闭真实 OS window 实例；每窗独立 WidgetTree；无 GameEditor |
| C3 | GUI | 跨 window drag primitive，不含 tab 语义 |
| C4 | Editor | EditorSurface 降级为单窗 facade：ES-1..ES-5（context 解耦、单元素 session、scope、factory、按 window id 路由）；禁止第二扇 editor window 冒充完成 |
| C5 | Editor | 主窗口 Level Editor 常驻，旧 tabs 完成 session-owned 路径 |
| C6 | Editor | Material/UI/Script root editors 与 owned-tool nested dock |
| C7 | Editor | root editor/owned tool tear-off-re-dock，owned tool 不得 dock 到其他 root editor/tab |
| C8 | Editor | window topology persistence 和 recovery |
| C9 | Release | 双窗口、GPU parity、长时 resize/close/drag soak |

每个 checkpoint 的代码、测试和 plan/progress/matrix 必须同一提交；不能用拆文件、registry、placeholder 或纯文档冒充 feature 完成。

## 整线验收

GUI Framework：两个 native window 同时运行；一个 window 一个 WidgetTree；输入/focus/capture/tooltip/DPI/resize 独立；共享 device 正确 acquire/submit/present；关闭窗口无 GPU use-after-free。Feature Gallery `Windows` 页是框架层第一份真实多窗消费者，先于 GameEditor 验证 `GUIApp::openWindow`。

GameEditor：main window 永远有 Level Editor；Material/UI/Script 可作为 tab 或独立窗口；跨窗 re-dock；document dirty/undo/selection 正确；重启恢复窗口和布局。

架构：GUI Framework 不依赖 GameEditor，RHI 不认识 tab，DockContext 不创建 native window，没有第二套 loop、中心 bus 或 EditorPanel。

## 8. 本轮审查后收口的歧义

- “顶层 tab”现在专指 `WindowRootEditor`，不是所有 `EditorTabSpawner` 注册项。
- “不能 dock 到其他 tab 之下”的 tab 现在专指 `EditorOwnedTool`，必须绑定 `ownerEditorId`；它可以 nested，也可以成为独立 editor window，但不能挂到其他 root editor/tab。
- “完全不属于某个 editor/document 的独立工具窗口”使用 `WindowTool` scope；它可以被 GUI Framework 承载为 native window。
- `FDockContext` 的职责仍是一个 dock model/session；多个 editor/window 通过多个 context 实例表达，不把一个 context 偷换成全局 workspace。
- 一个 editor document 可以有多个 view，但每个 view 的 WidgetTree、viewport、focus 和 layout state 必须按 window/editor instance 隔离。
- `EditorSurface` 保留为单窗口 UI 编排 facade，不演进成 `EditorWindowManager`；`EditorWindowSession` 也不吸收其全部字段变成更大的 god object。
- `tick(App&)`、`applyWindowMetrics(App&)` 和 `dynamic_cast<EditorInspectorTab*>` 是过渡切口；新代码只走 session + `FEditorSurfaceContext` + `WidgetTree::wantsTextInput()`。
- 第二个 `EditorWindowSession` 只能出现在 ES-5 之后，且依赖 GUI Framework 已能承载第二扇 native window（C1/C2 + C2G FeatureGallery 实例）。
- Feature Gallery 多窗口是 gui-framework 层验收，不是 editor tear-off；不得用 `UIDockFloatingHost` 或复制 `FWorkbenchSurface` 冒充 OS window。
