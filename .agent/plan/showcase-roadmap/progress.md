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

## 2026-10-09 — M8 / PB4 进程内存统计（RSS / 峰值）

- 紧接 M4 领的另一半：两者是用户 2026-10-07 明确要求分开的两套口径，M4 只完成了其一。
- `Core/Os/OsMemory`（`sampleProcessMemory` / `hasUnifiedMemory`）+ `ProcessMemoryBenchSource`（`process_memory` 段，由 `BenchmarkRun` 注册，与 PB2 的源同处 `Core/Profiling`）；编辑器 `RuntimeGpuMemorySection` 泛化为 `RuntimeMemorySection`，显存与进程内存并列并注明统一内存下不可相加。
- 范围按用户选的 A：堆分配按子系统归类（profile 构建下的全局分配钩子）挪到 PB6。
- 详见 `../perf-baseline/progress.md`（PB4 条目），含一个值得讲的观察：Apple Silicon 上 GPU 显存 806 MiB 与进程 RSS 253 MiB 既不能相加也不能相减——MoltenVK 的 private-storage 资源占同一块物理内存但不映射进进程。

## 2026-10-09 — M6 / PB6 报告脚本 + worktree 文档维护

- `Script/perf/bench_run.py`（一条命令跑一组场景）+ `bench_compare.py`（两份 summary → markdown 对比表，含 meta 不一致告警）。详见 `../perf-baseline/progress.md`。
- **首份 macOS profile 基线已入库**：`perf-baseline/baselines/macos/profile-15277c00/`（15 次运行 / 9 场景，干净 checkout）。三条结论：2D 合批 10 倍精灵帧时间不变（8.365 → 8.344 ms）；3D 网格梯度是提交瓶颈（1k/5k/10k = 8.33 / 32.56 / 63.45 ms，gpu 只有 2.94 / 8.57 / 14.75 ms）；点光源梯度被 `MAX_POINT_LIGHTS=6` 截断，测不出真实曲线。
- 并发构建会毁掉数字（同场景两次 frame p50 差 87%），正式基线必须单独跑。
- 顺带维护了 worktree 相关文档（`ya-build` skill「共享缓存」段）：补了**开独立 worktree 的配方**（此前只写了共享机制、没写怎么建）、**三方库复用口径**（xmake 包在 `~/.xmake/packages` 全局共享、worktree 零重下；真正随 worktree 线性增长的只有 `build/`，一份约 11 G），以及两条实测坑：构建前先 `df -h`、删 worktree 前先 `chmod -R u+w`（三方目录是只读的）。
- PB4 余项（堆分配按子系统归类）**未做**：计划里的「全局分配钩子」与本工程链接形态不匹配（每模块一个 dylib，macOS 两级命名空间下替换 `operator new` 拦不到别的 image），要换方案再开工。
- 实测确认：重型 submodule（`Vulkan-Samples-Assets` ~1G、`LearnOpenGL` ~250M）与 Vulkan SDK 在 worktree 里都是指回主项目的链接 + skip-worktree，`git status` 干净，全程无下载；小 submodule（ImGui / ImGuizmo / log.cc / utility.cc）各处保持标准 git。

## 2026-10-09 — PB2 收口（M1 pass 级 GPU timestamp）

- M1 完成：`RenderGraphExecutor` 逐 pass 括 `beginGpuPass/endGpuPass`，Vulkan 侧独立的 pass timestamp query pool 按 flight 延迟回读（挂在 fence 等待之后，`VK_NOT_READY` 跳过不等），`gpu_passes` 段进 `bench_summary.json`，编辑器 profiling 段有 pass 表；同 checkpoint 带 `render_counters`（draw / dispatch / 绑定 / 2D 合批）。
- 测量默认关：`RuntimeState.passGpuTimingEnabled` 默认 false，只有基准采样窗口内才开（结束还原），编辑器对应 `profile.runtime.passGpuTiming` 配置键。
- 两个真 bug 由实跑数据抓出来（单测抓不到）：`endGpuPass` 把结束时间戳写回开始那个 query index 导致整帧 `VK_NOT_READY`；回读失败时 pass 名字不清，几帧就撞上限。详见 `../perf-baseline/progress.md` 本节。
- 验证：`ya-testing` debug 1764/1764（新增 4 例）；HelloMaterial 完整基准 17/17 个 pass 有数（count 全 600），pass p50 求和 4.04 ms 对整帧 gpuMs p50 4.51 ms。
- 复查返工（同一提交序列的后续提交）：`render_counters` 的清零原先挂在采样点，预热帧累积量被第一帧吞下，max 被撑大 120 倍（max/p50 精确等于 `warmupFrames`）；清零改到帧边界 `VulkanRender::beginRecordedFrame`，实测比值降到 1.0x。另删掉一个零读取的死状态 `_bPassGpuTimingFrameOpen`，并把 `passGpuTimingEnabled` 的「默认关」写成有编译期条件的契约（它在 `ECompileMode::Enabled` 下会被 `normalizeRuntimeToggle` 压成常开）。`ya-testing` 1767/1767。
- 未达成：M 组验收的三次 p50 ±3% 本轮没实测——本机 debug 构建且有并行编译，噪声大于信号；正式数字按 M6 / PB6 在 profile 口径下出。

## 2026-10-11 — GA1 gui profile 可配置可构建

- 3D 示例（HelloMaterial / GreedySnake / 2DRpgPrototype / PerfLab）只在 `ya_profile ~= gui` 时 include。GUIFrameworkSmoke 与 GUIWorkbench 两个 profile 都在。
- `Example.xmake.lua` 的示例路径改成目录名 `2DRpgPrototype`。另一种大小写在大小写不敏感盘上是第二个 include，`Entry.cpp` 会进链接两次。全新配置只登记这一条源文件。
- `Script/ci.sh` 的 `gui-shared` 补上 `ya-gui-headless-host-test` 和 `GUIWorkbench --smoke-actions`。
- 文档：`gui-framework` skill 去掉不存在的 `make test-gui`，改成 closure / widgets / headless-host / workspace 四条；`Test.xmake.lua` 不再把 closure test 写成 gui profile 里唯一的测试；场景数按目录写成 31。
- 验证在独立 worktree（`/tmp/ya-gui-ga1`，不改共享工作区的 engine 配置）：`ya_profile=gui` 配过；`ya-gui-minimal-host`、`GUIWorkbench`、`ya-gui-headless-host-test` 链接通过。共享工作区仍是 engine profile。

## 2026-10-11 — W5 目录名 2DRpgPrototype

- git 索引与磁盘目录项都是 `Example/2DRpgPrototype`。`2DRpgPrototype.yaproject`、`2DRpgPrototype.yamodule`、`Source/2DRpgPrototype.h` 与之一致。
- `Example.xmake.lua` include `./2DRpgPrototype/xmake.lua`。代码、测试、脚本、PerfLab 精灵场景里的贴图路径都是这一种大小写。场景重存基线的路径键跟着改。贴图路径写进场景正文的（Sprites_500 / 2000 / 5000、PerfLab town tileset），哈希按新正文更新。同一测试里四份 UI 文档的哈希已和当前文件对不上（文档在基线锁定之后改过），一并刷新。
- 未跟踪的 `Content/UI/Panel*.yaui.json` 留在这个目录下，不入库。
