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

## 2026-10-07 — GA0 纯 GUI app 线盘点

- 结论写进 plan §3 GA，GA 表重排为 GA0–GA6。依赖边界干净；门禁零散（无批量场景入口、无提交的像素基准、无打包路径、Windows 未跑过）。
- 核实阻塞：`Example/Example.xmake.lua` 无条件 include 依赖 `ya-engine` 的 3D 示例，根 `xmake.lua` 注释声称的守卫不存在，gui profile 很可能配置不过。
- 约束：gui profile 构建在独立 worktree 里做，不在共享工作区重新 `xmake f`。

## 2026-10-07 — M 组子计划 `perf-baseline` 立项

- 口径拍板：压测场景 = 多光源、多网格、Sponza、2D 合批（用户补充：面试高频）、GUI；开窗口固定 1920×1080、关垂直同步、去上限、预热 120 / 采样 600；进程内存 = RSS / 峰值 + profile 构建下按子系统的分配追踪；正式基线入库。见 `../perf-baseline/plan.md`。
- 派发规则纠正：W2 与此前 B2-6 / B2-7 是**续接** Grok 4.7 派的，续接已证实变 fast；规则写进 `subagent-delegation` skill（只用 frontmatter 标定模型的 `ya-worker` / `ya-reviewer`，只新开不续接）。

## 2026-10-07 — W2 收口

- W2 完成（严格审查 + 一次返工）：`addNativeMethod` 先到者胜（补填签名分支删除）；组件序列化规则单一来源 `ECS/ComponentJson::assembleComponentJson`（「反射（可选）+ 追加 custom」），`SceneSerializer` / `component.get` / `component.set` 三处共用、读写对称；`installLuaScriptBackend` 断言只装一次 + `LuaScriptBackendScope` RAII；`world.spawnSprite` 无路径即无贴图；Npc 交互传交互者；`GridMoverTest.lua` 进 gtest。

## 2026-10-07 — W3 收口

- 根因：focus-loss / mouse-leave 的 pointer reconcile 在**处理处**读活硬件（`queryGlobalMouse().buttonMask`）；`WindowFocusLostEvent` / `WindowMouseLeaveEvent` 改为自带 `heldButtons`（事件构造时 `SDL_GetGlobalMouseState` 采样），host 两侧用 `event.getHeldButtons()`。skill 契约句与 memory `pointer_reconcile_reads_live_hardware.md` 同步。
- 遗留（记 memory，随输入线收口）：`EditorInputNode::reconcilePointerSessionsWithPlatform` 仍读活硬件（修法涉及 `InputRouter` 注入接口）；`GUIAppCrossWindowDragTest.SourceTaggedMoveHitsForeignWindowUsingGlobalMouse` 仍 warp 真实光标、~7/60 间歇失败。
- 提交前基线：`ya-testing` 1708/1708、`ya-gui-closure-test` 641/641、`Script/automation/2d-rpg/` 七个脚本全过（`rpg_control.py` 是库不是入口）。

## 2026-10-07 — PB1 收口与两条新计划项

- PB1 完成（严格审查 + 一次返工）：`--benchmark <config.json>` + `Core/Profiling` 聚合器（`IBenchSource` 注册接口给 PB2–PB4）；`bench_summary.json`（meta / frame 窗口统计 / cpu_stages）；旧 last-frame 汇总删除；GPU 整帧时间戳回读改到 fence 等待之后（原实现几乎恒 `VK_NOT_READY`，`gpuMs` 冻结）。返工要点：`--cpu-profile` 去 CLI 默认值，`cpuTrace: false` 真正生效（此前三次 p50 是 trace 开着测的，作废）。
- 入库口径（HelloMaterial，profile，trace off，immediate，uncapped，600 采样）：三次 frame p50 = 8.673 / 8.355 / 8.539 ms，相对均值最大偏差 1.96%（验收 ±3%）；全量 `ya-testing` 1713/1713。
- 已知边界（perf-baseline 记录）：固定 dt 未覆盖脚本 `elapsedSeconds` 与渲染 host clock（墙钟驱动动画不按帧对齐）；采样窗口内 `FileWatcher::poll` 仍每帧跑；summary 写失败退出码未做实机复现。
- 新计划项：W4（profile 构建下 5 个 GUI 测试栈损坏，PB1 复核发现，P1，GUI 线排查）；S5（脚本 native 注册重构：模板签名推导 + 反射化函数库，用户 2026-10-07 拍板，P1，波 3 开头、先于 G3.2，与 C3 串行）；GA1 附带修 `Example.xmake.lua` 大小写路径（全新 checkout 链示例失败）。
- S5 方案问答闭环（2026-10-07）：路线定为「单一注册表条目 + 双调用约定」（SOTA 形态：签名进 `ClassRegistry`、调用体走 ScriptValue 跳板，热路径不过 any 装箱）；注册时序 = GameRuntime 集中清单；迁移 = 试点先行。子计划 `script-native-registry/` 已建。
- S5 升级（2026-10-08）：S5.1 审查暴露反射层结构问题，用户决定反射 + 脚本整体规范化，新子计划 `reflection-runtime/`（RR0–RR8），原 `script-native-registry/` 暂停并入 RR8；仍为 P1、先于 C3 / G3.2。

## 2026-10-09 — M4 / PB3 GPU 显存统计收口

- 领任务时核对冲突面：`reflection-runtime` 线在改 Script / 反射 / PropertyAccessor / 一批编辑器文件，PB2 线在改 `BenchmarkRun.*` / `PerfBenchSources.*` / `RuntimeProfilingSection.*` / `Profiling.h`。M4 落在 `Vulkan*` 后端 + `RHI/include` + `Core/Profiling` 汇总器，与两者零文件重叠。
- 实现要点与验证见 `../perf-baseline/progress.md`（PB3 条目）：RHI 公开 `EGpuMemoryClass` / `FGpuMemoryStats` / `GpuMemoryBenchSource`，Vulkan 按 allocation 记账 + `vmaGetHeapBudgets`，`gpu_memory` 段由设备自注册，编辑器新增 `RuntimeGpuMemorySection`。
- 一处偏离计划 §3 M4 的措辞：面板没有并进 profiling 面板，而是 Runtime Tools 里独立一段——那段本轮被 PB2 线占用，等它收口后可以合并。
- HelloMaterial 实跑（debug）出数：总 806.58 MiB，VMA 自报 allocationBytes 与分类求和一致；MoltenVK 无 `VK_EXT_memory_budget`，budget 降级为 heap size（已在类型注释里写明）。
