# Game UI Authoring

## 目标

让 Game UI 有一套**独立的文档资产 + 场景挂载 + 视口预览**链路，而不是把 UI
子树内联进 Scene。目标形态（用户已确认的方向）：

```text
Scene
  └─ SceneWidgetEntry    { documentRef, rootSlot, zOrder, autoMount, overrides }
        ↓ resolve
     UIDocument asset          可独立打开 / 编辑 / 保存
        ↓ instantiate
     WidgetTree                预览树 / 运行时树
        ↓ buildSnapshot
     UIFrameSnapshot           → compose
```

独立 UI 编辑器保持不变；场景视口可以看到 Game UI（Unity Game View 那种预览），
但不把 UI 变成 2D 场景节点。

## 与 Scene 2D 计划的关系

本计划只负责 Game UI 的 UIDocument、WidgetTree、mount、designer 和 UI compose；
Scene 中的 authored 2D sprite、正交 camera 和 Scene runtime sprite-rendering workload 由
.agent/plan/scene-2d-world-and-game-ui/ 负责。两条线共享 View/Present 时序，但不共享
Scene/ECS 与 WidgetTree 数据结构。

## 明确不做（本计划边界）

- 不引入 UMG 那一整套 UWidget / UUserWidget 平行类型。
- 不在本计划内引入 Node2D、Transform2D、Camera2D 或第二套 Scene 树。若需要 2D 世界对象，
  统一按新计划使用现有 Node3D/TransformComponent + CameraComponent 正交模式 + Scene sprite workload；
  不把这条能力偷偷塞进 Game UI。
- 不把屏幕空间 UI 挂进 3D Scene hierarchy / ECS。
- 不造统管 UI+Scene+Editor+Viewport 的 UIManager，不造中心事件总线。
- 世界空间 UI（血条 / 名字牌）是**后续独立渲染特性**，不是本线的场景模型改动。

## 分层

```text
GUI Framework   WidgetTree / UIDocument / UIDocumentStore / Layout / Snapshot
Scene           SceneWidgetEntry（只放文档引用 + 挂载意图）
GameRuntime     GameUIHost / SceneUIComposition / snapshot 生成
GameEditor      EditorUIDesignerSession / 视口 Game UI 预览 / viewport compose
```

## Phase 0 — 运行时 UI 有明确的更新驱动（已落地）

缺陷：`GameUIHost::buildSnapshot()` 只 layout + paint；`WidgetTree::tick` 在全仓库
只有编辑器 chrome 一处调用（`EditorSurface::tick`）。运行时挂载的树因此从不推进
behavior / tween 状态，带动画或自刷新的 HUD 会永远停在第一帧。编辑器的 designer
preview tree 同样只 buildSnapshot、从不 tick，是同一类缺陷的另一处。

已落地：

- `GameUIHost::update(FUIFrameClock)` 转发到 `WidgetTree::tick`。驱动点仍在
  `RuntimeRenderContext::buildGameRenderFrame`，与 `buildSnapshot()` 成对，语义是
  「被展示的树必须被推进」。放渲染侧而非逻辑侧：暂停时逻辑被 gate，但暂停帧仍然提交。
- 时间策略不再是一个说不清语义的 `dt`：`FUIFrameClock{gameDelta, realDelta}` 由调用点
  一次算出（暂停只影响 gameDelta），`EUIUpdateClock` 由 host 声明它读哪一个。默认
  RealTime，保持上一版行为（暂停菜单继续动画）；纯 gameplay HUD 可改 GameTime 随游戏冻结。
- 门禁 `GameUIHostTest.UpdateAdvancesMountedTreeBehaviors`（挂载与 snapshot 都不推进）与
  `GameUIHostTest.ClockPolicyDecidesWhetherPausedFramesAdvanceTheTree`（同一暂停帧下两种
  策略得到不同累积时间）。注意门禁测的是「推进了多少秒」而不是「访问了几次」：暂停帧
  仍然拜访树，只是给 0 秒。

未完成：per-subtree 时钟（暂停菜单与 HUD 同帧不同速）——今天一个 host 一棵树，先不做；
需要时再拆树或加 per-widget clock。designer preview tree 保持不 tick（Authoring，见 Phase 2）。

## Phase 1 — Scene 存文档引用，不存内联文档

验收：

- SceneWidgetEntry 只携带 documentPath，不再持有 shared_ptr<UIDocument>。
- UIDocument 有磁盘形态（.yaui），由 UIDocumentStore 唯一持有 live 实例。
- 挂载路径、编辑器、Inspector 都从同一个 store 取文档。
- 编辑器保存写 store（立即对层级/运行时可⻅）+ 写盘。
- 内联文档的生产者（scripts 的 ui.add_to_scene）没有保留价值，直接删除。

## Phase 2 — 从 GameUIHost 拆出 Scene UI composition

## Phase 2b — 三棵树三种模式，各自拥有生命周期（已落地）

同一个 `.yaui` 会被实例化多次，而且不能共享，因为状态不同：

```text
UIDocument                  持久化结构 / 默认字段 / 布局 / 子节点
EditorUIDesignerSession     一个文档的编辑：选择 / 拖拽 / 缩放 / dirty
EditorGameUIPreview         当前 Scene 的 mounts：layout / paint / 几何，跨帧复用
GameUIHost                  运行时实例：输入 / focus / capture / click / 动画 / 游戏状态
```

三种模式：Authoring（designer，不 tick 不 dispatch）、Runtime（GameUIHost）、Interactive
Preview（未做，需要时显式增加，带自己的 clock）。

已落地：

- `EditorUIDesignerSession` 的 Authoring 契约写进头注释：预览树是私有的，没有
  `tick` / `dispatchEvent` 入口，画布点击只能选中和拖拽，碰不到 `UIButton::onClick`。
  门禁 `EditorUIDesignerSessionTest.CanvasPickingSelectsAButtonWithoutRunningItsClickHandler`。
- 场景 UI 预览从「每次 compose 新建 WidgetTree」改成持久宿主
  `EditorGameUIPreview`（GameEditor 持有，`EditorViewportCompositor` 使用）。重建条件
  是 mount 输入而不是帧：场景、mount 列表、被挂文档的 revision、预览 extent。
- `UIDocumentStore::revision(path)`：消费者要缓存派生结果时必须能分辨「同一个文档」和
  「文档被改过」；比对 shared_ptr 身份会漏掉原地改写。
- 门禁 `EditorGameUIPreviewTest`：稳定场景多帧只实例化一次；文档编辑触发重建；
  extent / 场景变化触发重建；预览不 dispatch 输入。

验收：

- 场景激活/去激活、entry 遍历、overrides 应用能从宿主里单独测试。
- GameUIHost 退回「presentation 适配器」：窗口 rect / 输入 / snapshot。

## Phase 3 — 视口 Game UI 预览

验收：

- viewport 有三种明确的 UI 组合模式：SceneOnly / SceneWithGameUI / GamePreview；这不是
  World2D/World3D authoring mode。World2D authoring 由新计划的 camera/tool profile 负责。
- Game UI 与编辑器 overlay（gizmo / 选框）是两条独立图层，不共用一个列表。
- 场景预览与编辑器 canvas 预览仍然是**两棵树**，不合流。

## Phase 4 — UI 资产浏览与打开

验收：

- UI 文档在 Content Browser 可见、可打开、可新建。
- 新建文档落到 contentDir，并被 entry 引用。

## Phase 5 — 交互预览与动画

在 Scene -> mount -> WidgetTree -> snapshot -> viewport 稳定之后再谈输入预览、
UI runtime state、动画轨道、binding。

## 当前边界

- 阶段一未做：UI 资产浏览器（阶段四）；因此场景 entry 的文档路径目前只能由
  脚本/测试构造，编辑器侧只能「打开已存在的 entry 文档」。
- overrides 语义保持现状（场景级字段覆盖），instance / appearance 边界的
  正式拆分留到阶段四。
