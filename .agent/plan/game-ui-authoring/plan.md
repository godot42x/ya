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
  **已被取代（2026-09-28，`game-ui-script-framework` F0 / D1）**：暂停不再跳过整段逻辑，
  驱动点移到 `GameRuntimeTickOrchestrator::tickUILogic`；渲染侧只留 `setPresentation` +
  `buildSnapshot`。
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

## Phase 6 — UI Designer 可用性（先追平 origin/main 的 ImGui 基线）

2026-09-29 用户判断：保留模式设计器「基本无法用」。第一目标不是追 UMG，而是追平
`origin/main`（`91ded16e`，`Panels/UIDesignerPanel.cpp`，ImGui）已经能用的那一版，
再在同一数据模型上补撤销。撤销模型用户已定：**文档快照**。

### 基线（origin/main 能做、现在不能做或变差的）

| 能力 | origin/main（ImGui） | 现在（retained） | 退化原因 |
| --- | --- | --- | --- |
| 新建 | New 弹出注册表类型列表选根类型 | `newDocument("panel")`，类型不存在，必失败 | 硬编码短名 |
| 打开 / 另存 | Open .yaui（文件选择）、Save As（路径） | 只能从内容浏览器双击打开；无 Save As，untitled 保存不落盘 | 迁移时没带过来 |
| 布局属性 | 检查器反射出 position / size / anchor，直接改 | 看不到任何布局字段；非 canvas 父（row / column）的子控件完全无法调 | `gui-anchor-to-slot` CP5/CP6 把几何挪到父侧 slot，CP6 写了「要求 typed slot 编辑」，但 slot 编辑器从未实现 |
| 控件属性 | `renderReflectedType` 全量 | `PropertyGraph` + `EditorAutoPropertySection`，只出叶子类型 | 需逐类型核对缺哪些行 |
| 层级 | 点选、右键 Delete、拖拽 Before/Into/After + 落点反馈 | 点选 + 拖拽重排；无右键菜单 | 迁移时没带过来 |
| 预览尺寸 | 跟随视口 | 固定 800×600 | `applyPreviewExtent` 已成死代码并删除 |

两版共同缺陷：结构 / 几何编辑不进撤销；检查器改属性不标 dirty、不发布到 store；画布拖完只标 dirty
不发布；Palette 会往叶子控件下塞子控件。

### 对象模型与 owner（本 phase 不变的约束）

- `EditorUIDesignerSession` 是 UI 文档的**唯一写入口**。画布 / 层级 / Palette / 检查器只调它的编辑
  API，不直接改预览控件后再「顺便」同步。
- 一次编辑 = 一次事务 `commitEdit(label, mergeKey)`：记编辑前后的 `UIDocument` JSON，推入该文档
  `EditorDocumentSession::undo()`，标 dirty，发布到 `UIDocumentStore`（内存，不写盘）。
- 撤销 / 重做 = 用快照重建预览树（`installPreview`），选择按子节点路径恢复。撤销闭包只持有
  JSON 与路径，**不持有预览控件指针**（重建后指针全部失效）。
- 连续拖拽（画布移动 / 缩放、检查器数值拖动）一次手势合并为一步：`UndoStack::beginMerge/endMerge`
  + 同一 mergeKey。
- 检查器写预览控件后，由属性绑定的 change hook 回调会话提交，不给检查器第二条写路径。
- 设计器仍是 Authoring 模式：不 tick、不 dispatch、不跑脚本（`designer_is_authoring_mode` 门禁不变）。

### Checkpoints（每个一次提交）

本轮（用户 2026-09-29「先做 designer 内部编辑 UI 相关的，完成这部分的闭环」）顺序为
U2 → U3 → U4；U1 文档生命周期排在本轮之后。

- **U1 文档生命周期**：New 选根类型（只列容器类型）；Open（文件选择 `.yaui`）；Save As（保存对话框，
  默认 `Content:UI/`）；untitled 的 Save 转 Save As。按钮进设计器宿主工具栏。
  门禁：`EditorUIDesignerSessionTest` 新建 `engine.panel` 可实例化；Save As 写盘且 store 可 resolve。
- **U2 文档事务与撤销**：上面的对象模型落地；Palette 添加、删除、层级重排、画布移动 / 缩放、检查器
  改属性全部走 `commitEdit`；Ctrl+Z / Ctrl+Shift+Z 走 UI 页的 `edit.undo/redo`。
  快捷键与 Edit 菜单按**当前页**的 root 解析：action id 是与页无关的动词（`edit.undo`、
  `selection.delete`…），每个页 root 各自绑定；`EditorSurface::activePageRoot()` 由选中页签的
  spawner `ownerEditorId` 找 root。选择不是编辑，但 `select()` 同时更新「下一次编辑撤销回到的选择」。
  撤销闭包按会话生命期 + 文档 id 校验，设计器已销毁或已换文档时为空操作（该步照常出栈）。
  门禁：每种编辑 → undo → redo 后文档 JSON 与选择路径一致；一次拖拽只产生一步；场景挂载的 HUD
  随编辑刷新（store revision 前进）。
- **U3 Slot 检查器**：检查器在控件属性之上加「Slot（Canvas / Box / Overlay …）」分组，按父布局的
  slot 类型投影。做法是让 slot 类型接入反射，复用同一 `PropertyGraph` / `EditorAutoPropertySection`，
  不另写一套 slot 控件；slot 写入走 setter（触发 layout 失效）再 `commitEdit`。Canvas slot 带锚点
  预设（四角 / 边中 / 居中 / 拉伸）。同时核对各控件类型检查器缺的行。
  门禁：改 anchor / offset / size / padding 后 `UIDocument::toJson` 的 `childSlots` 对应变化且可撤销。
  落地形态：反射的是 slot 的 authoring args（`FCanvasSlotArgs` 等，公开聚合），不是 slot 私有字段；
  `EditorUISlotEdit` 持有 args 副本，PropertyHandle change hook 用 `UISlot::assign` 写回（`apply`
  保持 construct-time 语义不变）。锚点预设 = `withCanvasAnchorPreset`（GUI Layout，贴齐语义，保留尺寸）。
  文档根的父边属于设计器宿主，不可编辑（`EditorUIDesignerSession::editSlot`）。
- **U3 发现（未做，待定）**：`UIContainer`（row/column）、`SizeBox`、`ScrollViewport`、`SplitPane`、
  `Overlay`、`Expander` 没有反射字段；它们的布局属性（spacing / padding / direction / 主轴对齐 /
  SizeBox 覆盖尺寸 / split 比例）既不能在检查器编辑，也**不进 UIDocument**（控件序列化走反射）。
  这是文档格式问题，不属于 slot 检查器，需要单独立项。
- **U4 画布与层级补齐基线**：预览树尺寸 = 设计分辨率（预设 + 自定义，存设计器会话，不进文档），
  画布画出设计框，打开文档时缩放到适配；层级右键菜单 Delete / Duplicate；Palette 选中叶子控件时
  插到其后而不是塞进去。
  门禁：切设计分辨率后锚点布局按新尺寸重排；叶子控件不会获得子控件。

### 延后（用户要求之后再整理，不在本 phase）

锚点可视化、吸附 / 对齐线、键盘微调、多选、复制粘贴、Wrap With / 替换、重命名、可见性 / 锁定、
从 Palette 拖入画布、脚本挂载（`game-ui-script-framework` S6）、交互预览（Phase 5）。

### 验证

- 每个 checkpoint：`xmake b ya-game-editor ya-testing`，完整 `ya-testing`，
  `ya-gui-closure-test`，编辑器 smoke（HelloMaterial、GreedySnake 120 帧 exit 0）。
- 画布手势无法自动化进入 UI 页，每个 checkpoint 的手测步骤写进 progress，由用户确认。

## 当前边界

- 阶段一未做：UI 资产浏览器（阶段四）；因此场景 entry 的文档路径目前只能由
  脚本/测试构造，编辑器侧只能「打开已存在的 entry 文档」。
- overrides 语义保持现状（场景级字段覆盖），instance / appearance 边界的
  正式拆分留到阶段四。
