# TODO

总计划见 `plan.md`。这里只记状态和归属指针；细节在各子计划或已有线里。

图例：`[ ]` 未开工 · `[~]` 进行中 · `[x]` 完成 · 括号内为归属。

## P0

收口与文档
- [x] W1 文档改正：「兼容 OpenGL」、`render-arch` 过时符号（`RenderFrameCoordinator` → `RuntimeRenderContext`）
- [~] W2 B2 遗留债（8 项，见 plan §3 W2）
- [~] W3 `GUIWindowManagerTest` 间歇失败定位

测量与压测
- [ ] M1 pass 级 GPU timestamp
- [ ] M2 窗口均值 / p95 / p99
- [ ] M3 基准模式（固定分辨率、关垂直同步、去上限、丢预热帧）
- [ ] M4 GPU 显存统计（VMA，按资源类别）
- [ ] M8 进程内存统计（RSS / 峰值、按子系统的堆分配）
- [ ] M5 压测场景
- [ ] M6 前后对比报告脚本

渲染
- [ ] R1 frames-in-flight 1→2（`render-view-family` M4）
- [ ] R2 点光源阴影 GPU 剔除修稳并默认开启
- [ ] R3 CSM 多级 cascade 修稳并默认开启

2D demo（`rpg-prototype`）
- [ ] G2.1 相机 2D 字段 + 跟随组件、文本逐字显示
- [ ] G2.2 存档 / 读档
- [ ] G2.3 背包 / 任务 / 商店 / 对话分支（数据驱动）
- [ ] G2.4 完整流程内容
- [ ] G2.5 打包 + 录屏

3D demo（新子计划，待建）
- [~] G3.0 子计划设计与能力差距盘点
- [ ] G3.1 角色控制器 + 相机 + 动画混合
- [ ] G3.2 交互与解谜
- [ ] G3.3 关卡与光照
- [ ] G3.4 性能叠加层 + 打包 + 录屏

纯 GUI app 线
- [~] GA0 盘点与子计划
- [ ] GA1 gui profile 构建 + Workbench 场景 + 像素门禁一条命令跑绿
- [ ] GA2 Workbench 打包 + 录屏 + GUI 开销数字

GUI
- [~] `ui-behavior-capabilities` C2
- [ ] `game-ui-script-framework` S4

文档与吸收
- [ ] D1 模块依赖图（自动生成）
- [ ] D2 三篇设计文档：脚本后端、GUI 框架、render graph
- [ ] D4 README / 视频 / 性能报告 / 面试问答汇总

## P1

- [ ] M7 显存与进程内存优化
- [ ] GA3 GUI 框架独立性证明 + 对外 API 文档
- [ ] R4 RDG 编译缓存 + pass 剔除
- [ ] R5 RDG transient 纹理别名
- [ ] R6 SSAO 模糊、FXAA
- [ ] R7 独立渲染线程
- [ ] R8 RHI 能力矩阵
- [ ] R11 `rdg-cache-dx` P3–P5
- [ ] R12 `render-view-family` P2 / P3；纯 2D View（G2.6）
- [ ] A1 音频接口 + miniaudio 后端 + 组件 + 脚本 API
- [ ] D3 设计文档：渲染线程与帧时间轴、音频、存档
- [ ] GUI：`font-framework-convergence`、`editor-undo-redo`、`gui-framework-editor-readiness` Phase 8、`gui-invalidation-architecture`、`gui-style-system-convergence`、`gui-anchor-to-slot`

## P2

- [ ] R6 TAA
- [ ] R9 bindless / descriptor indexing
- [ ] R10 async compute
- [ ] R13 `render-application-boundary` 剩余项
- [ ] R14 OpenGL 或低端后端（有真实需求再开）
- [ ] A2 自研混音器与空间化
- [ ] G2.7 tilemap 静态实例缓冲
- [ ] G3.5 3D 轻动作扩展
- [ ] GUI：`gui-framework-editor-runtime-refactor`、`gui-kernel-ux-parity`、`gui-editor-structure`、`source-layout-subtraction`、`editor-ui-grouping`、`display-compose-encoding`
