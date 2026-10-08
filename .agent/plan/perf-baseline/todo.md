# TODO

- [x] PB1 基准模式 + 汇总器 + `bench_summary.json`（meta / frame / cpu_stages）
- [x] PB2 pass 级 GPU timestamp + render_counters（编辑器 profiling 段的 pass 表；耗时总量占未提交，见 progress 的验证一节）
- [x] PB3 GPU 显存统计（VMA，按资源类别）；面板是编辑器 Runtime Tools 里独立的 GPU Memory 段（未并入 profiling 段——那段本轮由 PB2 线占用）
- [~] PB4 进程内存统计：RSS / 峰值已交付；堆分配按子系统归类（profile 构建下的全局分配钩子）待 PB6 在 /tmp profile worktree 里一起验（用户 2026-10-09 选 A 方案）
- [x] PB5 压测场景：多光源、多网格、2D 合批、Sponza（`Example/PerfLab` + 生成脚本 + 10 组 scene/config；正式数字留给 PB6）
- [ ] PB6 对比报告脚本 + 一条命令跑一组 + macOS 首份基线
- [ ] PB7 GUI 基准（与 GA3 共用）

## 已知边界（PB1 返工记下，本轮不修）

- PB1 确定性边界：相机/逻辑固定，脚本 elapsed 与 host clock 仍墙钟
- 采样窗口里 `FileWatcher::poll` 仍每帧跑，是已知测量噪声

## PB2 记下的边界

- `gpu_passes` 的 key 是 render graph 的 pass 名，带 view 后缀（`Deferred GBuffer.view4294967297`）；同名的多个 view 会合并成一个 key。
- pass 计时只覆盖 pass 自己的命令，不含它前后的 barrier（barrier 归图，不归 pass）。
- 每帧 pass 数上限 256（512 个 query）；超出的 pass 不计时，只 WARN 一次。
- `VK_QUERY_RESULT_WITH_AVAILABILITY` 在 MoltenVK 上对**已完成**的 query 也回 0，所以「没写过的 query 读回 0」才是可用性判据。回读一律带 `PARTIAL`：单个 pass 丢一个时间戳不再作废整帧。