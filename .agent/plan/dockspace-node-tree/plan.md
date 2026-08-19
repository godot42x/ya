# DockSpace → ImGui 风格 DockNode 嵌套停靠 + 浮动窗口 + 8向 hover 预览

> 计划线：dockspace-node-tree
> 创建：2026-08-19
> 状态：规划已收口；下一实施切片为阶段 0（drag lifecycle + model baseline）

## 可行性结论与实施前提

这个计划**可行，但现有版本不能直接照着实现**。当前 `UIDockSpace` 是把模型、
visual widget、tab widget 裸指针和 drag 临时态混在一起的三栏特例；若直接把
`_groups[3]` 换成含 `UITabBar*` / `UIContainer*` 的手写树，只会把现有耦合放大到
递归结构。实施前必须先完成下面四项设计收口：

1. **Dock model 与 WidgetTree projection 分离**：`FDockNode` 是纯数据树，不能拥有或
   长期保存 `UIElement*`、`UITabBar*`、`UISplitPane*`。WidgetTree 只是该模型在当前
   window 中的可重建投影。
2. **一个 dock transaction 是唯一改树入口**：tab merge、split、tear-off、re-dock 都
   先修改已验证的 model，再同步受影响的 visual subtree；禁止各个 tab callback 直接
   detach/reparent 任意 widget。
3. **WidgetTree drag API 要补生命周期观察点**：当前只在 drop target 切换时调用
   `setDropHighlight()`，没有每次 move 的回调，也没有“没有 target 的 release”回调。
   因此无法稳定实现 8 向预览和 drag-out tear-off；这是阶段 0 的前置，不是
   `UIDockSpace` 内部能靠轮询补掉的问题。
4. **8 向 corner 不能伪装成普通二分 split**：一个叶子仅有 source/target 两个 tab
   组时，矩形的“角落 + 剩余 L 形区域”不可由二叉矩形树表示。corner drop 必须是
   明确的 compound operation，会生成第三个、可见的 empty leaf；不能画 quarter
   preview，落下时却静默退化为 cardinal split。

这四项做完后，dock 内核与未来 DSL/JS、document 格式无关：上层只声明 panel、
初始树和 persistence；运行时的几何、route、transaction 和 owner 仍由 GUI 核心负责。

## 目标

把 `UIDockSpace` 从「固定三栏 tab 交换」升级为 ImGui docking 范式（用户选 B：完整 ImGui 式，非最小集）：

1. **任意嵌套停靠树**：dock 区是一个 `FDockNode` 二叉树，每个内部节点是二分 split（水平/垂直 + ratio），每个叶子是一个 tab 组。拖 tab 到某叶子的上/下/左/右 → 把该叶子裂成 split；拖到中心 → tab 合并进该叶子。
2. **可浮动窗口**：拖出 tab 形成带标题栏的浮动面板（挂 Popup 层、自由定位、自保活），可再拖回停靠到任意 dock 节点。范围：**先单窗口内浮动层**（与 ImGui 一致），预留多 window 扩展点。
3. **8向 hover 预览**：拖拽时实时显示「将停靠成什么布局」的半透明预览（中心 + 上下左右 + 四角）。

## 决策（已与用户确认）

- **Split 可视化载体**：复用 `UISplitPane` 作 FDockNode 的 Split 可视载体（白送 divider 拖拽 + hover cursor）。行业惯例（Qt QSplitter 树 / Avalonia GridSplitter / ImGui 纯数据树+自绘分割线）一致指向「二叉递归分割 + 独立可视分割线」，方案 A 贴合本引擎现有抽象且符合行业形态。
- **浮动窗口范围**：先单窗口内浮动层（Popup 层），后续再扩展多 OS window。把「内容归属」抽象成接口层，未来多 window 只是 reparent 到另一个 WidgetTree，不动 dock 逻辑。
- **payload**：保持 `std::string` 类型（不改 WidgetTree 公共接口），加编解码 helper。

## 参考框架结论

这条线不是“再造一套 UI 大框架”，而是把 DockSpace 约束成一个清晰的布局/编排层：

- **Qt**：widget 负责状态与呈现，layout 负责几何；splitter 是专用布局节点，不是业务容器。DockSpace 应该学这一点，拆分/比例/拖拽属于 layout 语义，不属于 panel 内容。
- **UE Slate**：路由、focus path、modal/popup、tooltip 都是树上的状态流转；dock 的浮动/重停靠也应走树路由，而不是旁路全局查找。
- **Chrome / DOM**：命中是单一 topmost hit，事件沿祖先链传播；overlay、popup、portal 只是不同层级的呈现，不改变路由基本模型。
- **Flutter**：Element / RenderObject / Widget 分层最关键的一点是“数据与几何分离”。DockSpace 的 node tree 负责几何，tab/panel 只保存内容与临时交互态。

因此，这份计划的底线是：

1. DockSpace 只做布局与停靠编排，不吞 app state。
2. Split / Leaf 的几何规则独立于面板内容。
3. focus / hover / drag / modal 不放进 panel 业务对象里。
4. 先单窗口闭环，后多窗口扩展，且扩展点只加 owner 层，不回改布局内核。
5. dock layout 先是 GUI 底层能力；DSL / JS 只负责声明和编排，不负责内核几何与停靠规则。

### dock layout 的层级归属

- **底层 GUI**：必须提供 dock tree、split、tab、focus/drag/drop route、floating host 这些原语。
- **上层 DSL / JS**：可以描述 panel 结构、初始布局、命令绑定、workspace 配置，但只能调用底层原语。
- **布局库**：可以提供通用的 box/split/scroll 算法，但不能替代 dock 的 owner / route / reparent 规则。
- **结论**：dock layout 不应该依赖某个脚本环境才能存在；脚本只是让它更容易被声明式使用。

## 对象模型（先定边界，再写代码）

### 1. 事实源分层

- `UIDockSpace`：一窗口 dock controller。当前阶段拥有 `FDockTreeModel`、投影根和
  per-window drag/focus transient state；未来由 workspace coordinator 创建多个
  `UIDockSpace`，但不让 node 直接知道 OS window。
- `FDockTreeModel`：纯数据模型，拥有整棵 `FDockNode` 树与 NodeId 分配器。
- `FDockNode`：只有 `Split` / `Leaf` 两类的 model node。Split 保存几何参数，Leaf
  保存 tab 的 **PanelId** 序列和 selected PanelId；它不保存任何 widget 指针。
- `FDockPanelRecord`：dockspace 的 panel registry（PanelId → title、`shared_ptr` widget、
  可选 close policy）。panel widget 的强引用唯一在 registry，叶子只保存 id。
- `FDockNodeView`：model node 在当前 WidgetTree 的短生命周期投影；Split view 包含
  `UISplitPane`，Leaf view 包含 tab bar 和 content host。它由 `UIDockSpace` 的
  view map 持有，model mutation 后可按 subtree 销毁/重建，绝不反向成为事实源。
- `FDockDragSession`：一次 drag 的临时状态，保存 PanelId、source `DockSpaceId`、
  source LeafId、最近 hover LeafId/Zone、pointer；不进入 model，也不序列化。
- `UIFloatingDockWindow`：非 modal 的浮动 leaf view；只负责 title bar、移动、激活和
  内容 mount。它挂在一个 `UIDockFloatingHost`（Popup layer 的普通 child）下，而**不**
  继承 `UIPopupOverlay`；后者的 shield/dismiss/focus 语义不适合常驻多实例浮窗。

### 2. 节点职责

- `Split`：model 只负责 orientation、ratio、min extent；`UISplitPane` 只负责 divider
  命中/paint/capture 和将拖后的 ratio 回写 controller。
- `Leaf`：model 只负责 tab order、selected panel、empty-leaf policy；Leaf view 负责
  tab bar、content mount、空态显示、drop target 预览。
- panel widget：只负责业务内容呈现，不知道自己是否在 dock 树里、也不知道 split 规则。

### 3. Owner / lifetime 规则

- `FDockTreeModel::_root` 为 `std::unique_ptr<FDockNode>`；child 也为 unique_ptr，
  parent 仅为 non-owning pointer。split/merge 只能通过 model method 执行，不能在
  `UIDockSpace` 外手改 child 指针。
- `PanelId` / `NodeId` 是单调递增的稳定整数；tab 文案不是 identity，重名 panel 也
  必须可独立移动。所有 scenario/dump 都断言 id + title，不能只按 title 查找。
- panel widget 的强引用由 `FDockPanelRecord` 持有；visual parent 的引用只是在它显示
  时的第二个强引用。transaction 在 detach 前取得 registry 的 local shared_ptr，直到
  新 mount 完成，随后才允许释放临时引用。
- model node、view node、drag target 都不能把裸 pointer 缓存到下一帧。每次 event
  callback 以 NodeId/PanelId 回查 model；view rebuild 会使旧 UIElement pointer 失效。
- 交互临时态（hover/drag/focus preview）和持久树结构分离，取消 drag 必须完全无 model
  mutation；成功 drop 是原子 transaction，不允许中间状态被下一次 snapshot 看见。

## 关键约束（来自探查证据）

- `UISplitPane`/`UISplitLayout` 硬限两子（UILayout.cpp:552-595），但 ImGui DockNode 本就是二叉分裂，**不放开此限制**——自建 `FDockNode` 树，每 Split 恰好两子，复用二叉语义。
- `layoutAssigned(rect)` 是递归下发协议（UIElement.cpp:136-140）；DockSpace 现只把 rect 给第一个子后 break（DockSpace.cpp:105-110），改为「递归分配给 root FDockNode」。
- `addDetachedChild` 在已入树节点上**不标 layout dirty**（UIElement.cpp:302-310）→ split/merge 后必须手动 `markLayoutDirty()`。
- `detach` 仅 keepAlive 一层（WidgetTree.cpp:428-449）→ 移动 panel widget 子树必须额外保住 `shared_ptr`，否则复现 P7 `use_count==1` 析构崩溃。
- 全局单脏位 `_bLayoutDirty`（WidgetTree.cpp:462）→ 每次 dock 操作整树重排，按「一致性优先性能」口径可接受。
- hover 预览**零新 API**：`addSprite(rect, color, nullptr)` 传 nullptr 纹理画半透明填充（UIFrameSnapshot.cpp:30 确认 null texture draws white texture）。用 addSprite + addRectOutline + addLine 即可，放在 `DockSpace::paintSelf`（Content 层）。
- 拖拽骨架现成：`WidgetTree::beginDrag/updateDrag/findDropTarget/endDrag`（WidgetTree.cpp:1042-1140）；`findDropTarget` 跨层命中已支持（层是 root 子，zOrder 优先）。

## 数据结构

新建 `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/DockNode.h`：

```cpp
using DockNodeId = uint64_t;
using DockPanelId = uint64_t;
using DockSpaceId = uint64_t;
inline constexpr DockPanelId kInvalidDockPanelId = 0;

struct FDockPanelRecord {
    DockPanelId                 id;
    std::string                 title;
    std::shared_ptr<UIElement>  widget;
    bool                        closable = true;
};

struct FDockNode {
    enum class EKind { Split, Leaf };
    EKind                         kind;
    DockNodeId                    id;
    FDockNode*                    parent = nullptr; // non-owning
    // Split:
    ESplitOrientation             orientation = ESplitOrientation::Vertical;
    float                         ratio = 0.5f;
    float                         minExtent[2] = {120.0f, 120.0f};
    std::unique_ptr<FDockNode>    child[2];
    // Leaf:
    std::vector<DockPanelId>      panelIds;
    DockPanelId                   selectedPanel = kInvalidDockPanelId;
    bool                          bPersistentEmptyLeaf = false;
};
```

`FDockTreeModel` 持有 `std::unique_ptr<FDockNode> _root`，同时管理 node/panel id
分配、parent repair 和结构不变量；`UIDockSpace` 另外持有 `panel registry` 与
`NodeId -> FDockNodeView` 的投影 map。初始树仍是
`Split(vertical) → [Leaf(left), Split(vertical) → [Leaf(center), Leaf(right)]]`，仅作为
Workbench 的默认布局，而不是 `addPanel(zone)` 的长期 API。

### 必须新增的 model 操作（没有 public child 指针写入）

```text
addPanel(record, initialLeaf)
movePanel(panelId, sourceLeafId, targetLeafId, EDropPlan)
splitLeaf(targetLeafId, ECardinalSide, panelId)
splitLeafCorner(targetLeafId, ECorner, panelId)
mergeIntoLeaf(targetLeafId, panelId, insertIndex)
removePanel(panelId)
normalizeAfterMutation(affectedNodeId)
validateInvariants()
```

`validateInvariants()` 在 debug、headless scenario checkpoint 和 model mutation 后运行：
每个 PanelId 恰好出现一次；leaf 才能持有 panels；split 恰有两个 child；parent 关系
正确；selected panel 属于 leaf 或为 invalid；ratio/min extent 有效；没有未登记 panel。
这比从 WidgetTree 反推 dock 状态可靠得多。

### view projection 和 layout 契约

`FDockNodeView` 由 `UIDockSpace::materializeNode(node)` 生成：

- Split node → `UISplitPane`，两个 child view 分别 attach 到它。node 的
  orientation/ratio/min extent 下推到 `UISplitLayout`。divider 拖动必须通过一个明确的
  `onRatioChanged(NodeId, float)` 回调写回 model；不能只改 `UISplitPane` 的 transient
  layout，否则下一次重建会跳回旧比例。
- Leaf node → `UIContainer(vertical)`，内含 `UITabBar` 和一个 content host；tab 切换只
  改 model 的 `selectedPanel`，再重挂 registry 中同一个 widget。空 leaf 显示 drop
  placeholder，不伪造业务 panel。
- `UISplitPane` 是**投影内的 layout/interaction host**，不是 DockNode owner；
  `UISplitLayout` 的两 child 限制恰好是 model 的二叉不变量。
- 每次 transaction 只重建最小受影响的 projection subtree；在所有 projection 变更完成后
  调一次 `WidgetTree::invalidateLayout()`。布局 pass 只读取 model+view，绝不决定 panel
  归属。

布局遵守左上角逻辑像素：rect 和 min extent 均 clamp 非负；ratio 先 clamp 到 `[0,1]`，
再根据 `(available - divider)` 与两个 min extent 求可行区间。若 min extent 之和超过
available，则按比例压缩两边到非负，而不是出现负 rect 或越界 scissor。

### 8 向区域和 compound drop 的精确定义

`computeZone8` 只做几何分类；`resolveDropPlan` 决定可执行 mutation。区域为
`Center / North / South / West / East / NorthWest / NorthEast / SouthWest / SouthEast`：

- center 使用 leaf rect 中央 50% × 50%（不是硬编码像素），edge band 是剩余区域中
  不含 corner 的部分；corner 为四个角各 25% × 25%。实际预览 rect 必须来自同一
  `resolveDropPlan`，不能另写一套数学。
- cardinal：将 target leaf 替换成一个 Split；source panel 在 side 对应 child，旧 target
  leaf 是另一个 child。默认新 panel child ratio 0.30（受 min extent clamp），方向为
  West/East → Vertical，North/South → Horizontal。
- corner：这是 two-step split，且**允许并明确显示**一个 `bPersistentEmptyLeaf`。例如
  NorthWest：先 Vertical split（left = second Horizontal split，right = old target）；再在
  left 侧 Horizontal split（top = new panel，bottom = persistent empty leaf）。四角的先后
  顺序固定为「左右先、上下后」，方位镜像。这样 preview、drop 和 serialization 对同一棵
  三 leaf 树负责；后续 tab 可填补 empty leaf。
- 不允许“角落 drop 静默降级成 left/top”。若当前 extent 小到 compound plan 的三个
  leaf 都不满足最小尺寸，`resolveDropPlan` 返回不可用，paint 不显示 corner target，
  `canAcceptDrop` 也不能接受该 zone。

### empty leaf、collapse 与关闭规则

- source leaf 因 move 变空时：若不是 `bPersistentEmptyLeaf`，在 transaction 末尾向上
  collapse（以 sibling 替代 parent，直到 root 或遇到 persistent empty leaf）；root 可以
  作为空 leaf 保留。
- corner operation 创建的 persistent empty leaf 不自动 collapse；它是 layout 的可见
  目的地，而非错误残留。用户关闭该 leaf 的最后一个 panel（或 future reset-layout）时
  才允许明确删除该 placeholder 并 normalize。
- tab close 不是本计划默认功能；若已有 panel close command，它必须先问 registry 的
  `closable`，再走 `removePanel` transaction，不能直接 `WidgetTree::detach`。

## 分阶段实施（每阶段有代码切片、模型断言和 scenario）

### 阶段 0：先补 drag lifecycle、模型诊断与基线

这是实施 blocker，不能跳过。

- 在 `WidgetTree` 为 drag session 增加可选 observer：`onDragMove(payload, point, target)`、
  `onDragTargetChanged(...)`、`onDragFinished({Dropped, Cancelled, NoTarget}, point, target)`。
  保留现有 `beginDrag(source, payload, label)` overload；新 overload 只增加可选 callbacks，
  不把 Dock 类型泄露给通用事件系统。observer 在 `clearDragSession` 之后调用，入参只传
  point、result 和仍附着 target 的安全标识，不能传会悬挂的 raw target 给下一帧。
- `UIDockSpace` 用 observer 在**同一 target 内每次 mouse move**更新 preview。现有
  `setDropHighlight(bool)` 只表达 enter/leave，继续保留给普通 drag-drop 控件，不能拿它
  承担 zone 更新。
- 新建纯 model（`FDockTreeModel`、registry、id、invariant validator）和 dump schema，
  先不替换三栏 visual。dump 必须输出 node kind/id/parent、ratio、min extent、leaf tab ids、
  selected id、empty policy、floating list、drag preview 与 focus path。
- 给 P7 三栏补基线 scenario：tab select、tab drag enter/leave/cancel、content fill、空 leaf、
  window resize。

**完成标准**：未命中 drop target 的 release 能被 dock 收到；同一 leaf 内移动会改变
preview；取消没有 mutation；基线 dump 可解释每个已注册 panel 的唯一位置。

### 阶段 1：纯 model transaction + 单元/closure 验证

- 实现 `DockNode.h/.cpp`（模型不 include `UITabBar` / `UIContainer`）；创建默认三 leaf
  tree、panel registry、find leaf/node、merge、四 cardinal split、empty-source collapse。
- 实现 `resolveDropPlan`：给定 target leaf rect、pointer、model 和 min extent，返回
  `Invalid / Merge / Cardinal / Corner` 的 immutable plan（含预览 rect、被影响 node id、
  orientation、child order）。`computeZone8` 只是它的子步骤。
- 每个 mutation 用 `FDockTransaction` 包装：pre-validate → 修改 model → normalize →
  post-validate → 返回 change set；失败时 model 不变。首次实现只支持 merge/cardinal，
  corner 在下一阶段进入 transaction，避免一个巨型不可调试首提交。
- 给 `UISplitPane` 添加 ratio change callback 或同等 controller binding，使 divider 最终写回
  NodeId 对应 model；在这一阶段用 model-only 测试证明 resize/min extent clamp 正确。

**验证**：新增不依赖 SDL/RHI 的 dock model tests：重名 title、重复 move、source=target、
空 source collapse、ratio 的足够/不足空间、每个 transaction 后 invariants。

**完成标准**：model 测试能在 GUI closure target 中运行；任何非法 payload/id/target 都
返回失败而不会半改树。

### 阶段 2：model → WidgetTree projection，替换硬编码三栏

- 用 `materializeNode/rebuildProjectionSubtree` 替换 `_groups[3]`；初始 model 投影仍呈现
  原三栏，Workbench call site 可以临时保留 `addPanel(name, widget, zone)` adapter，
  adapter 内部只做 `registerPanel + initialLeafId`。新业务 API 不再接收 zone index。
- Leaf view 重建采用 registry 保活 → detach previous selected content → 更新 tabs →
  attach selected widget 的固定顺序。选 tab 是 model transaction（更新 selected id），而非
  tab callback 直接操作 content host。
- Split view 的 ratio 变化写回 model，下一帧 projection/re-layout 不回跳；改变结构时只
  重建 affected root，随后一次 invalidateLayout。
- 保持 `UISplitPane` / `UISplitLayout` 为递归投影的唯一 split geometry owner；
  `UIDockSpace::layoutAssigned` 只将自己 rect 下发给 projection root，禁止再自算一套
  split rect。

**验证**：原 `dock.jsonl` 加结构 checkpoint，断言 three leaves、tab order、selected id、
bar/content rect；窗口 resize 后 ratios、min extent、内容 clip 不变形。

**完成标准**：删除 `_groups[3]` 与 `rebuildZone/movePanel`；P7 三栏所有行为由 model
projection 实现且没有业务 widget 重建/丢状态。

### 阶段 3：5 向 docking（center + cardinal）与可视 preview

- `UIDockSpace` 实现 dock drag observer：onMove 用 `resolveDropPlan` 更新
  `FDockDragSession`，onTargetChanged 清理旧 preview，onFinished 在 Dropped 时提交。
- `paintSelf` 仅依据 immutable preview plan 画半透明 fill/outline/arrow；preview 不改
  model、不 attach widget、不触碰 GPU 资源。
- commit 的顺序固定为：decode payload → resolve source/target by id → re-resolve plan
  （防 resize/structure 变化）→ model transaction → projection change set → focus restore。
- drop 到 source leaf center 是 no-op，drop 到其他 leaf center 是 tab merge；cardinal 根据
  plan 裂变，不采用 screen x 的三栏阈值。

**验证**：每个 cardinal side + center 走 scenario；拖进 target 后移动到不同 side 的 preview
必须切换；Esc/cancel、释放在无 target、resize between enter/release 均不能改变 tree。

**完成标准**：每次成功 drop 后 panel id 全局唯一、source 不残留、selected panel 与可见
content 一致；scenario 的 snapshot JSON 能检验 preview 与最终 node tree。

### 阶段 4：4 个 corner compound docking

- 把 `splitLeafCorner` 接入同一个 `resolveDropPlan` / `FDockTransaction`，严格使用上文
  的「左右先、上下后」树形与 persistent empty leaf 规则；不要在 input handler 特判。
- corner preview 画 compound plan 的新 panel rect，以及 persistent empty leaf 的低强调
  placeholder；用户能够看懂这次操作会保留一个可投放区域。
- min extent 不够时 corner zone 进入 disabled 状态：不高亮、不接受 drop，并在 dump 里
  记录 rejected reason。

**验证**：四角 × {足够窗口、最小窗口}，断言三 leaf tree 的 exact parent/order、empty
policy 与 preview/drop 一致；随后将另一 tab 放入 placeholder，断言它不再为空且不会被
自动 collapse。

**完成标准**：8 个 zone 都有确定的 model outcome 或明确的 disabled reason，没有视觉与
最终布局不一致的 fallback。

### 阶段 5：单窗口浮动 host 和 re-dock

- 新建 `UIDockFloatingHost`（Popup layer 常驻普通 child，维护多个 floating windows
  的 z-order）与 `UIFloatingDockWindow`（标题栏、移动、active visual、一个 Leaf view）。
  不复用 `UIPopupOverlay`，不引入 modal shield/dismiss；浮窗 press 提升 z-order。
- dock drag `NoTarget` release 才启动 tear-off transaction：将 panel 从 source leaf model
  移到 floating record，再把它 mount 到 floating leaf view。拖拽期间 source 不变，因此
  cancel 永远不需回滚。初始 position 由 release logical point 计算并 clamp 在 window
  logical extent 内。
- 从 floating tab 再发起 dock payload；dock target 接受后，transaction 先移动 model
  panel，再销毁空 floating window/view。浮窗 title-bar drag 只移动 visual position，不
  改 dock tree。
- popup/modal/focus 规则：floating host 是非 modal popup child；modal overlay 必须比它
  具有更高 z-order 且阻止其 input。transfer 前记录 focused PanelId/descendant；transfer
  后若原 focus widget 仍 attached 且 focusable 则恢复，否则 focus target leaf tab bar。

**验证**：tear-off、window 边界 clamp、bring-to-front、cancel、re-dock、modal 打开时
不能激活浮窗、focused control transfer；dump 断言 floating record、popup order、focus path。

**完成标准**：单窗口内可存在多浮窗；任何 close/re-dock 都不会丢 registry panel/widget，
也不会留下 Popup layer orphan。

### 阶段 6：收尾、持久化边界与多窗口 port（不实现多 OS window）

- 删除 P7 三栏 payload（`dock-tab:<zone>:<label>`）和 raw zone API；统一为版本化、
  无 title 歧义的 `dock/v1/<space-id>/<source-leaf-id>/<panel-id>` 编解码 helper。字符串
  仅是 WidgetTree transport，schema 归 DockSpace。
- 为未来 persistence 定义 **纯 model DTO**（NodeId 不持久化；持久化 node shape、ratio、
  panel stable key、selected key、floating geometry），但本阶段不写磁盘、不给 DSL 绑定，
  以免工作区格式锁死在未验证模型上。
- 多窗口只定义 `IDockWorkspaceCoordinator` 的调用方向：它拥有 shared panel registry 和
  DockSpaceId → `UIDockSpace` lookup；一个 `UIDockSpace` 请求 transfer，coordinator
  验证 source/target 后分别调用两个 model transaction。`WidgetTree` 不认识
  DockSpaceId，也不负责跨树 owner。当前 coordinator 的实现仍只注册一个 window。
- 统一 dump、scenario、feature matrix，移除临时日志与旧 adapter；审计 public GUI headers
  不把 `FDockNodeView` / `UIElement*` 作为持久 dock API 暴露。

**完成标准**：核心 model 可脱离 Workbench 构建测试；未来 multi-window / workspace
persistence 只加 coordinator/serializer，不回改 node tree、transaction 和 event route。

## 关键文件清单

**新建**：
- `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/DockNode.h`
- `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/DockNode.cpp`
- `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/DockFloatingHost.h`
- `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/DockFloatingHost.cpp`
- `Test/Framework/GUI/DockNodeTests.cpp`（或现有 GUI closure test 的同等目标）

**修改**：
- `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/DockSpace.h`（model/controller、
  projection map、drag session；删除 `_groups[3]`）
- `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/DockSpace.cpp`（model transaction、
  projection、preview、drop、focus restore）
- `Engine/Source/Framework/GUI/Runtime/Widgets/WidgetTree.h/.cpp`（通用 drag observer/finish
  result；不包含 dock 类型）
- `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/SplitPane.h/.cpp`（ratio write-back
  callback 或严格等价的 model binding）
- `Engine/Source/Framework/GUI/Runtime/Widgets/include/GUI/Widgets/Controls.h`（导出新增控件）
- `Example/GUIWorkbench/Source/WorkbenchDemoPages.cpp`（注册 stable panel key；过渡 adapter）
- `Example/GUIWorkbench/Scenarios/dock.jsonl`（新场景断言）

## Dock drag 状态机和 route 契约

```text
Idle
  -- Tab threshold --> Dragging(source panel stays mounted)
Dragging
  -- move over eligible leaf --> Previewing(plan; model unchanged)
  -- move outside / disabled zone --> Dragging(no preview)
  -- release on eligible leaf --> CommitDockTransaction --> Idle
  -- release without target --> CommitTearOffTransaction --> Idle
  -- Esc / cancel --> Idle
```

- `TabBar` 只负责识别 press/threshold，然后要求 `UIDockSpace::beginPanelDrag(panelId, leafId)`；
  它不编码 title/zone，也不直接调用 `WidgetTree::beginDrag`。DockSpace 创建 v1 payload 并
  注册 observer。
- `WidgetTree` 继续做 generic hit-test、capture、ghost 与目标选择。DockSpace 不自行读取
  SDL 鼠标坐标、不另开 event loop；所有 point 来自 tree 保存的 logical pointer state。
- 接受 drop 时 DockSpace 必须重新按当前 point + 当前 layout rect 解析 plan，不能相信上次
  hover 的 cache；resize、divider drag、panel close 发生在 drag 期间时，旧 plan 自然失效。
- `canAcceptDrop` 只在 payload schema、DockSpaceId、target leaf 和至少一个可行 plan 均合法
  时返回 true；`onDrop` 是 commit endpoint，不能再按屏幕横向阈值猜 zone。
- drag source 被 detach、tree teardown、modal 打开、window lost focus 时 Tree 必须 cancel；
  DockSpace 的 finish observer 仅清 transient preview，绝不以 cancel 触发 tear-off。

## 自动化验证矩阵

| 维度 | model/closure | headless scenario | windowed / GPU |
| --- | --- | --- | --- |
| tree ownership / id uniqueness / collapse | 必须 | dump checkpoint | - |
| cardinal + corner plan geometry / min extent | 必须 | resize checkpoint | screenshot 可选 |
| tab selection / content mount / focus restore | - | 必须 | 必须 |
| drag move preview / cancel / no-target tear-off | observer unit + model | 必须 | 必须 |
| floating move / z-order / modal block | - | dump + route trace | 必须 |
| split divider ratio persistence | model clamp | scenario resize | 必须 |
| lifecycle / GPU teardown | - | - | GUI closure + normal quit |

Scenario checkpoints 不能只断言 widget name。每个 checkpoint 至少有：model digest、panel
location map、leaf rect、selected PanelId、preview plan/disabled reason、WidgetTree focus path
和 route trace。窗口模式再用 `--gpu-shot` / `--offscreen-shot` 做渲染 parity；headless
不替代真机 hover cursor、clip 和浮窗手感验证。

## 明确不做

- 不实现 OS native multi-window、跨 process drag、dock workspace 的文件持久化、layout
  migration 或 JSON/XML/JS DSL。
- 不让 `UISplitPane` 变成 dock 数据结构，也不向通用 `UIContainer` 塞 tab/dock 状态。
- 不重写 WidgetTree 的 Preview/Target/Bubble route；只在既有 drag session 上补 observer
  lifecycle。
- 不将 floating window 误建成 `UIPopupOverlay` 或 modal；modal、menu、tooltip 仍走各自
  现有语义。
- 不为了 corner preview 允许负 extent、隐藏 extra leaf 或未说明的 fallback。

## 风险护栏

1. **detach keepAlive**：移动 panel widget 时，拆边前抓 `shared_ptr` 保活（沿用 P7 修复），split/merge 转移子树时同样保活，避免 `use_count==1` 析构崩溃。
2. **模型先于投影**：任何 UI reparent 都必须来自已成功 model transaction；若 projection
   同步失败，debug assert 并保留 registry 强引用，不能留下 widget 双挂/失主。
3. **layout dirty 与 geometry**：每个 projection 结构变更后只显式 invalidate 一次；所有
   rect/min extent clamp ≥0，split ratio 由 `UISplitLayout` 的同一算法约束。
4. **consistency over performance**：初版允许整棵 dock projection re-layout；先把 mutation
   原子性、focus、lifetime 做对，再做细粒度 diff。
5. **headless + 真机双轨**：scenario/dump 证明 model、route、几何；窗口/GPU 模式确认
   hover cursor、clip、preview 色彩、浮窗 z-order、正常退出和 teardown。
6. **payload 兼容**：WidgetTree transport 仍为 string，但 schema 版本化且只含 stable ids；
   decode 失败必须是 no-op + diagnostic，不能 `atoi` 后猜测。
7. **多 window 扩展预留**：只有 coordinator 将来知道多个 `WidgetTree`；DockNode、
   transaction、layout 与 generic RHI/window API 完全无关。

## 相关计划/记忆

- P7 原始三栏实现已合入 main（commit 0b4fc7cc, 609a2692）。
- 引擎级 detach keepAlive 修复（commit e8bd74a4）是本项目的基础护栏。
- 反馈记忆：GUI 可用性验证不能只看交互路径（须 dump tree 查每栏有内容/高度正常）。
