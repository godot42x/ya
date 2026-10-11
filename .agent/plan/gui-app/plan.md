# gui-app — 纯 GUI app 线（自研 GUI 框架独立产品形态）

2026-10-11 自 `../showcase-roadmap/` §3 GA 迁出成子计划（方向审查裁定：伞形计划只定优先级与依赖、不直接改代码；GA 归本线）。showcase §3 GA 只留状态与指向本目录的指针。

产品形态：`ya_profile=gui` 只构建 GUI 模块 + 最小 host（不带 3D、脚本、物理包）；产品是 `Example/GUIWorkbench`（Feature Gallery，`FWorkbenchSurface` 挂 GUIApp，`Scenarios/*.jsonl` 交互场景，`run_workbench_gpu_parity.py` 像素门禁）与 `Example/GUIFrameworkSmoke`。U 组各 GUI 线的能力在这里以独立应用验收。

终态对齐 showcase §0 第 3b 条：gui profile 构建的 GUIWorkbench 打包可运行，交互场景与像素门禁全绿。

## 1. GA0 盘点（2026-10-07，`ya-reviewer`）

GUI 框架依赖边界干净（GUI / Render2D / Workbench 源码不 include Scene / ECS / Render3D / Resource / Script / Physics；残留：`ya-rhi` public 带 entt 包、`ya-gui-host` 只能走 Vulkan swapchain）。门禁是零散单点：31 个 `Scenarios/*.jsonl` 没有批量入口；`run_workbench_gpu_parity.py` 只覆盖 Widgets 一页、比的是同帧 GPU 与离屏一致性；仓库里没有提交像素基准图（`Baselines/` 只有 5 份结构化快照，`gui_style_baseline.py` 写死 `build/macosx/arm64/debug`）；`ya.py package` 只认 `.yaproject`，GUI host 按仓库相对路径加载 shader 与字体，没有打包路径；Windows 在发布检查表里是 Blocker。已核实阻塞：`Example/Example.xmake.lua` 无条件 include 依赖 `ya-engine` 的 3D 示例（根 `xmake.lua` 注释声称的守卫不存在），gui profile 很可能配置不过；文档最后一次记录 gui profile 跑绿是 2026-08-09。`gui-framework` skill 里的 `make test-gui` 不存在。

## 2. 约束

gui profile 的构建必须在独立 worktree / 构建目录里做：在共享工作区 `xmake f --ya_profile=gui` 会改掉其他 worker 正在用的配置。

## 3. 工作项

| id | 内容 | 优先级 |
| --- | --- | --- |
| GA0 | 盘点（已完成）；子计划方案问答 | P0 |
| GA1 | 已完成（`2ee635a1`）：3D 示例（HelloMaterial / GreedySnake / 2DRpgPrototype / PerfLab）只在 `ya_profile ~= gui` 时 include；`ci.sh gui-shared` 补 headless-host-test 与 `--smoke-actions`；修文档漂移（`make test-gui`、closure-test「唯一 target」注释、场景数 31）。附带的大小写路径当时只修了一半（include 写成与目录一致的 `2dRpgPrototype`）；2026-10-11 拍板规范名 `2DRpgPrototype`，统一已随 showcase W5 落地（`c377e0df`） | P0 |
| GA2 | 一条命令门禁：headless 批量跑全部场景断言 + windowed GPU / 离屏一致性；基线脚本路径可移植；结构化 digest 作跨平台主门禁，像素基准按平台分存（MoltenVK 与原生 Vulkan 像素不保证一致）。**第一版实现未提交，返工中 → GA2r** | P0 |
| GA3 | GUI 帧开销与内存数字：windowed CPU / GPU 帧时间、进程内存（M8 口径）、显存（M4 口径）。GA3r 已收口：与游戏运行时共用 `Core/Profiling/BenchmarkSession`，profile Dock 数字见 `progress.md` | P0 |
| GA4 | 打包成独立应用：不依赖仓库根的资源根、预编译 shader 或带 Slang 运行时、字体、MoltenVK / SDL 动态库；macOS `.app` 与 Windows 目录布局 | P0 |
| GA5 | Windows 跑绿（MSVC 构建 + 全部场景），需要用户的 Windows 机器 | P1 |
| GA6 | 框架独立性证明：D1 依赖图、对外 API 文档；`ya-rhi` 的 entt 泄漏 | P1 |

## 4. 返工 checkpoint（2026-10-11 方向审查新增）

GA2 / GA3 的第一版实现曾留在共享工作区未提交。GA2r 已另提交入库。GA3r 已收口：

- **GA3r 共享基准会话**（T2，完成）：游戏运行时和 GUI host 共用 `Core/Profiling/BenchmarkSession`（预热 / 采样 / 结束、五个数据源、`bench_summary.json`）。两边的配置都内嵌 `FBenchmarkCommonConfig`，GUI 扩展只留 `page`，拒绝 `uncapFrameRate`。GUI 改分辨率走宿主的 `matchDrawablePixelSize`。profile 构建、无其它构建、Dock 1920×1080、immediate、预热 120、采样 600，swapchain 1920×1080：frame p50 3.050 ms / p99 16.593 ms，cpuMs p50 0.775 ms，gpuMs p50 2.122 ms，RSS p50 192.78 MiB，GPU 合计 46.66 MiB，`unifiedMemory` true。
- **GA2r 场景按控件定位**（已另提交）：场景按控件定位，断言恢复严格。

## 5. 执行规则

沿用 `../showcase-roadmap/plan.md` §5：一个可验收目标一个提交、计划与代码同提交、按显式路径 stage；性能结论必须带数字；收口写吸收笔记（GA1–GA3 已欠，随 GA2r / GA3r 一并补）。

## 6. 已知约定

菜单项按显示文字命名（`Menu_{label}`，与 `MenuBar_{label}` 同一套）。做本地化时要改成显式 id，否则查找名会跟着译文变。
