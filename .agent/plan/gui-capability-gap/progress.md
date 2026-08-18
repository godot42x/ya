# GUI 能力补齐（第二阶段）Progress

> 主线：`gui-capability-gap` 第二阶段——GameEditor ImGui 替换前置（GUI App 线全量补齐）。
> 记录：每轮完成内容、验证结果、剩余问题。

## 2026-08-18 — 调研 + 计划定稿（本轮）

**完成**：
- 全量扫描 GameEditor ImGui 依赖（32 文件 / 140 API / 1062 次调用），产出六层职责认知 + 缺口对照表（audit.md §5）。
- 用户拍板范围：控件+布局+绘制+拖拽+TreeView 编辑+DockSpace；排除 ImGuizmo/字体/IME/剪贴板。
- 验证方式拍板：Gallery demo + scenario 断言双保险。
- Plan agent 设计 7 期分期（P1 矢量原语 → P2 Table → P3 输入控件 → P4 拖拽重排 → P5 TreeView 编辑 → P6 P1 交互 → P7 DockSpace），plan.md 定稿。

**验证**：无代码改动，无回归。

**剩余问题**：
- P1 的 scenario 断言方式待定（draw item 级不在 WidgetTreeDump 内，需扩展 dump 或借 capture 像素）。
- 实施前需确认 `FLineRender` screen 路径现状（P1 第一刀先读 `Render2D/LineRender.h/.cpp`）。

**下一刀**：P1 矢量绘制原语。
