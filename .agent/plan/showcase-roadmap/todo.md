# TODO

总计划见 `plan.md`。这里只记状态和归属指针；细节在各子计划或已有线里。

图例：`[ ]` 未开工 · `[~]` 进行中 · `[x]` 完成 · 括号内为归属。

## P0

收口与文档
- [x] W1 文档改正：「兼容 OpenGL」、`render-arch` 过时符号（`RenderFrameCoordinator` → `RuntimeRenderContext`）
- [x] W2 B2 遗留债（8 项，见 plan §3 W2；严格审查后返工一次，序列化规则收进 `ECS/ComponentJson`）
- [x] W3 `GUIWindowManagerTest` 间歇失败定位（根因：reconcile 在处理处读活硬件；事件自带 `heldButtons` 修掉；同族 `EditorInputNode` 与跨窗拖拽 flaky 记入 memory `pointer_reconcile_reads_live_hardware.md`，后续随输入线收口）
- [x] W4 profile 构建下 5 个 GUI 测试栈损坏（`4e95276d`）：根因是 `UIFrameBuilder` 的 `_ctx` 为 `const UIFrameBuildContext&`，`UIFrameBuilder(UIFrameBuildContext{})` 把引用绑到临时对象，`build()` 再拷贝已释放的栈——不是 profile-only 栈溢出。5 例全部把 ctx 绑到临时；debug 全过只是因为 profile 计数分配器改变了分配行为才暴露。改为按值持有。顺带在 `74dab681` 修掉同为 profile-only 的 clamp 用例（"free 32 bytes 层级不变" 只在桶为空时成立）
- [x] W5 RPG 示例目录统一为 `Example/2DRpgPrototype`（`c377e0df`；git 索引、磁盘目录项、`.yaproject` / `.yamodule`、头文件，以及代码、测试、脚本里的路径）

GUI 与编辑器活跃线
- [~] S5 反射 + 脚本层整体规范化（`reflection-runtime/` RR0–RR9 除 RR4 外已落地，RR5b / RR8 的 Windows 验证待跑；吸收原 native 注册重构，S5.1 已入库）
- [x] `ui-behavior-capabilities` C3（Lua `call()` 热路径，`e56d5d9f`）

测量与压测
- [x] M1 pass 级 GPU timestamp（`perf-baseline` PB2，含 render_counters 与编辑器 pass 表）
- [x] M2 窗口均值 / p95 / p99（perf-baseline PB1）
- [x] M3 基准模式（固定分辨率、关垂直同步、去上限、丢预热帧）（perf-baseline PB1）
- [x] M4 GPU 显存统计（VMA，按资源类别；`perf-baseline` PB3）
- [x] M8 进程内存统计：RSS / 峰值 + 按子系统的堆分配追踪（`perf-baseline` PB4 全部交付，`1dcab812` 补上子系统归属：Resource / Render / Script 三桶实跑都有数）
- [x] M5 压测场景（`perf-baseline` PB5，`Example/PerfLab`）
- [x] M6 前后对比报告脚本 + 首份 macOS profile 基线（`perf-baseline/baselines/macos/profile-15277c00/`；索引已复查重建：commit 回填、路径相对化、跨场景比较告警）

渲染
- [x] R1 frames-in-flight 1→2（`render-view-family` M4：旋钮 1→2 + deletion queue init 收敛 + 槽 1 轮转实证；基准对比见 perf-baseline）
- [ ] R2 点光源阴影 GPU 剔除修稳并默认开启
- [ ] R3 CSM 多级 cascade 修稳并默认开启
- [~] R15 延迟管线聚光灯 + 点光源上限理顺（R15a 点光 SSBO 无上限已落地 `shadow-and-light-line/`；R15b 聚光灯待做）

2D demo（`rpg-prototype`）
- [x] G2.1 相机 2D 字段在 `CameraComponent`；跟随留在 `FollowCamera.lua`。对话逐字在 `Dialogue.lua`
- [x] G2.2 存档 / 读档（`world.save` / `world.load`：场景路径 + `Persist`；脚本 `onSave` 决定写什么）
- [x] G2.3 对话分支、背包、任务、商店（`Shop.lua`：护符 3 枚硬币；商人对话框成交）
- [x] G2.4 完整流程：城镇摊位 → 房子罐子 → 回城买护符 → 井的结局
- [x] G2.5 打包冒烟已通过（同机打包冒烟、无音频——A1 未做，验收口径待用户确认；`Package/2DRpgPrototype`，跑过一帧）。通关录屏在 `Engine/Saved/Automation/2d-rpg-playthrough/playthrough.mp4`（呈现帧，不进 git）

3D demo（`3d-demo/` 子计划，2026-10-12 立项）
- [x] G3.0 子计划设计（G3.0 盘点 + 2026-10-12 两轮方案问答收口，五件套齐全，见 `../3d-demo/`；待用户确认：示例目录名、素材选型）
- [ ] G3.1 物理组件重设计（Kinematic / 传感器 / glTF 网格与 convex 碰撞 / 接触事件进 ECS；第一波）
- [ ] G3.2 物理脚本 API（第二波；与 RR8 时序对齐）
- [ ] G3.3 角色控制器（第二波）
- [ ] G3.4 glTF skin / animation / 节点层级导入（第一波）
- [ ] G3.5 动画按名字播放 + 交叉淡入（第二波；push 缺 walk 顶替为已知缺口）
- [ ] G3.6 第三人称相机 + InputMap 鼠标轴（输入轴部分第一波先行）
- [ ] G3.7 运行时生成模型 / prefab、世界计时器（第二波）
- [ ] G3.8 交互与解谜内容（第三波）
- [ ] G3.9 关卡与光照（第三波；依赖 R15b ✓、聚光灯阴影（新增项）、R2、R3）
- [ ] G3.11 性能叠加层 + 打包 + 录屏（第三波；依赖 A1 与 F5 指标口径修正）

纯 GUI app 线（2026-10-11 迁 `gui-app/` 子计划，目标、盘点、约束与进度见该目录）
- [~] GA0 盘点（已完成）与子计划问答
- [x] GA1 gui profile 恢复可配置可构建（`2ee635a1`；大小写随 W5 `c377e0df` 收口）
- [x] GA2 一条命令门禁：全部场景 + GPU / 离屏一致性 + 可移植基线（GA2r 返工完成：场景按控件定位；随 gui-app T3 提交入库）
- [~] GA3 GUI 帧开销、进程内存、显存数字（实现未提交，返工中：GA3r 共享基准会话）
- [ ] GA4 打包成独立应用

GUI
- [x] `ui-behavior-capabilities` C2（C1–C3 全部完成：C2 `3ae03e7f`、C3 `e56d5d9f`；归档等结论进 skill）
- [x] `game-ui-script-framework` S4
- [x] `game-ui-script-framework` S5（H9 点锚点仍 blocked_on_D7）
- [x] `game-ui-script-framework` S6、S7（H9 仍 blocked_on_D7）

文档与吸收
- [ ] D1 模块依赖图（自动生成）
- [ ] D2 三篇设计文档：脚本后端、GUI 框架、render graph
- [ ] D4 README / 视频 / 性能报告 / 面试问答汇总
- [ ] D5 吸收笔记补写：测量（perf-baseline）、反射重构（reflection-runtime）、2D demo（rpg-prototype）、W3 / W4（先补面试要讲的线）

## P1

- [ ] M7 显存与进程内存优化
- [ ] GA5 Windows 跑绿
- [ ] GA6 GUI 框架独立性证明 + 对外 API 文档
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
- [ ] G3.10 编辑器物理形状 inspector 与模型摆放（P1）
- [ ] G3.12 3D 轻动作扩展
- [ ] GUI：`gui-framework-editor-runtime-refactor`、`gui-kernel-ux-parity`、`gui-editor-structure`、`source-layout-subtraction`、`editor-ui-grouping`、`display-compose-encoding`
