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
