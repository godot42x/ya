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

