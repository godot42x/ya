# GUI 能力补齐（第二阶段）Session Checklist

> 更新时间：2026-08-18
> 作用：`gui-capability-gap` 第二阶段（Editor Parity：GUI App 线补齐 GameEditor 所需 feature）每轮的固定开工/收尾步骤。
> 第一阶段（响应式数据绑定）已收口，历史 checklist 见 git 历史 / feature_matrix 旧 track。

## 每轮开工前

1. 读 `plan.md` §3 当前期 + `progress.md` 上一轮停点 + `feature_matrix.json` 状态。
2. `git status --short`，确认工作区干净（排除子模块/WIP，参照 commit 纪律）。
3. 确认当前只推进一期（P1→P7 顺序，依赖关系见 plan.md §6）；上一期未收口先收口。
4. 基线：`xmake b GUIWorkbench` 通过 + `xmake run GUIWorkbench --scenario <既有 scenario>` 抽样通过。

## 每轮进行中

1. 新控件遵循既有骨架：REFLECT + hitFilter/focusPolicy + VisualFlag（瞬态）/ changed-only setter（持久）+ paintSelf + handleInputEvent。
2. **标脏纪律零容忍**（漏标脏已复发 4 次）：任何视觉状态变更 → VisualFlag 或 invalidateProperty(Paint)。
3. MVC/MVVM 分治：受控控件（TextField 类）不硬塞 Reactive 绑定；纯展示/数据驱动控件才接 bindXxx。
4. attach 按拓扑序（先父到祖父，再子到父）。
5. 布局内缩用 padding 不用 position；固定尺寸子控件进 box 要设 crossAlignment。
6. 每期 Gallery demo + scenario 同 commit 落地，不拆碎提交。

## 每轮收尾前

1. `xmake b GUIWorkbench` 通过。
2. 新增 scenario 全过：`xmake run GUIWorkbench --start-page <X> --scenario <abs path> --scenario-dump-dir <dir>`。
3. 既有 8 页无回归（抽跑 1-2 个既有 scenario）；**加页后同步检查 runDemoAutomation tab 索引**（Dock 页会移位 Editor tab）。
4. 更新 `feature_matrix.json`（本期 items → pass）+ `progress.md`（本轮内容/验证/剩余）。
5. 工作区可接力；一期 = 一个自洽 commit。

## 当前下一刀

**P1 矢量绘制原语**：开工前先读 `Engine/Source/Framework/Render/Render2D/LineRender.h/.cpp` 确认 screen 路径现状，再定「FLineRender 扩展 vs 细四边形兜底」。
