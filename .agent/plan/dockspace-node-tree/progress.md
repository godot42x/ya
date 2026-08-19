# Progress — dockspace-node-tree

## 2026-08-19 — 规划阶段

### 已完成的前置工作（在 main 上）

- **P7 原始三栏 DockSpace** 已合入（commit `0b4fc7cc` 可用性修复 + `609a2692` panel 填满修复）：
  - `addPanel(name, widget, zone)` 支持三栏分配（0=left/1=center/2=right）
  - 空 zone 占位（`UITabBar::_emptyPlaceholder` + computeDesiredSize/paintSelf）
  - panel 用 box stretch 填满 zone（setStretchLastChild）
- **引擎级 detach keepAlive 修复**（commit `e8bd74a4`）：WidgetTree::detach 拆边前抓 shared_ptr 保活，修复 use_count==1 析构崩溃——本计划的护栏基础。
- **headless scenario 通路**（commit `e8bd74a4` 同期）：GuiScenarioEventSource 公共化到 App/Kernel，DockSpace 拖拽可在 headless 下端到端验证。

### 当前规划状态

- 阶段 0 已完成：WidgetTree 的通用 drag lifecycle observer 与
  EDragFinishResult（Dropped / NoTarget / Cancelled）已验证；下一切片是纯
  `FDockTreeModel`，仍不改现有三栏 visual projection。

- 用户确认选 B（完整 ImGui 式 DockNode 嵌套树 + 浮动 + 8向预览）。
- 三个 Explore agent 已探查：布局系统 / 拖拽+绘制 / 浮动窗口，证据充分。
- 设计决策已确认：Split 复用 UISplitPane 载体；先单窗口内浮动、预留多 window；payload 保持 string。
- 计划已完成第二轮架构收口，实施顺序扩为 0～6 阶段：generic drag lifecycle → pure
  model transaction → WidgetTree projection → 5 向 → 8 向 → floating → multi-window port。
- 关键修正：`FDockNode` 不再混入 `UITabBar*` / `UIContainer*`；改为 pure model +
  `FDockNodeView` projection + panel registry。此前“手动析构 raw node tree”的方案已废弃。
- 关键 blocker 已明确：现有 `WidgetTree::beginDrag/updateDrag/endDrag` 没有每 move
  observer 和 no-target finish 回调，不能正确做 hover zone 或 tear-off；必须作为阶段 0
  的通用事件系统小改动先完成。
- 8 向 corner 语义已明确为 compound two-split + visible persistent empty leaf；不允许
  用视觉 quarter 预览却在 drop 时退化为 cardinal。
- 当前进入阶段 1；不直接重写 `DockSpace.cpp`，先建立可单测的纯 dock model。

### 关键探查结论（证据锚点）

- UISplitPane 两子硬限（UILayout.cpp:552-595）→ 不放开，自建 FDockNode 二叉树。
- layoutAssigned 递归协议（UIElement.cpp:136-140）→ DockSpace 递归分配 rect 给 root 节点。
- addDetachedChild 不标脏（UIElement.cpp:302-310）→ split/merge 后手动 markLayoutDirty。
- detach keepAlive 仅一层（WidgetTree.cpp:428-449）→ 移动子树须额外保活。
- addSprite(nullptr) 画半透明（UIFrameSnapshot.cpp:30）→ hover 预览零新 API。
- findDropTarget 跨层命中（WidgetTree.cpp:1074-1082）→ 浮动面板拖回 dock 可行。
- PopupOverlay 的 shield/dismiss/focus 语义（PopupOverlay.cpp:34-105）→ 不能复用；
  浮窗应由常驻 `UIDockFloatingHost` + 多实例 `UIFloatingDockWindow` 承载。

### 剩余风险

- Dock model projection 需要确保 UI view rebuild 不使 drag target / focus 缓存变悬挂；
  event callback 只能回查 stable NodeId/PanelId。
- `UISplitPane` 目前只把 divider ratio 写到可选 Reactive；需要一个窄的 controller
  write-back，避免重投影时 divider 跳回旧 model ratio。
- 四角 compound drop 创建 persistent empty leaf 是明确 UX 成本；scenario 和 Workbench
  文案必须令它可理解，后续才能决定是否需要 reset-layout / explicit close-empty-leaf。
- 浮动 panel 与 Popup modal 的 z-order、focus restore 需要按 route trace 真机验证；
  不能复用 `UIPopupOverlay` 的 dismiss/single-content 语义。

### 下一步

建立无 `UIElement*` / `UITabBar*` / `UIContainer*` 的 `FDockTreeModel`、transaction
和 invariant validator，并以 model-only 测试覆盖 merge、cardinal split、source collapse
与 ratio/min-extent 规则。

### 2026-08-19 — 阶段 0 首轮实现

- WidgetTree::beginDrag 接受可选 DragSessionObserver，保持原有三参数调用兼容。
- onMove 对每次 updateDrag 同步通知；onTargetChanged 只在接受目标切换时通知。
- endDrag 区分 Dropped 与 NoTarget，cancelDrag 报告 Cancelled；回调在清理会话状态后
  执行，drop target 通过 shared_ptr 保活到 onDrop 完成。
- 新增 WidgetTreeTest 覆盖每次 move、目标切换、drop/no-target/cancel 以及会话清理。
- 将 widgets 测试中遗留的 `MouseButtonPressedEvent(0)` / release 同类调用改为
  `EMouse::Left`，与当前输入 API 的强类型约束一致。
- `xmake b ya-gui-widgets-test` 通过；聚焦
  `WidgetTreeTest.DragObserver*` 的两个新测试通过。
- `xmake b GUIWorkbench` 通过；headless P7 场景通过，写出
  `dock_initial.json` 与 `dock_dragged.json`，三栏/三个 tab 的结构断言均通过。
- 全量 `ya-gui-widgets-test` 当前为 122/127；以下 5 项是本切片外的既有基线失败：
  `UIFrameSnapshotTest.BuildResolvesItemsToRenderPixelsInPaintOrder`、
  `UIFrameSnapshotTest.ScrollViewportClipsContentToViewportRect`、
  `UIFrameSnapshotTest.SplitPaneClipsChildrenToOwnPaneRect`、
  `UIFrameSnapshotTest.ContainerClipResizeInvalidatesChildSegments`、
  `ToolControlsTest.SplitPaneDividerDragChangesRatioAndEndsSession`。
- `xmake b ya-gui-closure-test` 仍被目录迁移残留阻断：
  `Render2DClipTest.cpp` 引用已不存在的 `GUI/Draw2D/Render2D.h`；这与 observer
  行为无关，留给 GUI Draw2D include-path 基线修复单独处理。

阶段 0 的 generic drag observer/finish result 与 P7 基线 dump 已收口；接下来建立
无 UI 指针的 `FDockTreeModel` 和 model tests。

### 2026-08-19 — 阶段 1 pure Dock model 完成

- 新增无 `UIElement*` / `UITabBar*` / `UIContainer*` 依赖的 `FDockTreeModel` 与公开
  `DockNode` wrapper；模型只持有 binary node tree、panel identity registry、父子关系、
  split ratio/min extent 与 empty-leaf policy。
- 实现 register/add、merge move、四方向 cardinal split、source empty collapse、remove
  以及 invariant validator。注册但尚未放入布局的 panel 被视为合法 detached registry
  entry，split 可将其作为新 panel 放入目标 leaf；重复 identity、未知 id、同 leaf move
  等非法 mutation 在修改前拒绝。
- mutation 采用 precondition → backup → mutate → normalize → validate → rollback 的
  原子语义；同 leaf move 不再先删除后失败。
- 新增 `DockNodeTest` 覆盖 identity、cardinal split、merge/order、empty-source collapse、
  invalid no-mutation、ratio clamp、geometry validation 与 remove registry。
- 验证：`xmake b ya-gui-widgets-test` 通过；`xmake r ya-gui-widgets-test
  '--gtest_filter=DockNodeTest.*'`：7/7 通过。

下一步进入阶段 2：以 `FDockTreeModel` 为唯一事实源建立 `UIDockSpace` 的 WidgetTree
projection，先复现当前三栏默认布局，再逐步替换 `_groups[3]`，不在 model 中引入视觉
widget 指针。

### 2026-08-19 — 阶段 2 projection bridge 首轮

- `FDockTreeModel` 增加 `splitEmptyLeaf` 与稳定 leaf preorder 查询；默认三栏布局由模型
  创建两个嵌套 split 和三个持久 leaf，不再需要虚假 panel 占位。
- `UIDockSpace` 开始持有 model、zone leaf ids 和 stable panel ids。兼容的 `addPanel` 会先
  注册/挂载 model panel，再建立现有 tab/content visual projection；跨 zone move 先提交
  model transaction，成功后才更新视觉 groups。
- 这是一轮过渡 bridge：当前 `_groups[3]` 视觉结构仍保留，尚未完成递归 `FDockNodeView`
  projection；因此 n2 继续保持 in_progress，下一切片要把 split/leaf view 从 model 递归
  materialize。
- 验证：`xmake b GUIWorkbench` 通过；Dock headless scenario 通过，初始与拖拽 dump 均生成。

### 2026-08-19 — 阶段 2 递归 projection 切片

- `UIDockSpace::layoutAssigned()` 不再手写固定 `outerSplit/innerSplit` 视觉树，改为从
  `FDockTreeModel::root()` 递归 materialize：split 节点映射为 `UISplitPane`，leaf 节点
  映射为 tab bar + content host。
- 默认模型 ratio 明确写入 `0.22` 与 `0.78`，保持 P7 三栏的既有几何契约；模型仍是布局
  事实源，视觉树只在首次 attach 时生成。
- 当前仍保留 `_groups[3]` 作为过渡 panel/widget registry；它不再构造 split 布局，但
  还承担 leaf view 的 tab/content 绑定。按 stable leaf id 回查 zone，未知 leaf 只生成空容器。
- 验证：`xmake b GUIWorkbench` 通过；
  `xmake run GUIWorkbench --start-page Dock --scenario Example/GUIWorkbench/Scenarios/dock.jsonl
  --headless --scenario-dump-dir build/dock_dump_phase2_recursive2` 通过。
  `DockZoneLeft` 宽度断言 `278.600006`、center/right、三个 tab、拖拽 checkpoint 均通过。
- 本切片不宣称 projection 阶段完成：divider ratio 尚未回写 model，`_groups[3]` 尚未替换
  为按 `DockPanelId` / `DockNodeId` 管理的独立 `FDockNodeView` registry。

### 2026-08-19 — 阶段 2 divider model write-back

- `UISplitPane` 增加窄的 ratio-change callback；divider 拖动仍只修改布局状态，但在同一
  次交互中将最终 ratio 通知宿主。
- `FDockTreeModel::setSplitRatio()` 作为唯一模型 mutation 入口，负责有限值检查、clamp
  与 invariant 保证；`UIDockSpace` materialize split 时按 stable `DockNodeId` 绑定回写。
- 这样 resize / 重新 materialize 时不会因为视觉 split 的瞬时 ratio 丢失而跳回旧值；仍未做
  model mutation 后的局部 subtree rebuild 和独立 view registry。
- 验证：`xmake b GUIWorkbench` 通过；Dock headless initial/dragged scenario 通过。

### 2026-08-19 — 阶段 2 selection ownership

- 增加 `FDockTreeModel::selectPanel()`，tab selection callback 先更新 model，再做 content
  widget detach/attach；selected panel 不再只存在于 `UITabBar` 的视觉状态。
- 增加 model-only selection ownership 测试；DockNode focused suite 8/8 通过。
- 当前阶段仍不重建整个 subtree；下一步是建立按 `DockNodeId` / `DockPanelId` 索引的
  projection registry，把 view 生命周期从三组 zone 临时数组中分离出来。

### 2026-08-19 — 阶段 2 leaf view registry

- 新增 `FLeafView` 与 `_leafViews: unordered_map<DockNodeId, FLeafView>`；leaf 的
  `UITabBar/UIContainer` 句柄按稳定 leaf id 管理，不再存放在 `FTabGroup[3]`。
- `FTabGroup` 现在只保留 zone 过渡数据与 panel/widget registry；materialize、rebuild、tab
  selection 都通过 leaf id -> view registry 查找视觉对象。
- 首次 projection 时清理 view registry，避免重复 materialize 后残留旧 leaf view。
- 验证：`xmake b GUIWorkbench` 通过；Dock headless 初始/拖拽 scenario 全部通过。
- 仍待下一切片：view registry 的非 zone leaf 支持、局部 subtree rebuild、panel registry 从
  `FTabGroup[3]` 解耦，以及 model mutation 后的 view teardown/focus 保护。
- 增加 `SplitRatioMutationIsClampedAndAtomic` model test；DockNode focused suite 10/10 通过。

### 2026-08-20 — 阶段 3 cardinal preview/drop 首轮

- `UIDockSpace` 现在通过 drag observer 的 `onMove` 维护 immutable preview；`paintChildren`
  后绘制半透明 preview，避免被 leaf content 盖掉。preview 区域按 target leaf 的 center /+  cardinal 结果计算，不再只是整块 outline。
- payload 改为稳定 `DockPanelId` 编码（`dock-panel:<id>`）；drop 时先解析 preview，再
  按 preview 执行 `FDockTreeModel::movePanel / splitLeaf`。same-leaf cardinal split 已
  回归为真正的 model mutation，而不是 zone swap。
- 为了保住三栏锚点，`movePanel(..., collapseSource=false)` 让 merge 路径先不塌缩源 leaf；
  默认 P7 三栏命名仍可稳定通过旧 smoke。`DockTabBar0/1/2` 与 `DockZoneLeft/Center/Right`
  基线保留。
- 新增正式 smoke 场景 `Example/GUIWorkbench/Scenarios/dock_cardinal_split.jsonl`；headless
  验证通过，拖到 leaf 边缘会生成 `DockSplit`，而不是只换 tab。
- 下一步仍缺：corner compound preview、非 zone leaf 真正投影、以及 panel registry 从
  过渡 zone 数据结构里完全剥离。
