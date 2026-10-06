# Progress

## 2026-10-07 — 立项

- 起因：B2 收口后用户确定方向——先处理渲染半成品与缺口，2D 推成可玩 demo，再开一个 3D demo 展示渲染；面向 2027 年春系统岗（偏 UI）面试。
- 依据：两份只读盘点。
  - 渲染：默认延迟管线；RDG 每帧全量 `compile()`（`RenderGraphExecutor.cpp:77`），无 pass 剔除、无 async compute，transient 纹理只跨帧池化不帧内别名；`kFramesInFlight = 1`（`RenderDefines.h:49`）；点光源阴影 GPU 剔除 + indirect 已实现但默认关闭，有历史回归；CSM 数学与采样在，默认 1 级；SSAO 无模糊，无 FXAA / TAA；无 bindless；OpenGL 源码不参与编译（`RHI/Backend/xmake.lua`）。
  - 测量：只有整帧 GPU timestamp；`profile_summary.json` / `gpu_summary.json` 记最后一帧；默认 FIFO + 120 FPS 上限；无 VMA 统计；无渲染压测场景。
- 两轮问答拍板，见 `plan.md` §1。
- 下一步：波 1（W1–W3、M1–M6、G3.0 设计、`ui-behavior-capabilities` C2）。

## 2026-10-07 — 用户补充两处

- GPU 显存与进程内存是两套口径：原 M4 拆为 M4（GPU 显存，VMA）与 M8（进程内存，RSS / 峰值 / 堆分配按子系统），M7 优化分别以两者为依据。
- 漏掉的纯 GUI app 线（`ya_profile=gui` + GUIWorkbench / GUIFrameworkSmoke）单列 GA 组，GA0 盘点进波 1。

## 2026-10-07 — 波 1 开工

- W1 完成：根 `AGENTS.md` 与 `render-arch` skill 改为「Vulkan 是唯一参与编译的后端，OpenGL 源码只作历史参考」；skill 第 1 条的 `RenderFrameCoordinator`（代码里已无此符号）改为 `RuntimeRenderContext` 写出帧顺序（AB7）。
- 派发中：W2（B2 遗留债）、W3（`GUIWindowManagerTest` 间歇失败）、`ui-behavior-capabilities` C2、G3.0 / GA0 两份只读盘点。
- 派发约束（用户 2026-10-07 重申）：subagent 只用 grok-4.7、fast 关闭——实现用 `ya-worker`，只读盘点 / 审查用 `ya-reviewer`；不用 `explore` 等沿用父模型的类型。
- M 组（`perf-baseline` 子计划）待问答拍板后开工。

## 2026-10-07 — G3.0 能力盘点

- 只读盘点（`ya-reviewer`）结论写进 plan §3 G3：渲染侧够用，差距在物理（玩具级）、glTF 骨骼导入、动画混合、输入轴、跟随相机、prefab / 世界计时器。G3 表按引擎工作项重排为 G3.0–G3.12。
- 新增 R15：延迟管线不支持聚光灯（只在前向 Phong 生效），点光源上限 6、`MAX_POINT_LIGHTS_DEFERRED=32` 未被使用。
- 抽查核实：物理形状固定尺寸常量、未注册接触监听、无 `CharacterVirtual` / 射线；`Limits.slang` 上限值。
- G3 子计划目录待方案问答后建立。
