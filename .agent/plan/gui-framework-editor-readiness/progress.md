# Progress

## Phase 10A-dir 当前 checkpoint（2026-09-06）

- Dock 相关源码与公开转发头从 `Controls/` 平铺迁到 `Controls/DockSpace/`（`DockNode` / `DockContext` / `UIDockSpace` / `UIDockFloatingHost` / `UIDockFloatingWindow`）。
- 公开 include 改为 `GUI/Widgets/Controls/DockSpace/...`；`ya-gui-widgets` 用 `Controls/**` 收集子目录。TabBar 仍留在通用 Controls。
- 验证：`xmake b ya-gui-closure-test`；`xmake r ya-gui-closure-test -- --gtest_filter='WidgetLayoutTest.*:DockNodeTest.*:WidgetTreeTest.Dock*'`。

### Phase 10A-dir 保留项

- 未宣称 retained editor ready；Windows/MSVC、OpenGL presentation、soak/imgui 仍是后续门禁。
- 其余 10E tab owner 未做。

## Phase 10A-name 当前 checkpoint（2026-09-06）

- 拆开 Dock 会话与投影的命名：`UIDockWorkspace` 改为 `FDockContext`（`F` 前缀、非 widget），文件 `DockContext.h/.cpp`。
- `UIDockSpace` 只表达 in-window docked-tree 投影；`UIDockFloatingHost` 只表达 floating 投影。绑定改为 `setContext` / `bindContext` / `syncFromContext`。
- 验证：`xmake b ya-gui-closure-test`；`xmake r ya-gui-closure-test -- --gtest_filter='WidgetLayoutTest.*:DockNodeTest.*:WidgetTreeTest.Dock*'`。

### Phase 10A-name 保留项

- 未宣称 retained editor ready；Windows/MSVC、OpenGL presentation、soak/imgui 仍是后续门禁。
- 其余 10E tab owner 未做。

## Phase 10A-fix 当前 checkpoint（2026-09-05）

- Dock 的 split-resize / 选 tab / hide-tab-bar 不再从 live chrome 回调里 `fireDockUpdated()` 整树 rematerialize；这些路径只改 live 投影并 `notifyDockLayoutListeners()` 做 layout 持久化。
- `hideTabBar` 只 Collapsed 掉 leaf tab strip（title bar），面板 widget 继续挂在 `DockContent` 并填满 leaf；右键菜单 `Hide Tab Bar` 与左上角 12px 折角都走同一条 live 路径。
- `rebuildProjection()` 在拆掉旧 chrome 之前先 unlink 已挂载 panel，再 `reparent`/`attach`；禁止对仍有 parent 的 `HierarchyBody` 等 panel `addDetachedChild`。
- 测试：`WidgetLayoutTest.DockHideTabBarClickHidesStripAndKeepsPanelContent` / `DockTabBarContextMenuHidesTitleBarOnly` / `DockSplitResizeKeepsPanelAttachedWithoutRematerialize` / `DockProjectionRebuildReparentsLivePanelWidgets`。
- 验证：`xmake b ya-gui-closure-test`；`xmake r ya-gui-closure-test -- --gtest_filter='WidgetLayoutTest.*:DockNodeTest.*'`（100/100）；全量 `xmake r ya-gui-closure-test`（402/402）。

### Phase 10A-fix 保留项

- 未宣称 retained editor ready；Windows/MSVC、OpenGL presentation、soak/imgui 仍是后续门禁。
- 其余 10E tab owner（Content Browser / UI Designer / Runtime Tools / Asset Inspector）未做。

## Phase 10F-1 当前 checkpoint（2026-09-05）

- 将 PropertySlot 的 map key 从空字符串 sentinel 改为 `std::optional<std::string>`；field、sequence element、map value 三种定位状态现在可区分，空字符串 key 不再与 field 混淆。
- PropertyAccessor 与 PropertyHandle 的 map 访问、删除及 retained Inspector remove-row 路径已同步迁移到 optional key 判别。
- 新增 `PropertyAccessorTest.EmptyStringMapKeyIsRepresentedAsMapSlot`，覆盖空 key 定位、读取、写入和删除。
- 验证：`xmake b ya-testing`；`xmake r ya-testing -- --gtest_filter='PropertyAccessorTest.*:EditorPropertyGraphTest.*'`（33/33）。

### Phase 10F-1 保留项

- 本 checkpoint 只冻结 slot 定位契约；尚未处理 PropertyHandle 裸实例地址生命周期、结构化 mutation 结果、all-or-nothing 多选写入，以及 Core Reflection 与 editor projection 的职责拆分。
- 下一 checkpoint 进入稳定 PropertyInstance identity/resolver 设计，必须先覆盖 Undo 对象销毁/重建场景，再迁移调用方。

## Phase 10F-4A 当前 checkpoint（2026-09-05）

- PropertyHandle 的多实例写入和容器 mutation 入口现在会先预检全部实例的 mutable address；任一实例不可访问时，整个操作在写入前拒绝。
- 模板 set/restore、integer、enum、color、asset 和 sequence/map mutation 均接入该 preflight，降低多选编辑留下部分写入的风险。
- 新增 EditorPropertyGraphTest.MultiInstanceWritePreflightsAllAddresses，验证第二个实例不可用时第一个实例保持原值。
- 验证：xmake b ya-testing；单测 EditorPropertyGraphTest.MultiInstanceWritePreflightsAllAddresses 通过。

### Phase 10F-4A 保留项

- 这不是完整的结构化 mutation result，也不是跨自定义 setter/容器异常的事务回滚；后续仍需引入明确的 all-or-nothing transaction contract 和错误枚举。
- PropertyHandle 仍保存裸实例地址；稳定 PropertyInstance identity/resolver 仍是下一主要 checkpoint。

## Phase 10F-2A 当前 checkpoint（2026-09-05）

- PropertyHandle 新增可选 InstanceResolver；每次 validity/access/mutation 前刷新实例地址，允许对象替换后重新解析，而不是继续使用旧地址。
- EditorInspectorTab 将 resolver 接入顶层 component property binding：通过 Scene + Entity handle + component type 重新取得当前 component。
- 嵌套 composite leaf 暂未接入 resolver，因为当前 Core collectLeaves 仍只携带 nested owner 的裸地址；该边界明确保留给后续 PropertyGraphBuilder 迁移。
- 新增 EditorPropertyGraphTest.PropertyHandleRefreshesResolvedTopLevelInstance，验证对象替换后写入新实例且旧实例不变。
- 验证：xmake b ya-testing；顶层 property resolver targeted tests 通过。

### Phase 10F-2A 保留项

- 这是顶层 component 的 resolver 增量，不是完整 PropertyInstance identity 系统；当前 resolver 仍捕获 Scene 指针和 EnTT handle，尚未统一为跨场景稳定 identity 对象。
- Undo 对 nested/composite property 仍可能捕获裸 nested address；必须在移出 collectLeaves 前完成完整路径 resolver，不能用局部猜测补齐。

## Phase 10F-4B 当前 checkpoint（2026-09-06）

- PropertyHandle 现在会对 sequence remove/insert 的索引、map value 的存在性和 map key 插入冲突做全实例预检。
- 多选容器 mutation 在实例形状不一致时会在写入前拒绝，不再依赖底层容器对越界操作的静默 clamp/no-op 行为。
- 新增 EditorPropertyGraphTest.MultiInstanceContainerMutationPreflightsShape，验证 vector 长度不一致时不会只修改较长实例。
- 验证：xmake b ya-testing；容器 preflight targeted test 通过。

### Phase 10F-4B 保留项

- 结构化 mutation result 和异常/自定义 accessor 的事务回滚仍未完成；当前是成功前置条件预检，不是完整 command transaction。
- 稳定 PropertyInstance identity/resolver、nested path resolver 和 Core/editor 职责拆分仍待后续 checkpoint。

## 当前状态

- 计划建立：2026-09-03
- 实现改动：Phase 5B style key/type catalog 已完成闭环
- 提交：本 checkpoint 随代码一并提交
- 当前阶段：Phase 5B（theme key + style type catalog/schema 校验）

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

Phase 4A 已完成；以下记录本 checkpoint 的闭环与边界。

## Phase 4A 当前 checkpoint

- 新增纯 authoring-time ui::fragment(...) 与可读别名 ui::group(...)；它们只在父 builder 上展开，不创建运行时 widget，不引入 Description/Reconciler。
- 新增构造期条件组合 ui::when(...)、ui::unless(...)、ui::ifElse(...)。条件只决定本次 live construct 是否添加 child；运行时动态结构仍必须由专用控件和 ReactiveList 管理。
- fragment/conditional 通过父 builder 的既有 child(...) 重载展开，因此每个 child edge 继续使用 parent-specific typed SlotArgs；错误的 canvas/box slot 在编译期被拒绝。
- 新增 editor-like 组合回归，验证 fragment flatten、条件分支和匿名节点行为；新增静态 typed-slot acceptance/rejection 断言。
- xmake b ya-gui-closure-test && xmake r ya-gui-closure-test：340/340 通过。

### Phase 4A 边界

- 尚未实现运行时 retained switcher，也没有把 when 误用成每帧重建机制；动态集合继续留在列表控件内部。
- 尚未补通用 withSlot(...) fragment item 语法；当前已有的 .child(node, typedSlot) 保持唯一布局入口，避免 DSL 再造 slot 层。

下一 checkpoint：Phase 4B，基于真实 editor-like 页面审计动态结构边界，先完成 keyed row-instance reconciler / virtualization 的最小闭环，再扩展 switcher 或页面级动态组合。

## Phase 4A follow-up checkpoint

- DSL builder 现在拒绝同一父节点下重复的显式 sibling key；匿名节点仍允许并保持无稳定 identity。
- 拒绝发生在 child edge 加入前，避免重复 key 进入 live WidgetTree；日志提供 parent/key 诊断。
- 新增重复 key 回归测试；closure 全量通过：341/341。

下一 checkpoint仍为 Phase 4B：keyed row-instance reconciler / virtualization 最小闭环。

## Phase 4B 当前 checkpoint

- UITableGrid 的 selection 在 keyed ReactiveList<FTableRow> 上改为 identity-backed：控件记录选中行 key，列表发生 insert/move/remove 后按 key 重映射 index。
- 外部直接写入 Reactive<int> 仍被视为新的 selection intent；只有列表 revision 变化时才执行旧 key 的位置恢复，避免把旧选中对象错误替换成新位置对象。
- 被删除的选中 key 会明确落为 -1，同时清除 hover 行 transient state；非 keyed 数据源保持原有 index selection 语义。
- 新增跨 insert/move/remove 的真实 binding 回归；closure 全量通过：342/342。

### Phase 4B 边界

- 当前完成的是 keyed row selection/lifecycle 基础契约，不是 row widget instance reconciler，也不是 virtualization；TableGrid 仍按数据绘制行文本。
- 下一步仍需先定义可复用 row factory、visible range 与 transient state ownership，再引入真正的 row instance 复用。

## Phase 4B identity migration checkpoint

- UITableGrid 的公开 selection API 已从 Reactive<int> row index 全量迁移为 Reactive<std::string> row id；旧 index 绑定路径已删除，不保留兼容双写。
- keyed list 发生 insert/move/remove 时，selection 直接保持 row id；被删除的 id 归为空字符串。
- GUIWorkbench table demo 与 BindingContractTest 已迁移到 keyed selection；回归覆盖点击、外部 selection 修改及结构变更。
- closure targeted table tests 通过；提交前需完成全量 closure。

### Identity migration 边界

- TableGrid 仍是 flat paint consumer，尚未引入可复用的 row widget instance、visible-range virtualization 或 row factory。
- 下一 checkpoint 需先定义 row instance ownership / factory / slot lifecycle，再实现真正的 keyed row reconciler；不得恢复 index selection 兼容层。

## Phase 4B row-instance checkpoint

- 新增 UIKeyedChildReconciler，以 parent-owned child edge 为生命周期边界：按非空唯一 key 创建、复用、移除和重排 live UIElement。
- 同 key 的 widget 实例在 reconcile 后保持原指针与 transient state；消失 key 通过 WidgetTree::detach 清理 tree membership、focus/capture/paint cache；顺序调整使用同 parent reparent，保留原 Slot。
- 新增 factory failure、duplicate/empty key rejection 的统一诊断入口，并要求 reconciler 运行在已挂载 parent 上，避免 detached subtree 的隐式生命周期。
- 新增真实 closure 回归，验证三轮 reconcile 的创建次数、实例复用、移除和顺序；targeted 与全量 closure 均通过：342/342。

### Row-instance checkpoint 边界

- 这是通用 keyed child lifecycle 基础设施，还没有把 TreeView/TableGrid 的 flat paint 改造成 row widget factory，也没有 visible-range virtualization。
- 下一步将把该 reconciler 接入一个真正的 editor-like list consumer；在接入前先定义 row slot initializer、可见范围和滚动 offset 的契约。

## Phase 4B Content Browser consumer checkpoint

- EditorSurface 的 Content Browser mount list 与 entry list 已改用 UIKeyedChildReconciler；目录刷新不再先 detach 全部 rows 再重建。
- 同名 mount/entry 在刷新时复用原 UISelectableRow 及其 label child，只更新 item id、文案、选中态和 activate callback；消失条目通过 reconciler detach，顺序变化保留原 BoxSlot。
- UIKeyedChildReconciler 增加 optional updater：create/reuse 之后对当前 key 顺序调用，供 consumer 刷新文案和回调而不重建实例。
- Content Browser 的两类列表仍由 FileExplorer fingerprint 驱动，未引入每帧重建；Scene Save dialog 暂不迁移，避免把两个 consumer 混入一个 checkpoint。
- 修正 EditorInspectorTab 中 canvas parent 错用 overlaySlot 的真实 typed DSL 错误。
- 新增 updater 复用回归；xmake r ya-gui-closure-test 全量通过（344/344）；ya-game-editor 构建通过。

### Consumer checkpoint 边界

- 该 consumer 已具备 keyed child instance 复用，但滚动容器仍由 UIScrollViewport 管理，尚未实现 visible-range virtualization；当前所有目录 rows 仍保留在 live tree。
- 下一步迁移 Scene Save rows，并随后设计真正的 row factory + visible range/scroll offset contract。

## Phase 4B Scene Save consumer checkpoint

- EditorSurface 的 Scene Save mount list 与 entry list 已改用同一套 UIKeyedChildReconciler + updateContentRow 协议；目录刷新和选中态变化不再先 detach 全部 rows 再重建。
- 同 key 的 Scene Save row 在选中目录变化时复用原 UISelectableRow，只更新 selected / callback；关闭对话框会 reset reconciler，避免持有已销毁 parent。
- Content Browser 与 Scene Save 共用 row factory 与 preferred-height slot 写入，不引入第二套 list lifecycle。
- 新增 selectable-state updater 回归，锁定“选中对象变化不重建 row instance”。
- xmake r ya-gui-closure-test 全量通过（345/345）；ya-game-editor 构建通过。

### Scene Save checkpoint 边界

- 两个 editor list consumer 都已接入 keyed instance reuse，但仍没有 visible-range virtualization；大目录仍会把全部 rows 留在 live tree。
- 下一 checkpoint 设计 row factory + visible range / scroll offset contract，再进入真正的虚拟化；不得恢复 detach-all rebuild。

## Phase 4B visible-range checkpoint

- 新增 FKeyedVisibleWindow / computeKeyedVisibleWindow / sliceKeyedVisibleWindow：由 itemCount、row extent、spacing、viewport、scroll offset、overscan 计算 first/count 与 leading/trailing spacer。
- 未布局的 viewport（extent<=0）会物化全表，避免第一帧没有内容高度；过扫描在列表两端被 clamp。
- UIKeyedChildReconciler 增加 SlotBinder，create/reuse 后按当前 key 顺序写 parent-owned slot（Content Browser / Scene Save 的 row height 走这条路径，不再在 updater 里改 slot）。
- Content Browser entry list 改为 leading SizeBox + keyed visible rows + trailing SizeBox；滚动 offset / viewport 变化会刷新窗口，目录切换重置 scroll。Mount list 与 Scene Save 仍全量物化。
- 新增 window/overscan 与 spacer 保高回归；xmake r ya-gui-closure-test 全量通过（347/347）；ya-game-editor 构建通过。

### Visible-range checkpoint 边界

- 这是均匀行高的窗口化，不是 widget pooling：滚出窗口的 key 会 detach，滚回来走 factory 重建。
- TableGrid / TreeView 仍是 flat paint consumer；Scene Save 与 mount list 未窗口化。
- 下一阶段可进入 Phase 5 style/resource invalidation，或把同一窗口契约接到 TableGrid/Hierarchy。

## Phase 5A 当前 checkpoint

- `lookupStyleFieldImpact` / `lookupStylePatchImpact` 按反射字段分类 layout/paint/resource：`FBrush` 为 Paint+Resource；`fontSize` / `padding` / `minSize` 为 Layout；`FScrollBarStyle.width` 保持 Paint-only（overlay 厚度，不是布局输入）。
- `UIStyledWidget::setStyleField` / `setStyle` / `clearStyleField` / `clearAuthoredStyle` 默认走 catalog；`UIText::setFontSize` 仍可用显式 impact 按 AutoSize 覆盖。
- Resource 标志只是后续异步就绪 invalidation 的 metadata，本 checkpoint 不改变 `invalidateProperty` 枚举。
- 新增 catalog、patch union、UIText/UIButton/UIPanel dirty 回归。
- xmake r ya-gui-closure-test 全量通过（351/351）。
- 未完成：resource-ready invalidation、visual state matrix。

### Phase 5A 边界

- 没有引入 per-type 字段表，也没有做 schema key 校验或 brush/font ready 传播。
- `FButtonStyle.padding` 当前不驱动 button content layout（仍是 widget `_contentLayout`），catalog 仍把 `padding` 标成 Layout，避免 Tab/Text badge 和后续消费者漏测。

## Phase 5B 当前 checkpoint

- `YA_GUI_STYLE_CATALOG` / `StyleKey::*` 是 theme key 与 `TStyle` 的单一词汇；`lookupStyleKey` 区分 Known / Empty / UnknownKey / TypeMismatch。
- `UITheme::define`、`UIElement::setStyleKey` 和 document `deserializeFields` 走 `diagnoseStyleKey`：未知 key 与类型不匹配记诊断并 WARN，仍写入（明确诊断，不静默吞掉，也不硬拒绝）。
- `editor.<key>` 复用同一词汇，不是 EditorTheme 第二套机制。`canvas` 登记为 FPanelStyle 角色 key，`ui::canvas()` 使用 `StyleKey::Canvas`。
- `YA_GUI_AUTHORED_STYLE_IO` 现在报告 `getStyleTypeIndex()`，unstyled widget 为 0。
- 新增 catalog 全量 Known、editor prefix、unknown/mismatch 诊断，以及 document deserialize 未知 key 回归。
- xmake r ya-gui-closure-test 全量通过（354/354）。
- 未完成：resource-ready invalidation、visual state matrix。

### Phase 5B 边界

- 没有做编译期 DSL 类型拒绝（builder 仍收 string）；catalog 常量提供可在 C++ 侧引用的词汇。
- 没有要求每个 theme 必须 define 全部 catalog key；缺 key 仍 fallback 到 `TStyle{}`。
- 构造函数直接写入的默认 family key 不走 diagnose（派生 `getStyleTypeIndex` 在基类构造期不可用）；attach 后的 setStyleKey / 文档加载会校验。

## Phase 5C 当前 checkpoint

- `FontManager::resourceRevision()` 在 register/load/unload/clearCache 以及 `flushPendingGlyphs` 真正捕获新字形时递增。`WidgetTree::buildSnapshot` 消费该 revision：首次只记录，之后变化则对整棵已挂载树 `markLayoutDirty(ResourceReady)`，避免嵌套 fill 容器凭 assigned-rect skip 用过期文字度量。
- `UIFrameBuildContext.generation` 只表示 resolver identity（brush/image 纹理就绪）；scale/offset/DPI 仍是 `BuildContextChanged`。Host 继续在 snapshot 之后 `flushPendingGlyphs`（Core Rule 6），不再自己 `invalidateSubtree`。
- 没有把 `EUIPropertyImpact::Resource` 接到 `invalidateProperty`；`bResource` 仍是字段 metadata，就绪事件在树级 revision / generation。
- 新增 FontManager revision、嵌套文字 remasure、image resolver miss→generation bump 命中纹理的回归。
- Theme switch / style key 仍由既有 Phase 5 测试覆盖。
- xmake r ya-gui-closure-test 全量通过（357/357）。
- 未完成：visual state matrix。

### Phase 5C 边界

- 没有把 FontManager 接到 GUI Reactive（跨层依赖）。1 帧字形延迟仍在：paint 登记缺失、flush 后下一 snapshot 才消费 revision。
- 没有做 visual state matrix；稀疏/full freeze API freeze 与 naked color 清理不在本 checkpoint。

下一 checkpoint：Phase 5 visual state matrix（normal / hovered / pressed / focused / disabled / selected / error / drop-target）。

## Phase 5D 当前 checkpoint

- `EWidgetVisualFlag` / `composeVisualFlags` / `FVisualChrome` / `resolveVisualFill` 冻结 exclusive fill 优先级：Disabled > DropTarget > Error > Pressed > Selected+Hovered > Selected > Hovered > Focused > Normal。
- `UIButton` 与 `UISelectableRow` 经 `visualChrome(style)` 消费该表；Button 补 selected/error/dropTarget fill；SelectableRow 的 drop 走 `dropTargetFill`（默认等于原 selectedHovered）。
- 没有把 hover/pressed 升到 UIElement 基类，也没有改 CheckBox/TextField/Menu/Tab/TableGrid 的本地 if/else。
- 新增优先级矩阵、disabled 盖住 hover、row drop 盖住 selected 的回归。
- xmake r ya-gui-closure-test 全量通过（360/360）。
- 未完成：其余控件接入同一 matrix；稀疏/full freeze API freeze；naked color 清理；无主题/资源缺失 GPU 回归。

### Phase 5D 边界

- Selected+Hovered 是组合刷，不是第九个独立 exclusive 状态。
- SelectableRow 仍不把 Pressed 送进 matrix（按下外观保持原 hover/select 语义）。
- 新控件 paint 必须走 `resolveVisualFill`，不得再写一套优先级。

下一 checkpoint：把剩余 interactive 控件接到同一 visual fill matrix，或进入 Phase 5 的 sparse/full freeze / naked-color / GPU fallback 回归。

## Phase 5E 当前 checkpoint

- CheckBox / ComboBox / MenuBar / Tab / MenuItem / TableGrid / TreeView 行填充改走 `visualChrome` + `resolveVisualFill`，不再各写一套 hover/selected if/else。
- CheckBox.checked 与 Tab.selected 映射为 Selected；Selected+Hovered 回落到 Selected，保留“选中盖住 hover”。
- Table/Tree 的 idle normal 是透明刷，paint 仍按 alpha 跳过未选中且未 hover 的行。
- 新增 mapping、checked+hover、selected tab 回归。
- xmake r ya-gui-closure-test 全量通过（363/363）。
- 未完成：TextField/SpinBox/Radio/DragFloat 等非这套 chrome 的控件；稀疏/full freeze API freeze；naked color 清理；无主题/资源缺失 GPU 回归。

### Phase 5E 边界

- 没有把 hover/pressed 升到 UIElement 基类。
- MenuItem 的 disabled 仍用 itemNormalFill（原行为），disabled 文案颜色仍是独立字段。
- TreeView 展开箭头 hover 仍用 `arrowHoveredFill`，不是行 chrome。

下一 checkpoint：Phase 5 稀疏/full freeze API freeze，或 naked color 清理，或无主题/资源缺失 GPU 回归。

## Phase 5F 当前 checkpoint

- `UIPanel` 不再在无 theme 时走 `_color` sprite 旁路；fill 只来自 `resolvedStyle()`。无 theme 且未 authored `fillColor` 时 `_color` 作为 fallback（对齐 `UIText`）。
- Image 仍是 content：themed 且无 authored overlay 时 theme chrome 赢；authored `setColor` 给 image tint。
- 新增 unthemed default fill 与 themed 忽略未 sync `_color` 回归。
- xmake r ya-gui-closure-test 全量通过（365/365）。
- 未完成：Workbench/demo `setColor` 字面量改走 theme key；TextField 等几何字段；无主题/资源缺失 GPU 回归。

### Phase 5F 边界

- `_color` 仍保留给 GI-202 `getColor`/`setColor` 与 document 反序列化 fallback，不是第二套 paint 路径。
- 没有改 Workbench demo 的裸 `setColor` 调用；那是 app 层 overlay，不是 framework 旁路。
- 0-extent panel 不再发出 draw item（`addBrush`/`sliceBrush` 跳过空 rect）。相关 snapshot 测试改为显式 `fixedSize`，而不是让空 rect 也能产出 sprite。

下一 checkpoint：Phase 5 GPU/无主题 fallback 回归，或进入 Phase 6 SelectionModel。

## Phase 5G 当前 checkpoint

- 同一棵树覆盖：无 theme 时 panel/image 走 style 默认；theme A/B 切换跟皮肤；resolver miss 画 placeholder；generation bump 后纹理进入 snapshot。GPU compose 只消费这份 snapshot，所以契约在 snapshot/headless 层冻结。
- `GUIHeadlessHost` 无 RHI 路径：第一帧 unthemed fallback，第二帧 theme switch 后颜色翻转。
- xmake r ya-gui-closure-test 全量通过（366/366）；xmake r ya-gui-headless-host-test 全量通过（3/3）。
- 未完成：Workbench/demo `setColor` 字面量；TextField 等几何字段；windowed `--gpu-shot` / `--offscreen-diff` 仍属 Phase 9。

### Phase 5G 边界

- 没有跑 windowed GPU/offscreen 像素 parity；那是 Phase 9 的 release gate，不是本 checkpoint 的 GPU 设备测试。
- UIImage 命中纹理后仍用内容字段 `_tint`，不是 chrome。
- 没有把 TextureLifetime 测试从 engine suite 搬进 closure（closure 已有 fake-texture 延迟就绪用例）。

下一 checkpoint：进入 Phase 6 SelectionModel。

## Phase 6A 当前 checkpoint

- `SelectionModel` 是 identity 选择源：selected 有序集合 + primary（必须在集合内或空）+ 独立的 hover/active/focus。不持有 Entity/Scene 指针。
- TreeView 通过 `primaryRef()` 共享同一模型；两棵树点击同步。EditorSurface 的 Hierarchy / ProjectList 绑定同一模型再写 EditorLayer。
- xmake r ya-gui-closure-test 全量通过（368/368）；xmake b ya-game-editor 通过。
- 未完成：command/action routing；undo/redo；property projection；viewport/inspector 写回同一模型；EditorLayer `_selections` Entity* 尚未替换。

### Phase 6A 边界

- TreeView 仍是单选写入 `primary`；多选 API（add/toggle）存在但控件还没有 modifier 手势。
- hover/active/focus 还没有接到 viewport/hierarchy 指针与键盘；本 checkpoint 只冻结模型。
- 没有把 EditorLayer 的 Entity* 选择向量删掉。

下一 checkpoint：Phase 6 command/action routing，或把 viewport 选择写入 SelectionModel。

## Phase 6B 当前 checkpoint

- `ActionMap` 是 identity 命令表：`define` / `execute(id)` / `dispatchKey`。菜单、快捷键、toolbar 不再各写一份 lambda。
- `FActionChord::primary` 在 macOS 匹配 Cmd，别处匹配 Ctrl；Shift 区分 Save / Save As。文本焦点下不匹配无 modifier 的 chord。
- EditorSurface File/View 菜单走 `UIMenu::FItem::fromAction`；Play/Stop/viewport 按钮走同一 `execute`；未处理的 KeyPressed 才 dispatch shortcut。
- xmake r ya-gui-closure-test 全量通过（370/370）；xmake b ya-game-editor 通过。
- 未完成：command palette / context menu 枚举 ActionMap；undo/redo；property projection；viewport 写入 SelectionModel；ImGui `EditorLayer::menuBar` 仍是平行路径。

### Phase 6B 边界

- 没有做 palette UI 或 hierarchy context menu。
- 没有把 viewport 2/3 键迁入 ActionMap（仍是 EditorLayer 在 viewport focus 时处理）。
- ImGui 菜单栏未删除。

下一 checkpoint：Phase 6 undo/redo transaction，或 viewport 选择写入 SelectionModel。

## Phase 6C 当前 checkpoint

- `UndoStack` 是 identity 撤销历史：`push` 记录已应用的 undo/redo 闭包（push 不调用 redo）。`beginMerge`/`endMerge` 只合并同一 merge session 且 `mergeKey` 相同的连续 push，用来收口拖动。`UndoTransaction` 把嵌套 push 收成一步。栈不持有 Entity/Scene 指针。
- `edit.undo` / `edit.redo` 走 ActionMap；EditorSurface 增加 Edit 菜单。macOS Redo 是 Cmd+Shift+Z，别处 Ctrl+Y。
- Inspector 变换拖动经 `UIDragFloat::_onDragBegan/Ended` 开闭 merge；键盘/commit 各成一步。`setValue(..., false)` 给 sync，避免 gizmo 刷新写入 undo。重命名按 UUID 查找 Node，不把 Node* 存进栈。
- xmake r ya-gui-closure-test 全量通过（374/374）；xmake b ya-game-editor 通过；`xmake r ya-testing --gtest_filter=EditorPropertyGraphTest.*` 通过。
- 未完成：property projection；multi-object / mixed value；viewport 写入 SelectionModel；gizmo 编辑未接入 UndoStack；闭包仍可能捕获 PropertyHandle 内的 void*。

### Phase 6C 边界

- 没有做 command palette。
- 没有把 ImGuizmo / viewport 变换写入 UndoStack。
- 没有把 EditorLayer Entity* 选择向量删掉。

下一 checkpoint：Phase 6 property projection，或 viewport 选择写入 SelectionModel。

## Phase 6D 当前 checkpoint

- `PropertyGraph::project` 是反射 → editor field model 的唯一入口：`build` 之后应用 `PropertyProjectionRegistry`。
- Transform projection 同时写入显示名和 `setPosition` / `setRotation` / `setScale`；未投影的 `build` 仍是直接字段写，不会标 dirty。
- Inspector 不再手写 Transform section：按选中实体上的 ECS component 指纹重建，凡 `hasRetainedEditors()`（vec3/float/bool/string）的类型物化 `EditorAutoPropertySection`。
- 删除 `EditorTransformSection`；控件仍不持有 Entity*，binding 由 projection 提供。
- xmake b ya-game-editor 通过；`xmake r ya-testing --gtest_filter=EditorPropertyGraphTest.*` 6/6 通过。
- 未完成：multi-object mixed UI；validation / missing resource；viewport 写入 SelectionModel；enum/color/asset 编辑器（Phase 7）。

### Phase 6D 边界

- 没有做 mixed-value 显示或批量提交 UI。
- 没有把 ImGui DetailsView / TypeRenderer 删掉。
- 没有把 viewport 选择写入 SelectionModel。

下一 checkpoint：Phase 6 multi-selection / mixed value，或 viewport 选择写入 SelectionModel。

## Phase 6E 当前 checkpoint

- Inspector 对 `EditorLayer::getSelections()` 的 **component 交集** 调用 `PropertyGraph::project`，一份 graph 持有全部 instance。
- `UIDragFloat` mixed 画 "—"；编辑把该轴写成相同值并清 mixed。`PropertyHandle::copy/restore*` 让 undo 恢复每个 instance 的原值，而不是用第一个覆盖全部。
- xmake r ya-gui-closure-test 全量通过（374/374）；xmake b ya-game-editor 通过；`xmake r ya-testing --gtest_filter=EditorPropertyGraphTest.*` 7/7 通过。

### Phase 6E 边界

- 没有做资源缺失/校验错误 UI。
- 没有把 Hierarchy modifier 多选接到 SelectionModel。
- ImGui DetailsView 仍是平行路径。

下一 checkpoint：Phase 6 viewport 选择写入 SelectionModel，或 validation/error state。

## Phase 6F 当前 checkpoint

- `SelectionModel::replace` 批量设置 selected 有序集合与 primary（去重、primary 前置、无变化不 bump）。
- `EditorLayer::selectionGeneration` 在 `setSelections` / `setSelectedWidgetEntryId` 递增；`EditorSurface::syncSelectionFromLayer` 在 `syncPresentation` 中把 viewport/widget 选择映射为 `e:{uuid}` / `ui:{entryId}` 并 `replace` 到共享 `_selection`。
- Hierarchy `setOnSelectionChanged` 仍写 layer（单选）；viewport 多选经 layer → generation → `replace` 同步回 TreeView primary。
- xmake r ya-gui-closure-test 全量通过（376/376）；xmake b ya-game-editor 通过；`xmake r ya-testing --gtest_filter='BindingContractTest.SelectionModel*'` 通过。

### Phase 6F 边界

- 没有把 Hierarchy modifier 多选直接接到 SelectionModel（仍经 ImGui SceneHierarchyPanel → layer）。
- ImGui DetailsView 仍是平行路径。
- validation / missing resource error state 未做。

下一 checkpoint：Phase 6 validation/error state，或 Phase 7 retained inspector primitives（enum/color/asset）。

## Phase 6G 当前 checkpoint

- `PropertyHandle::validationError` 读取反射 `manipulator_spec` 的 min/max；`EditorAutoPropertySection` 在 `sync` 时对 `UIDragFloat`/`UITextField` 调 `setError`，construct 时把 manipulate spec 写入 drag 的 `_min/_max/_speed`。
- `FDragFloatStyle`/`FTextFieldStyle`/`FImageStyle` 增加 `errorFill`；`UIImage` 对非空 `_assetPath` 解析失败或 `setResourceMissing(true)` 画 error fill（与中性 placeholder 区分）。
- `EditorSurface::syncViewportTexture` 在 scene 已加载但 viewport RT 不可用时对 viewport image 标 `setResourceMissing`。
- xmake r ya-gui-closure-test 全量通过（377/377）；xmake b ya-game-editor 通过；`xmake r ya-testing --gtest_filter=EditorPropertyGraphTest.*` 9/9 通过。

### Phase 6G 边界

- 没有把 Hierarchy modifier 多选直接接到 SelectionModel。
- ImGui DetailsView 仍是平行路径。
- enum/color/asset retained 编辑器属 Phase 7。

下一 checkpoint：Phase 7 retained inspector primitives（enum/color/asset），或 Content Browser retained controls。

## Phase 7A 当前 checkpoint

- `PropertyHandle` 增加 enum 读写：`isEnum`、`enumLabels`、`tryGetEnumIndex`、`setEnumByIndex`、`copyEnum`/`restoreEnum`；`isMixed` 覆盖 enum。
- `UIComboBox` 增加 `setSelectedIndex(..., bNotify)` 与 `setMixed`（显示 "—"）。
- `EditorAutoPropertySection` 对 enum 字段物化 `UIComboBox`，支持多选 mixed 与 undo。
- `PropertyGraph::hasRetainedEditors` 识别 enum 类型。
- xmake r ya-gui-closure-test 全量通过；xmake b ya-game-editor 通过；`xmake r ya-testing --gtest_filter=EditorPropertyGraphTest.*` 11/11 通过。

### Phase 7A 边界

- color（`glm::vec4`/`UIColorEdit`）与 asset reference 编辑器未做。
- Content Browser / Hierarchy virtualization 未做。

下一 checkpoint：Phase 7B retained color editor，或 asset reference picker。

## Phase 7B 当前 checkpoint

- `PropertyNode::bColor` 从反射 `Meta::Color` 元数据写入；`PropertyHandle::isColor` + `tryGetColor`/`setColor`/`copyColor`/`restoreColor` 支持 `glm::vec3`/`glm::vec4`。
- `UIColorEdit` 增加 `setColor(..., bNotify)` 与 `setMixed`（swatch 显示 "—"）。
- `EditorAutoPropertySection` 对 `.color()` 字段物化 `UIColorEdit`（非 color 的 vec3 仍走 DragFloat）。
- xmake r ya-gui-closure-test 全量通过（377/377）；xmake b ya-game-editor 通过；`xmake r ya-testing --gtest_filter=EditorPropertyGraphTest.*` 13/13 通过。

### Phase 7B 边界

- asset reference picker 未做。
- color 拖动连续改色未做 undo merge。

下一 checkpoint：Phase 7C asset reference picker，或 Content Browser retained controls。

## Phase 7C 当前 checkpoint

- `PropertyHandle::isAssetRef` + `assetRefKind` + `tryGetAssetPath`/`setAssetPath`/`copyAssetPath`/`restoreAssetPath`；`hasAssetResolveError` 读 `TextureRef`/`ModelRef`/`MeshRef` resolve state；multi-select mixed 比较 path。
- `EditorAssetPickerCallback`（`GameEditor/UI/EditorAssetPicker.h`）把 Browse 接到现有 `FilePicker`；`EditorInspectorTab` 注入，`PropertyHandle` 不依赖 editor UI。
- `EditorAutoPropertySection` 对 asset ref 物化 `UITextField`（path commit + undo）+ `UIButton`（Browse）；resolve failed 时 `setError`。
- xmake r ya-gui-closure-test 全量通过（377/377）；xmake b ya-game-editor 通过；`xmake r ya-testing --gtest_filter=EditorPropertyGraphTest.*` 16/16 通过。

### Phase 7C 边界

- ImGui `TypeRenderer` 的 `pathWrapper` 仍是平行路径（Phase 8 删除）。
- MeshRef Browse 复用 model picker（无独立 mesh picker）。
- Content Browser / Hierarchy virtualization 未做。

下一 checkpoint：Content Browser retained controls，或 Hierarchy virtualization。

## Phase 7D 当前 checkpoint

- 修复 Content Browser fingerprint：纳入 `getSearchText()` 与 `getSelectedPath()`，搜索过滤即时刷新 entry 列表。
- `FileExplorer::getSearchText()` 公开搜索状态；搜索框失焦时 sync 回写。
- Content Browser entry 行支持单选高亮（`selectContentItem` → `setSelectedPath`）；纹理文件选中时调用 `inspectAsset`。
- 抽取 `EditorListRows.h`（`contentRow` / `updateContentRow` / `makeContentRowFactory`）供 Content Browser 与 Scene Save 共用。
- Scene Save dialog fingerprint 纳入 search text。
- xmake r ya-gui-closure-test 全量通过（377/377）；xmake b ya-game-editor 通过；`FileExplorerNavigationTest` 4/4；`EditorListRowsTest` 1/1。

### Phase 7D 边界

- ImGui `ContentBrowserPanel` / `FileExplorer::render` 仍是平行路径（Phase 8 删除）。
- Icon view / 缩略图 / view-mode toggle 未在 retained 路径实现。
- Hierarchy virtualization 未做。

下一 checkpoint：Hierarchy virtualization，或 retained Content Browser icon view。

## Phase 7E 当前 checkpoint

- `UITreeView` 在 `UIScrollViewport` 子节点下用 `computeKeyedVisibleWindow` 只绘制可见行窗口（overscan=2）；`getPaintedRowCount()` / diagnostics `paintedRows` 暴露绘制行数。
- `EditorSurface` Hierarchy 面板用 `HierarchyScroll` 包裹 `HierarchyTree`，大树可滚动且 paint 不随节点数线性增长。
- `ToolControlsTest.TreeViewVirtualizesPaintInsideScrollViewport`：50 行树在 100px viewport 下 paintedRows < visibleRows。
- xmake r ya-gui-closure-test 全量通过（378/378）；xmake b ya-game-editor 通过。

### Phase 7E 边界

- flatten/hit-test 仍遍历完整可见行集；仅 paint 窗口化，不是 row widget pooling。
- ImGui `SceneHierarchyPanel` modifier 多选 / UI entry drag-drop 未做。
- TableGrid flat paint 仍未虚拟化。

下一 checkpoint：viewport/gizmo overlay，或 Hierarchy filter + drag-drop。

## Phase 7F 当前 checkpoint

- `EditorHierarchyOps`：`editorHierarchyEntityIdKey` / `parseEditorHierarchyEntityIdKey` / `moveEditorHierarchyEntity`（TreeView drop mode 0/1/2 → before/into/after；拒绝 `ui:` 行）。
- `EditorSurface` Hierarchy：`HierarchyFilter` `UITextField` + `bindFilter`；`setReorderable(true)` + `setOnReorderHandler` 经 `Scene::moveNode` 重排实体并保持选中。
- `EditorHierarchyOpsTest`：`MoveEntityBeforeSiblingReordersChildren`、`RejectsUiEntryIds`。
- xmake r ya-gui-closure-test 全量通过（378/378）；xmake b ya-game-editor 通过；`xmake r ya-testing --gtest_filter=EditorHierarchyOpsTest.*` 2/2 通过。

### Phase 7F 边界

- 仅 scene 实体（`e:{uuid}`）可拖放重排；Game UI `ui:` 条目与 ImGui `SceneHierarchyPanel` UI entry drag-drop 仍平行。
- Hierarchy modifier 多选、context menu、viewport/gizmo overlay 未做。
- TableGrid flat paint 仍未虚拟化。

下一 checkpoint：viewport/gizmo overlay contract，或 drag-drop asset workflow。

## Phase 7G 当前 checkpoint

- `EditorViewportHost.h`：`FEditorViewportHostState` + `IEditorViewportOverlay` + `EditorViewportOverlayHost` 定义 retained viewport overlay 契约（host rect/hover/focus/view/proj；overlay dispatch/active/capture）。
- `EditorSurface` 每帧 `syncViewportHostState`；viewport hover 时 `dispatchEvent` 先路由 overlay host；暴露 `viewportOverlayHost()` / `isViewportOverlayActive()`。
- `EditorViewportOverlayHostTest` 3/3 通过；ImGuizmo 桥接仍待后续 overlay 实现。
- xmake b ya-game-editor / ya-testing / ya-gui-closure-test 通过。

### Phase 7G 边界

- 尚无具体 overlay 实现（gizmo 仍走 ImGui `renderGizmo`）；WidgetTree chrome 下 viewport 工具输入待 ImGuizmo bridge。
- drag-drop asset workflow、menu/palette gaps 未做。

下一 checkpoint：ImGuizmo retained overlay bridge，或 drag-drop asset workflow。
## Phase 7H 当前 checkpoint（2026-09-04）

- EditorViewportGizmoOverlay 已接入 EditorViewportOverlayHost：viewport hover/focus 时先路由 overlay，W/E/R 切换平移/旋转/缩放操作，ImGuizmo active/capture 状态回传到输入路由。
- WidgetTree chrome 的 presentation 在 retained snapshot replay 后调用 EditorSurface::presentViewportGizmo，将 gizmo 作为 viewport overlay 提交到同一 presentation command buffer；未恢复 ImGui editor chrome。
- overlay 事件转发条件扩大为 hovered || focused，避免 gizmo 拖拽开始后鼠标离开 viewport rect 立刻丢失输入。
- 验证：xmake b ya-game-editor 通过；xmake r ya-testing -- --gtest_filter='EditorViewportOverlayHostTest.*' 通过。

### Phase 7H 边界

- ImGuizmo 的绘制和键盘 modifier 仍依赖共享 ImGui backend，属于 bridge，不是最终 retained renderer。
- 当前 runtime editor smoke 仍可能以 255 退出，不能据此宣称完整启动通过；下一步是定位该退出并接入 gizmo transform 的 command/undo 闭环。
## Phase 7I 当前 checkpoint（2026-09-04）

- 新增 EditorTransformUndo：按 IDComponent UUID 捕获多选 Transform 的 world 快照，撤销/重做时通过 Scene::getEntityByUUID 重新解析实体，避免闭包持有裸 Entity*。
- EditorLayer::renderGizmo 在第一次实际 ImGuizmo::Manipulate 时捕获 before，操作结束时捕获 after，并向 EditorSurface 共享的 UndoStack 提交一个 Transform gizmo 命令；多选沿用同一批 UUID 快照。
- EditorTransformUndoTest 验证稳定 UUID 恢复和无 Transform 实体过滤；overlay 契约测试继续通过。
- 验证：xmake b ya-game-editor、xmake b ya-testing，以及 EditorTransformUndoTest.*:EditorViewportOverlayHostTest.*（5/5）通过。

### Phase 7I 边界

- ImGuizmo 绘制/输入仍是共享 ImGui backend 的 bridge；最终 retained gizmo renderer 未完成。
- 当前撤销命令按 world matrix 快照恢复，未把操作类型、snap 参数和 UI 标签细分为独立 command payload。
- 下一步：为 widgettree editor 增加有界 automation smoke，确认 gizmo drag 的 begin/commit/undo 生命周期，再继续清理 ImGui editor paths。

## Phase 9A 当前 checkpoint（2026-09-04）

- 新增 Script/automation/editor/run_widgettree_editor_smoke.py 与 test_widgettree_editor_smoke.py：以现有 automation control 为唯一控制面，启动 run-editor --editor-chrome=widgettree，验证 ping、viewport rect、editor camera 写入、frame index 持续推进、presentation screenshot 落盘，并显式 quit 校验干净退出。
- smoke 运行日志落到 Engine/Saved/Automation/widgettree-editor-smoke.log，presentation 证据图落到 Engine/Saved/Automation/widgettree-editor-smoke-presentation.png；不再把“看起来能跑”当成 editor runtime readiness 证据。
- 本地验证：python3 -m py_compile Script/automation/editor/run_widgettree_editor_smoke.py Script/automation/editor/test_widgettree_editor_smoke.py 通过；python3 Script/automation/editor/run_widgettree_editor_smoke.py --skip-build --startup-timeout 90 --frame-budget 180 --min-frame-delta 20 --presentation-shot Engine/Saved/Automation/widgettree-editor-smoke-presentation.png 退出码 0。

### Phase 9A 边界

- 这是单机 macOS 的 bounded smoke，不是 cross-platform、长时 soak、DPI/CJK 或 GPU/offscreen parity 全量门禁。
- smoke 当前验证 editor runtime 稳定启动/绘制/退出，不覆盖 gizmo 交互脚本化拖拽、content browser 操作链或 ImGui 路径移除后的全工作流。
- 下一步：继续清理剩余 ImGui editor path，并补更细粒度的 widgettree editor automation 命令面。
## Phase 7J 当前 checkpoint（2026-09-04）

- 移除 EditorSurface 中 Asset Inspector 的 pending placeholder，新增 retained tab 内容：当前资产路径、预览区域和状态文案。
- retained tab 通过 EditorLayer 的 AssetInspectorPanel inspectedPath 读取既有资产选择状态；Content Browser 选中纹理后，路径同步到 retained UIImage 的 asset path。
- 验证：xmake b ya-game-editor 通过；widgettree editor smoke（frame progression + presentation screenshot + clean quit）退出码 0。

### Phase 7J 边界

- 旧 AssetInspectorPanel 的 ImGui 元数据编辑和 RGBA mask 控件仍保留，属于后续 Phase 8 清理范围；本 checkpoint 只移除 retained chrome 中的占位 tab。
- retained 预览当前依赖 UIImage 的资源解析/占位机制，尚未迁移完整导入元数据编辑器。
- 下一步：迁移 UI Designer 或 Runtime Tools，继续清理 pending placeholder 和 ImGui editor path。
## Phase 7K 当前 checkpoint（2026-09-04）

- 移除 EditorSurface 中 UI Designer 的 pending placeholder，改为 retained tab shell：New Panel、Save、Close 三个真实操作按钮，以及当前 document / selected widget 状态。
- 操作直接调用现有 UIDesignerPanel 的 document 生命周期（newDocument/saveDocument/clearDocument）；每帧通过 getOpenDocument/getSelectedWidget 同步状态，不复制 preview tree。
- 验证：xmake b ya-game-editor 通过；widgettree editor smoke（frame progression + presentation screenshot + clean quit）退出码 0。

### Phase 7K 边界

- UI Designer 的 palette、层级树、反射 inspector、preview canvas 仍由旧 ImGui panel 承载；本 checkpoint 只替换 dock tab 的 placeholder shell，未宣称完整迁移。
- 下一步优先迁移 Runtime Tools 的只读诊断 section，或继续补 UI Designer preview 的 retained surface，随后再删除对应 ImGui path。
## Phase 7L 当前 checkpoint（2026-09-04）

- 移除 EditorSurface 中 Runtime Tools 的 pending placeholder，改为 retained shell：Play、Simulate、Stop 三个真实动作按钮，以及当前状态 / 帧号显示。
- 动作直接调度现有 App 生命周期接口（startRuntime/startSimulation/stopRuntime/stopSimulation）；状态通过 App::isRuntimeMode/isSimulationMode 和 frame index 每帧同步。
- 验证：xmake b ya-game-editor 通过；widgettree editor smoke（frame progression + presentation screenshot + clean quit）退出码 0。

### Phase 7L 边界

- Runtime Tools 的 profiling、render settings、debug primitives、render target inspector 仍是后续更细粒度的 retained 迁移目标；本 checkpoint 只替换 shell 和核心控制动作。
- 下一步继续迁移剩余的 ImGui 诊断 section，并最终移除对应 path。

## Phase 7M 当前 checkpoint（2026-09-04）

- Dock leaf 的 selected panel 现在通过 `DockContent` 父容器的 `UIBoxSlot` 显式 `Fill` 挂载；不再依赖 child 自身的 canvas anchor 或默认 Auto slot。
- 这修正了 `HierarchyBody` 在 `DockContent4` path-A 下声明 stretch anchors 的启动诊断，并保证切换 dock tab 后 panel root 始终获得完整 content rect。
- 验证：`xmake b ya-game-editor`、`xmake b ya-runtime` 通过；重启 `xmake r ya-runtime --editor --editor-chrome=widgettree` 后未再出现 `declares stretch anchors`，进程可正常初始化并退出。

### Phase 7M 边界

- 这是 dock parent-owned slot contract 修复，不等于移除旧 ImGui editor path；UI Designer palette/inspector/preview、Runtime Tools diagnostics 等仍待迁移。

## Phase 7N 当前 checkpoint（2026-09-04）

- 新增 `RuntimeDiagnosticsSection` retained compound，接入 Runtime Tools tab，持续同步 RenderDoc 可用性、DLL/output 路径、最近 capture、capturing/queued delay 等只读状态。
- 该 section 只消费 `RenderDiagnosticsService::RenderDocState`，不复制 RenderDoc 生命周期；capture mutation buttons 仍由 legacy RuntimeToolsPanel 所有，边界保持清晰。
- 验证：`xmake b ya-game-editor` 通过；widgettree editor smoke 截图成功并退出码 0。

### Phase 7N 边界

- profiling、render settings、RenderDoc 操作按钮、debug primitives、render target inspector 仍未迁移；下一步继续逐个迁移 Runtime Tools diagnostics/control sections。

## Phase 7O 当前 checkpoint（2026-09-04）

- `RuntimeDiagnosticsSection` 增加 retained Capture Enabled、Show RenderDoc HUD、Capture Next Frame、Capture After 120 Frames 控件，直接调用 `RenderDocCapture` 的现有 API。
- 同步阶段根据 runtime/capture availability 设置 checked/enabled 状态；点击回调不持有 `App` 或 runtime 裸指针，按当前服务重新解析。
- 验证：`xmake b ya-game-editor` 通过；widgettree editor smoke 截图成功并退出码 0。

### Phase 7O 边界

- profiling、render settings、render graph diagnostics、debug primitives、render target inspector 仍未迁移；旧 ImGui `RuntimeToolsPanel` 保持编译但不作为 retained 数据源。

## Phase 7P 当前 checkpoint（2026-09-04）

- 新增 `RuntimeRenderSettingsSection` retained compound，接入 Runtime Tools，覆盖 pending/active render pipeline、viewport framebuffer scale、VSync、present mode 与 active pipeline reload。
- 所有修改通过 `RenderRuntime`、`ISwapchain` 现有 API；present mode 写入 frame task，避免在录制期直接重建 swapchain。
- 验证：`xmake b ya-game-editor` 通过；widgettree editor smoke 截图成功并退出码 0。

### Phase 7P 边界

- profiling、render graph topology/internals、debug primitives、render target inspector 仍未迁移；deferred/forward 专属大量参数仍由旧 ImGui section 提供。

## Phase 7Q 当前 checkpoint（2026-09-04）

- 新增 `RuntimeProfilingSection` retained compound，接入 Runtime Tools，展示 compile mode、CPU trace session、frame CPU/GPU metrics，并提供 CPU Trace、Perf Metrics、Static Init 和 metrics average window 控件。
- 控件直接调用 `profiling` 公共 API；`sync()` 在 compound 尚未 construct 时安全返回，避免 tab 注册期间提前同步导致生命周期崩溃。
- 验证：`xmake b ya-game-editor` 通过；widgettree editor smoke（port 19998）截图成功并退出码 0。

### Phase 7Q 边界

- render graph topology/internals、debug primitives、render target inspector、deferred/forward 专属详细参数仍待 retained 迁移；旧 ImGui profiling panel 保持编译。

## Phase 7R 当前 checkpoint（2026-09-04）

- 新增 `RuntimeRenderGraphSection` retained compound，接入 Runtime Tools，展示当前 active pipeline、最近 compiled frame graph 的 pass 数、dependency 数，以及是否已捕获到拓扑。
- 这一步只迁移拓扑摘要，不把 ImGui 的 bezier topology canvas 直接硬塞进 retained chrome；复杂图谱绘制和 pipeline internals 仍保留为后续独立切片。
- 验证：`xmake b ya-game-editor` 通过；widgettree editor smoke（port 19999）截图成功并退出码 0。

### Phase 7R 边界

- render graph topology canvas/internals、debug primitives、render target inspector、deferred/forward 专属详细参数仍待 retained 迁移；旧 ImGui render diagnostics 仍保留编译。

## Phase 7S 当前 checkpoint（2026-09-04）

- 新增 `RuntimeRenderTargetSection` retained compound，接入 Runtime Tools，列出当前 render target 总数以及每个 target 的 label、owner、extent、swapchain/offscreen 与 read-only 状态。
- 数据直接读取 `RenderRuntime::buildRenderTargetCatalog()`；本轮只迁移 catalog 摘要，不在 retained 面板里复制 ImGui 的 attachment preview / format editing 流程。
- 验证：`xmake b ya-game-editor` 通过；widgettree editor smoke（port 20000）截图成功并退出码 0。

### Phase 7S 边界

- Debug primitives、render target attachment preview/format editing、render graph topology canvas/internals、deferred/forward 专属详细参数仍待 retained 迁移；旧 ImGui panels 保持编译。

## Phase 7T 当前 checkpoint（2026-09-04）

- 新增 `RuntimeDebugPrimitivesSection` retained compound，接入 Runtime Tools，提供 Enabled、Depth Test、Draw Lines、Draw Shapes 开关，并显示 pending/frame/immediate lines/shapes 计数。
- 设置通过 `DebugRenderSystem::buildSettingsSnapshot/requestSettings` 进入现有 render-thread deferred 应用路径；不会在 UI 录制期直接修改 GPU pipeline。
- 验证：`xmake b ya-game-editor` 通过；widgettree editor smoke（port 20001）截图成功并退出码 0。

### Phase 7T 边界

- debug pipeline inspector、render target attachment preview/format editing、render graph topology canvas/internals、deferred/forward 专属详细参数仍待 retained 迁移；旧 ImGui panels 保持编译。

## Phase 8A 当前 checkpoint（2026-09-04）

- 删除 ImGui `ContentBrowserPanel::onImGuiRender` 及 `EditorLayer::onImGuiRender` 调用；`ContentBrowserPanel` 仅保留 FilePicker 所需的 folder/file 图标加载。
- retained `EditorSurface` Content Browser 是唯一正式路径；ImGui chrome 模式不再提供 Content Browser 窗口。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`；`xmake r ya-testing --gtest_filter='FileExplorerNavigationTest.*:EditorListRowsTest.*'`；widgettree editor smoke 退出码 0。

### Phase 8A 边界

- ImGui `SceneHierarchyPanel`、`DetailsView`、`AssetInspectorPanel`、Runtime Tools、UI Designer 等仍保留编译与 ImGui 渲染路径。
- `FilePicker` 仍依赖 ImGui 纹理描述符；下一步继续按功能删除其余 ImGui editor path。

## Phase 8B 当前 checkpoint（2026-09-04）

- 删除 ImGui `SceneHierarchyPanel::onImGuiRender` 及 `EditorLayer::onImGuiRender` 调用；`SceneHierarchyPanel` 仍保留 selection/move/duplicate 等 editor 数据面 API（viewport pick、undo 等仍经此面板同步）。
- retained `EditorSurface` Hierarchy（filter + scroll + entity drag-drop）是唯一正式层级 UI。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`（378/378）；`EditorHierarchyOpsTest.*` 2/2；widgettree editor smoke 退出码 0。

### Phase 8B 边界

- `SceneHierarchyPanel` 内 ImGui 绘制实现（`sceneTree` 等）仍编译但未调用；后续 checkpoint 可整块删除。
- ImGui Frame Stats、Asset Inspector 等仍平行。

## Phase 8C 当前 checkpoint（2026-09-04）

- 删除 ImGui `DetailsView::onImGuiRender` 及 `EditorLayer::onImGuiRender` 调用；`DetailsView` 内 ImGui 绘制实现仍编译但未调用。
- retained `EditorInspectorTab` 是实体/component 唯一正式 Inspector UI；选中 `ui:` hierarchy 条目时显示 Game UI Entry 摘要与 Open in UI Designer。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`（378/378）；`EditorPropertyGraphTest.*` 16/16；widgettree editor smoke 退出码 0。

### Phase 8C 边界

- Game UI Entry 的 zOrder/autoMount/transform/overrides/delete 仍只在未调用的 ImGui `DetailsView` 实现中；后续 checkpoint 迁移到 retained Inspector 或 UI Designer。
- ImGui Frame Stats、Asset Inspector、Runtime Tools、UI Designer 等仍平行。

## Phase 8D 当前 checkpoint（2026-09-04）

- 删除 ImGui `EditorLayer::statsWindow` / `FrameStatsPanel::onImGuiRender` 及 `EditorLayer` 对 `_frameStatsPanel` 的持有；retained `EditorSurface` Frame Stats tab 是唯一正式路径。
- `FrameStatsPanel` 仍编译（offscreen compose 工具），但 ImGui 窗口壳已删。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`；widgettree editor smoke 退出码 0。

### Phase 8D 边界

- retained Frame Stats 尚未恢复 avg FPS 滑动窗口；ImGui `FrameStatsPanel` 的 history 逻辑仍留在未调用代码中。
- ImGui Runtime Tools、UI Designer 等仍平行。

## Phase 8E 当前 checkpoint（2026-09-04）

- 删除 ImGui `AssetInspectorPanel::onImGuiRender` 及 `EditorLayer::onImGuiRender` 调用；`AssetInspectorPanel` 仍保留 `inspectTexture`/`clear`/`inspectedPath` 数据面 API（Content Browser 与 retained tab 同步路径）。
- retained `EditorSurface` Asset Inspector tab 是唯一正式资产检视 UI。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`；widgettree editor smoke 退出码 0。

### Phase 8E 边界

- ImGui `AssetInspectorPanel` 内 meta 编辑与 RGBA mask 控件仍编译但未调用；后续迁移到 retained tab 或删除。
- ImGui UI Designer 等仍平行。

## Phase 8F 当前 checkpoint（2026-09-04）

- 删除 ImGui `EditorLayer::runtimeToolsWindow` / `RuntimeToolsPanel::onImGuiRender` 及 `EditorLayer` 对 `_runtimeToolsPanel` 的持有；`migrateLegacyRuntimeSettings` 仍由 `EditorModule` 启动时调用。
- retained `EditorSurface` Runtime Tools tab（session/diagnostics/render settings/profiling/graph/targets/debug primitives）是唯一正式路径。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`；widgettree editor smoke 退出码 0。

### Phase 8F 边界

- ImGui `RuntimeToolsPanel` 内 deferred/forward/camera/clear-values 等折叠面板仍编译但未调用；后续整块删除或按需迁移到 retained sections。
- ImGui UI Designer 等仍平行。

## Phase 8G 当前 checkpoint（2026-09-04）

- 删除 ImGui `UIDesignerPanel::onImGuiRender` 及 `EditorLayer::onImGuiRender` 调用；`UIDesignerPanel` 仍保留 document/preview/selection API（retained tab 与 hierarchy Open in UI Designer 仍经此面板）。
- retained `EditorSurface` UI Designer tab（new/save/close、状态/选择同步、preview tree）是唯一正式 UI Designer chrome。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`；widgettree editor smoke 退出码 0。

### Phase 8G 边界

- ImGui `UIDesignerPanel` 内 palette/inspector/toolbar 绘制仍编译但未调用；完整 authoring 工作流仍依赖 `UIDesignerPanel` 数据面 + retained tab 壳。
- ImGui menu/toolbar/dockspace chrome host、FilePicker 等仍平行。

## Phase 8H 当前 checkpoint（2026-09-04）

- 删除 ImGui `GUIWorkbenchPanel::onImGuiRender`、`renderGUIWorkbenchWindow`、`EditorLayer::_guiWorkbenchPanel` 持有，以及 `EditorModule` 的 `EditorToolSurfaceCompositor` offscreen compose 路径。
- retained `EditorSurface` `FWorkbenchSurface` dock tab 是唯一正式 GUI Workbench UI；ImGui chrome 模式不再显示平行 Workbench 窗口。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`；widgettree editor smoke 退出码 0。

### Phase 8H 边界

- `GUIWorkbenchPanel` 类型仍编译为 legacy stub，待后续整块删除。
- ImGui menu/toolbar/dockspace/viewport shell、FilePicker、render graph debug window 等仍平行。

## Phase 8I 当前 checkpoint（2026-09-04）

- 将 `editor.chrome.host` 默认值与 `EditorModule` fallback 从 `imgui` 改为 `widgettree`；`--editor-chrome=imgui` 仍可显式启用 legacy ImGui chrome。
- retained `EditorSurface` 成为默认编辑器 shell；ImGui chrome 降为 opt-in legacy 路径。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`；widgettree editor smoke 退出码 0。

### Phase 8I 边界

- ImGui chrome host 代码路径仍完整保留（menu/toolbar/dockspace、FilePicker、render graph debug）；下一步继续删除或仅在 imgui 模式下编译。
- docking persistence / 多窗口仍待 Phase 8 后续 checkpoint。

## Phase 8J 当前 checkpoint（2026-09-04）

- 删除 ImGui `EditorLayer::renderGraphWindow`、View 菜单 Render Graph 项及 `bShowRenderGraphWindow`；`renderRenderGraphWindowContent` 仍编译但未调用。
- retained `EditorSurface` Runtime Tools 内的 `RuntimeRenderGraphSection` 是正式 render graph 摘要路径。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`；widgettree editor smoke 退出码 0。

### Phase 8J 边界

- ImGui chrome host（menu/toolbar/dockspace/viewport）、FilePicker 等仍平行。

## Phase 8K 当前 checkpoint（2026-09-04）

- 删除 ImGui `ImGui::ShowDemoWindow` 路径、View 菜单 Show Demo Window 项及 `bShowDemoWindow`。
- legacy ImGui chrome 的 `renderAuxiliaryUi` 现仅保留 `FilePicker::render` modal。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`；widgettree editor smoke 退出码 0。

### Phase 8K 边界

- ImGui chrome host（menu/toolbar/dockspace/viewport/project browser）与 ImGui `FilePicker` 仍平行；下一步迁移 FilePicker 或继续削减 imgui chrome shell。

## Phase 8L 当前 checkpoint（2026-09-04）

- `EditorLayer::cmdSaveSceneAs` 在 widgettree chrome 下经 `setSaveSceneAsHandler` 打开 retained `EditorSurface` scene-save popup，不再调用 `FilePicker::openSceneSavePicker`。
- legacy `--editor-chrome=imgui` 仍走 ImGui `FilePicker` scene-save 模式；asset Browse 仍依赖 ImGui FilePicker。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`；widgettree editor smoke 退出码 0。

### Phase 8L 边界

- Inspector/TypeRenderer asset Browse 仍打开 ImGui `FilePicker`；下一步迁移通用 asset picker 到 retained popup。

## Phase 8M 当前 checkpoint（2026-09-04）

- `EditorInspectorTab::makeAssetPicker` 在 widgettree chrome 下经 `EditorLayer::setAssetPickerHandler` 打开 retained `EditorSurface` asset-picker popup，不再调用 `FilePicker::openTexturePicker` / `openModelPicker`。
- popup 复用 scene-save 的 `FileExplorer` + mount/entry keyed rows + modal overlay；按 `EEditorAssetPickerKind` 过滤 texture/model 扩展名。
- legacy `--editor-chrome=imgui` 仍走 ImGui `FilePicker` asset 模式；`renderAuxiliaryUi` 仍仅保留 `FilePicker::render` modal。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`（378/378）；`xmake r ya-testing --gtest_filter='EditorPropertyGraphTest.*'`（16/16）；widgettree editor smoke 退出码 0。

### Phase 8M 边界

- legacy ImGui chrome host（menu/toolbar/dockspace/viewport/project browser）与 ImGui `FilePicker` 仍平行；下一步继续削减 imgui chrome shell 或移除 `imgui-local` 依赖。

## Phase 8N 当前 checkpoint（2026-09-04）

- 删除未再调用的 ImGui `DetailsView` 实现（组件/反射/script/skybox/environment 源文件）及 `EditorLayer::_detailsView` 成员。
- `AssetInspectorPanel` 收敛为 retained 所需的 `inspectTexture` / `clear` / `inspectedPath` 状态；ImGui `renderTextureInspector` 等死代码移除。
- `TypeRenderer` 仍保留给 `UIDesignerPanel` legacy 数据层；widgettree Inspector 继续走 `PropertyGraph` + `EditorInspectorTab`。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`（378/378）；`xmake r ya-testing --gtest_filter='EditorPropertyGraphTest.*'`（16/16）；widgettree editor smoke 退出码 0。

### Phase 8N 边界

- legacy ImGui chrome host（menu/toolbar/dockspace/viewport/project browser/debug）与 ImGui `FilePicker` 仍平行；`TypeRenderer` 仍服务 UI Designer legacy 层。
- 下一步继续削减 imgui chrome shell、迁移 UI Designer field inspector，或推进 docking persistence / `imgui-local` 移除。

## Phase 8O 当前 checkpoint（2026-09-04）

- 删除未调用的 ImGui `SceneHierarchyPanel::sceneTree` 及整套 draw/drag-drop 实现；面板收敛为 viewport 选择总线（`setSelection` / `handleEntityClick` / `replaceSelection` / `deleteSelection`），修饰键改读 `SDL_GetModState`。
- 删除未调用的 RuntimeTools ImGui helper 源文件（Profiling/Rendering/Session/Diagnostics）与 `RuntimeToolsPanelInternal.h`；`migrateLegacyRuntimeSettings` 仍保留在 `RuntimeToolsPanel.cpp`。
- 删除未调用的 `RenderTargetInspector` 与 `renderFrameStatsContent`。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`（378/378）；`xmake r ya-testing --gtest_filter='EditorHierarchyOpsTest.*'`（2/2）；widgettree editor smoke 退出码 0。

### Phase 8O 边界

- legacy ImGui chrome host（menu/toolbar/dockspace/viewport/project browser/debug/settings）与 ImGui `FilePicker` 仍平行。
- 下一步继续削减 imgui chrome shell 或推进 docking persistence / `imgui-local` 移除。

## Phase 8P 当前 checkpoint（2026-09-04）

- 删除未调用的 `UIDesignerPanel` ImGui `drawToolbar` / `drawWidgetTree` / `drawPalette` / `drawInspector` 及 ImGui drag-drop 反馈辅助代码；`UIDesignerPanel.cpp` 不再依赖 `imgui.h` / `TypeRenderer`。
- 保留 preview/document 数据层：`openSceneEntry`、`buildPreviewSnapshot`、`applyWidgetDrop`、`computeDropPos(itemMinY, itemMaxY, mouseY)` 等 API 供 retained tab 与 2D canvas 使用。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`（378/378）；`xmake r ya-testing --gtest_filter='UIDesignerPanelTest.*'`（1/1）；widgettree editor smoke 退出码 0。

### Phase 8P 边界

- legacy ImGui chrome host 与 `FilePicker` 仍平行；UI Designer palette/field inspector 仍待 retained 绑定。
- 下一步继续削减 imgui chrome shell、迁移 UI Designer retained palette/inspector，或推进 docking persistence。

## Phase 8Q 当前 checkpoint（2026-09-04）

- `FDockTreeModel::exportLayoutJson` / `importLayoutJson`：按 panel `stableKey` 序列化 split/leaf 树、ratio、tab 顺序与 selected tab；未知 key 拒绝导入；未出现在快照中的已注册 panel 挂到首个 leaf。
- `UIDockWorkspace::addPanel(stableKey, title, widget)`；EditorSurface 全部 dock panel 使用稳定 key（`viewport`、`content-browser`、`runtime-tools` 等）。
- Editor 启动时从 `ConfigManager` `editor.dockLayout` 恢复；dock 变更（split ratio / tab 选择 / drop）经 `appendOnDockUpdated` 写回配置。
- `DockNodeTest` 新增 export/import round-trip、unknown key 拒绝、orphan panel 挂载测试。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`（381/381）；`xmake r ya-testing --gtest_filter='DockNodeTest.*'`（16/16）；widgettree editor smoke 退出码 0。

### Phase 8Q 边界

- floating window geometry / 多 OS window coordinator 仍待后续 checkpoint；legacy ImGui chrome host 与 `FilePicker` 仍平行。
- 下一步继续削减 imgui chrome shell 或推进 floating-window persistence。

## Phase 8R 当前 checkpoint（2026-09-04）

- `EditorLayer` 新增 viewport 创作命令：`canViewportAuthor`、`cmdCreateEmptyNode`、`cmdCreateNodePreset`（`NodeCreateRegistry`）、`cmdDuplicateSelection`、`cmdDeleteSelection`。
- ImGui viewport 右键菜单改为调用上述命令（去除重复实现）。
- widgettree：`EditorSurface::openViewportContextMenu` 在 viewport 右键打开 retained `UIMenu`（3D presets 来自 registry）；Edit 菜单与 `ActionMap` 增加 Duplicate/Delete；`onEvent` 支持 Delete 与 Ctrl/Cmd+D。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`（381/381）；widgettree editor smoke 退出码 0。

### Phase 8R 边界

- Hierarchy 面板本身仍无 create/delete 树 UI；创作入口在 viewport 菜单/快捷键。
- legacy ImGui chrome host、Debug window 仍平行；见 `imgui-widgettree-parity.md`。

## Phase 8S 当前 checkpoint（2026-09-04）

- 新增 `FEditorFilePickerRequest` / `EditorFilePickerCallback`（`EditorFilePicker.h`）：统一 retained file/directory picker 请求；提供 script/material/directory/scene-json 工厂。
- `EditorSurface::openFilePickerDialog` 为通用实现；`openAssetPickerDialog` 薄封装；支持 file/directory selection mode。
- `EditorLayer::setFilePickerHandler`；widgettree `EditorModule` 接线；legacy `editorSettings` Browse 在 handler 存在时走 retained picker。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`（381/381）。

### Phase 8S 边界

- ImGui `FilePicker` 类型仍在 legacy chrome 使用；widgettree 默认路径已具备等价 retained API。

## Phase 8U 当前 checkpoint（2026-09-04）

- `EditorSurface::openEditorSettingsDialog`：retained modal（viewport sampler、`UIComboBox`；camera overlay `UICheckBox`；startup scene path + Browse/Apply/Reset）。
- View 菜单 `editor.settings` action；`EditorLayer` 公开 settings draft/apply API。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`（381/381）。

### Phase 8U 边界

- legacy ImGui `editorSettings` 窗口仍在 `onImGuiRender`；widgettree 默认路径使用 retained 面板。
- Debug images window、UI Designer tree DnD 仍待迁移；见 `imgui-widgettree-parity.md`。

## Phase 8T 当前 checkpoint（2026-09-04）

- `UIDesignerPanel::addPaletteWidget` / `paletteDisplayName`：从 registry 向选中节点（或根）添加子 widget 并 `syncPreviewToDocument`。
- `EditorSurface` UI Designer tab：三列布局（Palette 滚动按钮列表、主列 tree/toolbar、Inspector `EditorAutoPropertySection` via `PropertyGraph::project`）。
- 树选择与 canvas pick 双向同步（`designerSelectionPath` + `Reactive` selection）。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`（381/381）；`UIDesignerPanelTest`（1/1）。

### Phase 8T 边界

- Hierarchy tree 仍无 retained drag-drop reorder UI；2D viewport 画布操纵已存在。
- 下一步：Debug window（8V）或 ImGui shell 删除门禁（8W）。

## Phase 8V 当前 checkpoint（2026-09-04）

- 新增 retained `EditorDebugImagesTab` dock tab（stable key `debug-images`），默认挂在 content leaf；分类 combo、grouped mip/face combo、standalone RGBA mask 与 `UIImage` 预览。
- Catalog 过滤与 preview slot 索引抽到可测试 helper（`EditorDebugCatalogView.h`）；mask/group 选择仍由 `EditorLayer` 持有并写回 ConfigManager。
- `EditorLayer::getDebugSlotPreviewTexture` 把 identity/masked image view wrap 成 `Texture`，供 retained `UIImage` 使用；不在本 checkpoint 删除 `debugWindow()` / `onImGuiRender`。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`（381/381）；`xmake r ya-testing --gtest_filter='EditorDebugCatalogViewTest.*'`（2/2）。

### Phase 8V 边界

- cube-face 按钮网格 / 多列表格布局未迁移；combo 选择覆盖核心 workflow。
- legacy ImGui `debugWindow` 与 chrome host 仍平行，直到 8W 删除门禁。
- 下一步：floating dock geometry，或在 parity 行达标后推进 8W（移除 `onImGuiRender` shell / `imgui-local`）。

## Floating dock persistence 当前 checkpoint（2026-09-04）

- `UIDockWorkspace::exportLayoutJson` / `importLayoutJson`：在 dock tree JSON 上附加 `floating[]`（stable panel keys、selected tab、pos/size）。缺省 `floating` 兼容 8Q 的 tree-only 快照。
- EditorSurface 打开 tear-off（`bAllowFloating` / `bAllowTearOff`），Popup 层挂 `UIDockFloatingHost`；dock 与 floating 变更都写回 `editor.dockLayout`。
- 浮窗 title 移动 / resize 在 pointer release 时 commit geometry；tab 选择写回 `activePanelId`。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`；`xmake r ya-testing --gtest_filter='DockNodeTest.*'`。

### Floating dock 边界

- 仍是同一 OS window 内的 Popup-layer 浮窗，不是 native 多窗口 coordinator。
- 下一步：8W 仅在剩余 🔴 parity 行（gizmo bridge / optional imgui chrome）被接受或替换后删除 `onImGuiRender` / `imgui-local`。

## Phase 8W 当前 checkpoint（2026-09-04）

- `EditorModule` 只走 WidgetTree chrome；`--editor-chrome=imgui` / `editor.chrome.host=imgui` 忽略并 WARN。
- 删除 `EditorLayer::onImGuiRender` 及 ImGui chrome 实现：`EditorLayer.Layout.cpp`（menu/toolbar/dockspace/project browser/FilePicker modal）、`viewportWindow`、`editorSettings` ImGui 窗口、`debugWindow` 与 ImGui debug grid/slot 绘制。
- ImGuizmo overlay 仍经 `EditorSurface::presentViewportGizmo` 使用 `GuiSystem`；`imgui-local` 保留。
- 验证：`xmake b ya-game-editor`；`xmake r ya-gui-closure-test`（384/384）。

### Phase 8W 边界

- 未摘 `imgui-local` / `imguizmo-local`；`FilePicker` 与 `TypeRenderer` 仍编译但 widgettree 默认不再打开 ImGui chrome。
- Fullscreen / ImGui dock-padding 菜单项随 chrome shell 删除（➖）。
- 下一步：Phase 9 跨平台、DPI/CJK、long-run soak。

## Phase 9B 当前 checkpoint（2026-09-04）

- 新增 `EditorScaleBaselineTest`（`ya-gui-closure-test` / `ya-gui-widgets-test`）：用结构界线而不是墙钟时间卡住 editor 规模。
- Hierarchy：50×40 展开节点（2050 可见行）在 220px scroll viewport 内 `getPaintedRowCount() < 30`，第二次干净 snapshot `rebuiltWidgets==0`。
- Inspector：250 行 `UIText` 列第一次 snapshot 有 draw items，第二次 `rebuiltWidgets==0` 且 draw-item 数稳定。
- Content catalog：`computeKeyedVisibleWindow` 在 50 与 5000 条目、同一 viewport 下 window count 相同且 < 20；5000 条目的 `contentExtent` 仍按全量计算。
- 验证：`xmake r ya-gui-closure-test`（387/387）；`EditorScaleBaselineTest.*`（3/3）。

### Phase 9B 边界

- 这是 headless 结构/增量布局基线，不是大场景 ECS 帧时间、长时 soak、DPI/CJK、GPU/offscreen 或跨平台门禁。
- Hierarchy flatten/hit-test 仍读全量可见行；本 checkpoint 只证明 paint 窗口有界。
- 下一步：Phase 9C attach/detach + theme-switch soak。

## Phase 9C 当前 checkpoint（2026-09-04）

- 新增 `EditorLongRunSoakTest`：重复 editor 热路径，而不是单次 attach/theme 回归。
- 同一 subtree 64 次 attach/detach（dock tab hide/show）：attached draw-item 数稳定，detach 后 snapshot 为空，第二次 snapshot `rebuiltWidgets==0`。
- 64 次 destroy/recreate（document close / hot reload）：runtime id 单调递增，draw-item 数稳定。
- 32 次 theme A/B 切换：每次切换后 rebuild，随后干净 snapshot `rebuiltWidgets==0`，fill 颜色跟随当前 theme。
- 32 次 deferred texture generation miss/hit：miss 无 texture，hit 带 resolver 纹理，随后干净 snapshot。
- 验证：`xmake r ya-gui-closure-test`（391/391）；`EditorLongRunSoakTest.*`（4/4）。

### Phase 9C 边界

- 这是 headless 结构 soak，不是进程级小时 soak、热重载 shader/asset 文件系统，也不是 GPU 泄漏门禁。
- 下一步：Phase 9D DPI / CJK / keyboard / IME / clipboard / text editing。

## Phase 9D 当前 checkpoint（2026-09-04）

- `KeyEvent::isPrimaryModifierPressed`：macOS Cmd、别处 Ctrl；与 ActionMap 加速键同一规则。
- `WidgetTree` 默认内存剪贴板 + `setClipboardHooks`；`bindSdlClipboard` 接到 SDL，GUIAppHost 与 EditorSurface 在建树后绑定。
- `UITextField`：KeyTyped 提交 IME/Unicode；Backspace/Delete 按码点；primary+C/X/V 走 tree clipboard；paste 去掉换行/Tab。
- `EditorInputContractTest`：DPI 与 uiScale 正交折叠进 snapshot；CJK「你好」KeyTyped + 码点删除 + measure；copy/cut/paste；hooks 替换内存缓冲。
- 验证：`xmake r ya-gui-closure-test`（395/395）；`EditorInputContractTest.*`（4/4）；`xmake b ya-game-editor`。

### Phase 9D 边界

- Copy 当前复制整段 `_text`（无 selection range）。
- IME 候选窗仍是 OS/SDL；retained tree 只消费 committed `KeyTypedEvent`。
- CJK fallback 字体栈回归仍由 `WidgetLayoutTest` ScaledViewScalesFallbackGlyphsByOwnDesignSize 覆盖。
- 下一步：Phase 9E snapshot digest、GPU/offscreen parity、automation route trace。

## Phase 9E 当前 checkpoint（2026-09-04）

- 新增 `Script/automation/gui/run_workbench_gpu_parity.py`：把 snapshot digest、automation route trace 和 GPU/offscreen 零容差 diff 收成一条可重复门禁。
- Headless：`widgets_interaction.jsonl` dump `lastRoute`（Counter / NotesField）并写 `headless-snapshot.json`（structuralDigest + semanticDigest）。
- Windowed：同一 Widgets 页 `--gpu-shot` + `--offscreen-shot` + `--offscreen-diff`（tolerance 0）；`finishRun` 在 differing!=0 时退出码 3。
- 本地验证：`python3 Script/automation/gui/run_workbench_gpu_parity.py --skip-build` 退出码 0；日志 `pass=true differing=0 ratio=0.0000`（1280x800）；产物在 `Engine/Saved/Automation/gui-gpu-parity/`（不入库）。
- 既有 `UIFrameSnapshotTest` digest 与 `WidgetTreeTest` lastRoute 仍是 closure 层合同。

### Phase 9E 边界

- 这是 macOS/Clang/Vulkan 上的 GUIWorkbench 像素 parity，不是 editor chrome GPU shot，也不是 Windows/MSVC 或 OpenGL。
- 下一步：Phase 9F 跨平台回归证据 + editor release checklist。

## Phase 9F 当前 checkpoint（2026-09-04）

- 新增 `release_checklist.md`：Phase 9 发布门禁表。macOS/Clang/Vulkan 行（9A–9E、XP-MAC）为 Pass；Windows/MSVC、OpenGL presentation、小时级 soak、ImGuizmo-without-ImGui 为 Blocker。
- `GUIAppHost` 断言 `VulkanSwapChain`，没有第二条 OpenGL GUI compose 路径可在本机冒充回归。
- **不得宣称 retained editor ready**，直到 checklist 的 Blocker 行变成 Pass 或被产品明确 descope。

### Phase 9F 边界

- 本机无法提供 Windows/MSVC 或 OpenGL 运行证据；跨平台 skill 只约束编译契约，不是 XP-WIN 的替代。
- 计划 Phase 9 步骤 3 的 Windows/OpenGL 组合仍未关闭。

## Phase 10A 当前 checkpoint（2026-09-04）

- Dock leaf 改为 overlay：compact `tab.dock` strip + 左上角 12px fold 三角切换 `FDockNode::bHideTabBar`（展开=向下，折叠=向右；Collapsed tab bar，折角仍可点回来）。
- Dock content 不再 inset 12px；viewport/panel 自己管 padding。Editor menu/toolbar 和 Inspector form 同步压密度。
- `hideTabBar` 写入 dock/floating layout JSON；`DockNodeTest` 与 `WidgetLayoutTest::DockLeafTabBarIsCompactAndCanHide` 覆盖。
- `xmake r ya-gui-closure-test` 全量通过（398/398）；`xmake b ya-game-editor` 通过。

### Phase 10A 边界

- 没有宣称 ImGui/ImGuizmo 已移除。异常 Debug 窗来自 editor 帧仍 `GuiSystem::beginFrame` + `ImGuizmo::SetDrawlist()` 打到 `Debug##Default`。
- Inspector 类型覆盖（10B）和自研 gizmo（10C）未做。

## Phase 10C 当前 checkpoint（2026-09-04）

- `EditorViewportGizmoOverlay` 不再同步 ImGui IO，也不再依赖 `ImGuizmo`；现在只负责 retained viewport host 的输入路由（hover/drag/W-E-R）。
- `EditorLayer` 内建 native gizmo controller：屏幕命中测试 + 世界空间轴向平移/旋转/缩放 + stable UUID undo；多选仍以 primary entity 为 pivot，通过 world delta 同步到其他选中实体。
- gizmo 绘制从 `EditorSurface::presentViewportGizmo` 搬到 `EditorModule` 的 viewport compose callback，走 `Render2D::makeWorldLine/makeSprite`，因此 editor presentation frame 不再 `GuiSystem::beginFrame/endFrame/submit`。
- `Engine/Source/Applications/GameEditor` 与 `Engine/Source/Applications/GameRuntime` 源码内已无 `ImGuizmo` 引用；`ya-game-editor` / `ya-game-runtime` / `HelloMaterial` 的 **debug** 依赖文件不再链接 `imguizmo-local`。
- 验证：`xmake b ya-game-editor`；`xmake b ya-game-runtime`；`xmake b ya-testing`；`xmake r ya-testing -- --gtest_filter='EditorViewportOverlayHostTest.*:EditorTransformUndoTest.*'`；`python3 Script/ya.py run-editor --project Example/HelloMaterial/HelloMaterial.yaproject -- --exit-after-frame=60 --log-level=warn`。

### Phase 10C 边界

- `imgui-local` 仍因 `FilePicker` / `TypeRenderer` legacy helper 与 editor-internal texture bridge 保留；本 checkpoint 只完成 gizmo/viewport frame 对 `ImGuizmo` 的摘除。
- native gizmo 当前只覆盖轴向 translate/rotate/scale；尚未补 plane handles、uniform scale、mode switch UI。
- 下一步：Phase 10B inspector 类型覆盖，随后做 10E shell refactor + 第二轮 density/token 收口。

## Phase 10B 当前 checkpoint（2026-09-04）

- `PropertyGraph::build` 不再只停在 top-level 标量字段；现在会递归展开 reflected nested/composite property，并把 leaf node 物化为 dot-path（例如 `_params.albedo`、`_albedoSlot.textureRef`）。
- retained `EditorAutoPropertySection` 新增 `glm::vec2` / `glm::vec4` / `int` / `int32_t` / `uint32_t` 编辑能力；mixed state、undo merge、validation error 与多选写回都走同一条 `PropertyHandle` 路径。
- 直接收益：`TerrainComponent` 的 `_size` / `_gridResolution`、材质 component 的 `_params.*` 与 `TextureSlot.textureRef` 不再因为不是 `bool/float/vec3/string/enum/color/asset-ref` 的 top-level 字段而被整个跳过。
- `PropertyProjectionRegistry` 继续保留 editor 语义写回职责：`TransformComponent` 仍走 setter；材质类 (`PBR` / `Phong` / `Unlit`) 通过 owner change hook 保留 `onPropertyChanged(path)` 语义，不把 runtime 同步逻辑塞回 `EditorSurface`。
- 验证：`xmake b ya-testing`；`xmake r ya-testing -- --gtest_filter='EditorPropertyGraphTest.RecursiveProjectionFlattensNestedMaterialPropertiesAndInstallsChangeHooks:EditorPropertyGraphTest.TerrainVec2AndIntegerPropertiesSupportMixedEditingAndUndo:EditorPropertyGraphTest.AutoPropertySectionAssetPathCommitBrowseAndUndo'`。

### Phase 10B 架构判断

- 当前 `PropertyHandle` 仍承担了不少“本应由反射层提供”的通用能力：typed read/write、copy/restore、mixed compare、validation、nested leaf binding glue。
- 这些逻辑可以作为 Phase 10B 后半段的正式收口目标：在 `Core/Reflection` 中补出通用 `PropertyAccessor` / property-path walk / copy-compare-restore 能力，然后把 `PropertyHandle` 收瘦成 editor-specific adapter（multi-select mixed、undo glue、asset picker/error UI、owner callback）。
- 这次 checkpoint 先保证 inspector feature 闭环，不在 `EditorSurface` 继续堆实现；后续 reflection 下沉应优先落在 `Inspector/Property*` 与 `Core/Reflection/*` 边界。

### Phase 10B 边界

- 容器类编辑（旧 `ContainerPropertyRenderer` 覆盖面）和 custom renderer parity 仍未迁移；当前只补“高频标量/向量 + nested/composite flatten”。
- `TypeRenderer` / `ContainerPropertyRenderer` 现在没有 widgettree live caller，但在删除前仍要先完成 retained 容器/custom editor 替代，以及 reflection accessor 拆分，避免把 editor-specific 偶然实现固化成底层事实源。
- 按用户约束，新增 inspector/plan 能力不继续塞进 `EditorSurface`；现阶段只允许它保留现有装配职责，系统性拆分留到 Phase 10E。

## Phase 10B PropertyAccessor checkpoint（2026-09-04）

- `Core/Reflection/PropertyAccessor` 成为单实例 property 访问的事实源：typed get/set、equals/axis compare、enum/color/asset path、manipulate validation、以及 `collectLeaves` 的 nested path-walk。
- `PropertyHandle` 收瘦为 editor adapter：多选 mixed、N-instance copy/restore/undo glue、`Vec3Setter` / change hook、asset picker kind。不再内嵌 typed read/write 实现。
- `PropertyGraph::build` 改为消费 `PropertyAccessor::collectLeaves`，不再自己递归 ClassRegistry。
- 验证：`xmake b ya-testing`；`xmake r ya-testing -- --gtest_filter='PropertyAccessorTest.*:EditorPropertyGraphTest.*'`（21/21）。

### 边界

- 容器 element access 仍未进入 `PropertyAccessor`；下一步 10B 才把 sequence/map 编辑接到 retained inspector。
- custom renderer parity 仍未做；`TypeRenderer` 仍无 widgettree live caller。

## Phase 10B sequence container leaves checkpoint（2026-09-04）

- `PropertyAccessor::collectLeaves` 把 sequence-of-leaf 容器（`std::array` / `std::vector`，排除 map/set）展开成 `path[i]` 叶子；typed get/set/equals/validation 通过 `elementIndex` 访问元素地址。
- `PropertyHandle` 作为 editor adapter 转发 `elementIndex`；mixed compare 比较的是元素值地址，不是容器地址。
- `PropertyGraph` 用 `valueType(property, elementIndex)` 作为 node 类型，因此 retained `EditorAutoPropertySection` 能直接编辑 Skybox `cubemapSource.files[i]` 这类固定数组字符串，无需把 UI 塞进 `EditorSurface`。
- 验证：`xmake b ya-testing`；`xmake r ya-testing -- --gtest_filter='PropertyAccessorTest.*:EditorPropertyGraphTest.*'`（25/25）。

### 边界

- 只覆盖当前尺寸的 sequence 元素编辑；动态 vector 的 Add/Remove、map key-value 行、以及 `TypeRenderer` custom renderer（例如 TextureSlot 预览）仍未迁移。
- 下一步 10B：动态增删 / map / custom renderer；随后 Phase 10E 拆分 `EditorSurface`。

## Phase 10B container mutation / map / texture preview checkpoint（2026-09-05）

- `PropertyAccessor` 增加 `FValueLoc`（sequence index 或 map key）、动态 vector `appendEmpty` / `removeAt` / `insertEmptyAt` / `clearContainer`，以及 string-key map `insertMapKey` / `removeMapKey`。`collectLeaves` 为动态 sequence 与 map-of-leaf 发出 header + 值叶子。
- `VectorProperty::addEmptyEntry` / `insertEmptyAt` 补上真实 `emplace_back` / `insert`；旧 ImGui `ContainerPropertyRenderer` 的 `+` 以前是空操作。
- `EditorAutoPropertySection` 为 sequence/map header 提供 Add/Clear，为动态元素提供 Remove，结构变化后 `rebuildRows`；TextureRef 行增加 `UIImage` preview（替代 `TypeRenderer` TextureSlot after-property renderer）。
- 验证：`xmake b ya-testing`；`xmake r ya-testing -- --gtest_filter='PropertyAccessorTest.*:EditorPropertyGraphTest.*'`（30/30）。

### 边界

- map 只覆盖 leaf key + leaf value（`std::map<std::string, int>` 一类）；set、嵌套容器、非 string key 的任意 upsert UI 未做。
- `TypeRenderer` / `ContainerPropertyRenderer` 仍无 widgettree live caller，现已可删，但 `imgui-local` 仍被 FilePicker 占用，删除死代码留到后续 chrome 收口。
- 下一步：Phase 10E 拆分 `EditorSurface` + density/token；release gates 仍为 Windows/MSVC、OpenGL、hour-scale soak。

## Phase 10B property slot / typed-access cleanup（2026-09-05）

- `FValueLoc` + 构造函数末尾 `setter`/`elementIndex` 换成 `FPropertySlot`：字段用 `FPropertySlot::field`，序列用 `at(property, index)`，map 用 `at(property, key)`。`PropertyHandle` 只接收 `(ownerType, instances, slot)`；`Vec3Setter` 走 `setVec3Setter`。
- `PropertyAccessor` 底层 POD 不再摊开 `tryGetVec2/setFloat/...`；统一 `tryGet<T>` / `set<T>`。integer/enum/color/asset 仍是语义视图，因为它们不是单一 C++ 存储类型。
- `PropertyHandle` 作为 editor adapter：`tryGet/set/copy/restore` 模板覆盖 POD 与 `vec3` setter；bool snapshot 仍用 `copyBool`（避开 `vector<bool>`）。
- 验证：`xmake b ya-testing`；`xmake r ya-testing -- --gtest_filter='PropertyAccessorTest.*:EditorPropertyGraphTest.*'`（30/30）。

### 边界

- 未改 inspector 功能面；这是 accessor/handle 边界的 API 收口。
- 下一步仍是 Phase 10E：拆分 `EditorSurface` + density/token。

## Phase 10E file-picker owner checkpoint（2026-09-05）

- scene-save / generic file / asset picker 不再各维护一套 overlay + FileExplorer + keyed rows。`EditorFilePickerDialog` 是唯一 retained picker owner；save-as 用目录选择 + name 字段合成 `dir / (name + ext)`。
- `EditorSurface` 只保留 `openSceneSaveDialog` / `openFilePickerDialog` / `openAssetPickerDialog` 宿主入口，以及每帧 `_filePicker->sync`。asset kind → request 的映射在 `makeAssetPickerRequest`。
- 验证：`xmake b ya-testing`；`xmake r ya-testing -- --gtest_filter='EditorFilePickerDialogTest.*'`（3/3）。

### 边界

- 未拆 Content Browser / UI Designer / Runtime Tools / Asset Inspector / settings；那些只有在已有稳定 owner 边界时才拆，不做 line-count surgery。
- density/token 仍未做；editor chrome 仍有 magic 8/12/22/26。
- `imgui-local` 仍被 legacy `FilePicker` 占用；本步不宣称可删。
- 下一步：其余稳定 owner 或 density/token；release gates 仍为 Windows/MSVC、OpenGL、hour-scale soak。

## Phase 10E editor-settings owner checkpoint（2026-09-05）

- `EditorSettingsDialog` 持有 settings overlay 与 sampler/overlay/startup-scene 控件。`EditorSurface` 只提供 `FEditorSettingsBindings`（EditorLayer 读写 + `openFilePickerDialog`）并每帧 `_settings->sync`。
- Browse 走同一套 `makeSceneJsonFilePickerRequest` → `EditorFilePickerDialog`，不再在 `EditorSurface` 里拼 settings 控件树。`labeledButton` 收到 `EditorListRows.h`，避免第三份拷贝。
- 验证：`xmake b ya-testing`；`xmake r ya-testing -- --gtest_filter='EditorSettingsDialogTest.*:EditorFilePickerDialogTest.*:EditorListRowsTest.*'`（7/7）。

### 边界

- 未拆 Content Browser / UI Designer / Runtime Tools / Asset Inspector；那些仍嵌在 `EditorSurface` 装配路径里。
- density/token 仍未做。
- 下一步：density/token，或下一个已有稳定边界的 tab owner。

## Phase 10E density / inspector chrome checkpoint（2026-09-05）

- Inspector 行不再把整条 reflected path 写成 `Image / Uv Scale`。`propertyLabelFromPath` 把父路径收成 `PropertyNode::group`，行上只显示叶名；`bVisible` / `bEnable` 去掉匈牙利 `b` 前缀。`EditorAutoPropertySection` 在 group 变化时插入 eyebrow 分组标题。
- `editor_density` 放进 `EditorTheme.h`：label column 140、row 22、spacing 6。Inspector form / AutoProperty rows / list row height / toolbar 高度走同一套 token，不再各写 100×22。
- 底栏 dock tab 标题缩短（Content / Stats / Workbench / Runtime / UI / Assets / Debug）；稳定 panel key 不变，布局 persistence 不受影响。
- 验证：`xmake b ya-testing`；`xmake r ya-testing -- --gtest_filter='EditorPropertyGraphTest.*:EditorListRowsTest.*'`（27/27）。

### 边界

- EditorSurface 里仍有不少按钮 preferredSize 字面量（Play/Content header 等）；本步只收 inspector/list/toolbar 共用密度，不做全文件数字替换。
- 未拆其余 tab owner；release gates 仍为 Windows/MSVC、OpenGL、hour-scale soak、`imgui-local`。
- 下一步：其余稳定 tab owner，或继续把剩余 chrome 字面量迁到 `editor_density`。

## Phase 10F boundary extraction checkpoint（2026-09-05）

- 新增 GameEditor 侧 `PropertyGraphBuilder`，接管 nested/composite、sequence/map leaf 展开及 editor path/leaf role；Core Reflection 不再暴露 `FLeaf` / `collectLeaves`。
- 新增 `PropertyEditorMetadata`，接管 color、manipulate spec 和 validation；`PropertyHandle` / `PropertyGraph` 改从 editor adapter 读取这些语义。
- Core `PropertyAccessor` 保留 slot/address、基础类型、容器、enum/asset/value conversion；editor path 与 UI metadata 不再位于 Core API。
- 验证：`xmake b ya-foundation-core` 通过；Inspector targeted tests 在边界迁移前后保持通过（36/36）。

### 边界

- 本 checkpoint 未完成稳定 identity value object、nested path resolver、结构化 mutation result 和 capability registry。
- `ya-testing` 全量构建当前被工作树中其他未提交的 DockWorkspace → DockContext 重命名阻塞；未触碰或覆盖该用户改动。

## Phase 10F boundary extraction follow-up checkpoint（2026-09-06）

- PropertyGraphBuilder::FLeaf 现在保存从根对象到 leaf owner 的 ownerPath；PropertyHandle 在配置 resolver 后按 slot path 重新解析 nested/composite owner，根对象替换后 nested leaf 仍可读写。
- EditorInspectorTab 为所有 graph node 安装根实例 resolver，并附带 scene-path#entity-uuid:component identity；不再只对顶层字段刷新地址。
- PropertyProjection 的现有 change-hook 行为保持不变，避免在 projection 完成前捕获未安装的 resolver；后续会把 hook 进一步提升为 identity-aware command sink。
- Core 新增 FPropertyMutationResult / EPropertyMutationStatus，typed set<T> 通过 setResult 区分 invalid/read-only/type mismatch/unavailable/unchanged/changed；旧 bool API 保持兼容现有 Inspector consumer。
- 新增 NestedBindingReplaysOwnerPathAfterRootReplacement 与 TypedMutationResultPreservesFailureReason 回归测试。
- 验证：xmake b ya-game-editor、xmake b ya-testing；PropertyAccessorTest.*:EditorPropertyGraphTest.* targeted 37/37 通过。

### 边界

- 多实例容器操作仍只有 preflight + bool 结果，尚未具备统一 rollback/partial-failure transaction。
- PropertyHandle 仍保留直接地址作为无 resolver 的构造 fallback；稳定 identity 已进入 API，但 Undo command 尚未完全改为 identity-only。
- equality、integer、enum、asset-ref 和容器 key capability registry 尚未抽出。

## Phase 10F multi-instance scalar transaction checkpoint（2026-09-05）

- PropertyHandle 的模板 set<T> 现在先读取所有实例快照，再执行批量写入；若 typed accessor 在执行中返回拒绝，已应用实例会恢复到快照，避免部分成功。
- Vec3 projection setter 也纳入同一批量路径；resolver 刷新和 mutable-address preflight 仍在事务开始前执行。
- `setInstanceResolvers` 会清除旧 identity bindings，避免替换 resolver 后混用两套来源。
- 验证：`xmake b ya-game-editor`、`xmake b ya-testing`；`EditorPropertyGraphTest.*:PropertyAccessorTest.*` targeted 38/38 通过。

### 边界

- 容器 mutation 尚未有可恢复 snapshot（尤其 vector/map 的结构和值），因此仍依赖全实例 preflight；自定义容器异常回滚留待 10F-4。
- restoreInteger/restoreEnum/restoreColor/restoreAssetPath 等专用路径尚未统一到同一快照 helper。
