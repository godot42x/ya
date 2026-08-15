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
