---
name: ya-profiling
description: YA Engine profiling、automation trace 与低噪音性能冒烟指南。
---

## 适用场景

- 需要产出 speedscope CPU trace
- 需要配置 automation 下的 CPU trace / RenderDoc / screenshot
- 需要确认 profiling 该走编辑器配置还是 automation 配置

## 核心规则

1. `automation` 只用于自动化 / 冒烟 / 离线跑批，不作为编辑器人工操作的统一配置入口。
2. 编辑器人工操作下的 profiling runtime 开关继续走 `Engine/Saved/Config/Editor.json`。
3. 产出 speedscope CPU trace 需要 `profile` 编译模式。
4. 命令行参数在 automation 模式下只代表本次运行覆盖，不替代长期配置。

## 最小命令

```bash
python3 Script/ya.py cfg --mode profile
python3 Script/ya.py run --project Example/HelloMaterial/HelloMaterial.yaproject -- --exit-after-frame=300 --log-level=warn --log-detail-level=error
```

## 配置入口

1. 编辑器人工操作：`Engine/Saved/Config/Editor.json`
2. 自动化模式：`Engine/Saved/Config/Automation.json`
3. 临时覆盖：命令行参数

## 输出规则

1. 若显式传 `--cpu-profile-output`，优先使用该路径。
2. 否则若 automation 配置里有 `profile.cpu.output`，使用该路径。
3. 若两者都没有，默认写到 `Engine/Saved/Automation/Profile/`。

## 低噪音建议

1. 默认用 `--log-level=warn --log-detail-level=error` 跑 profiling smoke。
2. 若需要更多线索，优先升到 `info`，不要直接开 `trace`。
3. 分析结果时先 `rg` 过滤模块和关键词，再决定是否展开整段日志。

## 编辑器稳态性能门禁

改渲染、文字、字体、View 密度、编辑器面板之后，跑一遍真实编辑器的稳态门禁（不是只跑独立 `--game`）：

```bash
python3 Script/automation/editor/run_widgettree_editor_smoke.py \
  --project Example/2DRpgPrototype/2DRpgPrototype.yaproject --skip-build --perf-window 8 --pie
```

- 分别在编辑态和 PIE 态量 8 秒：帧率不得低于 `--min-fps`（默认 25，debug 构建实测约 80），
  稳态窗口里字体重建（`lazily building`）必须为 0。
- 全程日志不得出现 `raster sizes are in use at once`（字体每帧逐出重建）和
  `is not the panel device size`（视口图与面板设备像素不一致，即放大糊图）。
- 为什么有它：字体图集按个数逐出时，编辑器一帧 13 个字号超过 8 个的窗口，每帧重建三个字号，FPS 掉到 7；
  独立游戏模式和单元测试都测不出。修复前该门禁实测 8.4 fps、8 秒 202 次重建；修复后 82 fps、0 次。

## 相关 skills

- `ya-build`：构建模式、运行入口和目标选择
- `debug-review`：profiling 结果异常、日志过大或回归复盘
- `vscode`：编辑器内调试 / 启动配置

## 退出条件

- 已明确 profiling 该走编辑器配置还是 automation 配置
- 已有一条可复用的 trace / smoke 命令
- 输出路径和查看方式清晰
