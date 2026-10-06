# TODO

- [x] PB1 基准模式 + 汇总器 + `bench_summary.json`（meta / frame / cpu_stages）
- [ ] PB2 pass 级 GPU timestamp + render_counters
- [ ] PB3 GPU 显存统计（VMA）
- [ ] PB4 进程内存统计（RSS / 峰值 / 分配追踪）
- [ ] PB5 压测场景：多光源、多网格、2D 合批、Sponza
- [ ] PB6 对比报告脚本 + 一条命令跑一组 + macOS 首份基线
- [ ] PB7 GUI 基准（与 GA3 共用）

## 已知边界（PB1 返工记下，本轮不修）

- PB1 确定性边界：相机/逻辑固定，脚本 elapsed 与 host clock 仍墙钟
- 采样窗口里 `FileWatcher::poll` 仍每帧跑，是已知测量噪声
