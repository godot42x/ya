# DockSpace → ImGui 风格 DockNode 嵌套停靠 + 浮动窗口 + 8向 hover 预览

> 计划线：dockspace-node-tree
> 创建：2026-08-19
> 状态：规划中（待 ExitPlanMode 审批后进入实施）

## 目标

把 `UIDockSpace` 从「固定三栏 tab 交换」升级为 ImGui docking 范式（用户选 B：完整 ImGui 式，非最小集）：

1. **任意嵌套停靠树**：dock 区是一个 `FDockNode` 二叉树，每个内部节点是二分 split（水平/垂直 + ratio），每个叶子是一个 tab 组。拖 tab 到某叶子的上/下/左/右 → 把该叶子裂成 split；拖到中心 → tab 合并进该叶子。
2. **可浮动窗口**：拖出 tab 形成带标题栏的浮动面板（挂 Popup 层、自由定位、自保活），可再拖回停靠到任意 dock 节点。范围：**先单窗口内浮动层**（与 ImGui 一致），预留多 window 扩展点。
3. **8向 hover 预览**：拖拽时实时显示「将停靠成什么布局」的半透明预览（中心 + 上下左右 + 四角）。

## 决策（已与用户确认）

- **Split 可视化载体**：复用 `UISplitPane` 作 FDockNode 的 Split 可视载体（白送 divider 拖拽 + hover cursor）。行业惯例（Qt QSplitter 树 / Avalonia GridSplitter / ImGui 纯数据树+自绘分割线）一致指向「二叉递归分割 + 独立可视分割线」，方案 A 贴合本引擎现有抽象且符合行业形态。
- **浮动窗口范围**：先单窗口内浮动层（Popup 层），后续再扩展多 OS window。把「内容归属」抽象成接口层，未来多 window 只是 reparent 到另一个 WidgetTree，不动 dock 逻辑。
- **payload**：保持 `std::string` 类型（不改 WidgetTree 公共接口），加编解码 helper。

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
struct FDockPanel { std::string name; std::shared_ptr<UIElement> widget; };

struct FDockNode {
    enum class EKind { Split, Leaf };
    EKind kind;
    // Split:
    bool         horizontal = true;   // split orientation
    float        ratio = 0.5f;        // first child extent ratio
    FDockNode*   parent = nullptr;
    FDockNode*   child[2] = {nullptr,nullptr};
    // Leaf:
    std::vector<FDockPanel> panels;   // tab group
    UITabBar*    bar = nullptr;
    UIContainer* content = nullptr;
    int          selected = 0;
    // layout rect (filled during layout pass):
    Rect2D       rect{};
};
```

`UIDockSpace` 持有 `FDockNode* _root`（owner，手动析构整树）。初始树：root=Split(horizontal) → [Leaf(left), Split(horizontal)→[Leaf(center), Leaf(right)]]，对应旧三栏。

## 分阶段实施（每阶段可独立 headless 验证）

### 阶段 1：FDockNode 树 + 递归布局（替换硬编码三栏）

- 新建 `DockNode.h`，`UIDockSpace` 用 `_root` 树替代 `_groups[3]`。
- `layoutAssigned` 改为 `layoutNode(_root, rect)` 递归：Split 按 orientation+ratio 切分 rect 给两子（复用 UISplitPane 的 ratio/extent 算法，或直接在 DockSpace 内实现切分）；Leaf 内部 bar(top, auto) + content(below, stretchLastChild) 垂直排。
- `buildTree()` 懒构建（`getChildren().empty() && getTree()`）：创建 root 树，为每个 Leaf 创建 bar+content 并 attach 到 DockSpace 的子容器；为每个 Split 创建 UISplitPane 载体。
- `addPanel(name, widget, zone)` 映射到初始三 leaf（zone 0/1/2）。
- `rebuildLeaf(leaf)` 泛化现有 rebuildZone（detach 旧 content→清空 bar→重加 tab→syncSelectedTab→attach 选中 widget→markLayoutDirty）。
- **验证**：headless dock.jsonl 断言三 leaf 各有 tab、bar 高度正常、panel 填满（复用现有 dump 断言）。

### 阶段 2：8向 hover 分区 + 预览绘制

- 新建纯函数 `EDockZone8 computeZone8(const Rect2D& rect, const glm::vec2& p)`（中心占内 60% 矩形，四边各占薄带，四角占角块）。
- `UIDockSpace` 新增 `_previewRect` / `_previewZone8` 成员，在 `updateDrag` 期间计算命中 leaf + zone8。
- `paintSelf` 据 `_previewZone8` 画半透明 `addSprite`（目标 leaf rect 的对应子区域）+ `addRectOutline` + `addLine` 方向箭头。
- **验证**：dock.jsonl 加 drag 步骤，dump 后检查 `_previewZone8` 命中（临时日志或断言 preview 状态）。

### 阶段 3：onDrop 执行 split/merge

- `onDrop(payload, point)`：解析 payload（panelId + sourceNodeId）→ findDropNode(point) 命中 leaf → 据 zone8 执行：
  - 中心：tab 合并（movePanel 到该 leaf）
  - 上下左右：把该 leaf 裂成 Split（原 leaf 为 first child，新 leaf 为 second），新 leaf 承载拖入的 panel
  - 四角：嵌套 split（先水平后垂直，或反之）
- 重建受影响节点的 bar/content，`markLayoutDirty()`。
- **验证**：dock.jsonl 拖 Scene center→left 上 edge → 断言左 leaf 变 Split、含 [Inspector, Scene]。

### 阶段 4：浮动窗口 UIFloatingPanel

- 新建 `FloatingPanel.h/.cpp`：
  - 标题栏（可拖拽移动 setPosition 逻辑像素）+ content 区
  - `_selfHold = shared_from_this()` 自保活（仿 PopupOverlay.cpp:40）
  - 挂 Popup 层（`attachToLayer(ELayer::Popup, panel)`）
  - 多实例（弃用 PopupOverlay 的单例 g_retiredOverlays.clear() 约束）
- `UIDockSpace::_onTabDragBegin` 改：拖过阈值 → 生成 FloatingPanel 挂 Popup 层 + 原 tab 从 leaf detach（keepAlive widget shared_ptr）。
- 浮动面板标题栏拖拽移动；拖回时 findDropTarget 命中 dock leaf → reparent content 回 Content 层 + 合并进 leaf。
- **验证**：dock.jsonl 加「拖出成浮动 + 拖回停靠」步骤，dump 断言浮动面板存在/消失 + leaf 面板数变化。

### 阶段 5：清理 + scenario 断言更新

- 更新 dock.jsonl 覆盖新 8向 + 浮动场景。
- feature_matrix.json 标记 p7 升级完成。

## 关键文件清单

**新建**：
- `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/DockNode.h`
- `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/FloatingPanel.h`
- `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/FloatingPanel.cpp`

**修改**：
- `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/DockSpace.h`（_root 树替代 _groups[3]；_previewZone8 成员；API 调整）
- `Engine/Source/Framework/GUI/Runtime/Widgets/Controls/DockSpace.cpp`（递归 layoutNode；buildTree；rebuildLeaf；computeZone8；onDrop split/merge；paintSelf 预览）
- `Example/GUIWorkbench/Source/WorkbenchDemoPages.cpp`（addPanel 调用兼容）
- `Example/GUIWorkbench/Scenarios/dock.jsonl`（新场景断言）

## 风险护栏

1. **detach keepAlive**：移动 panel widget 时，拆边前抓 `shared_ptr` 保活（沿用 P7 修复），split/merge 转移子树时同样保活，避免 `use_count==1` 析构崩溃。
2. **markLayoutDirty 补偿**：每次树结构变更后显式 `markLayoutDirty()`（addDetachedChild 在已入树节点不标脏）。
3. **consistency over performance**：整树重排可接受，优先正确性。
4. **headless 验证**：每阶段用 dock.jsonl + `--scenario-dump-dir` 的 dump 断言每栏有内容、bar 高度正常、panel 填满、拖拽后分布正确——不依赖真机视觉（真机手感请用户确认）。
5. **payload 兼容**：保持 string 类型，加 `encodeDockPayload`/`decodeDockPayload` helper，不改 WidgetTree 公共接口。
6. **多 window 扩展预留**：浮动面板内容归属抽象成 `attachPanel`/`detachPanel` 接口，未来多 window 仅 reparent 目标切换。

## 相关计划/记忆

- P7 原始三栏实现已合入 main（commit 0b4fc7cc, 609a2692）。
- 引擎级 detach keepAlive 修复（commit e8bd74a4）是本项目的基础护栏。
- 反馈记忆：GUI 可用性验证不能只看交互路径（须 dump tree 查每栏有内容/高度正常）。
