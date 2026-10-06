# perf-baseline — 测量、压测与内存统计

2026-10-07 立项，`showcase-roadmap` M 组（M1–M6、M8）的子计划。目标：之后任何性能相关改动，都能用**同一条命令**在 macOS 与 Windows 上给出可复现的前后对比表。

## 0. 现状（2026-10-07 盘点）

- CPU：`YA_PROFILE_SCOPE` 在 profile / debug 构建写 speedscope；`PerfState` 记最近 1/10/30/60 帧均值；`GameRuntime/Profiling.cpp` 退出时写 `profile_summary.json` / `gpu_summary.json`，数值取**最后一帧**。
- GPU：`VulkanRender.cpp` 每帧只有一对 timestamp（整帧），没有 pass 级 GPU 时间。
- 内存：VMA 只做分配，无 budget / statistics；进程内存无统计。
- 运行：默认 FIFO（垂直同步）+ 启动时 120 FPS 上限，低于约 8 ms 的改进看不出来。
- 场景：没有渲染压测场景（HelloMaterial 约 6 个点光源；TownLarge 约 21 个精灵）。

## 1. 决策（2026-10-07 用户拍板）

| 议题 | 决定 |
| --- | --- |
| 压测场景 | 3D 多光源（点光源 + 投影体数量可配）、3D 大量网格（数千实例）、Sponza 类真实场景、2D 合批（大 tilemap + 大量精灵，面试高频）、GUI（Workbench stress 场景） |
| 运行方式 | 开窗口、固定 1920×1080、关垂直同步、去帧率上限、固定相机路径；预热 120 帧、采样 600 帧 |
| GPU 显存（M4） | VMA heap budget / usage，按资源类别汇总（贴图、render target、transient、buffer、上传区） |
| 进程内存（M8） | 平台计数（RSS、峰值）+ 堆分配追踪（全局分配钩子按子系统打标签，统计每帧分配次数与字节），只在 profile 构建开启。与 M4 两套口径，报告分列；Apple Silicon 统一内存需注明 |
| 结果存放 | 正式对比用的 summary JSON 与对比表提交到本目录 `baselines/<platform>/`；原始 trace 不提交 |
| 平台 | macOS 日常；Windows 正式数字由用户在 Windows 机上跑同一条命令回传 |

## 2. 统一输出格式

一次基准运行产出一份 `bench_summary.json`（本计划的单一事实源，替代「最后一帧」口径）：

- `meta`：项目、场景、提交号、构建模式、平台 / GPU / 驱动、分辨率、present mode、预热与采样帧数、时间戳。
- `frame`：CPU 帧时间、GPU 帧时间的 mean / p50 / p95 / p99 / max。
- `cpu_stages`：已有 `PerfState` / profile scope 的分阶段 CPU 时间，同样的统计量。
- `gpu_passes`：按 render graph pass 名的 GPU 时间统计（M1）。
- `render_counters`：每帧 draw / dispatch / indirect draw 数、pipeline 绑定数、descriptor 绑定数、2D 批次数与纹理表切换数（服务 2D 合批与网格场景）。
- `gpu_memory`：VMA budget / usage（按 heap）与按资源类别汇总（M4）。
- `process_memory`：RSS、峰值；按子系统的存活字节与每帧分配次数 / 字节（M8）。

统计来源按「源」注册到一个汇总器，各 checkpoint 只加自己的源，不改彼此的代码。

## 3. Checkpoints

| id | 内容 | 依赖 |
| --- | --- | --- |
| PB1 | 基准模式 + 汇总器：`--benchmark <config>`（分辨率、present mode 关垂直同步、去帧率上限、预热 / 采样帧数、相机路径、输出目录）；窗口统计（mean / p50 / p95 / p99 / max）；`bench_summary.json` 的 meta / frame / cpu_stages；统计源注册接口 | — |
| PB2 | pass 级 GPU timestamp（RDG executor 逐 pass 打点、按 flight 回读、不阻塞）+ `render_counters`；编辑器 profiling 面板显示 pass 表 | PB1 |
| PB3 | GPU 显存统计（VMA budget / statistics + 资源类别标签）进汇总器与编辑器面板 | PB1 |
| PB4 | 进程内存统计（RSS / 峰值；profile 构建下全局分配钩子 + 子系统标签 + 每帧分配计数）进汇总器与编辑器面板 | PB1 |
| PB5 | 压测场景：新示例项目（暂名 `Example/PerfLab`）含多光源、多网格、2D 合批场景（数量由生成脚本参数化）；Sponza 用拉取脚本放到不入库的目录并核对许可证；每个场景配一份基准 config（含相机路径） | PB1 的 config 格式 |
| PB6 | 对比报告：`Script/perf/bench_compare.py` 读两份 summary 生成 markdown 表（差值与百分比，超阈值标注）；`Script/perf/bench_run.py` 一条命令跑完一组场景；首份 macOS 基线入库 | PB1–PB5 |
| PB7 | GUI 基准：GUIWorkbench 接同一 summary 格式（与 `showcase-roadmap` GA3 共用） | PB1、GA1 |

## 4. 边界

- 不改渲染算法；本计划只加测量。测量本身的开销要可关（非基准运行默认关 pass 级 timestamp 与分配钩子）。
- 不手改 `Generated/*`。shader 侧需要的计数走 Slang 源。
- 不在帧录制中途建或删 query pool；query 回读按 flight 延迟，不 wait idle。
- release 构建不带分配钩子。

## 5. 验收

- 同一场景同一提交连跑三次，`frame` 的 p50 波动在 ±3% 内（macOS）。
- `bench_compare.py` 对两份 summary 产出表格，`baselines/macos/` 有首份基线。
- ya-testing 全绿；新增的统计源有单测（统计量计算、汇总器注册、summary 序列化）。
