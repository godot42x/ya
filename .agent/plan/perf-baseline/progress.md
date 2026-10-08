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

## 2026-10-07 — PB5 压测场景

- 完成：
  - `Example/PerfLab`：yaproject / yamodule / xmake.lua / Entry，`Example.xmake.lua` 按目录名注册（大小写与目录一致）。
  - `Script/perf/generate_perflab.py`：确定性生成（seed 固定），幂等（两次生成 md5 一致）；只写 scene JSON 与 `*.bench.json`，不写资产。
  - `Script/perf/fetch_sponza.py`：Khronos 旧仓 contents API `?ref=master` 404（仓库迁移），去 ref 走默认分支重定向可用——修法已固化在脚本里；先校验 Crytek / Meinl / Dabrovic 许可文本再下载到 `Engine/Saved/PerfAssets/Sponza/`（`Saved` gitignore 覆盖，不入库）。
  - 10 个场景各一对 scene + bench config（Lights 50/200/500、Meshes 1k/5k/10k、Sprites 500/2000/5000、Sponza），全走 PB1 schema，循环相机路径，`PerfLabBenchmarkConfigTest` 契约测试锁住。
- 验证：Lights_50 完整基准 600 帧（**debug 口径**，frameMs p50 10.276 ms，meta 与 config 一致）；其余场景 30 帧加载 exit 0；Sponza 模型实例化成功（PBR packed metallicRoughness 既有警告，不崩）；`ya.py test --target ya` **1714/1714**（1713 + 1 契约测试），主 agent 本机复跑确认。
- 已知引擎上限（详见 todo）：`MAX_POINT_LIGHTS=6` 截断光源梯度；`MaterialDescPool` 2048 使网格梯度不挂 Phong（共享 fallback）；无逐物体 castShadow。两项设计是否重构待用户拍板。
- 正式基线数字由 PB6 以 profile 口径连跑三次出，本次 Lights_50 仅验证链路。

## 2026-10-09 — PB3 GPU 显存统计（M4）

- 完成：
  - RHI 新公开类型：`RHI/Core/GpuMemoryClass.h`（`EGpuMemoryClass`：texture / renderTarget / transient / buffer / upload，外加 `Auto`「创建者没说」）、`RHI/Core/GpuMemoryStats.h`（`resolveGpuMemoryClass` 两条推导、`FGpuMemoryStats`、`GpuMemoryAccount`）、`RHI/Core/GpuMemoryBenchSource.h`（`gpu_memory` 段）。
  - `ImageCreateInfo` / `BufferCreateInfo` 加 `memoryClass`，`Auto` 时后端按 usage / memoryUsage 推导；显式值优先（RenderGraph 建出来的贴图显式标 `Transient`——它们的 usage 跟普通 render target 一样，启发式分不出来）。两个字段都不进 `isSame*CreateInfo`：它们是记账标签，不是资源规格。
  - Vulkan：`VmaAllocationInfo::size` 在创建时记进 `_memoryBytes` / `_memoryClass`，析构时按原值释放；`GpuMemoryAccount`（原子计数，release 在 0 处截断，不回绕）跟在 allocator 旁边；`VulkanRender::getGpuMemoryStats()` 取 VMA `vmaGetHeapBudgets`。可选开 `VK_EXT_MEMORY_BUDGET_EXTENSION_NAME`——没有它 VMA 只回退到「本 allocator 拿了多少」，拿不到进程口径。
  - `gpu_memory` 段由**设备自己**注册（`VulkanRender` 建/毁时挂卸 probe），不经过 `BenchmarkRun`：数字是设备的，注册放在数据所有者那里，任何 host 开窗就都有这一段，也不用去碰本轮被 PB2 线占用的 `BenchmarkRun.*` / `PerfBenchSources.*`。序列是每类 + `totalMiB`（MiB，float 存得下），facts 是每类峰值 + 每 heap 的 budget / usage / allocation / block。
  - 编辑器：新增 `RuntimeGpuMemorySection`（Runtime Tools 页，独立一段）。没并进 `RuntimeProfilingSection`——那个文件本轮 PB2 线在改。
- 验证：
  - `ya-rhi-vulkan-smoke` debug 18/18（新增 8 个：分类 / account 截断 / 汇总 / 源的序列与 facts / 不支持时报 `supported=false` / 峰值随窗口重置）。
  - `ya-testing` debug 1762/1763；唯一红灯 `PerfBenchSourcesTest.PassGpuTimeSourceWithoutProviderRecordsNothing` 是 PB2 线在改的文件，与本次无关。
  - HelloMaterial 实跑基准（debug）出 `gpu_memory`：totalMiB p50 806.58 = texture 567.86 + renderTarget 177.89 + buffer 28.19 + upload 16.58 + transient 16.06；VMA 自己的 `allocationBytes` 806.58 与分类求和一致（两套口径互相印证）。heap 0：budget 13107 MiB / usage = block = 833.95 MiB——MoltenVK 没有 `VK_EXT_memory_budget`，budget 落回 heap size、usage 落回 blockBytes，与 `FGpuMemoryHeapBudget` 注释里写的降级行为一致。
- 保留 / 偏离：
  - 只量不优化：transient 帧内别名（R5）、上传缓冲复用等留给 M7，按 M4 数字排。
  - 编辑器面板放进新段而不是 profiling 段：本轮 `RuntimeProfilingSection.*` 属 PB2 线，避免同文件并发改。
  - GPU 显存与进程内存仍然是两套口径，M8 / PB4 未开工。
