# TODO

- [x] PB1 基准模式 + 汇总器 + `bench_summary.json`（meta / frame / cpu_stages）
- [x] PB2 pass 级 GPU timestamp + render_counters（编辑器 profiling 段的 pass 表；计数清零在帧边界，max 不再被预热帧撑大——见 progress 的复查返工一节）
- [x] PB3 GPU 显存统计（VMA，按资源类别）；面板是编辑器 Runtime Tools 里独立的 GPU Memory 段（未并入 profiling 段——那段本轮由 PB2 线占用）
- [x] PB4 进程内存统计：RSS / 峰值 + 堆分配按子系统归类。子系统桶已接上（`1dcab812`）：Resource 在 `loadModelImpl` 与四处贴图解码点、Render 在 `RenderGraphExecutor::prepare` / `::executeCompiled`、Script 在 `script::callMethod` 漏斗。实跑 PerfLab `Lights_50`：render 17.5 MiB 逐帧波动 / resource 加载后恒定 0.054 MiB / untagged 29.5 MiB；GreedySnake（有脚本）script 0.0012 MiB，PerfLab（无脚本实体）script 正确为 0
- [x] PB5 压测场景：多光源、多网格、2D 合批、Sponza（`Example/PerfLab` + 生成脚本 + 10 组 scene/config；正式数字留给 PB6）
- [~] PB6 对比报告脚本 + 一条命令跑一组 + 首份 macOS profile 基线（`baselines/macos/profile-15277c00/`，15 次运行 / 9 场景，干净 checkout；Sponza 未含，资产未拉。索引已按复查结论重建：commit 回填、路径相对化）
- [ ] PB7 GUI 基准（与 GA3 共用）

## PB6 记下的边界

- 基线必须在**没有其他构建**的机器上采：并发编译时同一场景两次 frame p50 差 87%（11.58 / 21.71 ms）。
- `index.json` 的 `path` 相对 index 自身记录，root 外的保持绝对——相对路径走出目录树会依赖读者 CWD。搬动或入库整棵树后 `bench_compare.py` 仍按 index 位置解析。
- 跨场景比较会告警（`scene` 是 must_match 字段）：换场景得到的 delta 一律无意义，脚本不再让它长得像一次性能改善。
- `Lights_200` run1 是异常低值（6.023 vs 8.29 / 8.32），首次运行的预热效应；引用该场景用 run2 / run3。
- `Sponza` 场景要先 `Script/perf/fetch_sponza.py` 拉资产（不入库），本轮未采。
- `Lights_*` 三档全在 8.3 ms：`MAX_POINT_LIGHTS=6` 截断（见 PB5），三个场景的 `drawIndexedCount` p50 都是 586、descriptorBind 都是 78，提交量完全相同，光源梯度在 R15 / R3 解决前测不出真实曲线。
- 这批场景的显存稳在 270.7–273.0 MiB：压的是 CPU 与提交，不是显存容量；要压显存得换场景。

## PB4 余项（allocator 层）记下的边界

- allocator 层在**所有构建**都接管全局 `operator new`（`ya_process_allocator()` 无条件加源文件）；「路由到哪个 backend」由 C++ 侧的 `YA_BUILD_MODE_PROFILE` 决定。**不要在 xmake 里问构建模式**：求值项目描述时 `get_config("mode")` 是 nil、`is_mode("profile")` 也是 false（两种查询实测都拿不到 mode），门控会时灵时不灵——回归时同一份 debug 配置下 core 确实接管了 `_Znwm`。
- 改了源文件列表后 xmake 不会自动重生成 unity 文件：清 `build/.gens` 或 `xmake c`。只删生成目录而不清构建状态会让 `xmake b` 报 `unity_N.cpp: no such file`。
- 每模块各自编一份 `ProcessAllocatorInstall.cpp`：Mach-O 把镜像未定义的 `_Znwm` 绑回本镜像、MSVC 每个 DLL 一份 CRT，只在 Core 里定义一份拦不到其它模块。计数符号只在 Core 一处（`nm` 已验），因此全进程一套计数。
- `aligned_alloc` 在 macOS 上拒绝小于 `sizeof(void*)` 的对齐（`alignof(glm::mat4)==4` 就中招），必须回退到 `malloc`；否则每次 boxed 分配都是 `bad_alloc`。
- 分配尺寸记在**旁路表**而不是块前置头部（`74dab681`）：前置头部会让返回指针不再是 malloc 原块，跨镜像 / 第三方 free 会把没见过的地址交给 `free`。表按返回指针做键，insert-only（地址被复用即覆盖）。表满时 `FAllocatorStats::sizeTableComplete` 转 false，层级退化成下界并由两个消费点显式报出。
- `YA_MEMORY_OWNER` 是 **thread-local**：贴图解码跑在 `TaskQueue` worker 上，作用域必须开在 worker lambda **内部**；开在提交点等于把每张贴图的原始像素记到 worker 自己的 untagged 桶里。跨线程语义有单测钉住（worker 不继承提交线程的 tag）。
- 反过来，**不要用跨线程的全局 delta 断言归属**：计数器是全进程的，一个线程"分配了恰好 1 块"在忙进程里根本不可观测（实测内层线程读出 5）。断言 tag 用 `currentOwner()`，断言计数用同线程快照。
- 三个子系统桶已接上（`1dcab812`），此前恒为 0 的原因是**没有任何地方开过作用域**。实跑 `Lights_50`：render 17.5 MiB 逐帧波动（每帧图编译 + 屏障临时分配，随即释放），resource 加载后恒定 0.054 MiB。GreedySnake script 0.0012 MiB；PerfLab 无脚本实体，script 读 0 是正确的而非缺口。
- 回归报告见 `regression-allocator-2026-10-09.md`（3 处编译错误、2 个实现缺陷、门控问题与全部验证数据）。
## 已知边界（PB1 返工记下，本轮不修）

- PB1 确定性边界：相机/逻辑固定，脚本 elapsed 与 host clock 仍墙钟
- 采样窗口里 `FileWatcher::poll` 仍每帧跑，是已知测量噪声

## PB2 记下的边界

- `gpu_passes` 的 key 是 render graph 的 pass 名，带 view 后缀（`Deferred GBuffer.view4294967297`）；同名的多个 view 会合并成一个 key。
- pass 计时只覆盖 pass 自己的命令，不含它前后的 barrier（barrier 归图，不归 pass）。
- 每帧 pass 数上限 256（512 个 query）；超出的 pass 不计时，只 WARN 一次。
- `VK_QUERY_RESULT_WITH_AVAILABILITY` 在 MoltenVK 上对**已完成**的 query 也回 0，所以「没写过的 query 读回 0」才是可用性判据。回读一律带 `PARTIAL`：单个 pass 丢一个时间戳不再作废整帧。
- `render_counters` 的清零在帧边界（`VulkanRender::beginRecordedFrame`）而不在采样点，所以采样点只读。PB5 的压测场景看峰值时可以直接用 max。