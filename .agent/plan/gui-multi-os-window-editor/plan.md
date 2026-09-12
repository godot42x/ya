# GUI Framework 真正多 OS Window 与 GameEditor 多 Editor 计划

> 建立日期：2026-09-08；状态：实施中。C15 DockStackView / TabRegistry / commitDrop 已完成。剩余条件延后 R-5 / MW-901/902/903。R-5 无证据，保持一条 cmdBuf。
> Camera / N 视图已冻结。最小化/不可上屏见 [`c2_unpresentable_surface.md`](c2_unpresentable_surface.md)（**MW-206** 已完成）。
> 细节：[`c2_present_compose_model.md`](c2_present_compose_model.md)、[`c2_view_model.md`](c2_view_model.md)。

## 目标与结论

采用 GUI Framework 提供多 native window 基础、GameEditor 提供 editor/session/tab 语义的分层方案。主窗口默认聚合常驻 Level Editor；Material/UI/Script editor 可作为主窗口 tab，也可 tear-off 成真实 OS window，再 re-dock 到其他窗口。

## 冻结：device / present / camera（防止改偏）

最初 RHI 把 **device、OS 窗口、swapchain、viewport/相机目标** 捏在 `IRender`/`VulkanRender` 上。后续重构必须按三层拆，禁止再把相机尺寸绑回 swapchain，也禁止为多窗复制 `IRender::create`。

```text
IRender                         共享 device（factory / queues / VMA / deferred deletion）
IRenderSurfaceContext           一扇 OS 窗：native window + VkSurface + swapchain + acquire/present
Camera / WorldView              graphics → UI → view compose，写该相机离屏 RT（不是窗口）
```

一条管线按 Unity Camera 组织，单位是 Camera 不是窗口：

```text
Camera
  → graphics pass      Forward/Deferred + post → 该相机离屏 RT
  → UI pass            该相机 Game UI（post 之后，不进 bloom）
  → view compose       overlay / gizmos，仍写该 RT
  → display compose    仅当这扇 OS 窗要上屏：若干 image → swapchain[imageIndex]
  → present            该窗 IRenderSurfaceContext
```

两段 compose 不要混名：

| 名字 | 写到哪 | 谁 |
| --- | --- | --- |
| View compose | 这台 Camera 的离屏 RT | Camera / WorldView |
| Display compose | `swapchain[imageIndex]` | PresentSurface（`PresentationGraphService` 只服务主 world 窗） |

旧词 “viewport” 拆开：`PresentSurface`、`WindowChrome`（WidgetTree）、`WorldView`/`PreviewTarget`、`ViewportWidget`（chrome 里取样相机图的控件）。`ViewportState` 迁移期 = 唯一 `WorldView[0]`。`IEditorViewportHost` 是 ViewportWidget，不是 RHI、不是 Camera。Material/UI 窗用 PreviewTarget，不复制 `RenderRuntime`、不复制 GBuffer。

三套时钟禁止混用：surface present flight / Camera 离屏 extent / recording flight（pass slot、`DeferredDeletionQueue`）。`IRender::getPrimarySurfaceContext()` 只是 device pick 用的第一扇 surface。`primarySwapchain()` / `primaryFrameIndex()` 不是 viewport API。Fullscreen「相机 == 窗口」是 host 把窗口尺寸写入该 Camera 的 extent。`CameraComponent.bPrimary` 只表示 `WorldView[0]` 的默认相机，不是「全引擎唯一 viewport」。`syncRuntimeCameraAspect` 今天会把同一 aspect 写进所有相机，N Camera 落地前不要当正确语义用。

**本计划当前只做多窗 RHI。** Camera 链对象模型到 MW-201d 为止；`FRenderViewDesc`、N Camera、按 view 改 `syncRuntimeCameraAspect` 延后到 C2 完成之后，必要时另开计划（条件项 `MW-902`），不要塞进 extra present。

已落地（不要回退）：

- `IRenderSurfaceContext` + `createSurfaceContext`；主窗 swapchain/sync 在 `_primarySurface`
- `IRender` 不再持有 begin/end/getSwapchain 那套 present facade
- `PresentationGraphService` 注入 surface；world cmdBuf 按 `MAX_FLIGHTS_IN_FLIGHT`，不按 swapchain `imageIndex`
- Camera 离屏 RT ≠ swapchain；world postprocess sRGB 跟离屏 format
- MW-202：`VulkanSwapChain::recreate` 不再 `vkDeviceWaitIdle`；context 析构不再 queue waitIdle；resize/minimize/close 只 wait 该 surface 的 graphics + present-complete fence
- MW-203：每窗 Render2D pass slot（`acquirePassSlot` / `FRender2DComposePassDesc::passSlot`）；`kMaxPassSlots` 16
- MW-206：不可上屏只 skip 该 surface acquire/present；delay recreate 保持 dirty；GameRuntime 不再 `sleep` 掉整进程

RHI / host 仍未完成：

- extra GUI present：已落地 `GUIWindowManager::renderAll`（共享 device、per-surface compose）
- R-1：`FrameInput` 已分成 Camera / ViewCompose / DisplayCompose / Present 四组；submit 仍一次
- R-2：`CameraFrameInput` 携带 owner 计算的 view/projection/viewProjection/extent；pipeline 不读 surface
- R-4：host `FPresentFrame` 负责 acquire/present；`RenderRuntime` 只录制；仍一条 cmdBuf，不要顺手拆成 N Camera
- C2G MW-205：Windows 页 scenario/smoke 已闭环；golden BMP 延后（双 swapchain）

禁止：`IRender[]`；每窗 `IRender::create` / 复制 `GUIAppHost::init`；graphics/UI pass 读写 swapchain；用 ViewportWidget 布局改未绑定相机的 aspect；`PresentationGraphService` 服务辅助 GUI 窗；用进程级 `_bMinimized` / `sleep` 代替 per-surface 不可上屏。

**不可上屏 ≠ 暂停窗口 ≠ 暂停进程。** 最小化 / zero extent 只 delay **该** PresentSurface 的 recreate/acquire/present；AppKernel、其它窗、Camera/WidgetTree 默认继续。不把跳过的帧攒起来补 present。现状（整进程 sleep、host 直接 return、manager 跳过 tick）是债，**MW-206** 修。见 [`c2_unpresentable_surface.md`](c2_unpresentable_surface.md)。

### 渲染对象的严格边界

后续实现和 review 统一使用下面的对象关系；不要再用“viewport pipeline”同时指代相机渲染和窗口上屏。

| 对象 | 负责什么 | 不负责什么 | 典型输入/输出 |
| --- | --- | --- | --- |
| NativeWindow | OS 窗口、像素尺寸、DPI、焦点、关闭请求 | GPU image、相机矩阵、渲染 pass | window events / EditorWindowMetrics |
| IRenderSurfaceContext / PresentSurface | VkSurface、swapchain、acquire、present、surface fences、recreate | world 绘制、Camera view/projection、GBuffer | imageIndex、present target |
| Camera / WorldView | view、projection、viewport extent、near/far、相机输出 RT | acquire/present、OS window 生命周期 | immutable camera frame data → offscreen RT |
| BaseRenderPipeline | 固定 graphics pass 拓扑、资源依赖、shader/pipeline 选择、pass execution | 决定哪扇窗 present、创建 OS window、从 swapchain 猜 viewport | CameraFrameInput + scene/render snapshot |
| ViewCompose | 将 UI/gizmo/overlay 合成回 Camera 的离屏 RT | 选择 swapchain image、调用 present | Camera RT → Camera RT |
| DisplayCompose | 将 Camera/Preview/Chrome image 排到某 surface 的 swapchain image | 修改 Camera view matrix、执行 world GBuffer | source images + imageIndex → swapchain image |
| Present | 提交该 surface 的 command/sync 并显示 | 参与 scene/camera 绘制 | surface context → OS screen |

view matrix 不是 window state，也不是 BaseRenderPipeline 的隐式全局变量：Camera owner 在 graph build 前生成 view、projection、viewProjection 和 extent；CameraFrameInput 以 immutable frame data 传入 base pipeline；pipeline 只消费，不从 surface、swapchain 或 NativeWindow 反查矩阵/尺寸。ViewportWidget 的布局矩形只决定显示/采样 Camera 输出；只有明确绑定该 Camera 时，host 才能更新其 extent/aspect。

### 后续渲染重构顺序

这些步骤依赖 C2 已有的 surface/present 改造，不得倒置：

1. R-1 命名和输入契约收口：将 RenderRuntime::FrameInput 分组为 CameraFrameInput、ViewComposeInput、DisplayComposeInput、PresentFrameInput；只做 additive API 和调用点迁移，不改变提交次数。
2. R-2 view matrix / extent 从 pipeline 解耦：Camera owner 在 graph build 前确定矩阵和离屏 extent；Forward/Deferred、debug/overlay 只读 CameraFrameInput，不从 swapchain/window 猜尺寸。单 Camera 可继续用 WorldView[0] 适配，但不引入 N Camera。
3. R-3 明确两段 compose：view compose 写 Camera 离屏 RT，display compose 写 surface swapchain image；GUIRenderSurface 只负责 compose target，不 acquire、不 present、不读取 live WidgetTree。
4. R-4 PresentSurface 独立收口：acquire/present/recreate/zero-extent 只出现在 IRenderSurfaceContext / present coordinator；PresentationGraphService 继续只编排主 world surface，GUI extra 使用自己的 display compose target。
5. R-5 再评估 submit 边界：只有多个 Camera、异步 world 或多个 surface 的同步压力被实际测量后，才把 world/view compose 与 display compose 拆为两次 submit。当前统一 loop 可保持一条 command buffer，但类型和资源 ownership 必须先完成 R-1～R-4。
6. R-6 Camera 扩展另开闭环：R-1～R-5 稳定后，另行实现 FRenderViewDesc / N Camera / ViewId 到 ViewportWidget 的绑定；不得把 N Camera、独立 world preview 或复制 RenderRuntime 混入多 OS window 基础改造。

### 渲染重构验收证据

- Camera/base pipeline 不再调用 getSwapchain、getNativeWindow、primaryFrameIndex；PresentSurface 之外不出现 acquire/present。
- 单 Camera golden：改变 window size 不改变 Camera 离屏 extent，除非 host 明确更新该 Camera；改变 Camera aspect 不重建 swapchain。
- 双 surface smoke：同一 Camera RT 可被两个 surface display compose；关闭/最小化其中一扇不影响另一扇 Camera/present。
- 生命周期：surface recreate 只等待该 surface；Camera RT、pipeline resources 和 swapchain image 的 deferred deletion 边界可分别验证。
- frame trace 明确标记 Camera graphics/UI/view compose、Display compose(surfaceId,imageIndex)、Present(surfaceId)，禁止只输出笼统的 viewport render。

## Tab scope 必须显式建模

不是所有 tab 都是同一级 editor：

- `WindowRootEditor`：window 根 DockContext 的顶层 editor（Level/Material/UI/Script），是 native window tear-off 的默认迁移单位。
- `EditorOwnedTool`：某个 root editor 内部的 nested dock leaf/tab（Hierarchy、Inspector、Material Preview、UI Tree），不能 dock 到另一个 root editor/tab 的内部，但可以 tear-off 成独立 editor window；独立窗口仍保留 owner editor/document 语义。
- `WindowTool`：window 级工具（Content Browser、Output、Runtime Tools），不属于 document editor，可按 window policy 停靠或独立。

每个 descriptor、instance 和 persistence record 必须包含 scope、`ownerEditorId`（owned tool 必填）、document key、dock-compatibility 和 detachable policy；不能把所有注册 tab 扁平放进同一个 root DockContext。

`WindowRootEditor` 与 `EditorOwnedTool` 的关系是 ownership/placement 关系，不是 OS window 限制：root editor 的根 widget 持有一个 editor-owned nested `FDockContext` 投影；owned tool 不能 dock 到另一个 root editor/tab，但可以独立成 editor window，并继续携带 `ownerEditorId`。若产品需要完全脱离 owner 语义的 Inspector/Output，才注册为独立 `WindowTool`。

推荐一个 native window 对应一个 `WidgetTree`，但所有窗口由一个 `GUIApplication`/`GUIWindowManager` 统一 loop 调度。共享 `IRender` device、shader/font/texture services；每个窗口独立拥有 WidgetTree、snapshot、surface/swapchain、focus/capture 和 Render2D pass slot。

不采用一个 WidgetTree 管多个 OS window：pointer/focus/capture、tooltip/popup、DPI、clip、snapshot 和销毁边界都会被迫增加 window 维度。也不为每个 window 建独立 while-loop。

### Floating placement 与 native window 语义（冻结）

DockNode、floating placement、in-process floating projection 和 native OS window 不是同一个对象：

```text
FDockTreeModel / FDockContext
  ├─ docked projection       → UIDockSpace
  ├─ in-process floating    → UIDockFloatingHost（同一 OS window / 同一 WidgetTree）
  └─ native-window floating → GUIWindowSession（新 OS window / 新 WidgetTree）
```

- `DockNode` 只表示 split/leaf/tab 的布局模型，不持有 OS window 或 GPU 资源。
- 现有 `FDockContext::FFloatingWindow` 已重命名为 `FDockFloatingPlacement`；它记录 panel 集合、tab 顺序、位置尺寸、source dock scope、opaque `ownerEditorId`、document key 和 projection mode。
- `UIDockFloatingHost` 是 `InProcessOverlay` projection，继续使用 Popup 层和当前 surface；它不是 native multi-window。
- 真正的 detach 由 GUI window coordinator 创建 `GUIWindowSession`，为目标窗口创建 native window、surface/swapchain、WidgetTree、snapshot、input/focus 和 presentation。
- 一个 live `UIElement` 不得同时挂在两棵 WidgetTree；跨窗移动必须是 source detach → placement/session ownership transfer → target attach/rebuild 的事务。
- 一个 native window 对应一棵 WidgetTree；共享的是 document/editor/session/service，不是 live widget tree。
- `FDockContext` 不负责创建 native window。它只发布 detach/redock/placement 变化，GUI Framework 的 coordinator 执行窗口生命周期，GameEditor 负责 editor owner、document、close policy 和 tab scope。

因此 C7 必须先实现 `InProcessOverlay` 与 `NativeWindow` 两种模式的显式数据模型，再实现 root editor / owned tool 的真实 tear-off；不能把现有 `UIDockFloatingHost` 改名后冒充 OS window。

### Window chrome / 平台能力策略（冻结）

GUI Framework 不假设 macOS 和 Windows 的非客户区能力相同，也不在上层散落平台判断。native window 暴露 capability-driven chrome API：

```cpp
enum class EWindowChromeMode
{
    Native,
    Hybrid,
    ClientDrawn
};
```

至少需要抽象：titlebar 内容布局、drag region、resize hit-test、system buttons、safe-area、shadow、fullscreen/maximize 和 accessibility 能力。

- macOS 默认采用 `Hybrid`：允许透明/隐藏标题文字和 full-size content view，自绘 title/tab/toolbar，但保留系统 traffic lights、窗口安全区及 AppKit 的窗口行为。完全 borderless 作为显式 capability，不作为默认路径。
- Windows 可支持 `ClientDrawn`，但仍需保留或重建 resize、snap、maximize/restore、system menu、DPI、accessibility 和 shadow 行为；“隐藏全部边框”不是免费能力。
- Linux/其他平台默认 `Native` 或 `Hybrid`，由 backend capability 决定。

这套 chrome 策略属于 GUI Framework 的 `NativeWindow`/platform backend；Dock、EditorSurface 和 tab spawner 不得直接操作 NSWindow/Win32 non-client API。

### Dock / floating / cross-window drag 的层级归属（冻结）

完整的 DockSpace 行为属于 GUI Framework，不是 GameEditor 的重复实现。

GUI Framework 必须提供：

- dock tree、split/leaf/tab projection、hit-test、drop preview、tab reorder；
- in-process floating 与 native-window floating 两种 projection；
- generic drag session、source/target window 路由、boundary enter/leave、keep-alive、deferred close；
- source detach → target accept → attach/rebuild 的生命周期事务；
- IGUIWindowCoordinator / IGUIWindowSession 和每窗 WidgetTree/surface/presentation；
- 不包含 EditorRootId、DocumentKey、EditorOwnedTool 等业务类型。

GameEditor 只提供：

- tab spawner、WindowRootEditor / EditorOwnedTool / WindowTool scope；
- ownerEditorId、document identity、selection/undo/close policy；
- typed editor drag payload 和 canAcceptDrop(target, payload) placement policy；
- detach 后应创建哪种 editor session、回 dock 到哪个 scope，以及 owner 关闭时的处理。

调用方向固定为：GameEditor placement policy → GUI DockContext requestDetach/requestDrop → GUI cross-window drag router → target WidgetTree / Dock projection → GameEditor typed policy accept/reject。

GameEditor 不得重新监听 SDL 事件来实现跨窗拖拽，也不得直接创建 SDL/NSWindow/Win32 window。GUI Framework 负责“怎么拖、怎么命中、怎么迁移”；GameEditor 负责“这个 editor tab 能否放到目标 scope”。

## 当前仓库事实

GUI Framework 已有 `GUIWindowHost`、WidgetTree、UIFrameSnapshot、GUIRenderSurface、Render2DPassSlot；`UIDockSpace`/`UIDockFloatingHost` 仍是同一 tree 内的 dock/floating 投影。

GameEditor 的 `EditorSurface` 同时拥有一个 tree、DockContext、SelectionModel、ActionMap、UndoStack、viewport bridge；`EditorModule` 只有一个 surface；`FEditorTabSpawner` context 直接绑定 WidgetTree、EditorLayer 和 viewport sink。当前 floating 不是 native window，缺少 window registry、per-window presentation、跨 tree tab migration、nested editor-owned dock、window topology persistence。

## EditorSurface 迁移方案

不要把 `EditorSurface` 删除，也不要把它改造成多窗口管理器或更大的 `EditorWindowSession` god object。按 window-local / editor-root / app-global / legacy bridge 拆职责后，它只保留「当前窗口 UI 编排 facade」；窗口生命周期归 GUI Framework，editor/document 状态归 GameEditor session。

当前入口仍是 [`EditorSurface.h`](Engine/Source/Applications/GameEditor/include/GameEditor/UI/EditorSurface.h) 的 `tick(App&, float)`，以及 [`EditorModule.cpp`](Engine/Source/Applications/GameEditor/EditorModule.cpp) 直接持有 `_editorSurface`。`applyWindowMetrics()` 仍读 `IRender::primaryWindow()` / `primarySwapchain()`（ES-1 要改成 `EditorWindowMetrics`）；`wantsTextInput()` 仍 `dynamic_cast<EditorInspectorTab*>`。这些是迁移的第一批切口，不是把 Surface 再堆成 window manager 的理由。

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

改为消费窄 context，Surface 不再调用 `IRender` 的 window/swapchain API。ES-1 落地的是 metrics + viewport view/projection；`layer` / `tree` / spawners 仍由 Surface 持有。ES-2 session 持有 Surface 并按 window id 路由，不把 dock/tree 搬出 Surface。

```cpp
struct EditorWindowMetrics
{
    Extent2D logicalExtent;
    Extent2D framebufferExtent;
    float dpiScale = 1.0f;
};

struct FEditorSurfaceContext
{
    EditorWindowMetrics metrics;
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
};

void EditorSurface::tick(const FEditorSurfaceContext& context, float dt);
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

`wantsTextInput()` 已迁到 `WidgetTree`（WT-IME）：沿 focus path 问 widget capability。Inspector / Material / Script 不被 Surface 特判。`EditorInputNode` 读 window session / tree capability。

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

首轮不把完整 GameRuntime world graph 复制到每个 editor window。主窗口继续走一条 Camera 链 + 该窗 display compose；Material/UI preview 优先 PreviewTarget。N Camera / 独立 world preview 是冻结项，C2 完成前不开（`MW-902`）。

RHI 目标是共享一个 device、每窗口 surface/swapchain/frame resources、所有 create/rebuild/destroy 在 frame boundary 完成；command recording 只消费 immutable snapshot 和当前 surface。Camera graphics 不读写 swapchain。

### 当前 RHI 状态（2026-09-09，取代初版单窗调研）

`NativeWindowManager` 持有多个 `INativeWindow`，不创建 Vulkan surface。device 与 present 已拆：`VulkanRender` 是共享 device；每窗一个 `VulkanRenderSurfaceContext`（主窗 `attachExistingSurface`，extra `init` 自建 `VkSurfaceKHR`）。`IRender::begin/end/getSwapchain` 已删除；acquire/present 在 context 上。`createSurface()` 仍在 `findPhysicalDevice` 之前（bootstrap 第一扇 surface）。

`PresentationGraphService` 注入主窗 `IRenderSurfaceContext*`，只做该窗 display compose，不给辅助 GUI 窗用。`GUIAppHost` 持有 `_impl->present`，禁止再复制 `init` 造第二份 device。

对象模型：[`c2_present_compose_model.md`](c2_present_compose_model.md)、[`c2_view_model.md`](c2_view_model.md)。本节开头的「冻结：device / present / camera」是方向；C0 审计原文仍在 `c0_mw001`/`c0_mw002`（其中部分 API 名已过时，以本节为准）。

长期目标仍是 `IRender` ≈ device，`IRenderSurfaceContext` ≈ 一扇窗 present。`IRender::primarySwapchain()` 仅兼容/bootstrap，新代码用持有的 context。

每个 surface 独占 swapchain、current image、pending recreate、image-available、render-finished、frame fence、present-complete fence、imported present images。共享只允许 device 级资源。`Render2D` 静态 session 串行复用，每窗唯一 pass slot（MW-203）；第二套 Render2D owner 仅 `MW-901`。

Swapchain recreate / extra context 析构只 wait **该** surface 的 graphics fence + present-complete fence（present 之后的 empty submit），禁止 `vkDeviceWaitIdle` / 共享 queue `waitIdle`。GUI 最小化跳过 present，不 idle 整 device。进程退出销毁 device 仍可用 `IRender::waitIdle()`。

`MW-001`..`MW-004`、`MW-101`/`MW-102`、`MW-201`/`MW-201c`/`MW-201d`、`MW-202`、`MW-203`、`MW-206`、extra `renderAll`、`MW-204`/`MW-205`、`MW-207`、`MW-301`、`ES-1`、`ES-2`、`ES-3`、`ES-4`、`WT-IME`、`DS-1`、`ES-5`、`MW-401`、`MW-501`、`MW-502`、`MW-601`、`MW-602`、`MW-701`、`MW-702`、`MW-703`、`MW-704`、`MW-705`、`MW-706`、`MW-707`、`MW-801`、`MW-802`、`C9-P`、`C9`、`C10`、`C11`、`R-1`、`R-2`、`R-3`、`R-4` 已闭环。R-5 两次 submit 仅在有证据时，当前延后。

## GUI Framework 实施轨道

### G0：事实与契约

冻结不变量：主窗 `GUIWindowHost` 等于一个 native window、一个 WidgetTree 和一个 snapshot/presentation state；extra 是 `GUIWindowManager` slot（同样一窗一树一 surface），**禁止**复制 `GUIWindowHost` / `IRender::create`。所有 window 由一个 AppKernel loop 调度；window-local route 不泄漏；close/create/rebuild 延迟到安全边界。Present surface ≠ Camera RT，见计划开头冻结节。

### G1：NativeWindowManager

G1 必须消费共享 device + surface-context provider，禁止通过 `IRender::create` 为每个窗口创建完整 backend；先完成两个空白窗口的 tree/input/tick 生命周期，再接 presentation。

在 `Framework/GUI/Host` 增加最小 manager：create、requestClose、destroy、find、dispatch native event、tickAll、renderAll。保持一个 host 一个 tree；验收两个空白 window 同时运行，输入、resize、focus、close 互不影响。

### G2：per-window presentation

实现顺序固定为：先补 RHI surface-context factory 与 per-surface sync，再接 GUI host；不能复制 `GUIAppHost::init`。

接入 per-window surface/swapchain、frame resources、Render2D pass slot、resize/minimize/out-of-date 和 deferred destroy。验收两个窗口同时 present 不同 retained UI，关闭/resize 一个不污染另一个，GPU validation 无生命周期错误。C2 内部顺序：MW-202 → MW-203 → MW-206 → extra `renderAll` present（均已完成）。不要在这一轨实现 N Camera。最小化模型见 [`c2_unpresentable_surface.md`](c2_unpresentable_surface.md)。

具体落点：优先在 `RHI/Core` 增加 additive 的 surface/presentation context 接口和 Vulkan 实现，复用现有 `VulkanSwapChain`；不要第一步重命名或删除 `IRender`。`VulkanRender` 继续作为共享 device owner，并通过 context factory 为每个 `INativeWindow` 创建 surface/swapchain/sync。`GUIWindowHost` 只持有 context handle 和 `GUIRenderSurface`，不直接访问 `VulkanRender` 私有成员。

`PresentationGraphService` 注入该窗的 `IRenderSurfaceContext*`，做 display compose。辅助 GUI window 使用 window-local imported target + GUI compose。多 Camera / 多 WorldView 见 [`c2_view_model.md`](c2_view_model.md)，不复制 `RenderRuntime`。

### G2.5：FeatureGallery 多窗口实例

C1/C2 的空白双窗不能当作产品验收。GUI Framework 的第一份真实消费者是 [`Example/GUIWorkbench`](Example/GUIWorkbench) Feature Gallery，不是 GameEditor。

在 `GUIApp` 暴露 `openWindow` / `closeWindow` / `findWindow`（内部走 `GUIWindowManager`）。[`FWorkbenchApp`](Example/GUIWorkbench/Source/GUIWorkbench.h) 只调用这些 API，不自己持有第二套 loop，也不把 `FWorkbenchSurface` 改成 window manager。

落点：Composition 组新增 `Windows` 页（[`WorkbenchDemoPages.h`](Example/GUIWorkbench/Source/WorkbenchDemoPages.h) / `Pages/CompositionPages.cpp`，`surface.addPage("Composition", "Windows", ...)`）。页内可 Open / Close 额外 native window。每个额外窗口：

- `GUIWindowManager` slot：独立 `WidgetTree` + snapshot + `IRenderSurfaceContext` / swapchain（**不是**第二份 `GUIWindowHost`，也不复制 `GUIAppHost::init`）；
- 独立 retained UI 实例（标题、计数按钮等足以证明隔离），不复用主 Gallery 的 tree、focus、`FDemoState` widget handles；
- 不是第二份完整 Gallery shell，也不是 `FDockContext` floating / tear-off；
- 关闭额外窗口不退出进程，不销毁主 Gallery tree；关主窗才退。

验收：`xmake b GUIWorkbench`；`--start-page Windows` 进入该页；`--extra-window` 或页内 Open 创建第二扇 OS window。headless `windows_extra_os.jsonl` 锁页控件；windowed `--smoke-actions` 点 Open / extra-click / resize extra / Close。两窗独立；关副窗主窗仍在。Golden BMP 延后。GameEditor、dock tab 语义、EditorSurface 均不进入本 checkpoint。

### G3：跨窗口 drag primitive

G3 排在 C2G 之后：Feature Gallery 已能打开第二扇真实 OS window，drag primitive 才能在两扇 present 着的窗之间验证。Framework 只提供 source/target window id、window boundary enter/leave、drag keep-alive 和延迟 create/destroy；不解释 tab/editor/document。

已落地（MW-301）：source `WidgetTree` 持有 session；target 只走 `setExternalDropHover` / `dropExternal`，`isDragging()` 保持 false。`GUIApp` 在 `onEvent` 拦截跨窗 move/release/leave/Escape；source/hover 的 `requestClose` 延到 drag 结束；`runAfterDrag` 是延迟 create 钩子（`openWindow` 本身不阻塞）。验收：`GUIAppCrossWindowDragTest` A→B drop、leave keep-alive、deferred close。Windows 页 `windows-drag` → extra `extra-drop` 是消费者，不含 tab 语义。

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

MW-707 已把 dock JSON 的 overlay `floating[]` 与 NativeWindow `windows[]` 拆开；旧 Popup
坐标永不升格为屏幕坐标。MW-801 已落地 Editor envelope 顶层 `windows[]`（bounds/monitor/maximized/role）。
MW-802 已落地坏 monitor 迁到可用屏、缺失 document/unknown owner 丢弃且不改绑、`closing: true` / 空 extra 不恢复。
产品启动 recover 主窗 placement，并经 coordinator restore extra（C9-P）；DockSpace 可拆 tab 的 NoTarget 拖出走 native window（C10），未接线时仍 overlay。

恢复顺序必须是：先创建 window，再创建 root editors，再创建并绑定 nested/独立 owned tools，最后恢复 window tools、active tab 和 focus；任何 owned tool 找不到 owner 时丢弃该 placement 或回退到默认 owner，不允许静默改绑到其他 root editor。

## Checkpoint

| ID | 层级 | 单一可验收目标 |
|---|---|---|
| C0 | GUI/RHI/Editor | 完成 surface/swapchain/frame-resource 与旧 tab 事实矩阵（MW-001..004 工件已落地，不改 Engine） |
| C1 | GUI | 两个 native window、两个 WidgetTree、统一 loop |
| C2 | GUI/RHI | per-surface recreate（禁止 device waitIdle）；pass slot；不可上屏 skip present；extra GUI present。**不含** N Camera |
| C2G | GUI | FeatureGallery `Windows` 页打开/关闭真实 OS window 实例；每窗独立 WidgetTree；无 GameEditor |
| C2R | Render/RHI | typed Camera/compose/present 边界收口（R-1～R-5）；不含 N Camera、不强行拆 submit |
| C3 | GUI | 跨 window drag primitive，不含 tab 语义 |
| C4 | Editor | EditorSurface 降级为单窗 facade：ES-1..ES-5（context 解耦、单元素 session、scope、factory、按 window id 路由）；禁止第二扇 editor window 冒充完成 |
| C5 | Editor | 主窗口 Level Editor 常驻，旧 tabs 完成 session-owned 路径 |
| C6 | Editor | Material/UI/Script root editors 与 owned-tool nested dock |
| C7 | Editor | root editor/owned tool tear-off-re-dock，owned tool 不得 dock 到其他 root editor/tab |
| C8 | Editor | window topology persistence 和 recovery |
| C9-P | Editor/Runtime | 产品 extra OS window restore + present/input（`onAfterPresent`）；不是 soak |
| C9 | Release | 双窗口、GPU parity、长时 resize/close/drag soak |
| C10 | Editor | 产品 DockSpace NoTarget 对可拆 tab 开真实 OS window；未接线仍 overlay |
| C11 | GUI/Editor | Hybrid safe-zone：菜单在 traffic lights 右侧 Client；拖拽 gutter 才是 SDL drag；Workbench/Editor/extra dock 消费 `queryWindowChromeLayout`；DnD capture 可开 session |
| C12 | GUI/Editor | `GUIDragRouter` 是每个 input universe 唯一 drag session；GameEditor 跨窗 tab drop；唯一 tab extra 不产 empty leaf 窗 |
| C13 | GUI/Editor | 同一 router 收口 capture 所属窗、OS cursor、IME、extra clipboard、app-modal；不新造 router |

每个 checkpoint 的代码、测试和 plan/progress/matrix 必须同一提交；不能用拆文件、registry、placeholder 或纯文档冒充 feature 完成。

## 整线验收

GUI Framework：两个 native window 同时运行；一个 window 一个 WidgetTree；输入/focus/capture/tooltip/DPI/resize 独立；共享 device 正确 acquire/submit/present；关闭窗口无 GPU use-after-free。Feature Gallery `Windows` 页是框架层第一份真实多窗消费者，先于 GameEditor 验证 `GUIApp::openWindow`。

GameEditor：main window 永远有 Level Editor；Material/UI/Script 可作为 tab 或独立窗口；跨窗 re-dock；document dirty/undo/selection 正确；重启恢复窗口和布局。

架构：GUI Framework 不依赖 GameEditor，RHI 不认识 tab，DockContext 不创建 native window，没有第二套 loop、中心 bus 或 EditorPanel。RHI 不把 swapchain 当 Camera 目标；Camera 链见计划开头冻结节。

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
- RHI 三层：`IRender` = device，`IRenderSurfaceContext` = 一扇窗 present，Camera = 离屏 graphics→UI→view compose。swapchain 不是 viewport。
- View compose 写相机 RT；display compose 写该窗 swapchain。`PresentationGraphService` 只服务主 world 窗。
- Camera / N 视图在 C2 完成前冻结；不要在 extra present 实现 `FRenderViewDesc` 或复制 `RenderRuntime`。
- `IRender::primarySwapchain()` / `primaryFrameIndex()` 不是新代码的 viewport/flight API；用持有的 surface context 与 `FrameInput.flightIndex`。
- swapchain recreate / GUI rebuild / extra close 只 wait 该 surface 的 fence（MW-202 已落地）；禁止再引入 `vkDeviceWaitIdle` 卡住其他窗。
- 最小化 / zero extent：只 delay 该 PresentSurface 的 present；禁止进程级 sleep 或跳过其它窗（MW-206）。
- 指针拖拽 session 对每个 input universe 唯一（`GUIDragRouter`）；同一对象还持有 capture 所属窗、IME 窗与 app-modal。`WidgetTree` 只持有 source-local payload/ghost/capture widget。不是 `GUIApp` 单例，也不是进程单例。GameEditor 与 GUIApp 共用 router。
