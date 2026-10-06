# Progress

## 2026-10-07 — 立项

- 来源：`showcase-roadmap` M 组；现状盘点与口径拍板见 `plan.md` §0 / §1。
- 用户补充：2D 合批是面试高频，压测场景单列 2D 合批（批次数、纹理表切换数进 `render_counters`）。
- 下一步：PB1 与 PB5 并行（PB5 先用 PB1 约定的 config 格式）。

## 2026-10-07 — PB1 基准模式 + 汇总器

- 完成：
  - `Core/Profiling`：`BenchStats`（全样本 mean/min/p50/p95/p99/max）、`BenchAggregator` + `IBenchSource` + `BenchSourceRegistration`（`profiling::registerBenchSource(section, source)`）、`PerfBenchSources`（`frame` / `cpu_stages`）、`BenchSummary`（meta + JSON 写出 + git/平台事实）。
  - `GameRuntime/Benchmark`：`BenchmarkConfig`（严格解析、present mode 回退、相机路径按帧插值）、`BenchmarkRun`（关帧率上限、选 present mode、窗口按像素密度对齐 drawable、固定 dt、逻辑后渲染前写主相机、预热后开窗、写 `bench_summary.json` 后 `requestQuit`）。CLI `--benchmark` / `--benchmark-output`；config 格式见 `plan.md` §2.1，示例 `Example/HelloMaterial/Benchmark/HelloMaterial.bench.json`。
  - 删除「最后一帧」口径的 `profile_summary.json` / `gpu_summary.json`（全仓无消费者）及其每 tick 异步写盘、AppAutomation 中的 artifact 更新 / flush。
  - RHI：`IRender::getDeviceName()` → `getDeviceInfo()`（name / driver / apiVersion，驱动取 `VkPhysicalDeviceDriverProperties`）。
  - 修 `VulkanRender` 整帧 GPU timestamp 回读：原先在 frame fence 等待之前读刚提交的帧（`kFramesInFlight = 1`），几乎总是 `VK_NOT_READY`，`gpu.time.ms` 长期停在旧值；改为等完该 slot 的 fence 后读。
- 验证：
  - 共享工作区 debug：`ya.py test --target ya` 1708/1708 过（含新增 20 个：BenchStats / BenchAggregator / PerfBenchSources / BenchSummary / BenchmarkConfig / BenchmarkCameraPath）；冒烟 HelloMaterial / GreedySnake / 2DRpgPrototype `--exit-after-frame=120`、编辑器 `--exit-after-frame=30` 均 exit 0、无 ERROR；debug 下 HelloMaterial 基准跑通（p50 9.478 ms）。
  - `/tmp` 独立 worktree profile（Apple M5 / MoltenVK 1.4.1）：HelloMaterial 基准连跑三次 frame p50 = 9.029 / 9.187 / 9.319 ms（均值 9.178，各次偏差 ≤ ±1.6%）；present mode = immediate（可用 fifo / immediate），`frameRateCapped=false`，`Tick/FpsControl` p50 = 0。
- 保留 / 偏离：
  - 新增两个 GameRuntime 源文件使 unity batch 重排，暴露 `GameRuntime/Script/GameplayLua.cpp` 缺 `GameUIHost.h` include；该文件属 Script 线，本 checkpoint 未改，已由该线在共享工作区补上。
  - profile worktree 里 5 个 GUI 用例崩溃（`UIFrameSnapshotTest.AddBrushSolidStaysOneSprite` / `AddRectFilledMultiColorStoresPerVertexColors` / `AddSpriteStoresOpaqueSampleFlag` / `AddBrushNinePatchWithoutTextureStretches`、`ComposeClipReplayTest.NestedBuilderClipFlattensToOneClipRun`，栈被写坏），debug 下全过，属 GUI 线的 profile-only 问题。
  - macOS 上去掉 FPSControl 上限后，轻场景（GreedySnake）仍停在约 120 fps（窗口与全屏都一样，等待落在 `Render/Submit` 的 MoltenVK drawable 获取，swapchain 已是 3 张）：这是显示刷新节拍，不是引擎上限；HelloMaterial CPU 约 9 ms，不受影响。
  - 全新 checkout 下 2DRpgPrototype 链接失败（`Example.xmake.lua` 写 `2DRpgPrototype`、目录是 `2dRpgPrototype`，大小写不敏感文件系统下 `Entry.cpp.o` 被加两次）；共享工作区不受影响，与 PB1 无关。
  - 额外加了 `fixedDeltaSeconds`（默认 1/60）与 `cpuTrace`（默认关）两个 config 键：前者让动画 / 脚本按帧确定，后者去掉 trace 写盘噪声。
  - `RenderDiagnosticsService::getAutomationRenderDocCapturePath / PassSummaryPath` 已无调用方，留待 RenderDoc 线清理。
- 下一步：PB2–PB4 各自注册 `gpu_passes` / `gpu_memory` / `process_memory` 源；meta 可补显示刷新率。

## 2026-10-07 — PB1 返工

- 已知边界（本轮不修）：
  - PB1 确定性边界：相机/逻辑固定，脚本 elapsed 与 host clock 仍墙钟。
  - 采样窗口里 `FileWatcher::poll` 仍每帧跑，是已知测量噪声。
- `cpuTrace`：cxxopts 3.3.1 的 default 不计入 `count`，所以「命令行没写 `--cpu-profile`」本来就能分辨。缺口是 CLI default 没被 `tryGet` 读到，debug 停在 `isProfileBuild()`（false），`beginRuntimeSession` 又只在 overridden 或 profile 构建时套用。现把默认放进 `AppProfilingOptions`（profiling 可用时开）并在开 session 时套用；基准只在命令行真正出现 `--cpu-profile` 时用 CLI 值。
- 验证（入库口径，cpuTrace=false，present=immediate，sampled=600）：
  - HelloMaterial profile 三次 frame p50 = 8.673 / 8.355 / 8.539 ms（均值 8.522，最大偏差 1.96%，在 ±3% 内）。三次 `Engine/Saved/Profile` 文件清单与大小都没变。
  - `ya.py test --target ya --filter 'BenchAggregator*:BenchmarkConfig*'` 17/17。
  - `ya.py test --target ya` debug 1713/1713（1708 + 本轮 5 个新用例）。
