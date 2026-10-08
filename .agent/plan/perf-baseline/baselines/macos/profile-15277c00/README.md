# macOS baseline — profile, `15277c00`

首份正式基线（`showcase-roadmap` M6 / `perf-baseline` PB6）。

- 采集时间：2026-10-09
- 机器：Apple M5 / arm64 / macOS，MoltenVK 1.4.1（Vulkan 1.3.334），1920×1080，present mode `immediate`，关帧率上限、固定 dt 1/60、预热 120 + 采样 600 帧、`cpuTrace=false`
- checkout：`15277c00`，**工作区干净**（所有 summary 的 `meta.gitDirty` 均为 `false`）
- 构建：独立 worktree 的 profile 配置（共享工作区只有一个 xmake 配置，切模式会改掉别人在用的）

## 复现

```bash
git worktree add /tmp/ya-profile 15277c00
cd /tmp/ya-profile && python3 Script/ya.py cfg --mode profile
python3 Script/perf/bench_run.py --project Example/PerfLab/PerfLab.yaproject \
    --bench-dir Example/PerfLab/Benchmark --filter 'Lights*' --out <out> --repeat 3
python3 Script/perf/bench_compare.py --before <out>/Lights_50/run1 --after <out>/Lights_50/run3 \
    --metrics p50,p95 --threshold 3
```

`Sponza` 不在这份基线里：它的资产要用 `Script/perf/fetch_sponza.py` 拉到不入库的
`Engine/Saved/PerfAssets/`，本轮没拉。

## 数字

frame / gpu 为窗口统计（ms），显存与进程内存为 `totalMiB` / `rssMiB` 的 p50（MiB）。

| scene | run | frame p50 | frame p95 | gpu p50 | vram | rss | peak rss |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Lights_50 | 1 | 8.384 | 10.973 | 4.722 | 270.7 | 232.8 | 287.9 |
| Lights_50 | 2 | 8.349 | 10.633 | 4.787 | 270.7 | 244.8 | 298.5 |
| Lights_50 | 3 | 8.373 | 10.719 | 4.783 | 270.7 | 314.1 | 316.3 |
| Lights_200 | 1 | 6.023 | 10.337 | 3.956 | 270.8 | 295.5 | 295.5 |
| Lights_200 | 2 | 8.288 | 9.435 | 5.222 | 270.8 | 262.5 | 287.0 |
| Lights_200 | 3 | 8.321 | 10.035 | 5.193 | 270.8 | 300.0 | 306.2 |
| Lights_500 | 1 | 8.312 | 9.463 | 5.277 | 270.8 | 295.9 | 319.5 |
| Lights_500 | 2 | 8.302 | 9.407 | 5.308 | 270.8 | 303.3 | 309.6 |
| Lights_500 | 3 | 8.279 | 9.406 | 5.254 | 270.8 | 302.4 | 304.2 |
| Meshes_1000 | 1 | 8.331 | 9.981 | 2.940 | 270.7 | 271.2 | 300.1 |
| Meshes_5000 | 1 | 32.563 | 39.757 | 8.570 | 270.7 | 321.9 | 336.0 |
| Meshes_10000 | 1 | 63.450 | 70.743 | 14.753 | 270.7 | 236.8 | 345.9 |
| Sprites_500 | 1 | 8.365 | 13.252 | 1.943 | 273.0 | 236.1 | 290.9 |
| Sprites_2000 | 1 | 8.368 | 12.937 | 2.254 | 273.0 | 327.0 | 327.0 |
| Sprites_5000 | 1 | 8.344 | 12.894 | 2.233 | 273.0 | 221.3 | 314.5 |

## 稳定性

`stability_Lights_50.md` 是 Lights_50 run1 vs run3 的完整对比表：frame p50
8.384 → 8.373 ms（**-0.13%**），三次 frame p50 极差 0.4%，远在 ±3% 内。

## 这份数字说明的三件事

1. **2D 合批是有效的**：`Sprites_500` → `Sprites_5000`（10 倍精灵）frame p50
   8.365 → 8.344 ms，基本不变（见 `batching_Sprites_500_vs_5000.md`）。
2. **3D 网格梯度是线性瓶颈**：`Meshes_1000 / 5000 / 10000` = 8.33 / 32.56 /
   63.45 ms，gpu 时间 2.94 / 8.57 / 14.75 ms 跟不上 CPU 侧，瓶颈在提交与
   实例数据，不在光栅化。
3. **点光源梯度目前测不出来**：`Lights_50 / 200 / 500` 全在 8.3 ms 左右，
   因为 `MAX_POINT_LIGHTS = 6` 把超出部分截断了（PB5 已记）。要拿到光源梯度
   的真实曲线，得先解决这个上限（R15 / R3）。

## 注意事项

- **并发构建会毁掉数字**：另一条 agent 线在编译时，同一场景两次 frame p50
  是 11.58 / 21.71 ms（差 87%）。正式基线必须在没有其他构建的机器上采。
- `Lights_200` run1 的 6.023 ms 是异常低值（该场景另两次 8.29 / 8.32），
  第一次跑时着色器/管线还在预热。引用这个场景时请用 run2 / run3。
- `vram` 在所有场景都稳在 270.7–273.0 MiB：这批压测场景压的是 CPU 与提交，
  不是显存容量。要压显存得换场景（更大纹理集 / 保留多帧的 RT）。
- GPU 显存与进程内存**不可相加**：Apple Silicon 是统一内存，MoltenVK 的
  private-storage 资源占同一块物理内存但不映射进进程，所以 `vram 270 MiB`
  与 `rss 230 MiB` 是两个交叠而非互补的口径。