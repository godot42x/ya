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
