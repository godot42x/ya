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

## 明确不做

- 不引入 UMG 那一整套 UWidget / UUserWidget 平行类型。
- 不引入 Node2D / 2D 世界渲染器：当前场景渲染是 camera-based 3D only。
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

## Phase 0 — 运行时 UI 有明确的 tick 驱动（已落地）

缺陷：`GameUIHost::buildSnapshot()` 只 layout + paint；`WidgetTree::tick` 在全仓库
只有编辑器 chrome 一处调用（`EditorSurface::tick`）。运行时挂载的树因此从不推进
behavior / tween 状态，带动画或自刷新的 HUD 会永远停在第一帧。编辑器的 designer
preview tree 同样只 buildSnapshot、从不 tick，是同一类缺陷的另一处。

已落地：

- `GameUIHost::tick(dt)` 转发到 `WidgetTree::tick`。
- 驱动点在 `RuntimeRenderContext::buildGameRenderFrame`，与 `buildSnapshot()` 成对，
  语义是「被展示的树必须被推进」。放渲染侧而非逻辑侧：暂停时逻辑被 gate，但暂停帧
  仍然提交，暂停菜单自身的输入反馈/动画要继续跑。
- 门禁 `GameUIHostTest.TickAdvancesMountedTreeBehaviors`：挂载本身不 tick、
  buildSnapshot 不 tick、`host.tick()` 才计数。

未完成：designer preview tree 的 tick 驱动（与 Phase 3 的预览渲染一起做）。

## Phase 1 — Scene 存文档引用，不存内联文档

验收：

- SceneWidgetEntry 只携带 documentPath，不再持有 shared_ptr<UIDocument>。
- UIDocument 有磁盘形态（.yaui），由 UIDocumentStore 唯一持有 live 实例。
- 挂载路径、编辑器、Inspector 都从同一个 store 取文档。
- 编辑器保存写 store（立即对层级/运行时可⻅）+ 写盘。
- 内联文档的生产者（scripts 的 ui.add_to_scene）没有保留价值，直接删除。

## Phase 2 — 从 GameUIHost 拆出 Scene UI composition

验收：

- 场景激活/去激活、entry 遍历、overrides 应用能从宿主里单独测试。
- GameUIHost 退回「presentation 适配器」：窗口 rect / 输入 / snapshot。

## Phase 3 — 视口 Game UI 预览

验收：

- viewport 有三种明确模式：SceneOnly / SceneWithGameUI / GamePreview。
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
