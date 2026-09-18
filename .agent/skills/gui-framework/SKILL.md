---
name: gui-framework
description: YA GUI 框架（WidgetTree / 控件 / layout / Render2D pass slot / host 诊断）的模块地图与稳定契约。
---

## 适用场景

- 在 `Engine/Source/Framework/GUI/` 内改控件、布局、事件、快照、合成
- 开发 GUIWorkbench demo / GameEditor chrome
- 排查 GUI 渲染、布局、生命周期问题（GPU 资源 teardown、pass slot、clip/scissor）

## 计划与提交门禁

- GUI 迁移按用户指定的 feature/tab 闭环推进；不得用 registry、placeholder、纯拆文件或仅补 contract 文档冒充 feature migration。
- 一个 checkpoint 只能对应一个可运行、可验证的架构目标；代码、测试和 plan/progress 必须在同一提交中，且提交说明必须写清未完成项。
- `EditorSurface`、`UICompoundWidget`、WidgetTree 和 DSL 的边界若尚未验证，不得继续向宿主文件堆实现；先停下来做边界审计。

## UX vs closure 测试

- `ya-gui-closure-test` 覆盖 dump / dirty / route / snapshot contract，**不是**手感门禁。
  不要把「N dump tests passed」写成 UX 完成。
- 手感验收走 Gallery `--scenario` 的状态组合（见下方交互契约 1–11）和 editor 手测
  （选区、选色、dock 关 tab、Hierarchy 右键、Designer 树 DnD）。路径存在 ≠ 手感等价。
- closure dump/route/dirty **测不出**：`isHoverable`、presenter 选中循环、首次 expand
  的 Layout Reactive、Modal 输入独占、page `leave` 拆 Popup、ColorEdit SV 是否是
  ImGui 两层一维顶点色（S 再 V-alpha；不是四角 2D quad，也不是 HSV 格子）。Gallery scenario 必须点 hover/select/expand/resize，不能只 assert 控件存在。
- 内核体验长线见 `.agent/plan/gui-kernel-ux-parity/`。parity 表
  `.agent/plan/gui-framework-editor-readiness/imgui-widgettree-parity.md`
  里 ✅ 只表示 retained 有一条能完成核心工作流的路径。

## 模块地图

```text
Framework/GUI/
  Runtime/Widgets/    ya-gui-widgets     UIElement / WidgetTree / UIFrameSnapshot / 控件
                                         （Layout / Binding / Declarative 编进本 target）
  Runtime/Compose/    ya-gui-compose     共享 2D 合成 pass（UIFrameSnapshot -> Render2D）
  Tooling/            ya-gui-tooling     WorkbenchSurface / Workspace（工具 UI 外壳，demo 无关）
  Host/               ya-gui-host        standalone 宿主：SDL 窗口、Vulkan、帧循环、automation
Framework/Render/                        ya-render-2d / ya-render-resources（Font/glyph、Render2D）
Framework/App/Kernel  ya-app-kernel      唯一 while-loop；GUI 与 GameRuntime 共用
Example/GUIWorkbench/                    Feature Gallery（FWorkbenchSurface 分组 rail：
                                         Diagnostics/Controls/Layout/Paint/Text/Style/
                                         Overlays/Interaction/Data/Composition）
Applications/GameRuntime                 ya::App 产品壳（scene / RenderRuntime / modules）
Applications/GameEditor                  EditorModule + EditorWindowRegistry / EditorWindowSession
                                         （session 持有 EditorSurface；不是独立主循环）
```

`ya-gui-framework` 是聚合 meta target（widgets+compose+tooling 等），**不含** host。
GUI 测试只链 GUI closure（`ya-gui-closure-test` 不依赖 Scene/ECS/Render3D/Editor）。
standalone GUI 可执行文件显式链 `ya-gui-host`。

不要合并 `GUIWindowHost` 与 `ya::App` 的 present / 输入栈：Workbench 必须保持 GUI
closure；Editor 必须吃 3D viewport + swapchain。结构整理优先让 `EditorSurface`
成为可阅读 orchestrator，而不是再造一条产品宿主。

## 责任规则（不按目录搬文件）

划分按责任。禁止用物理搬家冒充边界收口。

1. `GUI/Window` 不 include GameEditor，**也不出现 `FDockContext` / DockPlacement 类型**。`IGUIWindowCoordinator` 只做 create/destroy/find session。Native dock placement 是 Host 适配器 `realizeNativeDockPlacement(coordinator, dock, …)`（`GUIDockNativePlacement.h`），不是 coordinator 方法。
2. `GUI/Docking` 只做通用 Tab/Dock/Layout/Drag transaction；要 OS 窗就调 `createSession`，自己不 `IRender::create`。
3. `GameEditor/Shell` 只编排 Surface / WindowRegistry / WindowLayout。
4. `GameEditor/Tabs` 只做 tab 内容。
5. `GameEditor/Docking` 只做 editor placement/ownership policy 和 payload。
6. `EditorLayer` 属于 editor runtime/domain，不是 GUI 宿主。
7. Workbench 不是 GUI Runtime 核心抽象；**EditorTheme 不得 include WorkbenchTheme**。共享 chrome 值在 `buildDefaultChromeTheme`。
8. 跨窗拖拽只保留 `GUIDragRouter`。`GUIApp` 的 `bindDragRouter` / `routeCrossWindowDrag` 是 Router 转发，禁止再加第二入口文件。

Primary 仍是 `GUIWindowHost`，extra 仍是 `GUIWindowManager`；并成一种 session 之前不要假装只有一套。

## 产品循环

唯一 while-loop 是 `AppKernel`。两条产品线在 kernel 之下分叉，不要读成「GameApp vs GuiApp」类型对。

**AppKernel 是共享控制面，不是应用基类——不要为了「让独立 GUI 更轻」而删它。**
它只持有三件每条产品线都需要、且各自实现会失真的东西：单实例 `OsProcessLock`、`AppAutomationRunController`
（exit-after-frame / 墙钟上限 / 远端退出）、以及唯一的事件泵 + 帧计时 + `exit` 判定。
实现 `IAppLoopDelegate` 的成本是 5 个虚函数，而 `GUIWindowHost` / `GUIApp` / `GUIHeadlessHost`
各自只在 `run()` 里写 4 行来构造它。

真正会让人误判的是 `ya-gui-framework` 这个**聚合目标**：它公开拉入 `ya-app-kernel` /
`ya-app-control` / `ya-hierarchy`，于是"GUI framework"看起来等于 GUI + app shell + module system。
要修的是这个聚合边界（见 `.agent/plan/source-layout-subtraction` S2），不是 Kernel 本身。
删 Kernel 会把上面三件事复制到 3 条产品线上，而它们各有 memory
（`control_instance_lifecycle`、`app_teardown_order_and_instance_lock`）记录过坑。

```text
AppKernel
  ├─ GUIApp / GUIWindowHost     GUI-only（无 Scene / ECS / Render3D）
  │    SDL → dispatchEvent → tree.tick → delegate.updateUI
  │    → buildSnapshot → compose → present
  │    GUIWorkbench：IGUIAppDelegate 挂 FWorkbenchSurface
  └─ ya::App                    游戏 / 编辑器产品壳
       GameRuntimeFrameOrchestrator
         tickLogic → EditorModule::onLogic
           play-mode viewport | editor camera | prepare compose pipelines
           EditorLayer::onUpdate | pending viewport resize
         tickRender → RenderRuntime world graph
           EditorModule::onViewportCompose
             EditorViewportCompositor
               2D: canvas preview + recordEditorCanvasSelectionOverlay
               3D: world RT + recordEditorWorldViewportOverlays
             setViewportDisplayImage
           EditorModule::onPresentation
             EditorWindowSession::tick（default window）
               → EditorSurface::tick
               rebuild-if-needed → window metrics
               → WidgetTree::tick → shell dialogs
               → pushViewportDisplay → buildSnapshot
               → publishViewportRect → viewport overlay host
             replayUIFrameSnapshot(..., EditorToolSurface)
           submitPresentFrame
         EditorModule::onAfterPresent
           close extras | reclaim empty | orphan GUI sessions
           GUIWindowManager::tickTrees + renderAll
```

Editor 全链路以 `Applications/GameEditor/EditorModule.cpp` 文件头注释为准；不要再造第二条产品宿主。
`EditorWindowSession::tick` 是 Module 侧 chrome 入口；`EditorSurface::tick` 仍是窗口内编排。Tab 由 `EditorTabSpawnerRegistry`
spawn，root 是 `UIElement` / `UICompoundWidget`；attach/detach/tick 只由 `WidgetTree`
驱动。Surface 只编排 shell、dock persist、viewport host bridge 和 dialogs。
禁止 `tab->sync`、禁止 Surface 持有 Tab 控件指针。不要再引入 `EditorPanel` 或中心
事件总线。长线见 `.agent/plan/archive/gui-editor-tab-lifecycle/`。

## WidgetTree 模型

- 层：`Content / Popup / Tooltip / DragIme`（项目内容不能覆盖系统层 zOrder）。
- 单视觉父契约：`attach`（新成员）/ `reparent`（显式迁移）/ `detach`（递归、清 transient state）。
- 输入：`dispatchEvent` 用显式 route：topmost candidate discovery 后执行 Preview
  (root -> parent) -> Target -> Bubble (parent -> root)。`Stop` 短路，`Pass` 继续 lower
  candidate；capture/focus/popup/modal/drag 都是 tree 级 route policy。`WidgetTree` 持有
  persistent pointer state、pointer path、focus path 和 route trace；`WidgetTreeDump`
  输出 `pointer`、`focusPath`、`lastRoute`（policy/path/phase/handled/result）。route callback
  可 detach 自身，executor 会持有 path 并重查 membership。
  **Pointer session 所有权（平台是物理状态的事实来源，框架不允许被它拖垮）：** press
  打开 session，session 只能以 release 或 cancel 结束。release 路由结束后框架自己回收
  仍被握住的 capture（`repairPointerSession`），控件不需要在每条路径上记得释放；没有
  press 的 `setPointerCapture` 直接拒绝。平台可能永远不投递 release（key focus 丢失、
  指针离开窗口、pane 被撕成另一个窗口、注入 press）：`WindowFocusLost` /
  `WindowMouseLeave` 时 host 用 `FOsMouseQuery::buttonMask`（`1u << EMouse::T` 编码）调
  `WidgetTree::reconcilePointerButtons`，同键第二次 press 时树自己
  `cancelPointerSession`（计数见 `getPointerSessionRecoveries()` /
  `gui.tree.pointer_recoveries`），并给 capture 控件一次 `clearTransientInputState()`；
  cancel 也必须结束 drag session（observer 收到 `EDragFinishResult::Cancelled`）。
  禁止把这类丢失 release 当成 `YA_CORE_ASSERT` 崩溃、静默吞第一次点击，或用“divider 外
  再按一次放 capture”当修复。另一条仍然成立的根因：`UIDockSpace` 在 capture 落在 dock
  子树里时 `syncProjection(Structure)`。标题 Client 洞必须在 `buildSnapshot`（layout）
  之后发布。
  drag&drop 的 source-local 状态（`beginDrag/updateDrag/endDrag/cancelDrag`、payload、ghost、observer）由树管理，唯一入口是
  `beginDrag(source, UIDragDropOperationRef)`。跨窗的 source/hover window 身份由 host `GUIDragRouter` 唯一持有。基类带通用 `payload` slot；领域拖拽
  继承加字段（`FDockPanelDragDropOp` / `FTreeReorderDragDropOp`）。目标用
  `as<T>()` / `isType()`。目标控件实现
  `canAcceptDrop/onDrop/setDropHighlight`。
  文本焦点：`UITextField` 消费 `KeyTyped`（IME 提交）、按码点 Backspace/Delete，选区
  （anchor/caret、Shift+方向、拖选、primary+A），以及
  primary+C/X/V（Cmd macOS / Ctrl 别处）经 `WidgetTree` clipboard（作用在选区上；
  无选区时拷切整缓冲）。默认内存缓冲；windowed host（含 extra 窗）用
  `bindSdlClipboard` 接同一块 OS clipboard。DPI 由 `setDpiScale` 与 `uiScale` 正交折叠。
  焦点/悬停时 `getCursor()` 为 `ECursorType::IBeam`。`UIDragFloat` / `UISpinBox`
  的 `_bEditing` 复用同一套 `FTextEditState`，不要再写第三套迷你编辑器。
  IME / 文本输入：`WidgetTree::wantsTextInput()` 沿 **focus path** 问每个节点的
  `UIElement::wantsTextInput()`。`UITextField` 为 true；`UIDragFloat` / `UISpinBox`
  仅 `_bEditing`；ColorEdit picker 仅 hex 编辑。Editor chrome 经 session 转发 tree，
  禁止 `dynamic_cast<EditorInspectorTab*>`。
- 快照：`buildSnapshot`（layout dirty 时才 layout + paint）→ 不可变 `UIFrameSnapshot`；
  录制只消费快照。命令录制期绝不读 live tree。业务代码不得在 paint/layout
  回调中直接修改 tree 结构；tooltip/drag 等 framework maintenance 只在显式 pass boundary 执行。
  渐变走 `UIFrameBuilder::addRectFilledMultiColor`（Y-down 四角：TL/TR/BR/BL）→
  `Render2D::makeRectFilledMultiColor`。GPU 一个 quad 是两个三角形，二维四角场会出对角缝；
  一维渐变（对边同色）才正确。HSV 方是两层 1D（S 再 V-alpha），不要用 sprite 格子或控件离屏 RT。
  编辑器规模基线在 `EditorScaleBaselineTest`：Hierarchy 只 paint 视口窗口、Content 目录
  `computeKeyedVisibleWindow` 与 catalog 规模无关、Inspector 列第二次干净 snapshot `rebuiltWidgets==0`。
  长时结构 soak 在 `EditorLongRunSoakTest`：同 subtree attach/detach、destroy/recreate、theme 切换、
  deferred texture generation。GPU/offscreen 像素门禁：`Script/automation/gui/run_workbench_gpu_parity.py`
  （headless lastRoute + snapshot digest + windowed `--gpu-shot`/`--offscreen-diff` 零容差）。

## 动画（framework 层）

边界：框架只做 Slate 那一半（时钟 + easing + 少量可动画属性）。轨道/关键帧/
clip player 属于未来 Game UI 层（对标 UMG WidgetAnimation），评价结果通过
**同一个可动画属性接缝**写回 widget，绝不在 widgets 内核里再造第二套
属性/失效系统。设计记录见 `.agent/plan/archive/gui-animation/plan.md`。

- 接缝（OCP）：一个 widget 类型用 `FUIAnimPropertyTable` 声明自己可被动画
  操纵的属性（own entries + base 链，见 `UIAnimation.h`）。驱动者（tween /
  将来的 clip player）只认 `(widget, propertyId, value)`：`findAnimatableProperty` /
  `applyAnimatableProperty` / `readAnimatableProperty`。加新可动画属性 = 在控件
  类型里加表项，**不改**驱动者。
- 基础目录：`UIElement` 自带 render transform 四通道 —— `opacity`(Float)、
  `renderTranslation`(Vec2)、`renderScale`(Vec2，围绕 `_pivot`)、`tint`(Vec4)，
  继承到整个子树（UMG RenderOpacity / RenderTransform 语义）。这不是 `UIOverlay`
  （叠层 layout host）。`renderRotation`
  未入目录：快照 item 是 axis-aligned quad，旋转需要 compose 支持 rotated
  quad。
- 写路径唯一真源是 changed-only setter（`setRenderOpacity` 等，失效走
  `EUIPropertyImpact::SubtreePaintContext`，因为子树继承 render transform）。禁止动画
  field poke、禁止把 `_bVolatile` 当动画、禁止在 paint/layout 回调里 spawn 动画。
- 时钟/tween：`UIAnimClock` 是 `UITweenBehavior` 的内部时钟（对标 FCurveSequence），
  **不是** WidgetTree 上的第二套 tick。驱动者是 `UITweenBehavior`（`UIBehavior`）：
  `WidgetTree::tick` → `UIElement::tick` → 行为列表。`wantsTick()` 只在播放中为真，
  结束自动回到干净、树不再拜访。挂上 tween 的**唯一接口**是 `ya::ui::animate(widget, dt)`
  （内部 `addBehavior`）；返回的 shared_ptr 只是同一实例的句柄。onFinished 可做
  ping-pong（playReverse）。
  写回 widget 一律走可动画属性接缝（`applyAnimatableProperty` / setter）；将来的
  Game UI clip player 也写同一条缝，仍然不是平行 dirty 通道。
- render transform 解析：`UIFrameBuilder::pushRenderTransform`，emit 时映射 rect/color/clip
  （缓存 draw-item 段存的是解析后结果，所以 transform 变化必须 invalidate 子树，
  setter 已保证）。
- 授权 DSL（首选写法）：`ya::ui::animate(widget 或 builder, duration)` 取回 tween，
  链式 `->fade(from,to,ease)` / `.scale()` / `.slide()` / `.tint()` /
  `.track(handle, from, to, ease)` / `.setDuration()` / `.setLoop()` /
  `.setOnFinished()` / `.play()`。typed 句柄 `ya::ui::anim::opacity|scale|translation|tint`
  把值域编进类型；自定义属性用 `ya::TUIAnimProperty<float>{"gauge"}` 声明，写错值类型
  是编译错误而不是运行时拒绝。behavior 由 widget 持有，所以句柄可丢弃
  （`ya::ui::animate(card, 0.2f)->fade(0.0f, 1.0f).play();` 就是完整动画）。
- 两态/重定位：`playToward(Forward|Backward)` 从当前位置继续，`setLerpNow(v)` 直接落位
  并停表。「目标态变了就朝它走」的控件（开关、hover 反馈）用这两个，不要用
  `play()`/`playReverse()` 重启，否则中途反向会跳值。
- 多关键帧（curve）：`tween->curve(ya::ui::anim::opacity, {ya::animKey(t, v, ease), …})`
  是同一个 track 的分段形式——仍是「一个属性 / 一个时钟 / 一个 widget」，只是求值从两端点
  变成 N 个键。键时间用**归一化时钟时间**（0..1），`setDuration` 整体改速；键拥有自己的时刻，
  同一时刻的后键胜出（离散跳变）；首键之前与末键之后保持该键的值（不外推）；时刻必须非递减、
  值域必须与属性声明一致，否则该 track 在 resolve 时被拒并只警告一次。
  多对象 / 事件轨 / blend 仍属 Game UI clip player，不要塞进框架。
- 默认带动画的控件：`UISwitch`（DSL `ya::ui::toggle(...)`）。它用 `animate()` 挂上
  一个 `UITweenBehavior` 驱动自己声明的 `progress` 通道（`kAnimSwitchProgress`）：值立即
  翻转，knob 位移 + track 配色插值；静止时 `wantsTick()==false`，
  `setTransitionSeconds(0)` 可整体关掉动画。
  哪些控件该默认携带动画、哪些应 opt-in，见 `.agent/plan/archive/gui-animation/plan.md` §8。
- 零时长时钟语义：`duration<=0` 表示“无动画”，`getLerp()` 返回被放置的那个端点
  （play→1、playReverse→0、setLerp(v)→v）。不要写回“恒返回 1”，否则 instant 控件会被画成
  终态（已由 `ZeroLengthClockReportsTheEndpointItWasPlacedAt` 锁住）。
- 验收：`GuiAnimationTest` 25 例（接缝/类型/tween 生命周期/render transform 映射/playToward/
  setLerpNow/零时长/curve 求值与拒绝/UISwitch 行为与 knob 几何）；Workbench `Animation/Tween` 页 +
  `Scenarios/animation_gallery.jsonl`（真实点击 + `assert_validation_clean`）。

## Dock 权责

划分按责任，不按目录。三层：

```text
GUI Framework     怎么开窗、投影 stack、拖拽、跨窗迁移
GameEditor        这个 tab 是什么、属于谁、能不能放这里、关闭怎么办
EditorSurface     这一扇 editor 窗的 chrome / viewport / dialog / snapshot 编排
```

| 角色 | 当前类型 | Framework 负责 | 不负责 |
| --- | --- | --- | --- |
| Window | `GUIWindowManager` / `IGUIWindowSession` | NativeWindow、WidgetTree、surface/present、input/focus/DPI | tab 业务、editor owner、`FDockContext` |
| Tab registry | `FDockContext::FTabRegistry` / `FPanel` | TabId、title、stableKey、content widget、opaque owner/document | `EditorRootId`、spawn factory |
| Dock layout | `FDockTreeModel` (`layout()`) | Split / Stack、tab 顺序、active、split 比、persist | OS window、editor 类型 |
| Dock session | `FDockContext` | TabRegistry + layout + overlay/native placement + opaque policy + `commitDrop` | 创建 OS window、听 SDL |
| Area 投影 | `UIDockSpace` | 物化 split、stack 投影生命周期、area overlay + `applyDrop` | editor policy、leaf hit |
| Stack / Well | `UIDockTabStack` / `UIDockTabWell` | merge/split/tab 插入的 drop target、tab 重排/发起 drag | 改 layout、创建 native window |
| Drag | `GUIDragRouter` + `WidgetTree` D&D | 跨窗路由、ghost、keep-alive、`FDockDropTarget` | Inspector 能不能进 Material |
| Overlay floating | `UIDockFloatingHost` / `UIDockFloatingWindow` | 同树 Popup 投影；drop 走同一套 target | 第二套 floating dock 模型 |

GameEditor：`FEditorTabSpawner` / `FEditorTabSpawnContext`（typed factory）；`canTearOffEditorTab` / `canAcceptEditorDrop` 注入 `canAdoptPanel`；`EditorNativeTearOff` 只调 coordinator + `transferPanelTo`；`EditorWindowSession` 绑 GUI session；`EditorDockWorkspace` 做 factory JSON / persist，不创建 OS window、不算几何。

禁止：每个 TabWell 自持 `FDockContext`；删掉窗口级 layout；Framework include `EditorRootId`；`UIDockFloatingWindow` 另搞一套 drop；`EditorDockWorkspace` 当 window manager。

`UIDockTabWell` / `UIDockTabStack` 是 leaf drop target。GameEditor policy 仍用 `canAdoptOntoLeaf` 签名。`UIDockSpace` 保留作 Area 投影名。

- `FDockContext` 是共享会话，不是 widget，也不是 OS window。NativeWindow placement 只是 coordinator 记录（MW-702/703），本对象不创建 native window。Overlay 的 `targetWindowId` 是 host window；`bindFloatingTargetWindow` 拒绝 overlay。跨树迁移走 `extractPanel` / `adoptPanel` / `transferPanelTo`（禁止 live widget 双挂载）。Policy 只吃 opaque `stableKey` / `ownerEditorId` / `documentKey`。`FDockPanelDragDropOp` 携带 `sourceContext`。Drop 命中由 Well/Stack 产生 `FDockDropTarget`。
- 指针拖拽 session 在进程里对每个 input universe（`GUIApp` 或 GameEditor 的 `AppKernel`）是唯一的：`GUIDragRouter` 记录 source/hover window，以及 pointer capture 所属窗、key-focus IME 窗、树内 modal 的 app-modal 范围。`WidgetTree` 只持有 source-local 的 payload、ghost、observer、capture widget、hover/tooltip。通用 D&D（TreeView / Designer / Asset / Dock）都从 topmost `canAcceptDrop()` 发现可提交目标；同树 hover 与跨窗一样走 `canPreviewDrop()` / `findDropHoverTarget`（dock chooser）。OS cursor 问 router（capture 窗或当前指针窗的 hovered）。GameEditor 不得再自己听 SDL 做跨窗拖拽；`EditorInputNode` / `EditorModule::onEvent` 共用同一 router。同一窗与跨窗 dock tab 拖拽共用一条路径：source 窗保留自己的 tree，ghost 是 DragIme 上的小 tile，目标树走 `setExternalDropHover`。禁止把 extra tree 偷到全屏/等大 pickup overlay（macOS Vulkan swapchain 往往不透明，全屏 overlay 会挡住 drop 目标）。指针不在任何可见窗内时才用 ~168×32 的 click-through desktop overlay 画 tab ghost。标题 TabBar 整条 rect 必须注册为 Client；只有 trailing gutter 是 Drag。从顶部 tabwell 拖出 tab 是 tab 手势，不是 OS 拖窗。最后一个可关闭 extra 页签一旦离开源窗，`bHideSourceWindowOnLeave` 立刻 `INativeWindow::hide()`，源窗不得跟着指针走；drop 到 dock 后 reclaim，NoTarget 再 show/挪到落点或开新窗。不要用 `SDL_HITTEST_DRAGGABLE` / `performWindowDragWithEvent` 去“拖 tab”。drag 期间 `SDL_CaptureMouse`；source-tagged 的 move/release 用 `OsEventPump::queryGlobalMouse` 做窗口 hit-test（capture 会把事件钉在源窗并可能钳制局部坐标），显式 foreign window id 仍信任事件坐标；hit-test 跳过 hidden/minimized。`WindowFocusLost` 必须清掉该树的 hover / tooltip / 普通 pointer-over；正在 capture/drag 的树仍不得注入远指针（capture 与 drag session 可以跨窗保留）。
- GameEditor 经 `FEditorTabDragPayload` / `canTearOffEditorTab` / `canAcceptEditorDrop` / `canRedockEditorTab` 做 placement policy。`tearOffEditorPanelToNativeWindow` / `handleDockNoTargetTearOff` / `redockEditorPanelToOwner` / `closeEditorWindow` 只调用 coordinator + `transferPanelTo`；空 extra 窗 `reclaimEditorWindowIfEmpty`。Locked / Level / 默认窗不能关或迁走。`EditorDockWorkspace` 不创建 native window、不 include `IGUIWindowCoordinator`。`EditorSurface` 只挂 generic `realizeNoTargetTearOff` 回调，不 include coordinator。
- DockSpace NoTarget：若 `FDockContext::realizeNoTargetTearOff` 返回 true，不创建 overlay；未接线或返回 false 时仍 `InProcessOverlay`。GameEditor 产品路径走 native OS window。`UIDockFloatingHost` 仍只投影 overlay。Host 适配器 `realizeNativeDockPlacement` 只把 `geometrySpace == Screen` 的 pos 写成 host origin；`createSession` 在 `monitorIndex < 0` 时保留窗口已查询的 monitor，避免 recover 丢掉 origin。TreeLocal 不得升格为 OS origin。
- `UIDockSpace` 是 DockArea 投影：把 context 的 docked tree 物化成 nested split + `UIDockTabStack` / `UIDockTabWell`。不拥有 model。内部投影是 `FDockStackView`。**唯一入口**是 `syncProjection(EDockProjectionSync)`：`Structure` 重建拓扑（drop / tear-off / `fireDockUpdated` / 空 Area 的首次 layout）；`Stack` 只灌一个 stack 的 tab+graft（`addPanel` / `activatePanel`）；`Chrome` 只改 well 可见性。禁止在 Area 外直接调 `rebuildProjection` / `rebuildStack`。drop 语义收口为 `FDockDropTarget`；布局变更走 `FDockContext::commitDrop`。Well/Stack 的 `canAcceptDrop` 命中 leaf；Area 处理 split gutter 回退，并拥有 chooser overlay / `applyDrop`。overlay floating 自己产生 `FloatingTabWell`。模型节点是 `EDockNodeKind::Split` / `Stack`（JSON `"leaf"` 仍可读）。同一树里 nested `UIDockSpace`（Level Viewport）盖住 window-root page leaf：同树 hover 走 `findDropHoverTarget`（chooser 是 preview-only）；外层 `resolveDropPreview` 在 innermost dock 不是自己时返回空，禁止把整块上半窗画成 page chooser。
- 禁止可见的空 dock stack（`DockStackN (drop tabs here)`）。Generic/Tools 最后一个 tab 离开后立刻 collapse，即使 `persistentEmptyLeaf` 曾为占位而设。Page well 可以空，但 chrome-only、不画 inner tab well。`addPanel` 必须清掉 persistent 标记；`extractPanel` / `commitDrop` 之后 `pruneEmptyGenericLeaves`。空 Tools well 需要时由 `ensureToolsLeaf` 再造，不能留在屏幕上当 drop 槽。
- `UIDockFloatingHost` 只投影 `InProcessOverlay` placement（同一 native window / 同一 WidgetTree 的 Popup 层）。它不是 OS window；`NativeWindow` placement 必须跳过。浮窗 merge 走 `FloatingTabWell`，不要再加一套 `targetFloatingId` bool。
- 绑定 API：`UIDockSpace::setContext` / `UIDockFloatingHost::bindContext`。不要再引入 `UIDockWorkspace` 这种与 Space 近义、还带 `UI` 前缀的会话类型。
- 源码与公开头收在 `Runtime/Widgets/Controls/DockSpace/`；include 为 `GUI/Widgets/Controls/DockSpace/...`。TabBar 仍是通用控件，不进这个目录。
- 停靠 tab 拖动（`FDockSpacePanelDragBehavior`：ghost + 无目标时 tear-off）和浮窗标题拖动（`FDockFloatingWindowPanelDragBehavior`：窗体跟随指针、skip-source hit-test、sticky preview）不是同一套手势。不要抽共享 helper。

## 布局契约（SizeToContent）

- Slot 按 **parent layout family** 分，不是每个 widget 一种 slot：
  `canvasSlot` → canvas host（`UICanvasPanel`、`UICanvasRoot`）；
  `boxSlot` → `UIContainer` / `UIExpander`；
  `overlaySlot` → 仅 `UIOverlay`（以及 dock leaf / floating window 这类真叠放 host）；
  `contentSlot` → 单 child 内容 host（`UIBorder` / Button / CheckBox / SelectableRow / SizeBox / ScrollViewport / SplitPane pane / CompoundWidget）。
  不要给 Expander / SplitPane 再包一层隐藏 wrapper child；SplitPane 的 pane 复用 `contentSlot`，不另造 `splitSlot`。
- Layout 类型按头文件隔离。控件头只 include 自己的 family（`UILayoutTypes.h` / `UIBoxLayout.h` / `UIContentLayout.h` / `UIOverlayLayout.h` / `UICanvasLayout.h` / `UISplitLayout.h` / `UITableLayout.h` / `UIScrollLayout.h`）。`GUI/Layout/UILayout.h` 是兼容聚合头，给实现 .cpp 用；不要在 Button/Text/WidgetTree 这类宽扇出头里再 include 它，否则改一个 layout 仍会全模块重编。
- Slot args 应用走 `UISlot::applyArgs(args)`：`TArgs::SlotType` 指向接受该 payload 的 slot 类，没有按类型 if/else。新增 slot 类型时加 `FNewSlotArgs::SlotType` + `apply(const FNewSlotArgs&)` + `serialize`/`deserialize`/`isAutoSizeActive`，不要改 `applySlotBuilder` / `UIDocument`。layout arrange 读取 typed 字段仍可用 `as<T>()`。
- 运行时类型是 `UICanvasPanel`；DSL 是 `ui::canvasPanel`。没有 `ui::panel` / `ui::canvas()` 别名。type id 仍是 `"engine.panel"`（`kTypeIdCanvasPanel`）。Canvas **不 paint**。`"panel"` / `"panel.canvas"` / `"canvas"` 是 visual theme key，挂在 `UIBorder` 上，不是 layout 类型。`ui::canvasPanel(...).setStyleKey("canvas")` 不再产生 chrome。

## 基础控件权责

| Widget | 负责 | 不负责 |
| --- | --- | --- |
| `UICanvasPanel` | canvas slot：anchor / insets / size mode；多 child 自由放置 | 填色、描边、圆角、纹理、nine-slice |
| `UIContainer` | box slot：align / padding / margin / fill / weight | paint |
| `UIOverlay` | overlay slot：同一 rect 里叠放 + align | paint |
| `UIBorder` | 画实心/主题 fill、outline、圆角；单 content child + padding | 纹理字段、nine-slice 字段、canvas anchors |
| `UIImage` | 内容纹理 / live RT / Stretch·Contain | 当 layout host；nine-slice chrome |
| `FBrush` | Solid / Image / NinePatch / frame-only `Border` 切片 | widget 树 |
| Card | `UIBorder` + `setStyleKey("panel.sidebar.card")` | 单独的类型 |

字段对照（旧 canvas 混在一起的 reflect）：

- `_color` / `_cornerRadius` → `UIBorder`（无 theme 时 `_color` 是 `FPanelStyle.fillColor` fallback，与 `UIText::_color` 同模式）
- `_image` → 不存在。内容图：`ui::overlay().child(ui::border(), overlaySlot().fill()).child(ui::image(), overlaySlot().fill())`
- `_bNineSlice` / `_nineSliceBorder` → `FBrush::ninePatch` / `FBrush::border`，写在 `FPanelStyle.fillColor`（主题 chrome），不是 widget 字段

迁移规则：

- 一个 fill child 的着色壳 → `UIBorder` + `contentSlot().fill()`（单 child 的旧 canvas insets 可以变成 Border padding）
- **多个不同 anchor 的 child 必须留 canvas**，paint 用 sibling fill Border：`canvas.child(border.HitTestInvisible, canvasSlot().fill()).child(real, canvasSlot().anchor/insets)`
- 不要用 Border padding / box align 去顶替多 child 的 canvas gutters

- Slot 未指定时的默认（必须先看得见，再调属性；**不是**一律 fill）：
  `contentSlot` / `overlaySlot` → Fill/Fill（单内容区 / 叠放占满父矩形）；
  `boxSlot` → 主轴 Auto + 交叉轴 Stretch（按内容堆叠；`fill()` 分剩余空间）；
  `canvasSlot` / 裸 `child(node)` → 左上角 Auto/Auto（按 desired 显示）。Canvas 的
  `fill()` 是显式的：默认 fill 会让每个 sibling 叠满父矩形，那是 overlay。
  **跨度 `anchor()` 或非零 `insets()` 会把该轴从 Auto 升成 Fixed（stretch）。**
  只写 `anchor({0,0},{1,1})` 而不 `fill()` 也必须铺满，禁止再量出 0 desired 把整块 UI 变成 0px。
  SizeToContent 落在 stretch 区域里必须显式、且放在 placement 之后：
  `fill().widthSizeMode(Auto)` / `heightSizeMode(Auto)`。
  `ui::canvasSlot()` 必须与 `UICanvasSlot` 同为 Auto；空 `FCanvasSlotArgs{}` 仍是
  Fixed 0x0（历史 `args.fixedSize = {w,h}` 载荷），不要把它当成 DSL 默认。
  Box 的 Fill 子节点在 Auto 父级里 leftover=0，主轴高度为 0（FeatureRail 那种
  `canvas.child(column.child(card, fill))` 没给 column `canvasSlot().fill()`）。
- SizeToContent / Slate DesiredSize 模型完全由 parent-owned slot 表达：canvas 在 attach 时把 Auto 种到 `UICanvasSlot` size mode。每轴解析优先级
  `Auto（preferredSize 非零则用之，否则 `computeDesiredSize`）> stretch（Fixed 轴上的 anchor span / insets）> slot authored size（fixedSize）`。
  DSL 跨度锚点 / insets 会把 Auto 升成 Fixed，所以 stretch 生效；不要依赖“看起来像 fill 的锚点 + Auto 轴”。
  需要在 stretch 区域里按内容测量时，在 placement 之后写回 Auto。`fill()` 是两轴 Fixed + `[0,1]`。
  child geometry 永远不是 layout 输入；`computeDesiredSize` / `computeIntrinsicSize` 只报告内容。不存在仍读取 child authored geometry 的 path-B。
- `UIText`：desired / intrinsic = `font.measureText(text) × lineHeight`（与 AutoSize 无关）；字体经
  FontManager 解析，closure 测试用 `registerFont` 注入合成字体。显式尺寸在 parent-owned slot 上。
- `UIButton` / `UISelectableRow` / `UICheckBox`（Content-Slot）：单 child 容器。标签是内容槽里的 `UIText`
  子节点（DSL：`.child(ui::text(...).setText(...))`）。`UISingleChildLayout` padding + 内容子节点填入
  内缩 rect（`layoutAssigned`，非 child `setPosition`）。CheckBox 的左 padding = `_boxSize + _labelSpacing`。
  desired = 内容子节点 + padding；显式尺寸在 parent slot 上。行缩进用
  `setContentPadding(FMargin{indent, 0, 0, 0})`。
- `UICompoundWidget` 是 single-child host：`construct()` 挂上的第一个 child 经 `UIContentSlot` 填满 compound rect，不再手写 `layoutAssigned`。
- 布局正式分为 `UIElement / UILayout / UISlot`：`UIContainer` 只是第一个 layout host，
  持有 `UIBoxLayout`；它不再持有 `_direction/_spacing/_padding/...` 这类 box 字段。
  `UILayout` 只负责 measure/arrange，`UISlot` 是 parent-owned parent-child 边对象。
  `installLayout()` 的 host 由 `UIElement::createSlotForChild()` 直接问 layout 要 typed slot，不必再覆写工厂（CanvasPanel / TreeRoot / DockSpace / DockFloatingHost）。成员持有 layout 的 host（Border / Button / CheckBox / Compound / SizeBox / Split / Scroll / Overlay / Container）仍自己转发 `createSlot`。
- `UIBoxSlot` 承载每 child 的 `Auto/Fill`、weight、**四边 `FMargin`**、cross alignment、
  min/max/preferred size 与 layout participation；slot setter 会使所属 tree 的 layout 失效。
  Fill 按权重分配剩余主轴空间且遵守 max size；Hidden 默认保留空间，可由 slot 明确关闭。
  Construct：`column.child(node, FBoxSlotArgs{.sizeRule = EUIBoxSlotSizeRule::Fill, .margin = FMargin::all(8), .preferredSize = {120, 22}})`；
  `childFill` 仍是只标 Fill 的简写。`setMargin({x, y})` 走 `glm::vec2` → 左右/上下对称
  （`FMargin` 不是 aggregate，两元素列表不会变成 left/top、right/bottom=0）。
  `FBoxSlotArgs::preferredSize` 非零轴覆盖 child desired；`ui::boxSlot().preferredSize({w,h})` 是 construct-time 写法。
- UIElement 不提供 `setSize/setPosition/getSize/getPosition` 或 authored geometry shadow；运行时与 imperative 构造代码必须先取得当前 `UISlot`，再显式修改 `UICanvasSlot` / `UIBoxSlot` / `UIOverlaySlot` / `UIContentSlot`；detached 构造使用 `addDetachedChild(..., slotInitializer)` 或 builder 的 pending edge intent。最终 rect 通过 `getLayoutRect()` 读取。
- attach 不再从 child geometry 推断 slot；显式 `FCanvasSlotArgs` / typed slot initializer 才是 edge 的唯一 authored placement 来源。默认构造尺寸不是布局输入。
- `UIPopupOverlay::_contentExtent` 是 popup-owned canvas edge 的内容尺寸；基类 Auto + preferredSize，Menu 覆盖为 fixedSize，Dialog 走 preferredSize。不要再通过 child geometry API 写内容尺寸。
- child 用 `getSlot()` 读取当前边，parent 用 `getSlotForChild()` 查询；reparent/detach 时旧 parent 销毁旧 slot，新 parent 创建默认 slot。不要缓存 slot。层挂载默认使用 `attach(*tree.getLayer(layer), widget)`；带几何意图使用 `attach(parent, widget, FCanvasSlotArgs)` 或显式 layer args。
  裸指针跨越 reparent/detach。
- `UIBoxLayout` 主轴按 desired/slot 排列，cross 轴默认 stretch；`computeDesiredSize` 聚合
  child + margin + spacing + padding。scroll/split 仍读取内容 desired，specialized layout
  已收口为 `UIScrollLayout` / `UISplitLayout` / `UIOverlayLayout`；`UIButton`、`UISelectableRow`、`UICheckBox`、`UICompoundWidget` 与 `UISizeBox`
  使用 `UISingleChildLayout`。  `UIDockSpace` 也是 single-child host：投影根填满 dock。  `FDockTreeModel::exportLayoutJson` /
  `importLayoutJson` 按 panel `stableKey` 持久化 split/leaf 树（不持久化 NodeId），leaf 可带
  `hideTabBar`（默认展示 tab strip；右键菜单或左上角 12px 折角只 Collapsed 掉 title bar，
  面板内容继续填满 leaf。split ratio / 选 tab / hide-tab-bar 只改 live chrome 并
  `notifyDockLayoutListeners()`；结构变化才 `syncProjection(Structure)`，且会先 unlink 再
  reparent/attach 已挂载的 panel widget，禁止对仍有 parent 的 panel `addDetachedChild`。
  floating 停靠 cardinal split 时，drag keepAlive 仍握着刚 detach 的 floating window，
  panel 的 `_tree` 已空但 `_parent` 还在 chrome 上：`releaseMountedPanels` 必须把
  **dock tree 里每一块 panel**（不只是旧 `_leafViews`）从任何 parent 上摘下来，
  `unlinkWidgetFromVisualParent` 在无 tree 时也要 `removeChildEdge`。否则 north-of-leaf
  新叶是空白，直到再切一次 tab。
  叶内 tab 可关（`UITabButton` close hit-zone → `FDockContext::closePanel`）。
  Page-role leaf 的 inner well 与 hide-affordance 永远 Collapsed（title chrome 才是
  page tabs）；`hideTabBar` 折角不得把 Level/UI 再画进 dock。
  TabWell 内左右拖只 `movePanel` 重排，不开始 dock session、不画 ghost。指针离开 well
  才 `_onTabDragBegin`。同 stack 的内容区显示 chooser；hover 到 cardinal 块则
  `splitStack`（含把当前 leaf 的一个 tab 拆出去）。未落在 chooser 块或 well 上的
  释放是 `NoTarget` → floating/tear-off。`canAccept` 只在 `commitsDrop()` 时为 true。
  Locked 页签（Level）`_bDraggable=false` 且 `_canBeginTabDrag` 拒绝 tear-off：
  press 仍 capture（避免 Hybrid Drag 整窗移动），`onDragDetected` 返回空，
  不出现 ghost、不开始 dock session。
  `FDockContext::exportLayoutJson` / `importLayoutJson` 在同一 JSON 上拆开
  overlay `floating[]` 与 native `windows[]`（panel keys + pos/size + selected +
  `hideTabBar` + `projection` / `geometrySpace` / `sourceScope` / `targetWindowId` /
  opaque `ownerEditorId` / `documentKey`）。overlay 坐标永远是 tree-local（Popup），
  缺 `projection` 的旧 `floating` 视为 `inProcessOverlay`，**不得**把 Popup 坐标升格成屏幕坐标。
  legacy `floating` 里的 `nativeWindow` 缺 `geometrySpace` 时仍是 TreeLocal，下次 export 迁到 `windows[]`。
  Editor 经 `ConfigManager` `editor.dockLayout` 恢复（envelope v3 仍用 `windowRoot` /
  `ownedNested`；OS 窗 topology `windows[]` 是 MW-801）。产品启动 restore extra OS
  window 并在 primary submit 之后 `renderAll`（C9-P）。`FDockContext::appendOnDockUpdated`
  与 `appendOnFloatingUpdated` 写回。Editor chrome 打开 `bAllowFloating` /
  `bAllowTearOff`，`UIDockFloatingHost` 挂在 Popup 层，只画 overlay placement。
  `UIDockFloatingHost` 是 canvas host；floating window 的位置/尺寸写在 host-owned `UICanvasSlot`，
  `setWindowRect` 直接更新 host-owned `UICanvasSlot`。窗口本身是 overlay host：chrome
  box Fill，resize handle 走 overlay Start/End+Fill，不再在 box arrange 之后手写 handle rect。
  `UIPopupOverlay` 安装 `UICanvasLayout`；每帧把 `resolveContentSlotArgs()` 写进 content slot，再交给 canvas arrange。
  specialized widget 只保留 paint/input transient state，不能再把 ratio/offset/padding 等几何状态塞回 widget 字段。
- `UIOverlay` 是叠放 host（不是 `UIPopupOverlay`）：每个 child 经 `UIOverlaySlot` 在同一父
  rect 内独立 Fill/Start/Center/End + 四边 padding + **preferredSize**。child 的 canvas anchor 被忽略。
- `UISizeBox` 是单 child 约束盒：padding + 可选宽/高 override + min/max。
- `UISplitLayout` 管 orientation/ratio/min extent/divider/padding + first-two-child arrange；
  `UIScrollLayout` 管 axis/offset/step/max offset + first-child arrange；scroll 到边界必须
  返回未处理，以便 route bubble 到外层。tree dump 的 `layout.type` 统一输出
  `box/singleChild/split/scroll/overlay/sizeBox/canvas`。
- 布局 rect 尺寸永远 clamp ≥0（负尺寸会传染进 clip/scissor）。

## 静态 DSL（live construct）

- Builder 所有权：`parent.child(builder)` 传**左值**时只挂载、不消耗调用者的 builder
  （内部 `takeMountRef` 先拷贝再 release），所以「先挂载、后取句柄」是安全的；
  `parent.child(std::move(builder))` 才是显式交出。任何已消耗 builder 上的
  `share()` / `widget()` 会断言报错，而不是递出空指针。禁止恢复「child() 偷偷清空左值」
  的语义：那正是 animation gallery `Toggle all` 崩溃（空句柄在几百帧后才被解引用）的成因；
  契约由 `DeclarativeContractTest.MountingAnLvalueBuilderLeavesItUsable` 等三例锁住。
- 默认路径：`ui::column/row/text/button/checkBox/slider/comboBox/image/textField/canvasPanel/border/splitPane/scroll/overlay/sizeBox/...` 组好 detached builder，再显式 `.release()` / `.share()` 取出 live `UIElement`，最后 `ui::attach(tree, parent, widget, slot)` 挂进树（Slate `SNew` + `SAssignNew`）。`ui::attach` 不隐式 `release()`；build 阶段与 attach 阶段分开。带 slot 的 `ui::attach(tree, parent, widget, slot)` 要求 `parent` 是带 `SlotArgs` 的具体 host（`UICanvasPanel`/`UIBorder`/`UIContainer`/`UISplitPane`/`UICanvasRoot`…），不能传 `UIElement&`，这样 canvas/box/overlay/content 在编译期选对。不要把整棵 DSL 包进 `ui::attach(...)`。`setAnchors` / `fillParent` / Border `setCornerRadius` / Border+Text `setStyleKey` / `setStyle`（freeze）/ `setStyleField`（单键 inherit）/ container `childFill` 与 `child(node, FBoxSlotArgs)` / overlay `child(node, FOverlaySlotArgs)` / content host `child(node, FContentSlotArgs)` 在 Construct 时写到 live widget。`setTooltip` 写在 base builder；Text `setWrap` / `setMaxWrapWidth` 控制折行；base `setVisibility`；split `setPadding`。`ui::button` 没有 `setText`；文字走内部 `UIText` 子 widget。值更新走 `Reactive<T>`；已知结构走 `attach`/`detach`/`setVisible`。自定义 / 复杂 demo widget（MenuBar、TreeView、TableGrid、DragFloat/SpinBox/RadioButton/ColorEdit/SearchComboBox、UIDragDropTile、DockSpace、SelectableRow）用 `child(UIElementRef)` 挂进 DSL 壳，不要为此扩 Construct。GUIWorkbench Feature Gallery 各页与 Workbench 内置 Editor demo（`FWorkbenchSurface::buildEditorDemo`）已是 `.release()` + `ui::attach`。Editor 的 `rebuildItemRows()` 仍是事件期 live attach/detach `UISelectableRow`。弹层（Menu / Modal / Dialog）仍在点击时 live 组装。Dock floating host 仍 `attachToLayer(Popup, host, fill canvas args)`。Render 仍是 raw retained 对照。GameEditor chrome 已切到 `EditorSurface`（整窗 WidgetTree，不是 ImGui 内嵌 panel）。
- `UIDescription` / `UIReconciler` / `UIRenderController` / apply hook **已删除**。不要恢复 Description → apply → widget 转发层。
- Document/script：`UIDocument::instantiate()`（registry factory）只实例化一次。变长集合走列表控件 + `ReactiveList`，不是整页 re-run。
- `UIScreen` 是挂卸 / z-order / input blocking，不是每帧 `render()` owner。Gallery / Editor 不使用它；接到游戏多表面（HUD/模态）之前保持搁置。

## Render2D pass slot

- `Render2D` 不认识 game/editor pass；调用方经 `Render2D::acquirePassSlot()` 获取不透明
  `Render2DPassSlot`（每进程静态递增，上限 `FQuadRender::kMaxPassSlots = 8`），资源按 slot
  懒分配（GUI app 只用自己需要的 slot）。
- 映射位置：`Render2DComposePass::composePassSlot(kind)`（compose kind 各占一个 slot）、
  `RenderOverlay::viewportOverlayPassSlot()`。
- 管线 prep 必须在录制前：depthless（depthFormat==Undefined → uiPipeline）与 depth 变体
  （screenPipeline）按目标附件格式缓存。**运行时 UI 复合管线必须在首帧 world 渲染前 prep**
  （display image 在帧图执行期才创建；见 memory：first-frame prep 时序坑）。
- 单帧多批次 flush 共享一块 host-visible 顶点缓冲：每批次写不同区域 + `vertexOffset` 定位，
  容量 `MaxVertexCount × kFrameFlushSlots`，超限 assert（见 memory：multi-flush 覆盖坑）。
  kind 切换也算一次该 backend 的 flush，连续同类仍合批。
- Session 是 state-change batcher：`pendingKind ∈ {None, ScreenQuad, WorldQuad, Line}`。
  `makeSprite` / `makeText` / `drawRoundedRect` / `makeRectFilledMultiColor` → ScreenQuad，`makeWorldSprite` → WorldQuad，
  `makeWorldLine` / wire → Line。kind 一变先 `flushPending()` 再切换，GPU draw 顺序 = emit
  顺序。禁止在 `end()` 里按 world→screen→line 固定倒空。clip 改栈 flush 的是 pending
  kind（三条 backend），不是只 flush screen quad。
- GUI snapshot 的 Line 仍 tessellate 成 sprite，不走 `FLineRender`。`FLineRender` 只用于
  对场景 depth 的 debug 线。2D 半透明叠放靠 painter’s algorithm，不要用 `pos.z` / depth
  解决 chrome 遮盖。
- clip 栈改动必须"先 flush 再改栈"；scissor 防御性 clamp 到窗口边界。

## GUI render surface

- `GUIRenderSurface`（`Runtime/Compose`）是 GUI compose target 的唯一资源边界：
  `createOffscreen()` 创建 Framework-owned `RenderImage`，`wrapExternal()` 包装
  imported swapchain image；二者都经同一 `prepare()` / `record()` 调用
  `Render2DComposePass`。
- surface 只拥有/保留目标 image、format 与最终 layout；**不** pump event、acquire
  swapchain image、present 或访问 live WidgetTree。window/present 仍属于 host，
  tree/snapshot 仍属于 WidgetTree。
- 最终 layout 是 surface 的不变量：offscreen 默认 `ShaderReadOnlyOptimal`，
  swapchain surface 为 `PresentSrcKHR`（`isDisplayComposeTarget()`）。调用方不能通过 compose desc 把二者留在
  错误 layout。view compose 写 Camera/offscreen RT；display compose 才用 PresentSrcKHR。
- 替换/销毁 surface 必须发生在 frame boundary，且旧 command buffer 的 submit 已完成；
  command recording 仅消费不可变 snapshot 与当前 surface。
- `RuntimeUIOffscreen` 是和 `RuntimeUIComposite` 分离的 compose kind / pass slot：
  可在同一 command buffer 中把**同一 snapshot**录制到 windowed 和 offscreen target，
  不复用 vertex/descriptor frame resources。`GUIAppHost` 的
  `--gpu-shot` + `--offscreen-shot` + `--offscreen-diff` 是零容差 parity 门禁。
  Present 路径只持有 `IRenderSurfaceContext*`：用 `ISwapchain` 与
  `buildPresentationImages`，不要 `as<VulkanSwapChain>()`。
  Primary 与 extra 窗创建走同一套 API：`FGUIWindowHostConfig::renderAPI`（默认 Vulkan）；
  extra 在已有 `IRender` 时用 `render->getAPI()`。acquire 之后若 rebuild 了
  presentation targets，必须校验 `imageIndex` 仍落在 targets/cmdbufs 内，失败则
  empty-submit 本帧；不要对着过期 index 再 acquire 一次。
- `replayUIFrameSnapshot` 把 snapshot 画进**已经 begin 的 raster pass**（不
  `beginRendering` / 不转 layout）。WidgetTree chrome 走这条路径：presentation
  pass 已经打开，WidgetTree 覆盖整个 swapchain，3D viewport RT 只作为 `UIImage`
  采样。管线 prep 用 **swapchain format**，slot 用 `EditorToolSurface`。不要把
  `GUIRenderSurface::record()` 塞进 presentation（它会自己 begin pass）。

## GameEditor chrome

- Module 经 `EditorWindowRegistry::find(kDefaultEditorWindowId)` 调
  `EditorWindowSession::tick`；Surface 仍做窗口内编排：`rebuild-if-needed` → window metrics →
  `WidgetTree::tick` → shell dialogs → push viewport display → `buildSnapshot` →
  viewport host bridge。禁止 `tab->sync`，禁止 Surface 持有 Tab 控件指针。
  `selection` / `actions` 在窗口的 `EditorRootSession`（Level / UI / Material / Script）；
  窗口 `activeRoot()` 仍是 Level。`undo` 在绑定的 `EditorDocumentSession`（无 document 时回落到
  RootSession 本地栈）。`EditorDocumentRegistry` 归 `EditorModule`，按
  `(kind, key)` 单例；两扇窗绑同一 scene key 共享 undo。Surface / WindowSession
  不拥有 registry。UI Designer 是 `WindowRootEditor`（`kUIEditorRootId`），Preview /
  Palette / Tree / Inspector 是 nested owned tools，走 UI document 的 dirty /
  RejectIfDirty close / per-kind preview claim（不是 Camera；画布仍是 Level 2D
  viewport）。Material/Script 同样是 WindowRootEditor + nested document tools
  （identity/dirty/undo chrome，还不是 material graph / script AST）。Owned tool 带
  `ownerEditorId`，dock 政策是目标 dock scope + owner。`canSpawnEditorTab` 管
  Window-menu / invoke（WindowTool 只进 window-root；owned tool 只进同 owner nested）。
  `canDockEditorTab` 管 drop 与 layout restore：Level nested 可以保留 WindowTool，
  window-root 可以保留 Level owned tool。UI/Material/Script nested 仍拒绝 WindowTool。
  重启必须按保存布局 `materializeTab(id, /*bRestoreLayout=*/true)`，不能再用 spawn
  政策把用户拖过的 tab 丢掉。`FDockContext`
  不认识 editor root。UI/Material/Script 的 nested dock 由 `EditorNestedDockHost`
  持有，不把 Surface 做成 dock manager；这些 nested dock 关闭 floating/tear-off（C7）。
  C5 起 owned tool 只进同 owner 的 nested `FDockContext`（window-root 不再扁平物化）。
  Level Editor 是 Locked、不可关闭的 window-root tab，内部 `UIDockSpace` 投影 nested dock。
  `editor.dockLayout` v4 信封为 `{version:4, windows:[{role, bounds, monitor, maximized, windowRoot, ownedNested, ...}]}`。
  v2/v3 `{version, windowRoot, ownedNested}` 仍映射到 main window。v1 扁平文档 remap
  到两个工厂布局（自定义 split 丢失）。Dock overlay `floating[]` 坐标仍是 tree-local。
  Extra OS window 只经 `IGUIWindowCoordinator` restore（`restoreEditorExtraWindows`）；
  GameEditor 不创建 SDL 窗。坏 monitor 经 `recoverWindowScreenPlacement` 迁到可用屏
  （按 index，否则按 name，否则 primary）；未知 origin（index<0 且无名）只改 size。
  缺失 documentKey / 未知 ownerEditorId 丢弃该 extra，不改绑到其他 root。
  `closing: true` 与空 torn-off extra 不恢复。产品路径仍只 tick/present 默认 native
  window；主窗启动会 recover 已持久化的 screen placement。
  `FEditorTabSpawnContext` 是 identity（windowId / scope / optional
  owner / document / placement / detach）加来自 owner session 的可选指针，以及
  窗口注入的 `app` / `presentSurface`（不是 `App::get()` / `primarySwapchain()`）。factory
  只创建 UI content，不持有 `EditorSurface`、不拥有 tree、不调用 `tick()`。Viewport spawn
  需要 viewportHost；Inspector spawn 需要 layer+SelectionModel。Content 经 Layer
  `cmdLoadScene`（`.scene.json`）或 `openDocumentEditor`（`.lua` / `.mat`）；Runtime Tools
  经 ActionMap + present surface。第二扇
  `EditorWindowSession` 只走 MW-401（独立 tree/dock，禁止两树一窗、禁止每窗
  `IRender::create`）。禁止把这些模型倒进 WindowSession god object。
  Tab 经 `EditorTabSpawnerRegistry` 注册，`EditorDockWorkspace::invokeTab` 按 stable key
  激活或 spawn（owned tool 若 owner 不是当前 host，先打开对应 WindowRootEditor 再进其 nested）。layout 工厂是
  `DefaultEditorDockLayout.json` + `DefaultEditorOwnedDockLayout.json`（Level nested
  为 hierarchy | play-toolbar 在 viewport 上 | inspector），外加 UI/Material/Script
  的 in-code nested factory。Window 菜单
  checkbox 切换已注册 tab；Layout → Default 一键恢复工厂布局（先
  `setPanelClosable(true)` 再关 Locked tab）。rebuild 期
  dock/workspace 政策在 `EditorDockWorkspace`，ActionMap 目录在
  `registerEditorActions`。`onAttached` 拉权威状态并订阅所属边界的 `MulticastDelegate`，
  `onDetached` 按 handle 退订。未选中 dock tab 是 detached subtree，不会 tick。
  不要再引入 `EditorPanel`、中心 MessageBus，或 `EditorTabRegistry` 那种 `std::function`
  袋子。结构见 `.agent/plan/archive/gui-editor-tab-lifecycle/`。
- 启动时 **WidgetTree 唯一 chrome**：整窗 default `EditorWindowSession`（持有 `EditorSurface`）+ `replayUIFrameSnapshot`；3D 仍离屏
  compose，树只采样那张 RT。`--editor-chrome=imgui` / `editor.chrome.host=imgui` 会被忽略并打 WARN。
- WidgetTree 输入：`EditorInputNode` 绑 `EditorWindowSession*` → session `dispatchEvent` →
  `WidgetTree::dispatchEvent`。
  所有权分层（不要在 Router 里每条事件 `cancelHeldKeys`）：
  1. Session 先把事件交给 chrome 树。Viewport gizmo overlay **只在 LMB drag** 时 Exclusive；hover 一条轴不得吞 MouseMoved，否则树的 hover 冻在 viewport 上，dock/split 收不到 press。
  2. Overlay 命中用 **指针是否在 viewport imageRect 内**（或 gizmo 正在 capture），不要用 stale 的 hover/focus。
  3. 点在 viewport image 上 `takeKeyboardFocus()`。Image 必须 `Focusable`，否则 WidgetTree 会 `setFocus(nullptr)`，WASD 只剩 hover 碰巧有效。
  4. Viewport 3D 右键菜单在 **release** 打开（press 记 pending；移动超过 ~4px 视为 look）。同一时间一个 viewport menu。
  5. RMB look 是 `EditorInputNode` 上的会话（`_bLooking`），不是“本帧 IM 里 RMB 还在 + stale viewportMouse”。按住期间 MouseMoved/WASD 继续进 `InputManager`，即使指针离开 image。
  6. 只把 **IM 当前按住的** KeyReleased / MouseButtonReleased 回写 IM。键盘所有权从相机丢掉的那一次（焦点/hover/look 全无）才 `cancelHeldKeys()`。
  7. `shouldCaptureInput()` = viewport hover/focus **或** RMB held。`FreeCameraController` 只读 IM，不读 WidgetTree。
  8. `UISplitPane` 分隔条双击（400ms / 6px，与 SpinBox 相同时钟）把 ratio 重置为 0.5，并写回 binding / dock callback。
- Viewport gizmo 已改为 retained host + native math controller：`EditorViewportGizmoOverlay`
 只路由输入，`EditorViewportGizmoController` 负责世界空间 translate/rotate/scale 与 undo，绘制在
 `EditorModule` 的 viewport compose callback 中走 `Render2D`（`layer.gizmo().recordOverlay()`）。
- Camera preview 是 **chrome，不是 runtime compose inset**：`EditorViewProducer` 声明的 preview
  View（`kPreviewViewId` = 2）只渲染到自己的 RT，`composeRect` 为空，所以 runtime 不把它 blit 到世界
  RT（那样世界 overlay——x-z 网格 / manipulator / 相机视锥线框——会盖在预览上，因为它们是世界空间内容，
  而 inset 是后画的）。同帧顺序不变：`onViewportCompose` 里 `EditorModule` 读
  `getViewOutput(kPreviewViewId)` → `EditorLayer::setViewportPreviewImage` →
  `EditorSurface::pushViewportDisplay`（device → layer → chrome 三段，和世界 viewport 同一条）→
  `EditorViewportTab::setPreviewImage`；Tab 内部是 `UIOverlay` 叠两兄弟：世界 `UIImage` 占满，
  `UIBorder`（`panel` 主题 + 1px padding）包住预览 `UIImage`，End/End 对齐、位置由 slot 的
  padding + preferredSize 表达（不要手写 rect）。preview View 的 features 只含 `Game`：
  预览=该相机所见，不画编辑器 gizmo。面板尺寸/位置唯一拼写是
  `EditorViewProducer::previewRect(imageRect)`（Tab 与 Layer 都从它取），所以 chrome 摆放与 Layer
  的输入排除不会各自漂移。**世界交互必须先问宿主**：`EditorViewportTab::isWorldPoint`（= `pickAt(point)
  == ViewportImage`）为 false 时 `screenToViewport` 直接返回 false、overlay candidate 也不成立，
  否则点在预览面板上会去 pick 面板下面的世界对象。收起预览 = 推 null 纹理（Collapsed），不保留空面板。
- `onImGuiRender` 编辑器 chrome shell（menu/toolbar/dockspace/viewport/debug/settings/project browser）已删除。
- GUIWorkbench 是独立 Feature Gallery（`FWorkbenchSurface` 挂 GUIApp）。左侧 rail 是
  分组 `UISelectableRow` 列表（`UIScrollViewport` + group `UIText`），**不是**垂直 `UITabBar`。
  `--start-page` / smoke 一律按页名 `selectPageByName`，禁止 `tabs[N]`。
  分组与页名：
  Diagnostics=`Render`；Controls=`Widgets`/`Inputs`；Layout=`Box`/`Hosts`/`ScrollSplit`；
  Paint=`Brush`；Text=`Text`/`Fonts`；Style=`Theme`；Overlays=`Menus`/`Dialog`；
  Interaction=`DragDrop`/`Enable`；Data=`Binding`/`Tree`/`Table`；
  Composition=`Dock`/`DSL`/`Editor`。
  旧名别名：`Layout`→`Box`，`Gallery`→`Binding`，`Modal`/`Interactions`→`Dialog`，
  `Unicode`/`中文测试`→`Fonts`，`RoundedRect`→`Brush`。
  页源码在 `Example/GUIWorkbench/Source/Pages/`，builder 仍从 `WorkbenchDemoPages.h` 转发。
  GameEditor 不把 Workbench 做成 dock tab。
  Dock 只把**当前选中 tab** 的
  panel widget `addDetachedChild` 进树；未选中的 panel 是 detached subtree。
- WidgetTree chrome teardown：`EditorSurface::shutdown` 必须在 compositor / VMA
  之前丢掉 tree、snapshot、viewport wrap；随后 `FontManager::clearCache()`，
  否则 RuntimeDefault atlas 会以 dedicated allocation 活过 allocator Destroy。
- 原 ImGui editor chrome shell（`onImGuiRender` / menu / toolbar / dockspace / viewport window）已删除。
  `TypeRenderer` / `FilePicker::render` / `FileExplorer::render` / editor ImGui texture
  bridge / GameRuntime `GuiSystem`/`ImGuiSystem` 已删。进程不再链接 `imgui-local`。
  不要再引入 ImGui-shaped `IGuiBackend` 或强迫 EditorSurface 走它。
- WidgetTree chrome 的 theme 走 `buildEditorTheme`（`GameEditor/UI/EditorTheme.h`），
  不要直接调 `buildWorkbenchTheme`。Workbench `text.header` 是 page title（`gui_type::kTitle` 28）。
  Editor 字号走 `editor_type`（`kHeader` 14 / `kBody` 12 / `kCaption` 11），由
  `buildEditorTheme` 写进 family 文案角色（`text.header` / `text.muted` / …）和
  `editor.<key>` overlay。Inspector 输入用 `editor.textfield` / `editor.dragfloat` /
  `editor.coloredit` / `editor.combobox`，**不要** `setFontSize`，也不要盖掉 family
  `textfield`（Designer canvas 仍是 gallery body 16）。Workbench 字号仍走 `gui_type`
  与 `textfield.compact`。Chrome 文案用 `text.header` / `text.muted` /
  `text.error` / `text.eyebrow` / `text.small` / `text.caption`，不要 `setColor` 字面量。
  Inspector ColorEdit / DragFloat 的内边距是 `FColorEditStyle::padding`（host row）/
  `FDragFloatStyle::padding`（ImGui FramePadding / ItemInnerSpacing），不要把
  `"R 0.60"` 画成一串居中字符串。`UIColorEdit` 是 `UICompoundWidget`：色板 + 每通道
  `UIDragFloat`（`_prefix` 为 R/G/B/A，前缀在格内左侧，不是旁边再叠一个 `UIText`）。
  通道 hover/drag 填色只走 `FDragFloatStyle`，不要在 `FColorEditStyle` 再复制一套。
  SV picker 仍是 paint leaf（两层 1D 顶点色），不是更多 widget。DragFloat
  `isHoverable()`，拖动时 `ResizeEastWest`。`UITextField` 同样有 `FTextFieldStyle::padding`，
  paint 把文字/caret clip 到 padded inner rect，并在焦点下横向滚到 caret；不要自适应字号，
  也不要为了长路径撑开 Fill 表单行。`TextureRef` 是「path 填满一行 + Browse + Show」，预览在下一行；
  禁止把缩略图、路径框、Browse 塞进同一行。Show 经 `EditorRevealAssetCallback`
  （`EditorLayer::revealInContentBrowser` → `EditorSurface::showContentBrowser` / `invokeTab("content-browser")`
  → `FileExplorer::setSelectedPath`）。`invokeTab` 必须先激活 **本窗已有实例**（window-root 或
  Level nested，用户把 Content 拖进内层 dock 之后仍在），禁止只查当前 dock 再 spawn 第二份。
  `propertyLabelFromPath` 的 `" / "` group 是 expander **路径**，
  Sampler Config 嵌在 Texture Slot 里，不要做成同级 collapsing header。
- Canvas `fill()` / `anchor({0,0},{1,1})` 的 `offset({x,y})` **只移动 min 角**，span 仍是父矩形全高/全宽
  （`resolveCanvasRect`）。菜单下的 Dock 必须用
  `insets(FMargin{left, top, right, bottom})` 收缩 fill，不能 `fill().offset({0, chromeTop})`。
  Hierarchy 过滤条与 tree 是 column 兄弟，不要 canvas overlap 再靠 insets 让位。
  Play/Stop/Simulate 是 Level owned tool tab（`play-toolbar`），工厂布局在 Viewport
  上方；不是 chrome strip，也不并进 Runtime Tools。
  合同：`WidgetLayoutTest.CanvasFillOffsetDoesNotShrinkTheChild` vs
  `CanvasFourSideOffsetsInsetTheChildWithoutAnExplicitSize`。
- `SelectionModel` 是 identity 选择源（`GUI/Binding/SelectionModel.h`）：selected 有序集合 + primary（空或不在集合外）+ hover/active/focus。不持有 Entity*。控件绑 `primaryRef()`；多选走 `add`/`toggle`；`replace` 批量同步。`EditorHierarchyTab` 在 `onAttached` 拉一次 Layer 选择，之后只订 `EditorLayer::onSelectionChanged`。
- `ActionMap` 是 identity 命令表（`GUI/Binding/ActionMap.h`）：菜单、快捷键、toolbar 都 `execute(id)`。`FActionChord::primary` 在 macOS 是 Cmd、别处是 Ctrl。WidgetTree 未处理的 KeyPressed 才走 shortcut；文本焦点下只匹配带 modifier 的 chord。`UIMenu::FItem::fromAction` 生成同一 execute 的菜单行。
- `UndoStack` 是 identity 撤销历史（`GUI/Binding/UndoStack.h`）：`push` 记录已应用的 undo/redo 闭包，不在 push 时调用 redo。`beginMerge`/`endMerge` 把同一 `mergeKey` 的连续 push 收成一步（拖动）；`UndoTransaction` 把嵌套 push 收成一步。栈不持有 Entity*。`edit.undo` / `edit.redo` 走 ActionMap（macOS Redo 是 Cmd+Shift+Z，别处 Ctrl+Y）。Inspector 拖动 `UIDragFloat` 在 `_onDragBegan/Ended` 开闭 merge；`setValue(..., false)` 是 sync，不进 undo。Gizmo / viewport 选择仍未接入。
- `PropertyGraph::project` 是反射字段 → editor field model 的入口（`PropertyAccessor::collectLeaves` + `PropertyProjectionRegistry`）。单实例 typed get/set/equals/validation 在 `Core/Reflection/PropertyAccessor`；`PropertyHandle` 只做多选 mixed、undo copy/restore、asset picker kind 和 owner callback。**写回必须标脏：** `project()` 对已注册 ECS 组件默认装 `IComponent::onEdit()` change hook（Mesh/Model/Billboard/Skybox/Environment/Terrain/SimpleMaterial 在 `onEdit` 里 `invalidate()`）。材质 projection 覆盖为 `onPropertyChanged(path)`；Transform projection 只装 `setPosition/setRotation/setScale`。禁止只 poke 反射字段、不丢 runtime cache——那是 imgui `DetailsView` 在 `hasModifications()` 后 `invalidate()` 的合同。`build()` 无 hook，不是 Inspector 入口。Inspector 对多选的 **交集** component 物化 `EditorAutoPropertySection`；`UIDragFloat` mixed 显示 "—"，编辑写回全部 instance，undo 按 instance 快照恢复。enum 字段走 `UIComboBox`；`.color()` 元数据的 `glm::vec3`/`glm::vec4` 走 `UIColorEdit`（非 color vec3 仍走 DragFloat）。`TextureRef`/`ModelRef`/`MeshRef` 走 path `UITextField` + Browse + Show；Browse 经 `EditorAssetPickerCallback`（widgettree：`EditorLayer::setAssetPickerHandler` → `EditorSurface::openAssetPickerDialog`；handler 缺失时 `FilePicker::open*` 仍作 fallback，无 ImGui `render`）。Show 经 `EditorRevealAssetCallback`（`EditorLayer::revealInContentBrowser` → activate `content-browser`）。`EditorAutoPropertySection` 把 `" / "` group path 展成嵌套 `UIExpander`（`Diffuse Slot` 含 `Sampler Config`），不要平铺成同级 header。`PropertyHandle::validationError` 转调 `PropertyAccessor` 的 manipulate spec 范围；`hasAssetResolveError` 对 failed resolve 画 error fill；`UIDragFloat`/`UITextField` `setError` 画 error fill。`UIImage` 对缺失 asset / `setResourceMissing` 画 error fill。没有 retained 可编辑字段的类型跳过。ImGui `DetailsView` / `TypeRenderer` 已删；`EditorInspectorTab` 是实体/component 唯一正式 Inspector UI，并显示 Game UI Entry 摘要 + Open in UI Designer。
- `EditorSurface` Content Browser：`EditorContentBrowserTab` 持有 `FileExplorer` 与 keyed window。
  `FileExplorer::contentGeneration()` 才是 catalog 身份（mount/dir/search/view/filter）；
  tick **禁止**每帧 `collectEntries` 拼 fingerprint。tick 只做 O(1)：generation +
  选中 path + scroll/viewport 像素。`collectEntries` 只发生在 keyed window rebuild。
  磁盘变更没有 watcher；navigate/search 才会看到新文件。选中纹理调 `inspectAsset`。
  行列图标走 `editor_icons` 资产路径，不再经 ImGui texture cache。
  construct 之后的 widget / explorer 指针是不变量：不要 `if (!_pathText)`；create/attach 失败应崩溃。
- `UITreeView` 在 `UIScrollViewport` 内只 paint 可见行窗口（`computeKeyedVisibleWindow` + `getPaintedRowCount`）；`EditorHierarchyTab` 用 scroll 包裹。flatten/hit-test 仍读全量可见行；无 per-row widget。**没有** UE `SListView` / `generateWidgetForItem`：行不是 child widget，图标走 `FNode::icon`（`FBrush`），展开钮走共享 `paintDisclosureButton`。行 leading 必须走 `layoutDisclosureLeading`（button + icon + title 同一条 HBox，垂直居中），不要按钮一套几何、文字另用整行高度。`FDisclosureSpec` / `EDisclosureKind`：`PlusMinus`（默认带框 +/-）、`Chevron`（同一条折线在 Y-down 下旋转 90°，不是 ASCII `>`/`v`）、`Glyph`（自定文字对）、`Image`（collapsed 图，缺 expanded 时 UV 翻转 180°）、`Hidden`（只留 icon）。`bindFilter` + `HierarchyFilter` 搜索框过滤节点；`setReorderable` + `FTreeReorderDragDropOp`：Hierarchy 走 `moveEditorHierarchyEntity`（`ui:` 条目仍不可重排）；UI Designer 树接到 `UIDesignerPanel::applyWidgetDrop`。结构变化走 `EditorLayer::onHierarchyChanged`，不在 Surface 轮询 fingerprint。ImGui `SceneHierarchyPanel::sceneTree` 已删（Phase 8O）；`SceneHierarchyPanel` 仅保留 viewport 选择总线 API。
- `UIExpander` 是 ImGui `TreeNode`：折叠 **widget 子树** 的 layout host（Details 组件段 / 属性组）。`ui::collapsingHeader` 只是 `ui::treeNode().setFramed(true)`（`TreeNodeFlags_Framed`），不是第二种控件。Header 默认带框 `+`/`-`；`setDisclosureKind` / `setDisclosureGlyphs` / `setDisclosureImages` 与 TreeView 共用 `FDisclosureSpec`；`setIcon` 画出 `mark icon Name`，`Hidden` 时只留 icon。不要用 `UITreeView` 做 Details 折叠：TreeView 是数据行列表，Expander 的 children 才是属性行。不要为 TreeView 造 per-row child factory。
- Viewport overlay：`FEditorViewportHostState` / `IEditorViewportOverlay` / `EditorViewportOverlayHost`；
  `EditorSurface::syncViewportHostState` + hover/focus overlay dispatch；gizmo 绘制不再经
  `GuiSystem`，而是在 viewport compose 中直接发 `Render2D` world-line/screen-handle。

## Style / Theme

- 机制在 framework：`UITheme` + `resolveThemeStyle` + generation token。值在 app：
  WorkbenchTheme（demo 壳）/ EditorTheme（GameEditor chrome）。
- **共享 chrome 值只有一个角色调色板**：`DefaultChromeTheme.h` 的 `tokens::FPalette`（`darkPalette(flavor)` / `lightPalette()` / `palette(bDark, flavor)`）+ `defineChromeStyles(theme, palette)`。surface 阶梯是 canvas < window < panel < raised（+ well），交互是 hover/pressed/selected/accent/accentFill，文字是 text/text2/text3/disabled，边是 borderSubtle/borderStrong/borderHover，状态是 success/error/errorFill/warning。加一个 key = 加一个角色，**不要**再写 `if (bDark)` 的第二份 block（旧 builder 每个 style 写两遍，正是 light/dark 漂移的来源）。`gui_chrome::tokens::surface(fill, radius, border)` 是「填充+圆角+描边」的构造入口。
- **flavor 是整张表，不是几个 key 的覆盖**：`EPaletteFlavor{OneDark, Neutral}`，`darkPalette(flavor)` 选 dark 表，`buildDefaultChromeTheme(bDark, flavor)` / `buildWorkbenchTheme` / `buildEditorTheme` 逐层透传。逐个 key 调色是外壳漂向「没人设计过的样子」的原因，而只盖三个 key 的「flavor」无法被当作一个 look 评审。light 只有一个表：flavor 是 dark 轴（one-dark 没有 light 对应物），不要在这里发明第二张 light 表。
- **accent 与 accentFill 是两件事**：`accent` 是**墨**（focus 环、caret、勾、tab 下划线），`accentFill` 是**底**（toggle 按钮、checked box、slider 已填充段）。一个蓝不可能同时干这两件事——在 dark 平面上够亮的蓝托不住白字；`error` / `errorFill` 同理。要写「带文字的强调填充」时用 Fill 那个。
- 圆角走 `tokens::radius`（kChip 3/kControl 4/kTab 3/kRow 4/kMenu 6/kCard 8），不要就地写字面量。斜坡刻意紧：22px 高的输入框配 6px 圆角是**网页**比例，工具 chrome 比网页更硬，同一个框降到 4px 才有边而不是融成胶囊。
- 对比度是契约不是口味，而且**表面和描边要克制**：文字要够（dark 下 text ≥12:1 / text2 ≥6.9:1 / text3 ≥4.4:1，按 canvas/window/panel/raised/well 五个静止平面取最小；hover/pressed 是瞬时冲刷，会更亮），但相邻 chrome surface 只差 ~1.06–1.13:1 —— 层级靠「刚好可辨的一级台阶 + 一条边」，不靠明度差；台阶一大，外壳就变成一堆深浅不一的方块。
- **输入控件是这条「克制台阶」规则的例外**：输入必须读作**下陷**，所以 `well` 比它所在平面低一整档（dark ~1.19:1 / light ~1.14:1），而不是像 chrome 平面那样只差一级。内陷**就是**输入的可供性；没有这个台阶，输入框只是另一个矩形，满屏输入框会显得平。
- dark 平面带真正的**蓝灰味**（蓝 ≈ 红的 1.3 倍），不是中性灰加一点蓝：贴黑的**中性**阶梯没有色相差可分辨，眼睛只剩明度，看起来就是「闷」。light 表相反，是灰底白内容，"raised" 表示 chrome 平面（比白内容**低**一档），不是浮起来。最深的平面别压到近黑，否则 dock/toolbar 像在外壳上挖的洞。
- 描边是**半透明白（light 用黑）**，不是不透明灰：这样一条 token 在任何平面上都成立。分两级，分开才是重点：`borderSubtle` 是 chrome 平面之间的发丝（~1.2:1，**按钮 / tab / 列表与树的选中行 / expander header 完全不画描边** —— 填充台阶已经把它们分开了，处处描边是「线框感」的最大来源）；`borderStrong` / `borderHover` 描的是**可交互**的东西（输入框、check box、popup、floating window），必须真能找到（对填充 ~1.63:1 / ~2.35:1）——**「虚」的观感绝大部分来自一条找不到位置的输入框边**。focus / dropTarget / error 这类瞬时状态留 accent 环。
- 字号走 `gui_type`（kTitle 20 / kHeader 16 / kBody 14 / kSmall 13 / kCaption 11，1:1 设备像素下的比例字体台阶）。atlas 只有一个字重，所以层级**只能**靠尺寸，别指望靠 weight。字体族走 `FTextStyle::fontFamily`（空 = 引擎 UI 主字面；非空 = 另一个已注册 family，如 `MONO_UI_FONT_NAME`），**不要**在控件里硬写 family 名。
- **色板（ColorEdit swatch）的边来自 style，不是控件字面量**：swatch 的 fill 是用户正在编的颜色，theme 无法预测，所以 `FColorEditStyle` 只管它**周围**的东西（`swatchBorderColor` / `swatchHoverBorderColor` / `swatchCornerRadius` / `swatchBorderThickness`）。不要再用「textColor 的半透明白冲洗」当描边：用户恰好选到同色时它什么也不是。走 `addRoundedSurface` 而不是 `addRectOutline`（后者跟不了圆角，会画成方框套圆角）。
- Resolve：稀疏 patch（JSON 键 = 反射字段名）overlay 到 theme 的 dense `TStyle`。`setStyle(TStyle)` 写全字段 = full freeze（不登记 theme 边）；`setStyleField` / Text·Border `setColor` 只盖出现过的键，其余 inherit，**必须**登记 generation + style Reactive。空 / null / `{}` = 无覆盖。
- Field impact：`lookupStyleFieldImpact` / `lookupStylePatchImpact` 用反射分类字段，而不是 per-type 表。`FBrush` → Paint+Resource；`fontSize` / `padding` / `minSize` → Layout；`FScrollBarStyle.width` 是 overlay 绘制厚度，不是 Layout；`UIScrollViewport` 把 children clip/hit 到 gutter 左侧，thumb 在 children 之后画，避免 list hover/selection 盖住滚动条。`setStyle` / `setStyleField` / `clearStyleField` / `clearAuthoredStyle` 默认走 catalog；`UIText::setFontSize` 等仍可显式覆盖。`bResource` 是 metadata；异步就绪不走 `invalidateProperty`。Font 由 `FontManager::resourceRevision()` 在 `WidgetTree::buildSnapshot` 消费：revision 变化则整树 `markLayoutDirty(ResourceReady)`（嵌套 fill 容器不能 skip 过期文字度量）。Texture 走 tree 上的 `FGuiTextureCatalog`（path-keyed `Reactive` revision）：`bind(path)` 订阅当前 paint widget，ready/fail 只 `markPaintDirty` 该 path 的订阅者。`UIFrameBuildContext.generation` 只表示 resolver **身份**被换掉（测试/host 换 source），日常 ready **不** bump 它、也不清全树 paint cache。Host 只 `flushPendingGlyphs`，不要再 `invalidateSubtree`。
- Visual fill matrix：`composeVisualFlags` + `FVisualChrome` + `resolveVisualFill` 是 exclusive 优先级（Disabled > DropTarget > Error > Pressed > Selected+Hovered > Selected > Hovered > Focused > Normal）。`visualChrome(style)` 覆盖 Button / SelectableRow / CheckBox / ComboBox / MenuBar / Tab / MenuItem / TableGrid / TreeView 行填充。CheckBox 的 checked 与 Tab 的 selected 映射为 Selected，且 Selected+Hovered 回落到 Selected（保持原“选中盖住 hover”）。Table/Tree 未选中且未 hover 的 normal 是透明刷，paint 仍按 alpha 跳过。`UITextField` / `UIDragFloat`（含 ColorEdit 通道）不走这套 matrix，但 **必须** 有自己的 `hoveredFill` + inset `borderColor`（self-clip 会吃贴边 1px stroke）。TextField 文字必须 clip 到 padded inner，不要画穿 chrome。
- Dock / floating chrome：`kDockContentInset`（1px）是 leaf `DockContent` 与 floating `_content` 的 padding，给 editor 控件 outline 留边；不要给每个 DragFloat 再加 margin。窗口底边裁切不是再加 padding 能解的：EditorSurface Dock 必须 insets 到 `kChromeTop` + `kChromeInset`，否则 fill dock 比窗口高一段 chrome。
- Key catalog：`YA_GUI_STYLE_CATALOG` / `StyleKey::*` 是 theme key + `TStyle` 的单一词汇。`lookupStyleKey` 校验 `define` / `setStyleKey` / document deserialize；未知 key 与类型不匹配记入 `StyleCatalogDiagnostics` 并 `YA_CORE_WARN`，不拒绝写入。空 key 表示不查 theme。`editor.<key>` 是同一词汇的 GameEditor overlay，不是第二套机制。`canvas` 角色 key 只表示「无 chrome 的 canvas host」，canvas 本身不 paint。
- 控件 paint/layout 读 `UIStyledWidget::resolvedStyle()`（dense cache）。merge（theme base + 稀疏 patch）在 dirty/recompute 时发生，不在每帧 paint 热路径。`resolveWidgetStyle` 仍是无缓存计算路径，给测试断言和非 `UIStyledWidget` 节点（如 ColorEdit 色板）用。cache 不落盘。
- 高频路径是实例 `setStyle` / `setStyleField` / `setStyleKey`（DSL 基类 builder 已暴露）；切 theme 是低频目录切换。未盖满的控件在切皮肤时未覆写字段跟着变。
- `_styleKey` 在 `UIElement` 上反射；稀疏 patch 经 `YA_GUI_AUTHORED_STYLE_IO` 虚函数写入 UIDocument 的 `_authoredStyle`（mixin 字段不能 `YA_REFLECT_FIELD`，MI 偏移不对）。缺键 = inherit；旧文档的全字段对象仍是 freeze。`FBrush`/`F*Style` 走运行时反射，merge 用 `deserializeProperty`。
- `FBrush`：纯色 = 无 resource + tint；Image 整张拉伸；NinePatch/Border 按 `margin`（纹理 px，1 tex px = 1 logical px）切成最多 9/8 个 snapshot sprite，compose 经 `uvScale`/`uvOffset` 透传。无纹理尺寸时退回整张拉伸。`sliceBrush` 是纯函数。
- **Surface 模型（style 的可视单元 = 一个刷子）**：纯色 `FBrush` 还带 `cornerRadius` 与 `borderColor`/`borderThickness`，即「填充 + 圆角 + 一像素描边」是一个值。`addBrush` 对它走 `addRoundedSurface`（外圈 border 色 + 内缩 fill，SDF round-rect 分支），所以一个控件每个状态只挑**一个** brush，hover 抬起填充时描边自动跟着抬。控件**不再**自持 `borderColor`/`outlineColor` 字段，也不再自己 `addRectOutline`（描边跟不了圆角，会画成方框套圆角）。由控件自身尺寸推导的几何（switch knob、radio dot 的圆）由控件自己算（`extent.y*0.5f`），因为 theme 不可能知道控件尺寸。
- 例外：**image / nine-patch / border 刷子忽略 `cornerRadius`/`borderColor`**（形状来自资源）；给它们再叠一圈主题描边就是关于形状的第二个真相。
- `UIBorder` paint 只读 `resolvedStyle().fillColor`（含主题圆角/描边）；`_cornerRadius` 是单实例覆盖（`instanceEditable`），叠在 style 的 radius 之上。无 theme 时 `_color` 是 fillColor fallback（与 `UIText` 的 `_color`/`_fontSize` 相同）。内容图不走 Border 字段：`overlay().child(border()).child(image())`。Widgets 禁止 include / 调用 `AssetManager`，禁止 paint 里 `loadTextureSync`。`ya-gui-closure-test` 必须仍不链 AssetManager。
- 无 theme / 缺纹理 / 延迟就绪的 GPU 输入就是 snapshot：`UIImage` Pending/miss 画 `placeholderFill`，catalog Failed 或 `setResourceMissing(true)`（live RT 缺席）画 `errorFill`。不要为「路径尚未加载」调 `setResourceMissing`。
  产品 host：`tree.setTextureSource(&gameUITextureSource())`（AssetManager lookup + async `loadTexture`，onReady → `catalog.notify(path)`）。Workbench：builtin source（lookup 即 Ready，`requestLoad` 空操作）。
  Widgets 禁止 include / 调用 `AssetManager`，禁止 paint 里 `loadTextureSync`。`ya-gui-closure-test` 必须仍不链 AssetManager。
  Headless host 与 windowed compose 消费同一份 packet；`setDpiScale` 与 `uiScale` 正交折叠（`EditorInputContractTest`）；windowed `--gpu-shot` 像素门禁走 `Script/automation/gui/run_workbench_gpu_parity.py`。
- 族 key：`panel` / `button` / `text` / `menubar` / `tab` / `split` / `scrollbar` /
  `dock` / `floating` / `image` / `popup`。角色 key：`panel.window` / `panel.canvas` / `panel.sidebar` /
  `tab.dock` / `tab.sidebar` / `text.header` / `text.muted` / `text.error` /
  `text.eyebrow` / `text.small` / `text.caption` / `menu.panel` / `tooltip` / `drag.ghost` / `drag.source` /
  `drag.target`。表单 key：`tree` / `textfield` / `textfield.compact` / `menu` /
  `selectable` / `dragfloat` / `checkbox` / `combobox` / `slider` / `table` /
  `spinbox` / `radio` / `coloredit` / `searchcombo`。
- `editor.*` 前缀只用于 GameEditor 显式覆盖，不是第二套词汇。
- Shell 控件（Border/CanvasPanel/Button/Text/MenuBar/Tab/Split/Scroll/Dock/Floating）和已接线的
  表单控件（TreeView/TextField/Menu/SelectableRow/DragFloat/CheckBox/ComboBox/
  Slider/TableGrid/SpinBox/Radio/ColorEdit chrome/SearchCombo）以及 Image 占位 /
  Popup 遮罩 / DragDrop tile paint 时读 `resolvedStyle()`；几何（rowHeight/indent/thumbSize）留在 widget。
  实例覆盖走 `setStyle(TStyle)`（freeze）或 `setStyleField`（单键 inherit）；Text/Border 的 `setColor` 只 overlay 颜色（Paint 粒度），字号等跟 theme。
  列表行标签走 `text` key，不要 `setColor` 冻色。布局宿主（CanvasPanel/Container/Overlay/SizeBox/DockFloatingHost）无 chrome paint。

## Host（ya-gui-host）

- 顶层命名：`GUIApp` 是 standalone GUI 的装配层（当前一个 primary
  `GUIWindowHost`）；`GUIWindowHost` 是主窗一窗口一 tree / SDL window / presenter /
  pointer context 的真实 owner。extra 窗走 `GUIWindowManager`（实现
  `IGUIWindowCoordinator`）：每个 `GUIWindowSession` 拥有 NativeWindow +
  WidgetTree + snapshot + 可选 `IRenderSurfaceContext`。新代码只使用 `GUIApp` /
  `GUIWindowHost` / `IGUIWindowCoordinator`，不得新增或恢复
  `GUIAppHost` 兼容别名，也不得平行再造一套 window manager。
- Window chrome 走 `GUIWindowChrome` capability API（`EWindowChromeMode` =
  Native / Hybrid / ClientDrawn）。macOS 默认 Hybrid（full-size content + traffic
  lights / safe-area）。消费者用 `queryWindowChromeLayout` / `FWindowChromeLayout`，
  不要用生 `queryWindowChromeInsets`：SDL safe-area 常为 0，Hybrid 仍要 floor
  traffic-light 最小宽（78）和 title 高（28）。标题带里 **page TabBar 整条
  layout rect** 必须 `updateWindowChromeTitleClientHits` 为 Client；只有
  trailing drag gutter 是 Drag（macOS 上 Drag 映射为 `ENativeWindowHitResult::Normal`，
  由 Cocoa monitor 做 `performWindowDragWithEvent`）。不要把 tab 行空区当成
  窗口 caption，否则拖 tab 会整窗跟着走。macOS Hybrid 的透明 titlebar **不会**
  把 Drag 交给 AppKit 双击 zoom；`GUIWindowChrome` 在 Hybrid/ClientDrawn 上装
  Cocoa local monitor，对 **gutter Drag** 的双击执行 `zoom:`（尊重
  `AppleActionOnDoubleClick`）。Windows 走 HTCAPTION，不要再在宿主里 toggle。
  双击最大化只来自顶部 tab bar 的空条（`UITabBar::_onStripDoubleClick`）或
  gutter Drag（chrome API / Cocoa monitor）；tab 按钮和 dock leaf tab bar
  不得 zoom。菜单在 title 带下方，不要塞进 titleContent。
  `GUIWindowChrome.cpp` / `GUIWindowPlacement.cpp` 只谈 `INativeWindow` 与
  `Os::display*`；`SDL_*` 留在 `SDLNativeWindow` / `Core/Os`，AppKit 留在
  `GUIWindowChromeCocoa.mm`。`applyWindowChrome` 属于 GUI Host，resize/move
  后必须重 apply 才能更新 hit-test。Dock、EditorSurface、tab spawner 只读
  insets / layout，不得 include NSWindow / Win32 non-client API。完全 borderless
  必须显式请求。
- 生命周期：init → run（SDL event → WidgetTree dispatch → snapshot → compose → present）→
  shutdown。resize 只在帧边界重建 presentation 资源。
- `GUIHeadlessHost` 是同一 AppKernel/WidgetTree/delegate 合同的无窗口变体：只产生
  immutable `UIFrameSnapshot`（可由 callback 检查/落盘），不创建 SDL window、RHI、
  swapchain 或第二套 run loop。它用于 automation、结构断言与 windowed/offscreen
  交叉取证。
- 诊断：`--dump-snapshot=path --dump-frame=N`（CPU 侧 BMP 光栅化快照）、
  `--dump-snapshot-json=path --dump-frame=N`（snapshot 几何/clip/text JSON + digest）、
  `--gpu-shot=path --gpu-shot-frame=N`（GPU readback BMP）。内置纹理 resolver
  （`builtin/white|black|multipixel|checkerboard`）供 image 控件在无资产系统时使用。
- Glyph flush：`flushPendingGlyphs` 仍在 snapshot 之后、command recording 之前（Core Rule 6）。缺失字形的 layout/paint 由下一帧 `WidgetTree` 消费 `FontManager::resourceRevision()` 驱动，host 不要再 `invalidateSubtree`。Texture 就绪靠 bump `UIFrameBuildContext.generation`。
- **teardown 铁律**：任何持有 GPU 资源的成员（readback buffer、shader storage、widget tree、
  command buffers、presentation targets）必须在 `delete render`（VMA 销毁）前释放
  （见 memory：VMA teardown 顺序坑）。

## 编辑器内嵌（已废弃）

- 旧路径曾把 WidgetTree 合成到离屏 RT 再 `ImGui::Image`。那些 panel 类型已删除；
  GameEditor chrome 只走整窗 `EditorSurface`，不要再扩离屏桥。
- `EditorToolSurfaceCompositor` 仍保留 shutdown，但 presentation 不再 compose
  workbench 离屏图。

## 构建 / 测试

```bash
make b t=GUIWorkbench && make r t=GUIWorkbench          # standalone demo
make r t=GUIWorkbench ARGS="--smoke-actions"            # 端到端自动化
make run t=HelloMaterial / make run-editor t=HelloMaterial
xmake b ya-gui-closure-test && xmake r ya-gui-closure-test
make test-gui                                            # closure + widgets + workspace
```

macOS / MoltenVK convergence gate (must be run on macOS, not emulated from a
Windows runner):

```bash
python3 Script/gui_convergence_macos_validation.py
```

## 控件交互契约（编辑态 / 弹出态规则）

这些是 2026-08-18 Gallery 交互验收轮沉淀的硬规则，写新控件时逐条自查：

1. **编辑模式不吞底层交互**：进入编辑态（_bEditing）后，控件原有的业务交互
   （SpinBox 的 +/- 步进、拖拽、打开菜单）必须仍可达——编辑态 press 分支要先
   commit/cancel 编辑再执行原交互，绝不能 `return false` 把 press 吞掉。
2. **弹出控件的 dismiss 必须释放交互残留**：菜单/弹出被真实关闭（外部点击/Esc/
   选挑）时，dismiss 回调要一次清完——filter 清空 + 主动 `setFocus(nullptr)`
   （否则控件继续画 '(type to filter)' 等占位态，用户要再点一次才恢复）。
3. **可见内容集变化必须同时标 Layout + Paint**：Reactive 的 Layout 粒度通知只保证重排；
   若重排后排列 rect 不变（固定高度树/表），增量 paint 缓存会继续画旧内容。
   `setExpanded/toggleExpanded` 必须 `markLayoutDirty()` + `markPaintDirty()`。
   首次 expand 前 `isExpanded` 必须创建 Layout 粒度 Reactive，否则 Auto 父级高度不涨。
   任何影响可见行/可见项的状态变化都按此处理。
4. **弹出刷新 ≠ 真实关闭**：「关旧开新」的刷新路径会触发旧菜单 dismiss 回调；
   回调里的清理逻辑（清 filter 等）必须用刷新标志（_bRefreshingMenu）隔离，
   只在真实关闭时执行。
5. **过滤/搜索的自动展开是一次性的**：过滤变化时自动展开匹配链（记录
   `_lastFilterApplied` 防重复），之后手动折叠/展开必须仍然生效——过滤
   不能持续强制展开（TreeView「filter 激活时箭头失效」即此坑）。清空
   过滤永不收拢任何东西。
6. **demo 的约束性行为要有可见文案**：选择性 accept（drop target 谓词）、禁用
   条件等「看起来像 bug」的设计，必须在控件 label / 页面说明里写明
   （如 'Zone B: only payload.2'）。
7. **Hover chrome 必须 `isHoverable()`，且 hover owner 的 `hitTestSelf` 含指针**：
   WidgetTree 只对 hoverable 节点发 enter/leave。DragFloat/SpinBox/ColorEdit 色板与通道
   不声明时，hover 填色永远不出现。`hoverOwnerAlongPath` 还会要求 `hitTestSelf(point)`：
   Expander 只把 header 当 hover 区，body 里的 label 不能点亮 header（或外层
   CollapsingHeader）。Button 仍会从 text child 冒泡，因为 button 的 hitTestSelf
   是整块 layout rect。
8. **Modal 只独占输入**：`_bModal` 让鼠标/键盘无法穿透到该 overlay 之外（直到
   OK/Cancel/Esc），外点消费但不关窗。框架不画遮罩、不提供 dim 开关。要挡住/
  模糊底下，在 dialog chrome 下叠一张 fill 的 CanvasPanel/Image（`HitTestInvisible`
  以免抢 hit），和 UE / Qt / Win32 modal window 一样。
9. **字符串匹配默认 ignore-case**：SearchCombo / TreeView filter 走 `StringMatch`，
   默认 IgnoreCase；要大小写敏感再显式 Sensitive。
10. **Presenter 必须同步 SelectableRow**：行上的 `_bSelected` 不是数据源。
    `_onSelect` 必须循环 `setSelected` 全部行，否则点了看起来没选中。
11. **离页必须拆 Popup 附着**：Workbench `FPage::leave` / `setPageLeave` 拆掉该页
    挂到 Popup 层的 host（Dock floating）。Content 层换页时 Popup 层会活下来。

## 人肉测试前的自动化验收

按 `Example/GUIWorkbench/Scenarios/` 的 jsonl 回放做第一道闸（跑法：
`xmake run GUIWorkbench --start-page <Page> --scenario <abs path> --scenario-dump-dir <dir>`，
exit 0 = 全 checkpoint 过）。页名必须是分组 rail 上的 Gallery 页（`Inputs`/`Tree`/`Brush`/`Dialog`…
不要再写已删除的 `Gallery`/`Layout`/`Modal`，除非有意走别名）。tooltip dwell 用
`--scenario-render`（`gallery_p1.jsonl`）。写验收场景时的覆盖要求：

1. **状态 × 交互组合矩阵**：有编辑态/弹出态的控件，必须覆盖「进入状态 → 各交互」
   的关键项（编辑态 × {键入, Backspace, Enter, Esc, +/- 点击, 外部点击}；菜单开 ×
   {过滤输入, 选挑, Esc, 外部点击}）。单条 happy path 会漏掉状态组合 bug
   （SpinBox 编辑态吞 +/- 即如此）。
2. **焦点生命周期断言**：弹出类控件关闭后断言 `focusPath` 不含该控件（或为空）——
   只断言 popup 结构开合不够（SearchCombo 两次点击 bug 即漏在焦点上）。
3. **数据副作用断言**：断言要锁到值级（filter 字段、visibleRows 计数），不能只锁
   控件存在/结构（filter 被刷新清除的 bug 就溜过了只查 popup 结构的断言）。
4. **双布局变体**：TreeView/Table 的行集变化测试，autoSize 和固定高度两种布局都要
   覆盖——前者 rect 变化掩盖了「Layout≠Paint」漏画，后者才暴露。
5. **渲染级验证走真机**：scenario 模式不渲染帧（buildSnapshot 不跑），G2 校验帧
   （漏标脏告警）和像素验证必须用真机。真机走 `control` 入口，别自己拼启动命令：
   `python3 Script/ya.py control start --project <p>`（有则接入、无则起一个且带墙钟上限）
   → `control call` / `control mcp` → `control stop`（详见 `ya-build` skill）。行为断言归
   scenario，渲染断言归真机，两条线分工。
6. **环境随机崩溃重试**：GUI 反复启动偶发 init 崩溃（0xC0000005，swapchain 创建
   阶段，与场景内容无关）。自动化脚本对场景运行加重试（每场景最多 4 次，间隔
   1.5s），不要在单次失败上误判回归。
