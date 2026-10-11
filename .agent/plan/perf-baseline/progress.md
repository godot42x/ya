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

## 2026-10-09 — PB4 进程内存统计（M8，RSS / 峰值部分）

- 用户选定范围 A：本轮只交付 RSS / 峰值 + 汇总源（debug 可验）；「堆分配按子系统归类」的全局分配钩子只在 profile 构建开，挪到 PB6 —— 它本来就要开 `/tmp` profile worktree 出基线，顺手一起验。
- 完成：
  - `Core/Os/OsMemory.{h,cpp}`：`ya::Os::sampleProcessMemory()`（macOS `task_info(MACH_TASK_BASIC_INFO)`、Windows `GetProcessMemoryInfo`、Linux `/proc/self/statm` + `VmHWM`）与 `ya::Os::hasUnifiedMemory()`。平台头（mach / windows.h / psapi）只进这一个 TU，不污染被广泛包含的 `Os.h`。
  - `ProcessMemoryBenchSource` 进 `Core/Profiling/PerfBenchSources`（与其他源同处），`BenchmarkRun` 注册 `process_memory` 段。序列 `rssMiB` / `peakRssMiB`（MiB，同 `gpu_memory` 的口径）；facts：`supported`、`unifiedMemory`。
  - 编辑器：上一条的 `RuntimeGpuMemorySection` 泛化为 `RuntimeMemorySection`，显存与进程内存上下两行并列，并附一句「统一内存下不要相加」的说明——直接落 plan §1 的「分别列出并注明」。
- 验证：`ya-testing` debug 1767/1767（新增 3 个：平台探针自洽、重复采样峰值不回退、源的序列与 facts）；HelloMaterial 实跑出 `process_memory`：rssMiB p50 253.06 / max 682.67，peakRssMiB 972.88（单调，与预期一致），`unifiedMemory=true`。
- 一个值得记的观察（不是 bug）：同一次运行里 GPU 显存 806.58 MiB 远大于进程 RSS（p50 253、峰值 972）。Apple Silicon 是统一内存，但 MoltenVK 的 private-storage 资源不在进程的 `resident_size` 里——它们占的是同一块物理内存，却不映射进进程地址空间。所以这两列既不能相加，也不能相减，只能并列看。面试讲内存时这是个好例子。
- 保留：堆分配按子系统归类（PB6）；Windows 的 `GetProcessMemoryInfo` 链接 `psapi` 已加进 `Core/xmake.lua`，但没在 Windows 上编译验证过。

## 2026-10-09 — PB6 对比报告脚本（基线被磁盘挡住）

- 完成：
  - `Script/perf/bench_run.py`：一条命令跑一组场景。走 `ya_bundle_tool.py build-targets` 拿目标清单（项目的玩法代码是运行时 dlopen 的独立 dylib，只编 `ya-runtime` 不够——第一次就是这么踩的），构建后直接驱动运行时（`--ya-project=` / `--benchmark=` / `--benchmark-output=`），输出到 `<out>/<scene>/run<N>/bench_summary.json` + `index.json`。支持 `--repeat`（稳定性）、`--filter`、`--timeout`、`--no-build`。
  - `Script/perf/bench_compare.py`：两份 summary → markdown。先比对 `meta`（commit / 构建模式 / GPU / 分辨率 / present mode 不同就明说不是干净的前后对比），再按段出 metric 表（p50 / p95 / p99 / mean / max 可配），超 `--threshold` 打标记；facts（`supported`、`unifiedMemory`、`heaps`）单独列，不参与百分比。
  - 顺带修了脚本对 meta 形状的假设：JSON 里 `meta` 是**平铺**的（`gitCommit` / `buildMode` / `gpu`，不是 `host.*`），分辨率是 `renderResolution{width,height}`。
- 验证：debug 构建下 `bench_run.py` 跑 HelloMaterial ×2 成功（含 index.json），`bench_compare.py` 对两次运行出表并正确标出所有超 3% 的行。
- 阻塞（不是代码问题）：
  - **首份 profile 基线没跑出来**。建了 `/tmp/ya-profile` worktree 并 `ya.py cfg --mode profile`（这一步完全按 skill 里写的走通了：重型 submodule 与 Vulkan SDK 都是链接、零下载、`git status` 干净），`ya-runtime` 也编过了，但编示例项目模块时磁盘写满（`No space left on device`）。主项目 debug `build/` 已 11 G，机器只剩 1.2 GiB。
  - **并发构建会毁掉数字**：同场景连跑两次 frame p50 = 11.58 / 21.71 ms（差 87%），因为另一条线在并发编译。正式基线必须单独跑。
- 已把这个代价写进 `ya-build` skill：每个 worktree 自带 `build/`（一份构建约 11 G），`/tmp` 与数据盘同卷，开第二个构建前先 `df -h`。
- 下一步：腾出 ~10 GB 后，在 profile worktree 里 `bench_run.py` 跑 PerfLab（Sponza 需先 `fetch_sponza.py`）×3 入库 `baselines/macos/`。

## 2026-10-09 — PB6 收口：首份 macOS profile 基线入库

- 阻塞解除：清掉 worktree 的构建目录后磁盘回到 106 GiB 可用，重开 `/tmp/ya-profile` 跑完整 profile 构建（`ya-runtime` 249 s + PerfLab 模块）。
- 基线：`baselines/macos/profile-15277c00/`，15 次运行 / 9 个场景（`Lights_50/200/500` ×3、`Meshes_*`、`Sprites_*`），干净 checkout（所有 summary `gitDirty=false`），1920×1080 / immediate / 600 采样帧。含 `README.md`（数字表 + 复现命令 + 三条结论）、`stability_Lights_50.md`、`batching_Sprites_500_vs_5000.md` 与全部 summary JSON（272 KB）。
- 脚本修正（单独一个提交）：`--filter` 从子串改成 glob（`Lights_50` 原本会连 `Lights_500` 一起跑）；`index.json` 改成累加（分批跑同一 `--out` 时原本会丢掉前一批）；`bench_compare.py` 能解析 `<scene>/` 多 run 目录（取最小 run 号并提示）。
- 三条结论：
  1. **2D 合批有效**：`Sprites_500` → `Sprites_5000`（10 倍）frame p50 8.365 → 8.344 ms，基本不变。
  2. **3D 网格梯度是提交瓶颈**：`Meshes_1000/5000/10000` = 8.33 / 32.56 / 63.45 ms，而 gpu 时间只有 2.94 / 8.57 / 14.75 ms——CPU 侧先到顶。
  3. **点光源梯度测不出来**：`Lights_*` 三档全在 8.3 ms，`MAX_POINT_LIGHTS=6` 截断（PB5 已记，要等 R15 / R3）。
- 稳定性：Lights_50 三次 frame p50 = 8.384 / 8.349 / 8.373 ms，极差 0.4%，远在 ±3% 内。
- 数据卫生：`Lights_200` run1 的 6.023 ms 是异常低值（另两次 8.29 / 8.32），首次运行的预热效应，README 注明引用该场景用 run2/run3。这批场景显存稳在 270.7–273.0 MiB，压的是 CPU 与提交而非显存容量。
- 又补进 `ya-build` skill 的坑：删 worktree 前必须 `chmod -R u+w`，否则 `setup_3rd_party.py` 设的只读三方目录会让 `git worktree remove` / `rm -rf` 一路 Permission denied。
- 未做：PB4 余项（堆分配按子系统归类）。计划里写的「profile 构建下全局分配钩子」与本工程链接形态不匹配：默认 `ya_linkage=shared`，每个模块一个 dylib，macOS 两级命名空间下在一个 dylib 里替换 `operator new` 拦不到其它 image 的分配。需要换方案（按 owner 自报字节，或接受 linker / dyld 级 interpose 的代价）再开工。

## 2026-10-09 — PB2 pass 级 GPU timestamp + render_counters（M1）

- 完成：
  - `ICommandBuffer` 加 `beginGpuPass(name)` / `endGpuPass()`（带默认空实现，不动既有 mock）；`RenderGraphExecutor::executeCompiled` 只给**有名字且有 execute 体**的 pass 括一次。
  - Vulkan：独立的 pass query pool（`kMaxGpuPassesPerFrame * 2` = 512），与整帧那对 frame timestamp 完全分开，frame 池的既有回读路径未被触碰。回读挂在 `beginRecordedFrame()` 的 fence 等待之后（和整帧 GPU 时间同一处），`VK_NOT_READY` 直接跳过、不等。
  - `RuntimeState.passGpuTimingEnabled` 开关，默认关；`BenchmarkRun::begin/finish` 只在采样窗口内开，结束还原，所以非基准运行没有 query 写入开销。编辑器侧对应 `profile.runtime.passGpuTiming` 配置键。
  - `Core/Profiling/RenderCounters.h`：9 个 relaxed 原子计数（draw / drawIndexed / indirectDraw / dispatch / dispatchIndirect / pipelineBind / descriptorBind / batch2D / textureTableSwitch2D）。递增在 `VulkanCommandBuffer` 的记录路径与 `ScreenDraw` 的合批 flush 上；递增本身就是已经在热的路径上的 relaxed 加，不带分支与查询。
  - `PassGpuTimeBenchSource` 收一个 `std::function` provider 而不是 `IRender*`——`ya-rhi` 依赖 `ya-foundation-core`，反向包含会成环；设备访问由 `BenchmarkRun`（已依赖 RHI）接上。
  - 编辑器 `RuntimeProfilingSection` 加 pass 表（按耗时降序），开关关时整段不显示。
- 两个真 bug（都是先写错、后被实跑数据抓出来的，单测抓不到）：
  - `endGpuPass` 把结束时间戳写到了和开始同一个 query index，于是配对的第二个 query 永远是 0；回读时那个 query 不可用，`vkGetQueryPoolResults` 整段返回 `VK_NOT_READY`，`gpu_passes` 全空。
  - 回读失败时 pass 名字一直不清，几帧就撞上 256 的上限，之后整帧都不再计时。改成窗口只有一帧的量，读回或读不回都清簿记。
  - 顺带：`WITH_AVAILABILITY` 在 MoltenVK 上对**已完成**的 query 也回 0，可用性位不能当判据；改用「没写过的 query 读回 0」。回读一律带 `PARTIAL`，单个 pass 丢一个时间戳不再作废整帧。
- 验证：
  - `ya-testing` debug **1764/1764**（新增 4 例：`PassGpuTimeSourceRecordsProviderKeys` / `PassGpuTimeSourceWithoutProviderRecordsNothing` / `RenderCounterSourceRecordsCountersAndTakesThem` / `ExecutorBracketsEveryNamedPassThatRuns`）。
  - HelloMaterial 完整基准（debug，600 采样）出 `gpu_passes`：**17/17 个 pass 全部 count=600**，最大项 SSAO Pass p50 1.70 ms，其次 Deferred Light 0.65、GBuffer 0.35、BloomBlurH 0.27；**pass p50 求和 4.04 ms 对整帧 gpuMs p50 4.51 ms**，说明计时确实盖住了帧内真实工作、且没有重复计。
  - `render_counters` 同时出数（drawIndexed p50 828、indirectDraw 696、descriptorBind 304、pipelineBind 36、dispatch 1、batch2D 4、textureTableSwitch2D 2），与 HelloMaterial 的物体量级吻合。
  - 编辑器 smoke（`--exit-after-frame=40`）无 ERROR。
- 保留 / 偏离：
  - **验收的三次 p50 ±3% 没做成实测**：本机是 debug 构建，且同时有另一个 agent 在编译，frame p50 三次分别 10.28 / 19.28 / 13.09 ms，噪声远大于信号，这个数字不能当基线。pass 表本身的稳定性看得出是稳的（三次都是 17 个 pass、count 全 600、pass 求和与整帧 GPU 时间同量级）。正式数字按 PB6 的口径在 profile 构建下重跑。
  - pass 名带 view 后缀（`Deferred GBuffer.view4294967297`），同名的多个 view 合并成一个 key；要拆开得改 graph 的 pass 命名，不在本轮。
  - 计时只覆盖 pass 自己的命令，不含它前后的 barrier——barrier 是图的事。
  - 与 PB3 的分工：两者都改 `VulkanRender.*` / `VulkanCommandBuffer.*`，本轮已把 PB3 在 `VulkanRender.cpp` 里那个 `GpuMemoryBenchProbe` 定义挪到析构函数之前（定义晚于使用会报 incomplete type），其余各自文件、不动对方逻辑。

## 2026-10-09 — PB2 复查返工

复查 PB2 自己的产出，抓到三处问题并修掉：

1. **render_counters 的 max 被预热帧撑大 120 倍（真缺陷）。** 清零原先在 `RenderCounterBenchSource::sampleFrame` 里，而源只在采样窗口内被调用，于是 `warmupFrames` 帧的累积量被第一帧采样整个吞下。八个计数器的 max/p50 比值精确等于 120（`drawIndexed` 828 / 99194），p50 正确、**max 错**——而 PB5 的网格压测正是要看峰值的场景。修法：清零移到 `VulkanRender::beginRecordedFrame`（game runtime、GUI 单窗、GUI 多窗三种 host 每帧都恰好跑一次的帧边界），源退化为只读。实测比值 120x → 1.0x。
2. **`_bPassGpuTimingFrameOpen` 是死状态**：写了五处、零处读，实际由 `_passGpuTimingQueryCount != 0` 承担。删除。
3. **`passGpuTimingEnabled` 的「默认关」说法不严谨**：它和别的开关一样过 `normalizeRuntimeToggle`，在把 profiling 无条件编进去的构建（`ECompileMode::Enabled`）下会被压成常开。当前 xmake 只有 CONDITIONAL / DISABLED 两种，所以行为无碍，但注释里的契约该写明条件。

- 验证：`ya-testing` debug **1767/1767**（`RenderCounterSourceRecordsCountersAndTakesThem` 改名并改为新契约：采样只读、清零归帧边界）。修复后实跑基准：`gpu_passes` 17/17 个 pass、count 全 600 不变；八个计数器 max == p50。
- 未变：`gpu_passes` 的 ±3% 三连跑仍未实测（debug + 并行编译，噪声压过信号），归 PB6 的 profile 口径。

## 2026-10-09 — PB6 复查：让已入库的基线能被信任与复现

PB6 的基线已入库，但复查发现三处问题，都在「这份基线本身可不可信、能不能被别人复现」这一件事上：

1. **`index.json` 的 commit 全是空串（真缺陷）。** `bench_run.py` 读 `meta.host.gitCommit`，而 `buildBenchSummaryJson`（`Core/Profiling/BenchSummary.cpp`）是把 host 事实**扁平合并**进 `meta` 的，没有 `host` 这一层——层级对不上，`.get()` 静默返回 `""`。已入库的 15 条记录全是 `"commit": ""`。这个 bug 隐蔽：同一批的 `bench_summary.json` 里 `meta.gitCommit` 正确（`15277c00`），`bench_compare.py` 也读对了，所以 `stability_Lights_50.md` 那张表里 commit 显示正常——**只有聚合索引这一层坏了，且不报错**。改成扁平读法，顺带补上 `dirty`（原来 `gitDirty` 也没进 index，基线自称干净与否无从在索引层核对）。

2. **`index.json` 的路径指向 `/tmp`，入库后即失效。** 15 条记录的 `path` / `config` 全是 `/private/tmp/pb6-baseline/...` 和 `/private/tmp/ya-profile/...`。这台机器上因为摘要已被拷进仓库所以还读得到，但 `/tmp` 会被清空，`ya-profile` 那个 worktree 也已经没了（`git worktree list` 只剩主工作区）。而 plan §5 的验收是「`baselines/macos/` 有首份基线」——一份 manifest 指向临时目录、换个机器全是死链的基线不算数。修法：`bench_run.py` 记**相对 index 自身**的路径（root 外的路径保持绝对，`../..` 走出目录树的相对路径会依赖读者 CWD），`bench_compare.py` 按 index 所在目录解析。两侧一起改，只改一半会读不出来。

3. **跨场景比较不告警。** `meta_lines` 里 `("scene", "scene", False)`——`must_match=False`，于是场景不同既不打 `**DIFFERENT**` 也不进 notes。实测拿 `Meshes_10000/run1` 对 `Sprites_500/run1`，脚本输出一张格式完整、数字齐全的表，`frame p50` 那行赫然写着 **-86.82% ▼**，全表零句提示。commit / buildMode / gpu / resolution / presentMode 都设了 must_match，偏偏漏了场景——而场景是唯一一个「不同就必然无意义」的字段。后来人拿这份基线做前后对比，完全可能把换场景读成性能改善。改成 must_match。

- 已入库的 `index.json` 按新格式重建（无损）：15 条 commit 从各自 summary 的 `meta.gitCommit` 回填为 `15277c00a8c2d6367db8f1d726f0c8a3865e726a`，`dirty=false`，路径改相对，`project` 从 `/private/tmp/...` 改回 `Example/PerfLab/PerfLab.yaproject`。逐条核对：15 条全部可解析、全相对、commit 长度 40、`frameP50` 与 summary 一致。
- 验证：
  - 移动后仍可读：`cp -r` 整棵树到 `/tmp` 再用 index 解析，成功。
  - 跨场景告警生效（Meshes_10000 vs Sprites_500 打出 scene differs），同场景（Lights_50 run1 vs run3）不误报，frame p50 -0.14% 与原表一致。
  - `rel_to_root` 两个分支分别验证：root 内 → `Lights_50/run1/bench_summary.json`，root 外 → 保持绝对。
  - `run_one` 的取值口径拿真实 summary 驱动比对：新写法得 `15277c00...`，旧写法得 `''`。
- 保留 / 未做：
  - **没有重跑基准**。基线数字本身没动过，改的只是索引和工具；且 PB6 刚记了并发构建能让同一场景 frame p50 差 87%，这个环境下重采只会污染数据。
  - 本轮全程未编译：并发 agent 正在做 PB4 的堆分配钩子（`Core/Memory/` 新增未入库），`Engine/Source/Source.xmake.lua:92` 引用了尚未定义的 `ya_alloc_hook`，`xmake show -t ya-runtime` 直接失败。本轮的验证全部落在脚本行为与既有 summary 上，不依赖引擎构建。
  - PB4 的堆分配钩子换方案一事（PB6 那节已记：shared 链接下替换 `operator new` 拦不到其它 image 的分配）本轮没碰，等那条线自己收口。

## 2026-10-09 — PB4 余项：引擎自己的 allocator 层

方案换了两次，最后落到「引擎抽象出 allocator 这一层」：

1. 先按「hook 全部分配」做，撞上 Mach-O 两级命名空间：实测 `libya-render-3d.dylib` 的 `_Znwm` 是未定义符号、`libya-foundation-core` 并不导出它，全进程都绑到 libc++abi —— **一个 dylib 里定义 `operator new` 拦不到其它模块。**
2. 用户指出两层问题：语义上不该叫 hook（那是偷看），架构上应该是一层 allocator；而且每次分配走虚函数不划算。
3. 最终形态（`Core/Memory/`）：
   - **静态多态**：后端满足 `AllocatorBackend` concept（`allocate` / `deallocate` / `name` / `stats`），`CountingAllocator<Backend>` 模板组合计数与分桶，`using ProcessAllocator = ...` 在头文件底部按宏选型。热路径全内联、无虚表。进程级 allocator 本来就是构建期决策 —— 运行期换是**错的**（旧 allocator 的指针会被新 allocator 释放），不只是慢。
   - `stats()` 是 concept 的一部分：backend 不能计数就**说**不支持（`MallocAllocator` 返回 `bSupported=false`），而不是报 0 让报告看起来像「测到 0」。
   - 分桶靠 `MemoryOwnerScope` / `YA_MEMORY_OWNER`（thread-local 住在 Core），`Untagged` 桶永远存在、不隐藏未标注的分配。
   - 每模块各自编一份 `ProcessAllocatorInstall.cpp`（`ya_process_allocator()`，由 `ya_std_module()` 自动带上）：Mach-O 把镜像未定义的 `_Znwm` 绑回本镜像、MSVC 每 DLL 一份 CRT，所以必须每模块一份；全部转发到 Core 的 `processAllocate`，跨模块 new/delete 仍然配对。
   - 只在 **profile** 构建编进全局分配函数（`is_mode("profile")`）；换成 mimalloc / jemalloc 就是「换 backend + 一行选型」。
- 验证：
  - `ya-testing` debug **1776/1779**（新增 11 个 allocator 用例：concept 静态断言、malloc 后端的零尺寸 / 过对齐 / 空指针释放、`CountingAllocator` 的分桶与嵌套 scope 还原、live 字节回落、超额释放截断不回绕、失败分配不计数、`deltaSince` 的 level+差值语义、未计数构建报不支持）。3 个失败是 `CoreValueTypes.*`，属反射线在改的 `reflects-core`。
  - profile 构建：`nm` 确认**每个模块都自己定义 `_Znwm`**（`libya-render-3d` 有 `T __Znwm`），而计数符号**只在 `libya-foundation-core` 一处** —— 全进程一套计数，不会每个 dylib 各数一份。
  - debug 构建确认**没有**编入 install TU（平台 allocator 原样保留）。
- 踩到的两个坑（已记进 todo）：xmake 求值项目描述时 `get_config("mode")` 是 **nil**，判模式必须用 `is_mode()`；改了源文件列表后 xmake 的 unity 文件不会自动重生成，要清 `build/.gens` 或 `xmake c`（我删了 `.gens` 导致共享 build 状态与磁盘不一致，最后用 `xmake c` + 全量重建修好，期间共享 build 缓存被清空了一次）。
- **给并行 agent 的提示**：上一条记录里提到 `Source.xmake.lua:92` 引用了未定义的 `ya_alloc_hook` 导致 `xmake show -t ya-runtime` 失败 —— 该函数已改名为 `ya_process_allocator()` 并有定义，这个阻塞解除。
- **未取运行期数字**：`heap.*` 序列与编辑器 Heap 行本轮没跑出数值 —— profile worktree 落在别人一个编译不过的提交上（`RenderCompanionSpecs.cpp` 用了不存在的 `renderFeatureMaskOf`），全引擎 profile 构建被挡。编译与单测层面已验；等那条线修好再补一次基准即可。
## 2026-10-09 — allocator 层回归（报告：regression-allocator-2026-10-09.md）

- 全量构建 + 全部门禁 + profile worktree 实跑，三处编译错误、两个实现缺陷、一个构建门控问题，全部定位到根因并修掉；每个结论都做了「改前 / 改后」对照。
- **编译错误 1（HEAD 上就有，非本次引入）**：`RenderCompanionSpecs.cpp:46` 用了未声明的 `renderFeatureMaskOf` —— 该函数声明在 `CompanionManager.h`，cpp 没 include。补一行 include。此前一直被增量构建掩盖，直到 `xmake c` + 全量重建才暴露；它也是上一轮拿不到 profile 运行期数字的原因。
- **缺陷 A（本次引入）**：`CoreValueTypes` 三个用例 `std::bad_alloc`。在本次改动之前的提交（`e56d5d9f`）建 worktree 跑同一组用例 13/13 通过，所以是自己的问题。根因：`MetaValue` 的 boxed 存储走 `::operator new(size, align_val_t(alignof(T)))`，而 `alignof(glm::mat4) == 4`，macOS 的 `aligned_alloc` **拒绝小于 `sizeof(void*)` 的对齐并返回 null**（libc++ 自己回退到普通 new），我的 `MallocAllocator` 把它当错误对齐直接丢过去，于是每次 boxed 分配都失败。修法：只有真超对齐才走 `aligned_alloc`，其余回退 `malloc`。
- **缺陷 B（本次引入）**：profile 基准里 `heap.liveMiB` = 4429 MiB 而 `rssMiB` 只有 360 MiB。绝大多数全局 `operator delete` 不传 size，`liveBytes` 只加不减 —— 那个「level」其实是累计量。修法：每个 block 前置 size 头部（过对齐时记录指针偏移，两条路径共用一条还原规则）。修后同一场景 `heap.liveMiB` 99.85 MiB / `freeMiB` 1.58 MiB/帧 / RSS 301 MiB，量级吻合。
- **构建门控不可靠**：`ya_process_allocator()` 原来用 `is_mode("profile")` 决定是否接管全局分配，实测 `[probe] is_mode(profile)=false is_mode(debug)=false get_config(mode)=nil` —— xmake 求值项目描述时还没加载 mode，两个查询都拿不到答案，门控时灵时不灵（同一份 debug 配置下 core 确实接管了 `_Znwm`）。改法：这一层在**所有构建**都接管（只在部分构建里存在的 allocator 不是一层，而且这样全部门禁都走这条路等于持续验证它），「路由到哪个 backend」搬到 C++ 由 `YA_BUILD_MODE_PROFILE` 决定。
- 顺带修 `bench_run.py --filter`：`Lights_50.bench.json` 的 stem 是 `Lights_50.bench`，改成 fnmatch 后精确名选不出任何东西；改为对去掉 `.bench.json` 的场景名匹配。
- 验证：全量构建通过；`xmake r -g test` **3369/3369 通过 0 失败**（`ya-testing` 1784，新增 17 个 allocator 用例）；profile worktree 全量构建通过 + PerfLab Lights_50 基准实跑出 `heap.*` 全套。
- 帧时间 8.758 ms vs 已入库基线 8.384 ms：不同提交、且当时机器有并发构建（本项目实测并发构建能让同一场景 frame p50 差 87%），**不作为性能回归判断**，只作「能跑通、数量级合理」的证据；要比必须在同一提交、机器空闲时重采。
- 剩余：三个子系统桶恒为 0（机制已测但还没在任何子系统入口打 `YA_MEMORY_OWNER`，所以没有真实归属数据）；Windows 未编译；这层在 profile 下的开销未量化。

## 2026-10-11 — PB7 GUI 基准

- GUIWorkbench 接上 PB1 的汇总器，不经过 GameRuntime。命令：`GUIWorkbench --benchmark Example/GUIWorkbench/Benchmark/GUIWorkbench.bench.json`。
- summary 段与游戏基准相同。`process_memory` 与 `gpu_memory` 分列；Apple Silicon 上 `unifiedMemory` 为 true。GUI 合成没有 render graph pass，所以 `gpu_passes` 为空，`render_counters` 记的是 2D 批次。
- 窗口缩放的 `requestRecreate()` 会把已经排队的 present mode 盖回旧的 `_ci`。Mailbox 在这台机器上不可用，于是基准落成 FIFO、帧时间贴在刷新率上。已排队的 CI 现在保留。
- 验证见 showcase-roadmap 同日 GA3 条目。短窗数字只说明管线通了，不是 profile 基线。
## 2026-10-11 — R1 A/B 数字 + 两个环境发现（分配钩子 / validation）

`render-view-family` R1（FIF 1→2，`327e9e30` + `cee44ae1` + `100ae306`）的同构建 A/B：数字与口径见
`baselines/macos/profile-100ae306-fifAB/README.md`——可测四场景 frame p50 **-24.5%~-39.8%**
（Lights_500 收益最大，符合"GPU 越接近 CPU，重叠收益越大"）。

过程中两个影响所有后续基准的发现：

1. **全局分配钩子让重 draw 场景不可基准化**：M8 的 profile 计数分配器（5fc2a91fa）把
   MoltenVK 每次 `vkCmd*` 的内部分配也路由进记账，`Meshes_10000`（1 万 draw/帧）被放大
   ~20×——FIF=1 与 FIF=2 双侧都在 900s 超时，纯钩子成本，与场景改动无关。旧基线
   `profile-15277c00` 的 commit 早于钩子落库，**与其对比无效**（这也解释了 PB6 里
   "点光源梯度截断"之后这轮复核时重场景的读数不可信）。新待办：钩子加基准开关
   （默认关，仅内存剖析任务显式开），否则 R 线的重场景数字全部被钩子噪声淹没。
2. **validation 层曾无条件编译开关**：`m_EnableValidationLayers` 硬编码 true，profile
   构建在 loader 找得到层时逐 draw 校验（同样 ~20× 量级）。已改为按构建模式（`100ae306`）：
   debug 开，releasedbg/profile/release 默认关。

另：bench A/B 期间三次"挂死"假象的完整归因链（validation 开销 → 旧实例占锁 → 钩子开销）
记在 `../render-view-family/progress.md` 的 R1 条目，避免后人重走。
