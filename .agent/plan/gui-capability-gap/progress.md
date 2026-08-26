# GUI 能力补齐（第二阶段）Progress

> 主线：`gui-capability-gap` 第二阶段——GameEditor ImGui 替换前置（GUI App 线全量补齐）。
> 记录：每轮完成内容、验证结果、剩余问题。

## 2026-08-25 — 移除 content 兼容入口，建立 UIComponent 类型边界（本轮）

**完成**：
- 仓库内已无 `.content(factory)` 调用，移除兼容别名，避免静态 DSL 与函数式扩展重复命名。
- 新增 `UIComponent = std::function<UIDescription()>`，并以 `UIComponentFactory` 作为 compose/when 的概念边界。
- 保留 `UIDescriptionFactory` 兼容概念别名，暂不破坏外部模板代码。
- 增加 UIComponent alias contract，确认 component 仍然只生成静态 description，不进入 runtime state。

**验证**：DeclarativeContractTest 通过（30/30）。

**下一刀**：把 component 从“无参 factory”扩展为带明确 compose context/props 的上层 API，但保持静态 builder 与 `UIDescription` 不变。

## 2026-08-25 — 静态 DSL 与函数式 compose 分层（本轮）

**完成**：
- `child(UIDescription)` / `children(UIDescription...)` 明确为底层静态 DSL，不接收 factory。
- 新增 `compose(factory)` / `composeChildren(factories...)` 作为函数式组合扩展；factory 立即生成稳定的 `UIDescription`，不进入 description/runtime state。
- `content(factory)` 暂时保留为兼容别名，后续新代码统一使用 `compose` 命名。
- 旧 contract 调用已迁移到分层 API，静态节点与函数式组合语义不再混用。

**验证**：DeclarativeContractTest 通过（29/29）。

**下一刀**：继续清理旧 `content` 调用，并单独设计 component/props/state/lifecycle 扩展，不把这些概念塞回静态 builder。

## 2026-08-25 — typed description common children 收口（本轮）

**完成**：
- `UIDescription::build()` / conversion 现在会先同步 common block，避免 common/legacy 不一致。
- `TUIChildrenBuilder` 的 `content()/children()/when()` 统一同步 `common.children`，公共 children 真正成为 reconciler 可读来源。
- `UIReconciler` 现在优先读 `common.children`，并保留 legacy children 作为兼容 fallback。
- 新增 contract：`content/children` 会同步 common children；仅填 `common.children` 的描述也能正常 reconcile。

**验证**：DeclarativeContractTest 通过（29/29）。

**下一刀**：继续删冗余 flat fields 的写入口，优先把 shared property 写入收成更少的 helper。

## 2026-08-25 — typed description helper 化（本轮）

**完成**：
- `UIDescription` 的公共块继续收束：`UICommonDescription` 负责 identity / layout / children；builder 侧 shared fields 通过 helper 统一同步。
- 现有 typed payload（Panel / Text / Button / TextField）继续保留，adapter 仍优先读取 common/payload，旧 flat fields fallback 不变。
- 这一步主要是为后续删 flat fields 降低机械重复。

**验证**：相关 typed payload contract 通过（4/4）。

**下一刀**：继续收缩 flat fields，优先把公共字段的写入/读取完全收进 helper/payload。

## 2026-08-25 — P8 typed description 迁移第四刀：Button/TextField payload（本轮）

**完成**：
- 新增 `UIButtonDescription` 与 `UITextFieldDescription` typed payload。
- `UIButtonBuilder` / `UITextFieldBuilder` 同步写入 typed payload；adapter 优先读取 payload。
- 保留旧平面字段作为 fallback，现有按钮 label / textfield focus 语义不变。
- 增加 payload contract，覆盖 button text/onClick 与 textfield text/fontSize。

**验证**：DeclarativeContractTest 通过（27/27）。

**下一刀**：继续收缩 flat fields，优先把公共字段迁移向只读兼容层，并考虑移除冗余旧字段。

## 2026-08-25 — P8 typed description 迁移第三刀：公共 description 抽取（本轮）

**完成**：
- 新增 `UICommonDescription`，把 identity / children / layout / interaction 统一收束到公共块。
- `UIDescription` 保留兼容 flat fields，但 builder 已开始同步写入 `common`。
- adapter 优先读取 `common`，旧字段作为 fallback，避免一次性打断现有 DSL。
- 增加公共 description contract，验证 shared fields 能正确保存。

**验证**：DeclarativeContractTest 通过（26/26）。

**下一刀**：迁移 `UIButtonDescription` / `UITextFieldDescription` 的 typed payload，并继续收缩 flat fields。

## 2026-08-25 — P8 typed description 迁移第二刀：UITextDescription（本轮）

**完成**：
- 新增 `UITextDescription` typed payload，覆盖 text / fontSize / color 的 authored optional 语义。
- `UITextBuilder` 保持现有 API，setter 同步写入 typed payload。
- adapter 优先读取 typed text payload，旧平面字段继续作为兼容 fallback。
- 增加 typed payload contract，验证 text 的三类属性完整保存。

**验证**：DeclarativeContractTest 通过（25/25）。

**下一刀**：提取公共 description（identity/layout/interaction/children），再迁移 Button/TextField payload，逐步删除旧 flat fields。

## 2026-08-25 — P8 typed description 迁移第一刀：UIPanelDescription（本轮）

**完成**：
- 新增 `UIPanelDescription` typed payload，panel builder 的 `setColor` 同步写入 payload。
- adapter 优先读取 typed payload，旧平面字段暂时保留，确保现有 DSL 调用和兼容描述不受影响。
- 增加 payload contract，验证 typed panel description 的 authored color 数据。

**验证**：DeclarativeContractTest 通过（24/24）。

**下一刀**：提取公共 description，并迁移 `UITextDescription`，继续保持旧 builder API 与 retained reconcile 行为兼容。

## 2026-08-25 — P8 架构决策：typed description 取代 god UIDescription（本轮）

**决策**：
- 参考 Slate 的控件专属 arguments、React 的 type/key/children、Flutter 的 immutable configuration/runtime state 分离、QML 的 per-type properties。
- `UIDescription` 不再继续扩展为包含所有控件字段的 god struct。
- 目标是公共 description + 控件专属 typed payload；实现优先考虑 variant + 间接递归层，保留值语义和清晰 ownership。

**后续约束**：
- `UIReconciler` 只管 identity/lifecycle/children order。
- `UIDeclarativeNodeAdapter` 按 typed payload 分发。
- 每种控件继续使用自己的 builder。
- authored/default 使用 optional/明确标志；runtime focus/capture/edit/callback 状态不进入 description。

**下一刀**：提取公共 description，并迁移 `UITextDescription`，继续保持旧 builder API 做兼容迁移。

## 2026-08-25 — P8 DSL 第七刀：callback 与 authored 语义（本轮）

**完成**：
- `UIDescription` 增加 `_bHasOnClick` authored 标志；`UIButtonBuilder::onClick` 显式设置该标志。
- adapter 仅在 callback 被声明式 authored 时覆盖 `_onClick`，避免普通 reconcile 把应用层/运行态 callback 清空或替换。
- contract 覆盖：未 authored 时保留 runtime callback；再次 authored 时才切换到声明式 callback。

**验证**：DeclarativeContractTest 通过（23/23）。

**下一刀**：继续审计其它 authored-vs-default 字段，并考虑把属性应用进一步按 widget kind 拆成可扩展 handlers。

## 2026-08-25 — P8 DSL 第六刀：Button/TextField typed property contracts（本轮）

**完成**：
- `UIButton.setText` contract：验证生成的 `action__label` 子节点在文本更新时保持实例复用，只更新 label 内容。
- `UITextField.setText/setFontSize` contract：验证字段属性更新保持 retained identity，并保留当前 focus。
- 与既有 adapter 边界一致，未向 `Declarative.cpp` 增加控件属性分支。

**验证**：DeclarativeContractTest 通过（22/22）。

**下一刀**：继续审计 builder/adapter 的属性表达方式，优先补齐 callback 与 authored-vs-default 属性语义，避免 DSL 更新时意外覆盖控件运行态。

## 2026-08-25 — Declarative 重构前置：reconciler 轻壳化（本轮）

**完成**：
- 新增 `UIDeclarativeNodeAdapter`，集中承载节点创建 / 属性应用 / kind 匹配。
- `UIReconciler` 仅保留验证、identity、children reconcile 与 tree lifecycle。
- 这次拆分不改行为，contract tests 仍全过。

**验证**：`ya-gui-widgets-test` 构建通过；`DeclarativeContractTest` 通过（19/19）。

**下一刀**：在 adapter 边界继续补容器属性矩阵，避免 `Declarative.cpp` 再增长成事实上的 god class。

## 2026-08-25 — P8 DSL 第四刀：外观属性矩阵 contract（本轮）

**完成**：
- 增加 `panel.setColor` 与 `text.setFontSize` 的 retained reconcile contract tests。
- 属性更新均验证 stable key 下复用原 widget 实例，不把声明式属性更新误变成 subtree replacement。
- 将 P8 typed property surface 从 enabled/focusPolicy 扩展到外观属性，形成后续属性矩阵测试基线。

**验证**：DeclarativeContractTest 通过（19/19）。

**下一刀**：补齐容器属性（spacing/padding/clipChildren/stretchLastChild）的 reconcile contract，并验证布局属性更新会触发正确的 layout invalidation。

## 2026-08-25 — P8 DSL 第三刀：typed focusPolicy surface（本轮）

**完成**：
- 为 typed builder 补 `setFocusPolicy(EWidgetFocusPolicy)`，继续保持每种控件自己的 builder，不引入通用 fat UIBuilder。
- reconciler 将 focusPolicy 直接回写 retained widget，属性更新不触发 widget 替换。
- contract test 覆盖 focusPolicy 从 Focusable → None 的稳定 key / 实例复用。

**验证**：DeclarativeContractTest 通过（17/17）。

**下一刀**：继续补现有框架已支持的 typed 属性 surface（优先容器/文本类的剩余公开字段），并把 declarative contract test 扩展为属性矩阵。

## 2026-08-25 — P8 DSL 第二刀：children/when 与 factory 约束（本轮）

**完成**：
- typed builder 增加 children(factory...) 多子节点组合。
- 增加 when(condition, factory) 条件节点组合，条件切换由既有 reconciler 清理 stale subtree。
- 增加 UIDescriptionFactory C++20 concept，编译期限制 factory 必须返回 UIDescription 或专属 builder。
- contract tests 覆盖条件节点移除、稳定 key sibling 复用，以及合法/非法 factory 的 static_assert。

**验证**：DeclarativeContractTest 通过（15/15）。

**下一刀**：将条件/列表组合抽成无副作用的描述辅助函数，并开始为 UI 控件补齐 typed property surface。

## 2026-08-25 — P8 DSL 第一刀：typed builder content factory（本轮）

**完成**：
- 在保留控件专属 builder 的前提下，为所有 child-capable builder 增加 content(factory)。
- factory 可返回 UIDescription 或可隐式转换为 UIDescription 的专属 builder，支持 React/EUI-NEO 风格的函数组合。
- 不改 WidgetTree、snapshot、reconciler 生命周期和 identity 规则。
- 新增 BuilderContentFactoryComposesTypedSubtree contract test，覆盖 column → row → button 与 sibling text 组合。

**验证**：DeclarativeContractTest 通过（14/14）。

**下一刀**：多子节点/条件节点组合语义 + builder API 编译期约束测试。

## 2026-08-25 — Dock preview 回归收口（本轮）

**完成**：
- 全量 widgets 回归发现 WidgetTreeTest.DragOverDockSetsPointSensitiveDropPreview 的旧测试坐标落在 leaf 外部，不再命中当前 chooser block。
- 将测试点修正到实际 WEST chooser 区，保留对 merge → split → merge → leave 的完整生命周期断言。

**验证**：xmake r ya-gui-widgets-test 通过（156/156）。

**剩余问题**：无。

## 2026-08-25 — 框架收口回归修正（本轮）

**完成**：
- DockNodeTest 全面切到现有 getRootNode() API，清掉旧 getRoot() 残留。
- FontManager::getFont() 在 bitmap 路径下优先复用已注册 base 的 scaled view，保住合成字体 / 预注册字体闭包测试。
- UIFrameSnapshotTest 断言同步到当前框架契约：self-clip、scroll viewport、split pane clip 以及 resize invalidation。

**验证**：xmake r ya-gui-widgets-test --gtest_filter="DockNodeTest.*:UIFrameSnapshotTest.*" 通过（59/59）。

**剩余问题**：无。

## 2026-08-18 — 调研 + 计划定稿（本轮）

**完成**：
- 全量扫描 GameEditor ImGui 依赖（32 文件 / 140 API / 1062 次调用），产出六层职责认知 + 缺口对照表（audit.md §5）。
- 用户拍板范围：控件+布局+绘制+拖拽+TreeView 编辑+DockSpace；排除 ImGuizmo/字体/IME/剪贴板。
- 验证方式拍板：Gallery demo + scenario 断言双保险。
- Plan agent 设计 7 期分期，plan.md 定稿。

**验证**：无代码改动，无回归。

**剩余问题**：
- P1 的 scenario 断言方式待定（draw item 级不在 WidgetTreeDump 内，需扩展 dump 或借 capture 像素）。

**下一刀**：P1 矢量绘制原语。

## 2026-08-18 — P1 矢量绘制原语（收口）

**完成**：
- `UIFrameDrawItem::EKind` 增 `Line`（lineFrom/lineTo/lineThickness 字段，复用 bClipped/clip 机制）。
- `UIFrameBuilder` 增 `addLine` / `addRectOutline`（4 线）/ `addBezierCubic`（客户端细分折线，1-64 段）。
- 消费链：`Render2DComposePass` Line 分支 = 旋转细 quad（`Render2D::makeSprite` transform 版，单位 quad 列向量映射线段方向/法线/起点；退化段画轴对齐点）。**零管线改动**——比原计划「FLineRender screen 路径」小 3 倍改动面，方案对调（主选=细 quad）。
- Gallery section「4. Vector primitives」：`FVectorDemoCanvas` 自定义控件展示水平/垂直/斜线 + 矩形框 + 贝塞尔。
- `gallery_vector.jsonl` scenario：锁 canvas 存在 + rect {430,110}（crossAlignment Start 防 Stretch）。

**验证**：GUIWorkbench 编译通过；gallery_vector scenario 通过（vector_canvas checkpoint）；modal_interaction 回归通过（4 checkpoint 全过）。

**剩余问题**：
- 无。P1 收口。

**下一刀**：P2 Table/Grid 布局。

## 2026-08-18 — P2 Table/Grid（收口）

**完成**：
- `UITableLayout` + `UITableSlot`（UILayout.h/.cpp）：格子布局，列宽 0=stretch 均分剩余、固定宽优先，行高统一，padding/clipsChildren；arrange 解析列 rect 后逐 child layoutAssigned。
- `UITableGrid` 控件（Controls/TableGrid.h/.cpp + include 镜像头）：数据驱动表格（bindData ReactiveList<FTableRow> + bindSelection Reactive<int>），paint 扁平画行（header 行/选中/hover 高亮 + 列/行分隔线走 P1 addLine + 自 clip），input hover/点击选中，autoSize 高度=行数×行高。
- `WidgetTreeDump` 加 tableGrid control 块（scenario 断言用）。
- Gallery section「5. Table」：4 列表格（固定宽 + stretch 列混合）+ reactive 选中。
- `gallery_table.jsonl` scenario：锁 rect.w=400 + control.type=tableGrid。

**验证**：GUIWorkbench 编译通过；gallery_table 通过；gallery_vector 回归通过。

**剩余问题**：无。P2 收口。

**下一刀**：P3 输入控件补全（UIDragFloat/UISpinBox/UIRadioButton/UIColorEdit/UISearchComboBox）。

## 2026-08-18 — P3 输入控件（收口）

**完成**：
- `Controls/InputExtras.h/.cpp`（+ include 镜像头）五个精简控件：UIDragFloat（press capture + 水平拖动调值 + 键盘步进）、UISpinBox（-/+ 双区点击步进 + hover）、UIRadioButton（点+label，组互斥由 host `_onSelect` 管理）、UIColorEdit（色块+RGBA 通道条，点击色块循环通道、拖动调值）、UISearchComboBox（焦点 KeyTyped 过滤 + UIMenu 弹出过滤项）。
- `WidgetTreeDump` 加五个 control 块（scenario 断言）。
- Gallery section「6. Input controls」全摆五控件（radio 组回调值捕获 shared_ptr 防悬垂）。
- `gallery_inputs.jsonl` scenario 锁五控件类型+初值。

**验证**：GUIWorkbench 编译通过；gallery_inputs 通过；vector/table 回归通过。

**剩余问题**：无。P3 收口。

**下一刀**：P4 拖拽重排（UIDropTarget/UIDragSource）。

## 2026-08-18 — P4 拖拽重排（收口）

**完成**：
- `Controls/DragDrop.h/.cpp`（+ 镜像头）：UIDragSource（press+6px 阈值起 WidgetTree beginDrag，payload/label 回调）+ UIDropTarget（_accept 谓词 + _onDrop + VisualFlag _bHighlighted + paint 用 P1 addRectOutline 画接受高亮框）。
- `WidgetTreeDump` 加 dragSource/dropTarget control 块。
- Gallery section「7. Drag & drop」：3 源（不同 payload）+ 2 目标（一个全接受、一个只收 payload.2）+ reactive drop 结果标签。
- `gallery_drop.jsonl` scenario 锁控件存在/类型（拖拽交互本身由 DragDrop 页既有 scenario 覆盖）。

**验证**：GUIWorkbench 编译通过；gallery_drop 通过；vector/table/inputs + dragdrop_interaction 全回归通过。

**剩余问题**：无。P4 收口。

## 2026-08-18 — G-A 框架护栏一（收口）

**完成**：
- G1：`UIElement::paint` 模板默认 `pushClip(_layoutRect)`（新 `_bSelfClip=true` opt-out），「widget 不画出自己 rect」成为框架保证；移除 TreeView/TableGrid/TextField 的手写 clip 验证护栏生效。
- G4：`UIStyleSet::define` 同名复用 handle + `set()`（绑定者自动 notify），不再替换 handle。

**验证**：全 7 场景回归通过（gallery_acceptance 9 checkpoint / vector / table / inputs / drop / modal / dragdrop）。

**剩余问题**：无。G-A 收口。

## 2026-08-18 — G-B 框架护栏二（收口）

**完成**：
- G3：`Event` 基类构造自动打 steady_clock 时间戳（inline 时钟读，无共享状态，跨 DLL 安全）；scenario 驱动每步 +10ms 模拟时间（双击判定确定性）；DragFloat/SpinBox 双击从位置近似改为 400ms 时间窗。
- G2：debug 构建每 60 帧用 unbound builder 强制全量重画，与增量结果逐项 diff（kind/pos/size/color/text/line/clip），不一致告警指明 draw item。

**验证**：
- 正常代码 2537 次校验帧 0 误报（间隔临时改 3 验证）。
- 人为制造漏标脏（canvas 不稳定 paint 不标脏）→ 182 次告警精确命中 canvas 背景 item（pos 16,784 尺寸 430x110）；probe 撤销后告警消失。
- 全 7 场景回归通过。

**重要发现**：scenario 模式不跑 buildSnapshot（不渲染帧），校验帧验证须走真机（--automation-control-port + quit 收集日志）。

**剩余问题**：无。G-B 收口。

## 2026-08-18 — P5 TreeView 编辑 + Table cell widget（收口）

**完成**：
- TreeView 三能力（叠加式，不破坏现状）：`_bReorderable`（行 press 6px 阈值起 drag，payload 前缀 tree-node:，drop 位置按行高 1/3 判定 before/into/after，`_onReorder(from,to,mode)` 回调由 host 重建 ReactiveList，插入高亮线/框走 P1 矢量）；`_onContextMenu(nodeId, point)`（右键回调 host 开菜单）；`bindFilter(Reactive<string>)`（匹配链过滤——节点自身或后代匹配才显示，过滤时全展开匹配链）。
- `UITableGrid` 升级：cell 支持任意 child widget（内部 UITableLayout 布局 + UITableSlot 定位 + cellHasWidget 抑制该 cell 文本），文本 cell 保持向后兼容。
- `WidgetTreeDump` 加 treeView control 块（visibleRows/selected，过滤断言用）+ `getVisibleRowCount()`。
- Gallery：TreeView demo 开 reorder/右键日志/过滤输入框；Table row3/col2 放真实 UIButton。
- `gallery_tree_edit.jsonl`：editing_widgets + tree_filtered（键入 "Li" 断言 visibleRows 4）。

**验证**：GUIWorkbench 编译通过；gallery_tree_edit 通过；全 7 回归场景通过。

**剩余问题**：无。P5 收口。

## 2026-08-18 — G-C 验收基础设施立项（插入 P6 之前）

用户定调「把 scenario 渲染帧开关立项插入排期」——scenario 模式不渲染帧是验收体系的结构缺口（G2 校验帧/像素级检查无法在行为场景内跑）。G-C：`--scenario-render` 开关（frame 步骤真实渲染）+ WidgetTree 校验 mismatch 计数 + 场景 `assert_validation_clean` 断言。排期：P1-P5 → G-A/G-B → G-C → P6 → P7。

另：本日沉淀 gui-framework skill 两个新 section（控件交互契约 5 条 + 人肉前验收 6 条，commit 82f95715）；四修复（spin 编辑态 +/-、combo dismiss 失焦、tree toggle 标 Paint、drop 标签，commit e68ecefd）。

## 2026-08-18 — G-C 验收基础设施（收口）

**完成**：
- `--scenario-render`：frame 步骤跑 snapshot-only 逻辑帧（layout+paint+G2 校验帧），**不碰 swapchain**——scenario 窗口可能不可 present，render->begin 会崩（实测定位）。
- WidgetTree 累计校验 mismatch 计数（getValidationMismatches，debug 才有）。
- 场景新步骤 `{"assert_validation_clean":true}`：渲染帧内任何漏标脏 → 场景失败。
- acceptance 尾部 70 帧 + 断言：74 帧渲染、校验帧触发、零 mismatch，11 checkpoint 通过。
- 修了解析 bug（assert_validation_clean 步骤缺 push/continue 落入 event 分支）。

**验证**：acceptance+render 通过；全 8 场景回归通过。

**剩余问题**：无。G-C 收口。

## 2026-08-19 — P6 交互补全（收口）

**完成**（两 commit：3aed5ced 前半 + 869e8fb6 后半）：
- tooltip：UIElement::_tooltip + WidgetTree 30 帧 dwell 挂 Tooltip 层（帧计数，无墙钟），hover 变化即移除。
- TextWrapped：UIText _bWrap/_maxWrapWidth（贪心码点断行 CJK 安全），paint 逐行 text item，AutoSize 高=行数×行高、宽=包裹宽。
- _bEnabled 子树禁用：setEnabled（changed-only + invalidateSubtree）+ isEnabledInTree（父链）+ dispatchRoute 路径级拦截（disabled 子树 input-inert）。
- UIDialog：Modal 角色薄壳（标题/内容槽/OK-Cancel/_onClosed 统一回调/Esc/shield 走 false），覆盖 layoutAssigned 居中。
- **GUIWorkbench gallery 化**：app 头注释把每个页面定义为 gallery 展区 + 各自 scenario；「新 feature 独立页」规则落文档。

**验证**：gallery_p1 6 checkpoint（wrap 几何/tooltip 开清/禁用组 notHandled/dialog 开+确认清层）；全 9 场景回归通过。

**剩余问题**：无。P6 收口。

**下一刀**：P7 DockSpace + 窗口管理（最后一期）。

## 2026-08-18 — 护栏补强立项（插入 P5 之前）

用户定调「每开发一个 feature 就出现体验 bug」，要求把框架护栏补强插入后续计划之前。护栏分期（plan.md 已更新）：
- **G-A**：G1 paint 默认 self-clip（消灭溢出绘制整类 bug）+ G4 UIStyleSet::define 同名 set 语义（消灭绑定孤立）。
- **G-B**：G3 Event 时间戳（双击统一）+ G2 debug 校验帧（漏标脏开发期抓）。

另：SearchCombo 焦点互锁 + filter 生命周期修复（commit 314075bb）——菜单开抢焦点触发 onFocusLost→closeMenu 互锁、_bFocused 从未设置；菜单生命周期与焦点解耦、打开后焦点拿回、Esc 自处理。acceptance 扩至 9 checkpoint。


## 2026-08-19 — P7 DockSpace 收口（三 commit：2f35be61 / e8bd74a4 / 22941965）

**完成**：
- UIDockSpace 三 zone（left/center/right 嵌套 UISplitPane）+ UITabBar 拖拽 tab（press 6px 阈值 armed → tree drag session 携带 dock-tab:<zone>:<label>）+ onDrop 按落点 x 阈值（0.25/0.82）选目标 zone + movePanel 重建两区；rebuildZone 用 syncSelectedTab 防旧 _onTabSelected 竞态。
- **配套引擎修复 1**（WidgetTree::detach keepAlive）：parent 持有最后强引用时 removeChildEdge 的 erase 会当场析构 widget（析构里 _tree 断言失败 + 后续悬垂操作）。这是 drag 崩溃根因（Tab_Console use_count=1）。
- **配套引擎修复 2**（headless scenario 通路）：GuiScenarioEventSource 公共化到 App/Kernel、assertScenarioTree 移入 WidgetTreeDump、GUIHeadlessHost 支持 scenario（checkpoint dump/断言/quit），--headless 与窗口模式跑同一场景。
- **配套引擎修复 3**（Vulkan 1.2 设备动态态）：启用 VK_EXT_extended_dynamic_state（CULL_MODE），不支持时 pipeline 回退静态 cull + QuadRender 跳过 vkCmdSetCullMode；顺带实现 MAX_ENUM 过滤。
- dock.jsonl：初始 6 断言 + dock_initial + drag Inspector center→left + DockTabBar0 断言 + dock_dragged 全通过；dump 验证 Inspector 迁到 left、center 留 Scene/Console。
- feature_matrix：p7_dockspace → pass。

**验证**：dock.jsonl headless 通过（7 断言 + 2 checkpoint，exit 0）；dump 对比确认面板迁移。**剩余问题**：验证层在 86% 显存（UE4 Debug 编辑器占用）下 vkCreateGraphicsPipelines 崩溃属环境问题（关验证层/集显验证均定位到验证层），代码侧已修复动态态合法性；headless 模式无 GPU 不受影响。
