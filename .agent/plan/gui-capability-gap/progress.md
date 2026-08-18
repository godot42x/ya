# GUI 能力补齐（第二阶段）Progress

> 主线：`gui-capability-gap` 第二阶段——GameEditor ImGui 替换前置（GUI App 线全量补齐）。
> 记录：每轮完成内容、验证结果、剩余问题。

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

**下一刀**：G-A 框架护栏一（G1 paint self-clip + G4 StyleSet set 语义），之后 G-B（Event 时间戳 + debug 校验帧），再进入 P5。

## 2026-08-18 — 护栏补强立项（插入 P5 之前）

用户定调「每开发一个 feature 就出现体验 bug」，要求把框架护栏补强插入后续计划之前。护栏分期（plan.md 已更新）：
- **G-A**：G1 paint 默认 self-clip（消灭溢出绘制整类 bug）+ G4 UIStyleSet::define 同名 set 语义（消灭绑定孤立）。
- **G-B**：G3 Event 时间戳（双击统一）+ G2 debug 校验帧（漏标脏开发期抓）。

另：SearchCombo 焦点互锁 + filter 生命周期修复（commit 314075bb）——菜单开抢焦点触发 onFocusLost→closeMenu 互锁、_bFocused 从未设置；菜单生命周期与焦点解耦、打开后焦点拿回、Esc 自处理。acceptance 扩至 9 checkpoint。

