# GUI 能力补齐：GameEditor ImGui 替换前置（第二阶段）

> 建立日期：2026-08-18
> 输入工件：`audit.md`（含 2026-08-18 全量 ImGui 依赖调研）
> 状态：调研完成、计划定稿（经 Plan agent 设计，用户拍板范围）
> 前一阶段（数据绑定主线：Reactive + 增量复用 + P0 三件套）已收口，见 feature_matrix.json 旧 track。

## 0. 结论摘要

自研 GUI 框架在 GUI App 线（GUIWorkbench）**先全量补齐 GameEditor 所需 feature**，每个 feature 在 Gallery 页 demo + scenario 断言验证；**最后**才考虑替换 GameEditor 的 ImGui。

调研结论（audit.md §2）：GameEditor 用 32 文件 / 140 API / 1062 次 ImGui 调用。控件大头已有对应，缺口集中在 **DockSpace / 矢量绘制 / Table 布局 / 拖拽重排 / TreeView 编辑 / 输入控件** 六块骨架 + P1 交互补全。

范围（用户 2026-08-18 拍板）：上述全部 + DockSpace。**排除**：ImGuizmo（3D 视口，非 GUI App 线）、字体 CJK/emoji 回退链（渲染资源线）、IME 合成（宿主线）、剪贴板。

## 1. 目标与非目标

### 1.1 目标

1. 补齐 7 期 feature（见 §3），每期在 `Example/GUIWorkbench` Gallery 页（或独立 Dock 页）有可交互 demo。
2. 每个 feature 在 `Example/GUIWorkbench/Scenarios/` 写 jsonl scenario 断言（rect/control/layers 递归子集断言）。
3. 严守架构契约：retain-mode；瞬态状态 VisualFlag；持久字段 changed-only setter；MVC 受控控件不强塞 Reactive 绑定。
4. 全部完成后，自有 GUI 具备替换 GameEditor ImGui 的**能力条件**（替换本身是后续独立决策）。

### 1.2 非目标

- 不替换 GameEditor 的 ImGui（本阶段不做任何 GameEditor 改动）。
- 不做 ImGuizmo 等价物、字体回退链、IME、剪贴板。
- 不做虚拟化列表、保留层缓存、immediate API 便捷层。
- DockSpace 只做最小可用子集（见 §3 P7），不复刻 ImGui 完整 docking。

## 2. 关键设计决策（已定）

| # | 决策 | 内容 |
|---|------|------|
| 1 | 矢量原语渲染链 | 主选「新 draw item kind `Line` + `FLineRender` 增 screen 路径（screen 顶点 + scissor 走 session.clipStack）」。**兜底**：builder 内细四边形细分（零渲染层改动，牺牲粗线/AA）——若 screen pipeline 卡住即切兜底，bezier 砍成分段直线 |
| 2 | DockSpace 形态 | 专用 `UIDockSpace` + `UIDockLayout`（内部节点树：叶子=tab 组，中间=分割），复用 UISplitPane/UITabBar/drag 会话/矢量高亮。最小子集=中央区+左右边缘停靠+tab 合并+分割条拖拽；浮动窗/持久化排除 |
| 3 | DropTarget API | 复用 UIElement 已有 canAcceptDrop/onDrop/setDropHighlight 钩子，`UIDropTarget` = 谓词+回调+高亮 VisualFlag；`UIDragSource` 封装 beginDrag 触发。不新加路由策略 |
| 4 | 模态对话框 | 扩展复用 `UIPopupOverlay`（Modal 角色已覆盖遮罩/焦点/Esc/自 detach），新增 `UIDialog` 薄壳（标题栏+内容槽+OK/Cancel+`_onClosed`）。不新建层 |
| 5 | TreeView 扩展 | 三能力叠加式（重排/右键/过滤），重排走 host 回调 `_onReorder(from,to,mode)` 重建 ReactiveList（控件不拥有数据变异）；右键复用 UIMenu |
| 6 | Table API | `UITableLayout`（布局）+ `UITableGrid`（数据驱动控件，仿 TreeView 扁平 paint 不虚拟化） |
| 7 | tooltip | 复用既有 Tooltip 层 + `UIElement::setTooltip` 存储 + hover 超时挂载 |
| 8 | TextWrapped | 控件层分行（UIText 持 `_bWrap/_maxWidth`，分词断行 + 逐行 addText；computeDesiredSize 返回包裹高度）。builder addText 保持单行 |

## 3. 分期（7 期，基础设施先行 → DockSpace 收尾）

### P1 矢量绘制原语（基石）
- `UIFrameDrawItem::EKind` 增 `Line`；`UIFrameBuilder` 增 `addLine/addRectOutline/addBezierCubic`（bezier 客户端细分折线）。
- 消费链：`Render2DComposePass.cpp` 增 Line 分支 → `FLineRender::addScreenLine`（screen pipeline + clipStack scissor）。
- Gallery section「4. Vector primitives」静态线/框/贝塞尔。
- scenario：`gallery_vector.jsonl`（断言需扩展 WidgetTreeDump 或借 capture 像素——实施时定）。

### P2 Table/Grid 布局
- `UITableLayout` + `UITableSlot`（row/col/span/alignment），仿 UILayout/UIBoxSlot；`UITableGrid` 数据驱动控件（bindData(ReactiveList<FTableRow>) + bindSelection）。
- Gallery section「5. Table」4 列数据表 + 选中高亮。
- scenario：`gallery_table.jsonl`（WidgetTreeDump 增 table layout/control 块）。

### P3 输入控件补全
- 新建 `UIDragFloat`/`UISpinBox`/`UIRadioButton`（组）/`UIColorEdit`/`UISearchComboBox`，仿 CheckBox/Slider 骨架。
- Gallery section「6. Input controls」。
- scenario：`gallery_inputs.jsonl`（control.type + value/checked 断言，WidgetTreeDump 增块）。

### P4 拖拽重排 + DropTarget/DragSource
- `UIDropTarget`（`_accept` 谓词 + `_onDrop` + VisualFlag `_bDropHighlight`，paint 用 P1 高亮边框）；`UIDragSource` 辅助封装。
- Gallery section「7. Drag reorder」可拖拽重排列表。
- scenario：`gallery_drop.jsonl`（dragSession 路由 + 高亮/顺序断言）。

### G-A 框架护栏一（P5 之前插入）：paint self-clip + StyleSet set 语义

> 2026-08-18 用户定调：每开发一个 feature 就出现体验 bug，护栏补强须插入后续计划（P5-P7）之前。

- **G1 `UIElement::paint` 默认 self-clip**：paint 模板自动 `pushClip(_layoutRect)`（opt-out `_bSelfClip=false`），「widget 永不画出自已 rect」从控件自觉变为框架保证——消灭溢出绘制整类 bug（TreeView/Modal field/Gallery 盖 status bar 同根因）。
- **G4 `UIStyleSet::define` 同名 set 语义**：同名再 define 复用已有 handle 并 `set()` 新值（绑定者自动收到 notify），不再替换 handle 孤立绑定。
- 验收：移除 TreeView 手写 clip 后全部 scenario 回归通过；define 同名后已绑定控件重绘（scenario 断言）。

### G-B 框架护栏二：Event 时间戳 + debug 校验帧

- **G3 Event 加 timestamp**：Event 基类增时间戳字段，SDL 桥接填充；场景驱动以步进帧近似；双击检测改时间戳（去掉位置近似）。
- **G2 debug 校验帧**：debug 构建下每 N 帧（默认 60）强制全量重画并与增量缓存结果 diff，不一致时告警指明 widget——漏标脏在开发期被当场抓住，而不是上线后靠肉眼。
- 验收：双击判定走时间戳且 scenario 通过；人为制造一处漏标脏（临时 patch）触发校验帧告警，修复后告警消失。

### P5 TreeView 编辑能力
- 叠加式：`_bReorderable`（行 press 起 drag；canAcceptDrop 判定行间/行内；`_onReorder` 回调）；`_onContextMenu(nodeId)`（右击 UIMenu）；`bindFilter(Reactive<string>)`（flattenVisible 剪枝）。
- Gallery section「8. TreeView editing」。
- scenario：`gallery_tree_edit.jsonl`。

### P6 P1 交互补全
- tooltip（决策 7）；TextWrapped（决策 8）；`UIElement::_bEnabled` 子树禁用（setEnabled changed-only + paint 灰度 + 输入入口拦截）；`UIDialog` 模态对话框（决策 4）。
- Gallery section「9. Tooltip/Wrap/Disabled/Dialog」。
- scenario：`gallery_p1.jsonl`。

### P7 DockSpace + 窗口管理（收尾，聚合前序）
- `UIDockSpace`/`UIDockLayout`（决策 2）+ 独立 `Dock` 页（全视口）。
- Gallery 加指针段指向 Dock 页。
- scenario：`dock.jsonl`（初始中央区 + 拖拽后 tab 合并/分割结构断言）。

## 4. 验收标准

1. 7 期全部落地：Gallery（或 Dock 页）demo 可交互。
2. 全部新增 scenario 通过：`xmake run GUIWorkbench --start-page <X> --scenario <abs>`。
3. 既有 8 页（Render/Widgets/Layout/Menus/DragDrop/Modal/ScrollSplit/Editor）无回归；Gallery 原 3 section 不破坏。
4. GUIWorkbench smoke（runDemoAutomation）不破坏——**新增 Dock 页会移位 Editor tab（case 20 点 tabs[7]），每加页同步更新 tab 索引与 FDemoState**。
5. 架构契约：VisualFlag/标脏纪律（漏标脏已复发 4 次，新控件零容忍）、changed-only setter、MVC/MVVM 分治。

## 5. 风险与停止线

| 风险 | 停止线/兜底 |
|------|------------|
| P1 渲染层改动（FLineRender screen pipeline）卡住 | 切细四边形兜底；bezier 砍分段直线 |
| P7 DockSpace 复杂度失控 | 砍到「中央区+左右停靠+tab 合并」，浮动窗/多级嵌套/持久化不做 |
| smoke 自动化 tab 索引硬编码 | 每加页立即更新 runDemoAutomation case 与索引 |
| 漏标脏复发 | 新控件瞬态一律 VisualFlag；code review 检查点 |
| Gallery 页过长 | section 化；Dock 已独立成页 |

## 6. 执行顺序

P1 → P2 → P3 → P4 → **G-A → G-B** → P5 → P6 → P7。每期 = 1 个自洽 commit（代码 + 工件更新 + scenario 同 commit）。

## 7. 修订记录

- **2026-08-18 首版**：ImGui 调研 → 范围拍板（上述全部 + DockSpace）→ Plan agent 设计 → 定稿。
