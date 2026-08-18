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

## 自动化验收流程（人工之前必跑）

GUIWorkbench 的 scenario 回放是「人工验收之前」的第一道闸：

1. **单页验收**：`xmake run GUIWorkbench --start-page <X> --scenario <abs path to jsonl> --scenario-dump-dir <dir>`（exit 0 = 全 checkpoint 过；dump dir 每个 checkpoint 一份 tree JSON）。
2. **交互驱动**：scenario 事件支持 `mouse_press/release/move`、`drag{from,to,steps,button}`、`mouse_wheel{dx,dy}`、`key_press/key_typed`、`window_size`；断言走 `{"assert":{"widget":..,"control":{..},"rect":{..}}}` 递归子集匹配（依赖 WidgetTreeDump 的 control 块——新控件加断言前先补 dump 块）。
3. **坐标技巧**：先跑一次只带 checkpoint 的 probe 场景 dump 拿实际 rect，再写死交互坐标（布局确定性依赖固定 `window_size`）；滚动容器内的控件交互，先 `mouse_wheel` 滚到位（offset 会 clamp 到 maxOffset，坐标 = 原 y - maxOffset）。
4. **双击等复合手势**：事件无时间戳，双击 = 两次 press 位置接近；编辑类控件要支持「首字符替换」语义（select-all）才可确定性断言提交值。
5. **像素级验收**（视觉项：对比度/颜色/朝向）：scenario 断言不了像素，用 GUIAppHost 的 `--automation-control-port` + `capture_screenshot{target:gpu}` 截 BMP 做脚本检查或人工看——scenario 负责结构与行为，截图负责视觉。
6. **范例**：`Example/GUIWorkbench/Scenarios/gallery_acceptance.jsonl`（滚动+双击输入+通道选择的完整交互验收，5 checkpoint）。

## 当前下一刀

**P1 矢量绘制原语**：开工前先读 `Engine/Source/Framework/Render/Render2D/LineRender.h/.cpp` 确认 screen 路径现状，再定「FLineRender 扩展 vs 细四边形兜底」。
