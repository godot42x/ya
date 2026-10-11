# Progress

## 2026-10-11 — 自 showcase-roadmap 迁出

- 方向审查（见 `../showcase-roadmap/progress.md` 同日条目）裁定 GA 线迁独立子计划；GA1 / GA2 / GA3 三条进度随迁。
- GA2 / GA3 第一版实现留在共享工作区未提交，按 GA2r / GA3r 返工（见 `plan.md` §4）。

## 2026-10-11 — GA1 gui profile 可配置可构建（已完成，`2ee635a1`）

- 3D 示例（HelloMaterial / GreedySnake / 2DRpgPrototype / PerfLab）只在 `ya_profile ~= gui` 时 include。GUIFrameworkSmoke 与 GUIWorkbench 两个 profile 都在。
- `Example.xmake.lua` 的示例 include 改成与当时目录一致的 `2dRpgPrototype`（另一种大小写在大小写不敏感盘上是第二个 include，`Entry.cpp` 会进链接两次）。2026-10-11 方向审查拍板规范名 `2DRpgPrototype`，统一已随 showcase W5 落地（`c377e0df`）。
- `Script/ci.sh` 的 `gui-shared` 补上 `ya-gui-headless-host-test` 和 `GUIWorkbench --smoke-actions`。
- 文档：`gui-framework` skill 去掉不存在的 `make test-gui`，改成 closure / widgets / headless-host / workspace 四条；`Test.xmake.lua` 不再把 closure test 写成 gui profile 里唯一的测试；场景数按目录写成 31。
- 验证在独立 worktree（`/tmp/ya-gui-ga1`，不改共享工作区的 engine 配置）：`ya_profile=gui` 配过；`ya-gui-minimal-host`、`GUIWorkbench`、`ya-gui-headless-host-test` 链接通过。共享工作区仍是 engine profile。

## 2026-10-11 — GA2 GUIWorkbench 一条命令门禁（实现未提交，返工中 → GA2r）

- 入口是 `python3 Script/gui_workbench_gate.py`：31 个 `Scenarios/*.jsonl` 的 headless 断言，然后 `gui_style_baseline.py` 的结构化 digest，然后 `run_workbench_gpu_parity.py` 的同帧 GPU / 离屏。二进制由 `Script/automation/gui/workbench_bin.py` 在 `build/` 下查找，不再写死 `build/macosx/arm64/debug`。
- `Script/ci.sh` 的 `gui-shared` 在 `--smoke-actions` 之后跑这条门禁（`--skip-build --skip-gpu`）。像素基准仍不是跨平台门禁。
- 七个场景对着当前控件几何重对过（方向审查判为返工点：像素坐标回放、断言放宽）：Theme 走 View → Light Theme；折行宽度用 `$gt 300` / `$lte 360`；ColorEdit 不再断言已删除的 `activeChannel`；Dialog OK、Console 合并、同叶 tab 重排与关闭、分割条拖拽都改到实测矩形上。`theme_light` 基线用同一条菜单路径，暗色底约 0.11、亮色底约 0.95，两份 semantic digest 不同。
- 验证：`workbench gate: scenarios=31 failed=0 digest=PASS gpu=PASS`。

## 2026-10-11 — GA3 / PB7 GUIWorkbench 基准（实现未提交，返工中 → GA3r）

- `GUIWorkbench --benchmark <config.json>` 走和游戏运行时同一份 `bench_summary.json`：`frame`（墙钟 / CPU / GPU）、`cpu_stages`、`render_counters`、`process_memory`（M8）、`gpu_memory`（M4，设备自己登记）。GUI 没有场景相机和固定步长，`gpu_passes` 为空（合成不走 render graph pass）。
- 配置 `Example/GUIWorkbench/Benchmark/GUIWorkbench.bench.json`：Dock 页，1920×1080，immediate，预热 120、采样 600。`--start-page` 优先于配置里的 `page`。headless 拒绝这条命令。
- 窗口缩放触发的 swapchain 重建不再丢掉已经排队的 present mode。否则 Dock 基准会落回 FIFO，帧时间被刷新率夹住。
- 验证：`GuiBenchmarkConfigTest` 3/3。debug 短窗（Dock，1280×720，immediate，采样 24）：frame p50 6.32 ms，cpu 5.64 ms，gpu 1.25 ms，RSS 214 MiB，GPU 合计 46.7 MiB，`unifiedMemory` true。这不是 profile 基线（方向审查判为返工点：平行实现了一套基准 → GA3r）。
