# GUI 失效与增量更新 TODO

> 更新时间：2026-08-15  
> 作用：把 `plan.md` 展开为可领取、可 review、可验证的最小任务。  
> 状态：`[ ]` 未开始，`[-]` 进行中，`[x]` 完成，`[~]` 条件延后，`[-x]` 停止。

## 当前切片

当前激活切片：`GI-303 Build-context generation`

执行规则：

- 同时最多一个任务为 `[-]`；
- 每个任务默认一个可 review 提交；
- 不跨 phase 混入 speculative optimization；
- 发现前提错误时先更新 `plan.md` / `progress.md`，再改代码。

## P0 — Correctness baseline 与可观测性

- [x] `GI-001` Invalidation metrics 与 reason trace
  - 依赖：无
  - 修改：
    - 扩展 `GuiPerfStats` 或相邻 diagnostics，记录 notify/dependent visit/dirty transition/cache-context invalidation；
    - 增加 debug-only `EUIInvalidationReason`；
    - dump last reason 与 transition count。
  - 验收：
    - clean frame、单 paint mutation、单 layout mutation 的 reason/计数可区分；
    - release 路径无高成本字符串分配；
    - closure test 通过。
  - 提交：`[gui] trace invalidation reasons and transitions`

- [x] `GI-002` Build-context cache validity baseline
  - 依赖：GI-001
  - 修改：
    - 为连续 build 使用不同 `offset/uiScale` 增加测试；
    - 增加 resolver/resource generation 测试 seam；
    - 先用保守 cache reset 验证正确性。
  - 验收：
    - clean widget 在 context 变化后使用新 sprite/text/clip 数据；
    - context 未变化时仍可复用 cache。
  - 提交：`[gui] validate draw cache build context`

- [x] `GI-003` Reactive edge 生命周期回归测试
  - 依赖：无
  - 修改：
    - mixed Paint/Layout consumers；
    - conditionally collected paint dependency；
    - persistent split-ratio binding；
    - rebind/unbind/detach/destructor。
  - 验收：
    - 无悬空 dependent；
    - paint rebuild 不会移除 persistent layout binding；
    - 条件读取切换后旧 edge 被移除。
  - 提交：`[test/gui] cover reactive edge lifecycle`

- [x] `GI-004` Workbench 性能基线
  - 依赖：GI-001
  - 修改：固定稳态帧、selection、rename、visibility、resize、scroll/split 样本。
  - 验收：记录 layout/paint time、painted/rebuilt、draw items、notify visits；证据写入 `progress.md`。
  - 提交：`[gui] add headless perf telemetry for workbench baseline`（perf 遥测工具）+ 文档。

## P1A — Property-aware mutation 与 binding

- [x] `GI-101` Reactive dependency edge 模型
  - 依赖：GI-003
  - 修改：
    - edge 保存 dependent + consumer identity + dirty level + lifetime kind；
    - 区分 paint-collected 与 persistent binding；
    - 移除 value-global dirty level 作为长期真相。
  - 验收：
    - 同一 reactive 同时服务 Paint/Layout consumer；
    - 同一 widget 的两个 property 不互相覆盖；
    - clear paint dependencies 不影响 persistent edge。
  - 提交：`[gui/reactive] make invalidation property aware`

- [x] `GI-102` Persistent binding API 与现有 consumer 迁移
  - 依赖：GI-101
  - 修改：
    - 迁移 `UISplitPane::bindSplitRatio()`；
    - 迁移 `UIStyleSet::bindTo()` 或删除不安全的 bind-time 注册；
    - 明确 unbind/rebind/destructor 清理。
  - 验收：GI-003 全部通过；Phase 2 统一 paint 后 binding 仍有效。
  - 提交：随 GI-101 一并完成（edge 模型与 persistent 迁移是同一改动，无法拆分编译）

- [x] `GI-103` UIText resolved measure/paint 一致
  - 依赖：GI-101
  - 修改：
    - `computeDesiredSize()` 使用 resolved text/style；
    - AutoSize binding 注册 Layout edge；
    - fixed-size paint input 保持 Paint edge。
  - 验收：
    - bound text/font size 改变同帧更新 desired size；
    - fixed-size color/text 不触发多余 layout。
  - 提交：`[gui/text] align bound measure and paint`

- [x] `GI-104` 最小 property impact contract
  - 依赖：GI-001
  - 修改：
    - 定义最小 `EUIPropertyImpact` 或等价 contract；
    - 覆盖 Paint/Layout/SubtreePaintContext；
    - setter 和 reflection mutation 共用，不建设通用 variant property system。
  - 验收：属性 impact 表可审计；调用方不能自行降级 dirty reason。
  - 提交：`[gui] define property invalidation impact`

- [x] `GI-105` Workbench changed-only setter 迁移
  - 依赖：GI-103、GI-104
  - 修改：
    - row selected/label；
    - highlight visibility/geometry/color；
    - preview/inspector/toggle text；
    - 删除 `_last*` / `bGeometryChanged` 补偿。
  - 验收：
    - 对应 `_bVolatile` 删除；
    - 稳态帧无 presenter 同值 rebuild；
    - Editor scenario/golden parity 通过。
  - 提交：`[gui/workbench] route presentation writes through properties`

- [x] `GI-106` Inherited paint-input audit
  - 依赖：GI-002、GI-104
  - 修改：盘点 clip、visibility、transform、opacity/theme、build context、resource resolver。
  - 验收：
    - 输出 property → affected subtree/cache 表；
    - 拍板 `invalidateSubtree()` 保留、重命名或 generation 替代；
    - 结论写入 `progress.md` 和 `feature_matrix.json`。
  - 提交：文档任务；若只改计划不要求代码提交。

## P1B — 强制 Property Mutation 契约

- [x] `GI-201` Authoring/reflection mutation transaction
  - 依赖：GI-104
  - 修改：
    - document instantiate/edit transaction 聚合最高 impact；
    - 禁止在对象不变量未完整时向 binding observers 暴露中间值。
  - 验收：批量反序列化只在 transaction 末产生必要 invalidation。
  - 提交：`[gui] add property mutation transaction`

- [-] `GI-202` Runtime visual/layout 字段封装
  - 依赖：GI-105、GI-201
  - 修改：按真实写路径将 `_text/_size/_position/_visibility/...` 收为 backing field。
  - 验收：
    - runtime 业务路径只经 setter/property API；
    - authoring-only 例外有清单和删除条件；
    - 现有反射/序列化仍工作。
  - 提交：按控件族拆成多个 `[gui/widgets] encapsulate ... properties` 小提交。
  - 进度：切片 1（基类 `_position/_size/_visibility`）已提交 `70ece56c`；切片 2（
    `UIText::_text` + `UIPanel::_color`）已提交 `1a4765e2`；剩余 authoring-only 字段
    （`UIText::_fontSize/_color/_hAlign/_vAlign`、`UIPanel::_image/_bNineSlice/_nineSliceBorder`、
    `UIButton` 颜色）已登记例外清单，待其有 runtime 写路径再迁移。

- [x] `GI-203` Direct-write 静态门禁
  - 依赖：GI-202
  - 修改：增加简单 grep/lint，禁止 GUI owner 外新增 runtime direct write。
  - 验收：门禁能捕获故意添加的违规赋值；不误报初始化/serializer 白名单。
  - 提交：`[gui] guard runtime property writes`

## P2 — 统一 Paint Pipeline 与 Cache Validity

- [x] `GI-301` Paint scope RAII
  - 依赖：GI-102
  - 修改：收口 push/pop paint widget，保证 early return/未来异常路径成对恢复。
  - 验收：嵌套 paint/current widget 测试通过。
  - 提交：`[gui] scope reactive paint collection`
  - 进度：`Reactive.h` 新增 `PaintScope` RAII（构造 push / 析构 pop，不可拷贝）；基类
    `UIElement::paint` 与 Container/ScrollViewport/SplitPane 四处手动 push/pop 全部改为
    PaintScope；新增 `PaintScopeRestoresStackOnNestedScope` 与
    `PaintWalkRestoresReactiveStack` 两个测试（153 tests PASSED）。

- [x] `GI-302` `paintChildren()` customization point
  - 依赖：GI-301
  - 修改：
    - 基类拥有唯一 self rebuild/reuse pipeline；
    - Container、ScrollViewport、SplitPane 只定制 child traversal/clip。
  - 验收：
    - 三者不再覆盖完整 `paint()`；
    - split per-child clip、scroll/container clip parity；
    - perf counter 语义一致。
  - 提交：`[gui] unify widget paint pipeline`
  - 进度：`UIElement::paintChildren` 改为 virtual；Container/ScrollViewport/SplitPane 删除
    `paint` 覆盖、改为覆盖 `paintChildren`（Container 条件 clip、Scroll 视口 clip、Split
    per-pane clip），基类 paint 成为唯一 self rebuild/reuse 模板；新增
    `ScrollViewportClipsContentToViewportRect`、`SplitPaneClipsChildrenToOwnPaneRect`、
    `LayoutHostsReuseSelfSegmentWhenClean` 三个测试（156 tests PASSED）。

- [ ] `GI-303` Build-context generation
  - 依赖：GI-002
  - 修改：host/tree 建立稳定 context/resource generation token；兼容时复用，不兼容时清两份 cache。
  - 验收：GI-002 通过；同 context clean frame 仍复用。
  - 提交：`[gui] invalidate draw cache on context changes`

- [ ] `GI-304` Inherited context invalidation
  - 依赖：GI-106、GI-302
  - 修改：按 audit 结论实现 subtree invalidation 或 context generation。
  - 验收：ancestor clip/visibility/context 改变后 descendants 输出正确。
  - 提交：`[gui] track inherited paint context changes`

- [ ] `GI-305` Phase 1/2 convergence gate
  - 依赖：GI-103、GI-105、GI-202、GI-302~GI-304
  - 验收：
    - closure/widgets/workspace tests；
    - Workbench smoke/scenario；
    - CPU snapshot JSON；
    - GPU/offscreen zero-diff；
    - baseline 对比写入 progress。
  - 提交：`[test/gui] cover invalidation convergence`

## P3 — 条件项：Batching

- [ ] `GI-401` Batching assessment
  - 依赖：GI-305
  - 启动条件：notify/dependent visits 是可测热点。
  - 输出：收益估算、flush owner、lifetime 方案。
- [~] `GI-402` Dirty transition coalescing
  - 依赖：GI-401 结论为实施。
  - 停止线：需要复杂全局 scheduler 而收益有限则取消。

## P4 — 条件项：CPU Subtree Paint Cache

- [ ] `GI-501` Boundary candidate profiling
  - 依赖：GI-305
  - 启动条件：clean traversal 是主要瓶颈。
  - 输出：候选 subtree、parent/child repaint 频率、预计 visited widget 降幅。
- [~] `GI-502` CPU subtree cache design
  - 依赖：GI-501 结论为实施。
  - 必须覆盖 nested boundary、hierarchy/clip/layout invalidation 和 ownership。
- [~] `GI-503` CPU subtree cache implementation
  - 依赖：GI-502 review 通过。

## P5 — 条件项：Keyed List Reconciliation

- [ ] `GI-601` 第二真实消费者确认
  - 依赖：GI-305
  - 启动条件：至少两个 dynamic list 需要 identity/state preservation。
- [~] `GI-602` Key/type identity contract
  - 依赖：GI-601。
- [~] `GI-603` 最小 list reconciler
  - 依赖：GI-602；不建设通用 Virtual DOM。

## 延后能力

- [~] 区域级 dirty region：只有 CPU item rebuild 与提交仍是热点时参考 Qt。
- [~] immediate convenience layer：为工具 UI 独立立项，不混入失效正确性主线。
- [~] composited repaint layer：当前不做。
- [~] 局部 layout boundary：当前不做。
- [~] GUI 动画接入：独立切片，设计见 `animation-integration.md`。动画值建模为 `Reactive<T>`、paint 时解析、tick 挂 host 层；不新增 tree 层/失效通道/不改 snapshot。启动条件：Phase 1A 落地 + 出现首个真实动画需求。
