# TODO

- [x] PB1 基准模式 + 汇总器 + `bench_summary.json`（meta / frame / cpu_stages）
- [x] PB2 pass 级 GPU timestamp + render_counters（编辑器 profiling 段的 pass 表；计数清零在帧边界，max 不再被预热帧撑大——见 progress 的复查返工一节）
- [x] PB3 GPU 显存统计（VMA，按资源类别）；面板是编辑器 Runtime Tools 里独立的 GPU Memory 段（未并入 profiling 段——那段本轮由 PB2 线占用）
- [~] PB4 进程内存统计：RSS / 峰值已交付；堆分配按子系统归类（profile 构建下的全局分配钩子）待 PB6 在 /tmp profile worktree 里一起验（用户 2026-10-09 选 A 方案）
- [x] PB5 压测场景：多光源、多网格、2D 合批、Sponza（`Example/PerfLab` + 生成脚本 + 10 组 scene/config；正式数字留给 PB6）
- [~] PB6 对比报告脚本 + 一条命令跑一组 + 首份 macOS profile 基线（`baselines/macos/profile-15277c00/`，15 次运行 / 9 场景，干净 checkout；Sponza 未含，资产未拉）
- [ ] PB7 GUI 基准（与 GA3 共用）

## PB6 记下的边界

- 基线必须在**没有其他构建**的机器上采：并发编译时同一场景两次 frame p50 差 87%（11.58 / 21.71 ms）。
- `Lights_200` run1 是异常低值（6.023 vs 8.29 / 8.32），首次运行的预热效应；引用该场景用 run2 / run3。
- `Sponza` 场景要先 `Script/perf/fetch_sponza.py` 拉资产（不入库），本轮未采。
- `Lights_*` 三档全在 8.3 ms：`MAX_POINT_LIGHTS=6` 截断（见 PB5），光源梯度在 R15 / R3 解决前测不出真实曲线。
- 这批场景的显存稳在 270.7–273.0 MiB：压的是 CPU 与提交，不是显存容量；要压显存得换场景。

## 已知边界（PB1 返工记下，本轮不修）

- PB1 确定性边界：相机/逻辑固定，脚本 elapsed 与 host clock 仍墙钟
- 采样窗口里 `FileWatcher::poll` 仍每帧跑，是已知测量噪声

## PB2 记下的边界

- `gpu_passes` 的 key 是 render graph 的 pass 名，带 view 后缀（`Deferred GBuffer.view4294967297`）；同名的多个 view 会合并成一个 key。
- pass 计时只覆盖 pass 自己的命令，不含它前后的 barrier（barrier 归图，不归 pass）。
- 每帧 pass 数上限 256（512 个 query）；超出的 pass 不计时，只 WARN 一次。
- `VK_QUERY_RESULT_WITH_AVAILABILITY` 在 MoltenVK 上对**已完成**的 query 也回 0，所以「没写过的 query 读回 0」才是可用性判据。回读一律带 `PARTIAL`：单个 pass 丢一个时间戳不再作废整帧。
- `render_counters` 的清零在帧边界（`VulkanRender::beginRecordedFrame`）而不在采样点，所以采样点只读。PB5 的压测场景看峰值时可以直接用 max。