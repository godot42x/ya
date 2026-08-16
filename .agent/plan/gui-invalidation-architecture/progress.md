# GUI 失效与增量更新进度

> 建立日期：2026-08-15  
> 作用：记录已发生事实、验证结果和下一轮接力点；未实施的设计不在这里冒充完成。

## 2026-08-15 — 建立审计与主计划

### 本轮完成

- 基于当前 Reactive、per-widget draw-item cache、Workbench presenter 和特殊 paint override 完成架构审计；
- 明确当前不存在 ancestor dirty propagation，也不存在可跳过 subtree traversal 的 boundary；
- 将计划调整为 correctness-first：Phase 0/1A/1B/2 必做，batching/subtree cache/reconciliation 条件启动；
- 识别两个额外正确性缺口：
  - dirty level 当前错误地挂在 reactive value 上，而非 dependency/property edge；
  - draw-item cache 未覆盖 build context 与 inherited paint context validity。

### 当前结论

- 第一刀不是 boundary 或 Virtual DOM，而是 invalidation reason、dependency edge 和 cache validity；
- `UISplitPane::bindSplitRatio()` 的 persistent binding 生命周期必须在统一 paint pipeline 前修正；
- public field + setter 只能作为迁移态。

### 验证

- 文档级 review；
- 本轮未修改 `Engine/Source`，未运行构建。

## 2026-08-15 — 补齐长期计划执行工件

### 本轮完成

- 新增 `framework-lessons.md`，把 WPF、Flutter、Vue、Qt、Slate、React、Godot、Dear ImGui 的可吸收长处映射到 YA 阶段；
- 新增 `todo.md`，将 Phase 0~5 展开为 `GI-*` 任务、依赖、验收和提交边界；
- 新增 `process.md`，定义 correctness-first 实施顺序、验证层级和 Phase 决策门；
- 新增 `feature_matrix.json` 与 `session_checklist.md`；
- 更新 `audit.md`：补 WPF property metadata，并链接跨框架吸收表；
- 更新 `plan.md`：登记配套工件与跨框架能力吸收边界。

### 当前结论

- 当前可直接领取的第一项为 `GI-001 invalidation metrics + reason trace`；
- 在 GI-001~GI-004 基线完成前，不开始 batching、boundary 或 reconciliation。

### 下一轮直接接力点

1. 将 `GI-001` 标记为进行中；
2. 定位现有 WidgetTree dump/perf metrics 输出点；
3. 先补 reason/transition 测试，再接 profiling metrics。

### 验证

- 文档与 JSON 静态一致性检查；
- 本轮仍未修改行为代码。

## 2026-08-15 — GI-001 完成：invalidation reason + transition 计数

### 本轮完成

- 定义 `EUIInvalidationReason`（None / PaintProperty / LayoutProperty / ReactivePaint /
  ReactiveLayout / ChildStructure / GeometryChanged / BuildContextChanged /
  InheritedPaintContext / Volatile），enum 无字符串分配；
- `UIElement::markPaintDirty/markLayoutDirty` 增加 reason 参数，仅在 0->1 dirty
  transition 记录 `_lastInvalidationReason` 并累计 tree 级 transition 计数；
- `GuiPerfStats` 扩展 `paintDirtyTransitions`/`layoutDirtyTransitions`/
  `cacheInvalidations`；`WidgetTree` 新增累计成员与 `getLastInvalidationReason()`；
- `ReactiveBase::notifyDependents` 传入 ReactivePaint/ReactiveLayout reason，并
  累计 `ReactiveDiagnostics{notifyCalls, dependentVisits}`（process-wide，经
  `getReactiveDiagnostics()` 读取）；
- 打点：VisualFlag->PaintProperty、setLayoutRect->GeometryChanged、
  notifyDependents->ReactivePaint/ReactiveLayout；Phase 1A/2 的
  PaintProperty/LayoutProperty/BuildContextChanged/InheritedPaintContext 枚举保留待打点。

### 代码/行为结论

- `UISplitPane::paint()` 覆盖完整 paint 流程、不清除 `_bPaintDirty`（已确认，是
  plan.md 1.2 记录的已知问题）。测试 2 改用 `ReactiveListProbeWidget` +
  `setDirtyLevel(Layout)` 验证 Layout reason；split 的 paint 统一属于 Phase 2
  (GI-302)，不在本诊断基线范围内。

### 验证

- `xmake b ya-gui-closure-test` 通过；
- `xmake r ya-gui-closure-test` 131 tests PASSED（新增 3 个：
  ReactivePaintMutationRecordsReasonAndTransition、
  ReactiveLayoutMutationRecordsReasonAndTransition、
  SameValueReactiveSetSkipsInvalidation）。

### 下一接力点

- `GI-002`：build-context/resource-generation cache validity 测试 seam + 保守 cache reset。

## 2026-08-15 — GI-002 完成：build-context cache validity baseline

### 本轮完成

- `UIFrameBuildContext` 新增 `uint64_t generation`（host 提供单调 token，用于 uiScale/offset
  之外无法自行比较的坐标映射/resource resolver 变化，如 viewport resize / DPI / asset reload）；
- `WidgetTree` 新增 `_bHasBuildContext`/`_lastGeneration`/`_lastUiScale`/`_lastOffset`，
  在 `buildSnapshot` 开头比较上下文；变化时清空两份 `_itemCache`、
  `++_cacheInvalidations`、并记录 `BuildContextChanged` reason（保守、correctness-first）；
- 首次 build 无前序上下文，不触发失效。

### 代码/行为结论

- 按 plan.md 3.5 决策落地：uiScale/offset 由 WidgetTree 直接比较（数值），
  resolver 不做 `std::function` 比较，由 host 经 `generation` 显式声明失效；
- 这是 Phase 2「unified paint template」之前就需要的正确性基线：否则 clean tree
  在窗口 resize / DPI 切换 / 纹理重载后会复用不兼容的 target-pixel 段。

### 验证

- `xmake b ya-gui-closure-test` 通过（13.6s）；
- `xmake r ya-gui-closure-test` 134 tests PASSED（新增 3 个：
  CleanTreeOffsetChangeRebuildsResolvedItems、
  CleanTreeUiScaleChangeRebuildsResolvedItems、
  CleanTreeGenerationChangeDropsCache）。

### 下一接力点

- `GI-003`：paint-collected / persistent reactive edge 生命周期回归测试。

## 2026-08-15 — GI-003 完成：reactive edge 生命周期回归测试

### 本轮完成

纯测试任务（无 contract 改动），补全 reactive edge 生命周期回归基线：

- `ReactiveDestroyedBeforeWidgetSeveresBackReference`：reactive 先析构、widget 后析构
  （反向析构顺序），验证 `~ReactiveBase` 的 `untrackDependency` sever 逻辑——repaint 的
  `clearDependencies()` 不碰已析构的 ref；
- `DetachedWidgetSurvivesReactiveSet`：detach（非析构）后 reactive set 安全，
  验证 `markPaintDirty` 的 `_tree == nullptr` 守卫；
- `RebindSplitRatioKeepsLatestBindingActive`：rebind 后最新 binding 生效且不 crash；
- `SplitRatioBindingPersistsAcrossRepaints`：bind-time persistent ratio binding 在多次
  repaint 后仍触发 layout（GI-102 persistent-edge 分离的回归守卫）。

### 代码/行为结论

- 现有 `ConditionalDependencySwitchRecollects` + `DestroyedDependentDoesNotDangle`
  已覆盖「条件读取切换移除旧 edge」与「widget 先析构」两个方向；本轮补齐反向析构、
  detach、rebind、persistent 存活四场景；
- mixed Paint/Layout consumer 当前**不支持**（`ReactiveBase::_dirtyLevel` 是 value-global
  单值），这是 GI-101 要改的 edge 模型，本测试任务不越界实现；
- `bindSplitRatio` rebind 不清理旧 binding 是已知缺陷（GI-102 修复），本测试只断言最新
  binding 生效，不测旧 binding 清理。

### 验证

- `xmake b ya-gui-closure-test` 通过；
- `xmake r ya-gui-closure-test` 138 tests PASSED（新增 4 个）。

### 下一接力点

- `GI-004`：Workbench 性能基线（文档任务，固定样本测 layout/paint time 等指标）。

## 2026-08-15 — GI-004 完成：Workbench 性能基线

### 本轮完成

- `GUIHeadlessHost` 新增 `bPerfTelemetry` 开关 + `--perf-telemetry` CLI（`GUIWorkbench`），
  每帧输出一行完整遥测：draw/painted/rebuilt/paintDirty/layoutDirty/cacheInv/notifyCalls/
  notifyVisits/layout/paint（GI-001 埋点此前无 host 消费，这是其首个消费端）；
- 跑 headless smoke（40 帧，覆盖 render probe 点击、tab 切换、counter/slider/checkbox/combo、
  menu、drag-drop、modal、scrollsplit、editor）采集基线。

### 基线数据（`xmake r GUIWorkbench --headless --smoke-actions --exit-after-frame 40 --perf-telemetry`）

| 样本 | draw | painted | rebuilt | paintDirty(累计) | layoutDirty | cacheInv | notify | layout ms | paint ms |
|---|---|---|---|---|---|---|---|---|---|
| 首帧（冷启动，Render 页） | 42 | 40 | 35 | 39 | 0 | 0 | 0/0 | 0.106 | 0.166 |
| 稳态帧（Render 页，无输入） | 42 | 40 | **0** | 39 | 0 | 0 | 0/0 | 0.000 | 0.102 |
| 单次点击（render probe） | 42 | 40 | 1 | 40 | 0 | 0 | 0/0 | 0.000 | 0.042 |
| tab 切换（Widgets 页重建） | 58 | 46 | 26 | 71 | 0 | 0 | 0/0 | 0.116 | 0.137 |
| 稳态帧（Widgets 页） | 58 | 46 | 1 | 72 | 0 | 0 | 0/0 | 0.000 | 0.062 |
| 交互高峰（drag-drop/modal 段） | 82 | 80 | 61 | 249 | 0 | 0 | 0/0 | 0.162 | 0.223 |

### 关键观察

1. **稳态帧 rebuilt=0**：增量 draw-item cache 复用生效，无输入帧零重建、layout 0ms；
   首帧冷启动 rebuilt=35/40（88%），之后纯交互帧 rebuilt 只随受影响 widget 增长——正确性基线成立。
2. **notifyCalls/notifyVisits 全程 0**：Workbench presenter 的每帧同步用的是 `_bVolatile`
   全量重画兜底（`d0f18f96`），**尚未走 reactive 依赖链**——这直接印证 GI-105
   （presenter 迁移到 property setter、删除 `_bVolatile`）是本计划 Phase 1 的核心收益点；
   当前 volatile 兜底导致交互帧 rebuilt 偏高（drag-drop 段 61/80=76%）。
3. **layoutDirty 全程 0**：smoke 交互不触发 layout 失效（tab 切换是 paint 级重建，
   无 resize/split 拖动）；resize/scrollsplit 样本留待 Phase 2 统一 paint pipeline 后复测。
4. **paintDirty 是累计值**（单调递增，本次未做每帧差值归一），Phase 2 若要 per-frame
   delta 需在 buildSnapshot 里 snapshot 前作差，暂不在本基线引入。

### 验证

- `xmake b GUIWorkbench` 通过；
- smoke PASS；遥测字段完整覆盖验收要求（layout/paint time、painted/rebuilt、draw items、
  notify visits）。

### 下一接力点

- Phase 0 完成，进入 `GI-101`（reactive edge 模型重构，区分 paint-collected vs persistent binding）。

## 2026-08-15 — GI-101 + GI-102 完成：property-aware edge 模型

### 本轮完成

把 reactive 的 dirty level 从 value-global 单值迁移为 **per-edge (widget, level) + 生命周期分类**：

- `ReactiveBase` 拆 `_paintDependents` / `_persistentDependents` 两组，edge 结构为
  `{widget, level}`；`setDirtyLevel()` 与 `_dirtyLevel` 删除；
- `Reactive<T>::get()` / `ReactiveList::size()/get()` / `Computed::get()` 增加
  `EDirtyLevel` 参数（默认 Paint），由读取 property/控件尺寸契约决定；
- `UIElement` 反向依赖拆 `_paintDependencies` / `_persistentDependencies` 两组；
  `clearDependencies()` 只清 paint-collected，新增 `clearPersistentDependencies()`；
- `notifyDependents()` 按每个 edge 自己的 level 标脏；`~ReactiveBase` 遍历两组 sever；
- 迁移现有 bind 点到 persistent API：
  - `UISplitPane::bindSplitRatio` → `addPersistentDependent(Layout)`，并**修复 rebind 不清理
    旧 edge 的已知缺陷**（GI-003 记录）；
  - `UIStyleSet::bindTo` → `addPersistentDependent(Paint)`；
  - `UITreeView` 删掉三处 `setDirtyLevel`，改为读取点传 level（roots/expanded→Layout，
    selectedId→Paint）。

GI-102（persistent binding 迁移）与 GI-101 是同一改动的不可拆分两半，一并完成。

### 代码/行为结论

- 「同一 reactive 服务 Paint+Layout consumer」「同一 widget 两个 level 不互相覆盖」
  「clear paint deps 不影响 persistent」三条验收全部有测试覆盖；
- 测试计数断言需在 `set/push` 后先 `buildSnapshot`（`getPerfStats()` 返回的是最近一次
  build 的快照，不是实时累计值）——这是 GI-001 埋点时定下的语义。

### 验证

- `xmake b ya-gui-closure-test` 通过（8.9s）；
- `xmake r ya-gui-closure-test` **142 tests PASSED**（新增 4 个：
  ReactiveMixedLevelConsumersGetCorrectInvalidation、
  SameWidgetTwoLevelConsumeBothEdges、
  PaintRebuildDoesNotDropPersistentStyleBinding、
  RebindSplitRatioClearsOldBinding）；
- `xmake b ya-engine` 聚合通过（44s，全模块无旧 API 残留）。

### 下一接力点

- `GI-103`：`UIText::computeDesiredSize()` 使用 resolved text/style，AutoSize binding 注册
  Layout edge（measure 依赖收集）。

## 2026-08-15 — GI-103 完成：UIText resolved measure/paint 一致

### 本轮完成

- `resolvedText()` / `resolvedStyle()` 增加 `EDirtyLevel` 参数（默认 Paint）；
- `paintSelf()` 按 `_bAutoSize` 决定读取 level：AutoSize 文本的 text/fontSize 是 Layout
  edge（写会重跑 measure+arrange），fixed-size 是 Paint edge（只重画）；
- `computeDesiredSize()` 改为读 `resolvedText()` / `resolvedStyle()` 的 resolved 值，
  修正 AutoSize + binding 时 measure 用裸 `_text`/`_fontSize` 的旧值不一致；
- 明确 measure 阶段（layout，早于 paint walk）不注册依赖：`currentPaintWidget()` 为空，
  `get()` 无副作用；Layout edge 由 `paintSelf` 在同一 level 建立。

### 代码/行为结论

- 修复了「bound AutoSize 文本内容/字号变了，measure 用旧值导致 desired size 不更新」
  的正确性缺口；
- 复用 GI-101 的 per-edge level 语义：同一 `_textBinding` 在 AutoSize/fixed-size 下自动
  落到不同 level，无需 bind 时区分。

### 验证

- `xmake b ya-gui-closure-test` 通过（10.5s）；
- `xmake r ya-gui-closure-test` **145 tests PASSED**（新增 3 个：
  AutoSizeTextBindingTriggersLayoutOnTextChange、
  FixedSizeTextBindingTriggersPaintOnly、
  AutoSizeTextComputeDesiredSizeUsesResolvedText）；
- `xmake b ya-engine` 聚合通过。

### 下一接力点

- `GI-104`：最小 property impact contract（property 分类：Paint / Layout / SubtreePaintContext）。

## 2026-08-15 — GI-104 完成：最小 property impact contract

### 本轮完成

- 定义 `EUIPropertyImpact` 枚举（`None` / `Paint` / `Layout` / `SubtreePaintContext`），
  放在 `UIElement.h` 紧邻 `EUIInvalidationReason`；
- `UIElement::invalidateProperty(impact)` 统一失效入口：setter 声明 impact 而不是
  直接选择 dirty API，三态映射到 `markPaintDirty` / `markLayoutDirty` / `invalidateSubtree`；
- `invalidateSubtree()` 增加 `reason` 参数（默认 `InheritedPaintContext`），修正它此前
  标 `None` reason 的问题；
- 修正 `UIBoxLayout::setClipsChildren` 的 impact 错误：clip 是 inherited paint context
  （重画子树、不重排），此前误用 `invalidateArrange()`（Layout）。这是当前代码里唯一
  一处 impact 分类错误，也是 `SubtreePaintContext` 的首个真实 consumer。

### Property impact 审计表（持续维护，process.md 3.2 要求）

| Widget / Layout | Property | Impact |
|---|---|---|
| UIButton | `_normalColor`/`_hoveredColor`/`_pressedColor`/`_focusedColor` | Paint（reflection 编辑） |
| UIButton | `_bHovered`/`_bPressed`/`_bFocused` | Paint（VisualFlag） |
| UIButton | `_enabledBinding` | Paint（resolved read） |
| UIButton | `_contentLayout.padding` | Layout（UISingleChildLayout） |
| UIButton | `_onClick` | None（runtime callback） |
| UIText | `_text`/`_fontSize` | Layout（AutoSize）/ Paint（fixed-size） |
| UIText | `_color`/`_hAlign`/`_vAlign` | Paint |
| UIBoxLayout | `_direction`/`_spacing`/`_padding`/`_mainAxisAlignment`/`_stretchLastChild` | Layout |
| UIBoxLayout | `_bClipChildren` | **SubtreePaintContext** |
| UIBoxSlot | `_sizeRule`/`_weight`/`_margin`/`_crossAlignment`/`_minSize`/`_maxSize`/`_preferredSize`/`_bParticipatesInLayout`/`_bReserveSpaceWhenHidden` | Layout |
| UISingleChildLayout | `_padding` | Layout |
| UISplitLayout | `_orientation`/`_splitRatio`/`_minFirstExtent`/`_minSecondExtent`/`_dividerThickness`/`_padding` | Layout |
| UIScrollLayout | `_axis`/`_scrollOffset`/`_scrollStep` | Layout |

### 代码/行为结论

- 未建设通用 variant property system（framework-lessons 的「不照搬」约束）；
- Layout 层的 setter 已有一套 changed-only `invalidateMeasure/Arrange`，本轮只新增
  `invalidateSubtreePaint()` 修正 clip，不重写那套机制；
- 两个枚举正交：`EUIInvalidationReason` 回答「为什么失效」（诊断），`EUIPropertyImpact`
  回答「影响范围」（契约），setter 声明 impact，reason 由入口内部决定。

### 验证

- `xmake b ya-gui-closure-test` 通过（16.6s）；
- `xmake r ya-gui-closure-test` **149 tests PASSED**（新增 4 个：
  PropertyImpactPaintDoesNotInvalidateLayout、
  PropertyImpactLayoutInvalidatesMeasure、
  SubtreePaintContextInvalidatesWholeSubtree、
  SetClipChildrenIsSubtreePaintNotLayout）；
- `xmake b ya-engine` 聚合通过（50.9s）。

### 下一接力点

- `GI-105`：Workbench changed-only setter 迁移（presenter 从 `_bVolatile` 每帧全量重画
  迁移到 changed-only setter + reactive，删除 volatile 兜底）。

## 2026-08-15 — GI-105 完成：Workbench changed-only setter 迁移

### 本轮完成

把 Workbench presenter 的「每帧直接写字段 + `_bVolatile` 兜底」迁移为「changed-only setter」，
删除了 volatile 兜底与手工补偿：

- 新增 7 个 changed-only setter（同值写入是 no-op，真实变化才 `invalidateProperty`）：
  - `UIElement::setPosition` / `setSize`（Layout）、`setVisibility`（SubtreePaintContext，
    且涉及 Collapsed 时升为 Layout）；
  - `UIPanel::setColor`（Paint）、`UIText::setText`（AutoSize→Layout / fixed→Paint）、
    `UITextField::setText`（Paint）、`UISelectableRow::setSelected`（Paint）；
- `syncPresentationState()` 全部改走 setter；
- 删除 7 处 `_bVolatile` 标记（highlightPanel / previewName / nameField / colorValue /
  sizeValue / row / label）；
- 删除 `_lastHighlightSize` / `_lastHighlightPos` / `_lastToggleText` 与 `bGeometryChanged`
  手工补偿——changed-only 检测由 setter 内置，presenter 不再自己比较。

### 代码/行为结论

- 稳态帧 `rebuilt=0`（GI-004 基线已验证 Render 页稳态 rebuilt=0，但那是 demo 页；本次
  删除 volatile 后，Editor 页 presenter 同值同步也是 no-op，不再每帧重画）；
- `setVisibility` 的 Collapsed 判定：`EWidgetVisibility::Hidden` 保留 layout space，
  `Collapsed` 不保留，只有两者互转时才升 Layout，其余用 SubtreePaintContext；
- setter 是 GI-202（字段封装为 backing field）的前置：GI-202 只需把字段改 private，
  setter 已经就位。

### 验证

- `xmake b ya-gui-closure-test` 通过（15.3s）；
- `xmake r ya-gui-closure-test` **149 tests PASSED**（无新增测试，本轮是 presenter 迁移，
  由既有 property impact 测试 + smoke 覆盖）；
- `xmake b GUIWorkbench` 通过（8.0s）；
- `xmake r GUIWorkbench --headless --smoke-actions --perf-telemetry` smoke **PASS**，
  稳态帧 `rebuilt=0`；
- `xmake b ya-engine` 聚合通过（45.8s）。

### 下一接力点

- `GI-106`：Inherited paint-input audit（决定 subtree invalidation vs context generation）。

## 2026-08-15 — GI-106 完成：Inherited paint-input audit

纯文档审计（无代码改动）。盘点所有会改变 descendants 缓存复用正确性的 inherited paint input，
并拍板 `invalidateSubtree()` 归宿。

### Inherited paint-input 盘点表

| Input | 影响 descendants 缓存? | 当前失效机制 | 状态 |
|---|---|---|---|
| clip（`clipsChildren` toggle） | 是（descendant segment 的 resolved clip rect） | `invalidateSubtree()`（SubtreePaintContext，GI-104 修正） | ✓ 正确 |
| clip（`_layoutRect` 变化） | 是（clip rect 变化） | 间接：layout re-arrange → child `setLayoutRect` → `markPaintDirty` | ⚠ 边界隐患（Phase 2 修，见下） |
| visibility（Hidden/Collapsed） | 隐式（ancestor 不可见 → paint 提前 return 不递归） | `setVisibility()`（SubtreePaintContext，GI-105） | ✓ 保守正确 |
| transform（`_pivot`/rotation/scale） | 未实现（`_pivot` 是 reserved 字段） | 无 | N/A |
| opacity/theme | 不存在（`FWidgetStyle` 是 per-widget bind，非 inherited） | 无 | N/A |
| build context（uiScale/offset/generation） | 是（全局 target-pixel + 纹理） | GI-002：`buildSnapshot` 开头比较后清两份缓存 | ✓ 正确 |
| resource resolver | 是（纹理） | GI-002：host 经 `generation` 声明失效 | ✓ 正确 |

### `invalidateSubtree()` 归宿决策：保留

1. clip 变化是 **per-widget** 的 inherited context 变化，不是全局 build context，**不能用 generation 替代**
   （generation 是全局的，一次变化清全树缓存，对单个 subtree clip 变化是过度失效）；
2. 语义清晰：`invalidateSubtree` = 「我的 inherited paint context 变了，子树需要重画以生成
   带新 context 的 segment」；
3. 无需重命名，当前名字准确表达意图。

### 审计发现：Phase 2 要修的边界问题

`UIContainer::paint()` / `UIScrollViewport::paint()` / `UISplitPane::paint()` 覆盖了 base paint，
**无条件重画自己**（`paintSelf` 不检查 dirty），children 则走 base `UIElement::paint()` 的 cache
复用。由此产生一个边界：

> container 的 `_layoutRect` 变（clip rect 变）但某个 child 的 rect 不变（如 fixed-size 靠左
> child）→ child 仍 `reuseCachedItems`，复用**旧 clip rect** 的 segment → 新 clip 区域外的内容
> 丢失/错误。

这是 plan.md 1.2 记录的「host 覆盖完整 paint 绕过 cache 流程」的同类问题，归属 Phase 2
「统一 paint template」（GI-302）。当前 layout 路径下大多被「re-arrange 会标脏 child rect」
间接覆盖，但 fixed-size child 的边界仍存在。GI-106 只审计，不修；记录为 Phase 2 的入口证据。

### 验证

- 文档任务，无代码改动；审计表写入本节，`invalidateSubtree` 决策写入 charter 层（保留）。

### 下一接力点

- `GI-201`（Phase 1B）：Authoring/reflection mutation transaction。

## 2026-08-16 — GI-201 完成：Authoring/reflection mutation transaction

### 本轮完成

在 `UIElement::deserializeFields()` 建立 mutation transaction 边界：

- 反射反序列化**直接写内存、绕过 changed-only setter**（`deserializeScalarValue` → 直接
  `*ptr = value`），所以逐字段写入既不触发失效、也不向 binding observers 暴露中间值——
  第二点天然满足；
- 缺口在「批量 restore 后漏失效」：对 live tree 上的 widget 批量恢复字段，UI 不会刷新。
  在 `deserializeFields()` 末尾聚合一次最高 impact 失效：`invalidateProperty(Layout)`；
- 对 detached `UIDocument::instantiate()` 这是 no-op（`_tree == nullptr`），且 attach 路径
  （`WidgetTree::attach`）本就会 `invalidateLayout()`，行为不变。

### 代码/行为结论

- 最小 transaction：不引入通用 property framework、不加 RAII transaction 对象、不碰反射
  核心，只把「批量反序列化」这个天然边界点显式化为一次聚合失效；
- `invalidateProperty(Layout)` 与 GI-104 的 setter 契约一致（内部走
  `markLayoutDirty(LayoutProperty)`），反射批量写与单个 setter 的失效语义统一；
- 反射零 notify 是因为绕过 setter；GI-202（字段私有化）会迫使反射改走 setter 或显式
  transaction，届时本边界是它的正确失效入口。

### 验证

- `xmake b ya-gui-closure-test` 通过（6.7s，仅 pre-existing `buildSnapshot` `[[nodiscard]]`
  C4834 警告，与既有测试模式一致）；
- `xmake r ya-gui-closure-test` **151 tests PASSED**（新增 2 个：
  DeserializeOnAttachedWidgetAggregatesSingleInvalidation、
  DeserializeOnDetachedWidgetIsNoOp）；
- `xmake b ya-engine` 聚合通过（1.36s 缓存命中）。

### 下一接力点

- `GI-202`：Runtime visual/layout 字段封装（把 `_text/_size/_position/_visibility/...`
  按真实写路径收为 backing field，按控件族拆多个小提交）。

## 2026-08-16 — GI-202（切片 1）：UIElement 布局属性封装

GI-202 按控件族拆多个小提交；本轮完成**基类 `_position/_size/_visibility`** 封装。

### 本轮完成

- `UIElement::_position/_size/_visibility` 改为 **protected** backing field；
- 新增 `getPosition()` / `getSize()` / `getVisibility()` 访问器（外部读）；
- 保留 `setPosition` / `setSize` / `setVisibility`（GI-105 已加）为唯一外部写入口；
- `_zOrder/_anchorMin/_anchorMax/_pivot/_hitFilter/_focusPolicy` 保持 public，登记为
  **authoring-only 例外清单**（暂无 runtime 写路径，待其获得 setter 后再封装）；
- 全量迁移外部写/读到 setter/getter：Workbench、WidgetTree、UILayout、Menu/MenuBar、
  SceneWidgetEntry、GameEditor（UIDesignerPanel/DetailsView）、GUI 测试套件（约 200 处，
  用脚本机械替换 `->_x = v` → `->setX(v)` 与 `->_x` 读 → `->getX()`，再人工修正）。

### 代码/行为结论

- 反射**不受影响**：`YA_REFLECT_FIELD` 在类作用域内取 `&class_t::FieldName` 成员指针，
  protected 字段仍可序列化；
- 派生类在 `paint/layout/computeDesiredSize` 里直接读 `_position/_size/_visibility` 合法
  （protected），无需改；
- 派生类构造函数里的 `_size = ...` 初始化（未 attach、setter no-op）保持直接写，不迁移；
- 关键风险与规避：脚本批量替换误触了 **ECS `TransformComponent::_position`（vec3）**
  与 GameEditor 注释里的 `tc->_position`，已全部 `git checkout` 回退；最终只留 UIElement
  派生对象的迁移。

### 验证

- `xmake b ya-gui-closure-test` 通过；
- `xmake r ya-gui-closure-test` **151 tests PASSED**（无新增，纯机械迁移）；
- `xmake b ya-engine` 聚合通过。

### 下一接力点

- `GI-202`（切片 2）：各控件族字段封装（`UIText::_text/_fontSize/_color`、
  `UIPanel::_color`、`UIButton::_normalColor/...` 等），继续按控件族拆小提交。

## 2026-08-16 — GI-202（切片 2）：UIText::_text + UIPanel::_color 封装

### 本轮完成

- `UIText::_text` → protected，补 `getText()`；`UIPanel::_color` → protected，补 `getColor()`；
  两个字段的 setter 已在 GI-105 就位；
- **authoring-only 例外清单**（保持 public）：
  - `UIText::_fontSize/_color/_hAlign/_vAlign`（fontSize/color 经 `bindStyle` runtime 覆盖，
    不写这些 base value 字段）；
  - `UIPanel::_image/_bNineSlice/_nineSliceBorder`；
  - `UIButton::_normalColor/_hoveredColor/_pressedColor/_focusedColor`；
- 全量迁移外部写/读到 setter/getter：Workbench（含 `logStatus` 的 runtime 直接写——GI-105
  遗漏点）、WidgetTree drag ghost、WidgetTreeDump、Menu、GUI 测试套件。

### 代码/行为结论

- 改用「**编译器驱动**」迁移（先改 protected，让 `error C2248` 精确列出每个非法访问点，
  逐个迁移），避免了切片 1 用脚本误触 ECS `TransformComponent::_position` 的风险；
- authoring 构造写（如 Workbench `makeText`）迁移到 setter 对 detached widget 无害：
  `invalidateProperty` 里 `markPaintDirty` 只设 flag（`_tree == nullptr` 不标树脏），
  attach 后第一帧照常重画；
- 反射不受影响（成员指针类作用域内取地址）。

### 验证

- `xmake b ya-gui-closure-test` 通过；
- `xmake r ya-gui-closure-test` **151 tests PASSED**（无新增，纯迁移）；
- `xmake b ya-engine` 聚合通过（exit 0）。

### 下一接力点

- `GI-202`（切片 3，可选收尾）：`UIButton` 颜色字段 + `UIText::_fontSize/_color` 的封装
  决策（这些当前是 authoring-only，若未来有 runtime 写路径再迁移）；或进入 Phase 1B 后续
  切片（`GI-203` 及之后，取决于 todo 顺序）。

## 2026-08-16 — GI-202 收尾 + GI-203 完成：Example 迁移 + Direct-write 门禁

### GI-202 遗漏收尾（`4bb91e8e`）

- 切片 1/2 只构建了 `ya-engine` 和 closure test，编译器驱动没覆盖 Example 外部消费者；
  `Example/HelloMaterial`、`GUIWorkbench`（WorkbenchDemoPages）、`GUIFrameworkSmoke` 里
  60+ 处 `->_position/_size/_text` 直接写（现在 protected）导致这三个 target 编译失败；
- 脚本迁移：`_position/_size/_visibility/_text` 写→setter、读→getter；UIPanel `_color`
  →setColor（UIText `_color` 保持 public 直接写）；修复脚本误触的 UITextField `getText`
  （UITextField 不继承 UIText，`_text` 仍 public、无 getText，改回 `->_text` 读）；
- 三个 Example target 全部构建通过（exit 0）。

### GI-203 完成：Direct-write 静态门禁（`Script/ya_gui_write_guard.py`）

- 门禁规则：GUI owner（`Framework/GUI`）外，禁止 `->_position/_size/_visibility/_text`
  直接赋值；用 setter 替代；
- 白名单 / 排除：
  - GUI owner 目录（派生类 paint/layout/measure/构造直接访问）；
  - `Framework/ECS` 目录（`TransformComponent::_position` 是 glm::vec3，与 GUI 无关，
    且 ya_module_lint 禁止 ECS 依赖 GUI）；
  - `//` 行注释（死代码不触发）；
- 验证：
  - 干净仓库返回 0（ok）；
  - 故意添加 `->_size = ` 违规 → 返回 1 且精确报文件:行；
  - 注释 / ECS 同名 `_position` 不误报（3 处初始误报已消除）；
- `_color` 因 UIText(public)/UIPanel(protected) 歧义未纳入，待 UIText::_color 封装后补；
- 门禁是「过渡门禁」：编译器 C2248 是硬门禁，grep 提供构建前快速反馈（plan §迁移门禁）。

### 验证

- `xmake b GUIWorkbench` / `HelloMaterial` / `GUIFrameworkSmoke` 均 exit 0；
- `python3 Script/ya_gui_write_guard.py` 干净仓库返回 0。

### 下一接力点

- **Phase 1B 全部完成**（GI-201~203），进入 Phase 2：`GI-301`（Paint scope RAII）。

## 2026-08-16 — GI-301 完成：Paint scope RAII

### 本轮完成

- `Reactive.h` 新增 `PaintScope` RAII（构造 `pushPaintWidget` / 析构 `popPaintWidget`，
  不可拷贝），作为 push/pop 的唯一配对点；
- 四处手动 `pushPaintWidget`/`popPaintWidget` 全部替换为 `PaintScope`：
  - 基类 `UIElement::paint`（UIElement.cpp）；
  - `UIContainer::paint`（Container.cpp）；
  - `UIScrollViewport::paint`（ScrollViewport.cpp）；
  - `UISplitPane::paint`（SplitPane.cpp）。
- 新增两个测试：
  - `PaintScopeRestoresStackOnNestedScope`：嵌套 scope 的栈顶与逐层恢复；
  - `PaintWalkRestoresReactiveStack`：真实 buildSnapshot（含 clean 复用帧）后
    `currentPaintWidget()` 恢复 nullptr，防栈泄漏。

### 代码/行为结论

- 三个 layout host（Container/Scroll/Split）仍覆盖完整 `paint()`（GI-302 的统一模板
  才是收掉覆盖的步骤）；本轮只把它们的 push/pop 换成 RAII，行为不变；
- 现有 `pushPaintWidget/popPaintWidget` 导出函数保留（`PaintScope` 内部调用，跨 DLL
  边界共享 module-local stack），不再有业务 paint 路径手动配对。

### 验证

- `xmake b ya-gui-closure-test` 通过；
- `xmake r ya-gui-closure-test` **153 tests PASSED**（新增 2 个）；
- `xmake b ya-engine` 聚合通过；
- `python3 Script/ya_gui_write_guard.py` 返回 0（ok）。

### 下一接力点

- `GI-302`：统一 paint 模板（基类唯一 self rebuild/reuse pipeline；Container/Scroll/
  Split 改为覆盖 `paintChildren` 定制 clip/children traversal，不再覆盖完整 `paint()`）。
