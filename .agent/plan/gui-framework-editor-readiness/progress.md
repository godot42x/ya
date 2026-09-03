# Progress

## 当前状态

- 计划建立：2026-09-03
- 实现改动：Phase 3F 已完成一轮闭环
- 提交：待本轮 checkpoint 创建
- 当前阶段：Phase 3F（ReactiveList mutation/update contract）

## 已确认

- Slot-first layout 已完成主迁移。
- WidgetTree retained 基础、snapshot、focus、popup、drag/drop 已可运行。
- GUIWorkbench 和 EditorSurface 已提供真实 consumer。
- 完整 GameEditor 仍未完成，主要缺口是 cache correctness、增量布局、Reactive 数据契约、editor data/undo/selection/commands、retained editor primitives 和 ImGui 清理。

## 本轮已做

- 为 UIElement 增加 subtree layout dirty mask，区分 measure / arrange。
- child measure dirty 沿祖先传播；slot arrange dirty 绑定到 owning host。
- 新增 child measure 传播与 slot arrange 归属测试。
- 修正 detach/reattach 测试的 rebuild 统计口径。
- xmake r ya-gui-closure-test 全量通过（328/328）。
- 未修改既有 `.gitignore`。

## Phase 0 已完成

- 已建立本计划的目标、硬边界、阶段路线、验收标准和能力矩阵。
- 已将 GUI skill 中过时的 path-B、child authored geometry 和 GUIAppHost compatibility alias 语义改为当前最终契约。
- 未引入任何 legacy 兼容内容。

## Phase 1 当前 checkpoint

- 已为每个 `UIElement` 增加单调递增 runtime id，paint cache 不再以裸地址作为身份。
- `WidgetTree::detach` 会清理被移除 subtree 在双缓冲 cache 中的绘制段。
- `UIFrameDrawItem` 增加完整字段比较，G2 校验覆盖 texture、UV、font、corner radius、text scale 等字段。
- 新增 detach/reattach paint-cache 回归测试。
- 新增 cross-tree reparent cache 回归测试，确认旧 tree 不保留 widget、目标 tree 首次构建重新绘制。
- 新增 destroy/reallocate runtime identity 测试，以及 drag ghost cancel 后 snapshot 清理测试。
- 明确 snapshot build mutation policy：业务 paint/layout 回调禁止结构变更，框架 tooltip/drag 维护仅在显式边界执行。
- `xmake r ya-gui-closure-test` 全量通过（325/325）。

## Phase 1 已完成

- paint cache identity、detach 清理、cross-tree 隔离、对象重分配和 drag ghost teardown 已形成完整回归。
- snapshot/full repaint 校验已覆盖当前 `UIFrameDrawItem` 的全部渲染字段。
- 当前未发现需要额外引入新的 cache manager、compat 层或 legacy 双写。

## Phase 2A 当前 checkpoint

- `WidgetTree::invalidateLayout` 现在接受 `Arrange / Measure / Structure` scope。
- Slot 的 arrange/measure setter 已分别标记对应 scope，不再全部降级为同一种 layout dirty。
- Tree 内部使用累计计数，`GuiPerfStats` 在每次 snapshot 中提供累计 scope 统计。
- `WidgetTree` 文档明确 snapshot build 期业务结构变更策略。
- 新增 `LayoutInvalidationScopesAccumulateAcrossSnapshots` 回归测试。
- 完整 GUI closure 通过：326/326。

## Phase 2A 已完成

- 完成 dirty taxonomy 的第一条可运行契约。
- 尚未实现真正的 subtree layout skip、measure cache、arrange cache；这些保留给后续 checkpoint。

## Phase 2B 当前 checkpoint

- child measure dirty 会沿布局祖先传播，确保父布局能够感知子内容尺寸变化。
- arrange dirty 绑定到 slot owning host，避免把几何变更误判为更大范围结构脏。
- 统计口径仍是 tree-level；本阶段只验证传播与分类，没有宣称局部跳过已经完成。

## Phase 2C 当前 checkpoint

- UIElement::layoutAssigned() 增加 assigned-rect proof：节点 rect 未变、节点及其祖先无 layout dirty 时，跳过该节点的 layout/children walk。
- tree-level layout invalidation 会确保 root 重新进入一次布局，clean sibling subtree 仍可局部跳过。
- attach/reparent 会清除旧 assigned-layout proof，并标记新 parent 的结构布局 dirty，避免跨 host 的 slot intent 或旧布局证明泄漏。
- UILayout 的 measure/arrange 参数变化现在标记 owning host 的节点 dirty，而不是只标记 tree bool。
- GuiPerfStats::layoutSkippedWidgets 提供累计局部跳过计数。
- 新增 Canvas sibling skip 与跨 host reparent 回归；修正 direct construct container 布局回归。
- targeted 与全量 xmake r ya-gui-closure-test 均通过（329/329）。

### Phase 2C 保留项

- 当前只实现 assigned-rect subtree skip，尚未引入独立 measure cache / arrange cache；后续必须以 generation/依赖契约补齐，不能把 bool proof 扩展成隐式全局缓存。

## Phase 2D 当前 checkpoint

- 将 assigned-layout proof 从 bool 升级为单调 layoutRevision / assignedLayoutRevision，任何 layout/arrange invalidation 都会使旧 proof 失效。
- attach/reparent 会推进 revision 并清空 assigned revision；同一 rect 也必须重新消费新的 parent-owned slot/layout contract。
- layout host 的 measure/arrange 参数变化改为标记 owning host 的节点 dirty，revision 与 dirty mask 一起形成可解释的失效边界。
- 针对跨 host reparent、同 rect 重新布局、Canvas sibling skip 和 direct-construct box layout 的 targeted + 全量 closure 回归均通过（329/329）。

### Phase 2D 未完成

- 尚未实现独立 measure cache / arrange cache，也未宣称当前 skip 已覆盖所有 specialized layout；下一阶段先补可观测计数与 cache dependency contract。

## 下一 checkpoint

## Phase 3A 当前 checkpoint

- 新增 ReactiveTransaction，嵌套作用域只在最外层退出时 flush pending refs。
- 同一事务内对同一 Reactive 的多次写入只遍历一次依赖；same-value 写入仍被抑制。
- 通知时先复制 dependent 列表，允许回调期间 bind/unbind/detach，不依赖正在修改的 vector。
- ReactiveBase 销毁会从 pending 队列移除自身，避免事务退出时访问已销毁 signal。
- 新增 transaction coalescing 回归；全量 closure 通过（330/330）。

### Phase 3A 边界

- transaction 是同步 UI-side contract，不提供跨线程投递；UI-thread 约束和跨线程诊断仍待后续 checkpoint 明确。

## Phase 3B 当前 checkpoint

- ReactiveList 增加 revision、keyed replaceKeyed() 与结构 diff 观测面（inserted/removed/moved）。
- keyed replace 会拒绝空 key / 重复 key，避免 list identity 模糊。
- push/removeAt/clear/replace/replaceKeyed 现在都会推进 list revision，并保留最近一次结构 diff。
- UITreeView 开始消费 keyed identity：根数据 revision 变化时会按 live node ids 清理 _expanded 中已移除节点的 transient state，而不是让旧状态泄漏到重新出现的同名节点。
- 新增 keyed ReactiveList diff/revision 回归，以及 TreeView keyed replace 清理 removed expansion-state 的真实 consumer 回归。
- 全量 xmake r ya-gui-closure-test 通过（332/332）。

### Phase 3B 边界

- 当前是 keyed identity/diff baseline，还不是完整 keyed list reconciler：尚未提供细粒度 update API、row instance 复用协议或 UI-thread 诊断。

## Phase 3C 当前 checkpoint

- Computed<T> 不再每次 get() 都重算；现在是 dirty-on-upstream-change、read-time lazy recompute。
- selector 求值期间会收集上游 Reactive / ReactiveList / Computed 依赖，并建立 computed-to-computed / source-to-computed 传播链。
- 上游变化会把下游 computed 标 dirty，并继续通知其下游 widgets / computeds，形成真正的 dependency graph，而不是“每次读取重跑 selector”。
- 新增 recompute/cycle diagnostics，循环 selector 会输出错误并保留最近一次稳定 cache，而不是无限递归。
- 新增 lazy cache、多层 computed 脏传播、cycle 检测回归；全量 xmake r ya-gui-closure-test 通过（335/335）。

### Phase 3C 边界

- 仍未冻结跨线程 mutation 与更严格的 reentrancy contract；ReactiveList 也还没有细粒度 update/move API。

## Phase 3D 当前 checkpoint

- ReactiveBase 析构时会反向通知 computed dependents 执行 unregisterUpstream(this)，不再把已析构 upstream 留在 downstream computed 的 _upstreams 里。
- Computed 保持双向解绑：自身析构会从 upstream 的 computed-dependent 列表中移除，上游析构也会把自己从 downstream 的 upstream 列表移除。
- 新增 upstream 先析构的回归，验证 computed upstream unlink diagnostics 触发且析构顺序安全。
- 全量 xmake r ya-gui-closure-test 通过（336/336）。

### Phase 3D 边界

- 这次解决的是 detach/unbind/lifetime safety，不包含跨线程 mutation 约束；UI-thread/reentrancy contract 仍待单独冻结。

## Phase 3E 当前 checkpoint

- Reactive / ReactiveList 的所有 mutation 入口现在都经过统一的 UI-thread 校验；首个 mutation 线程会被冻结为 mutation owner，其他线程写入会被拒绝并记入 diagnostics。
- notifyDependents() 期间再次触发的 mutation 不再递归立即重入通知；会被加入 pending queue，等最外层 notification/transaction 结束后再 flush。
- 新增 foreign-thread mutation rejected 回归，以及 reentrant mutation deferred until outer notify completes 回归。
- 全量 xmake r ya-gui-closure-test 通过（338/338）。

### Phase 3E 边界

- 这次冻结的是 mutation 线程与重入通知语义；尚未扩展到 keyed list 的细粒度 insert/update/move API，也没有跨线程投递队列。

## Phase 3F 当前 checkpoint

- ReactiveList 增加 editor 可复用的 insertAt、updateAt、move mutation API，并为 push/removeAt 增加明确的成功/失败返回语义。
- keyed list 下插入/追加会拒绝空 key 与重复 key；updateAt 禁止改变既有 identity key；越界操作不会修改数据。
- replaceKeyed 支持可选 value comparator；未提供 comparator 时对 retained key 保守报告 updated，避免相同 key 的内容变更静默丢通知。
- clear 现在会完整报告 removed indices，Tree/Table consumer 可以据此清理对应行状态。
- mutation 都推进 list revision 并更新最近一次 diff；现有 ReactiveTransaction 仍可对多次 mutation 做批量通知合并。
- 新增 keyed mutation identity、边界拒绝、update/move/clear diff 回归；全量 xmake r ya-gui-closure-test 通过（339/339）。

### Phase 3F 边界

- 当前已具备可复用 mutation/update contract，但还没有把 TableGrid 改造成 keyed row virtualization，也没有通用 row-instance reconciler；这些属于 Phase 4/7 的动态结构与 editor primitive 工作。

下一 checkpoint：Phase 4A，补齐 DSL fragment/group/helper、conditional/switcher 的可读组合能力，并保持 typed Slot 与 direct-live 架构不变。
