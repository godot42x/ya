# Showcase Roadmap — 项目级总计划

2026-10-07 立项。这是**伞形计划**：只定目标、优先级、依赖和执行规则，不写实现细节。每条工作流开工时，要么挂到已有活跃线，要么新建子计划目录（五件套 + 自己的问答门禁），细节在子计划里定。

## 0. 目标

面向 2027 年春的系统岗面试（偏 UI，目标高薪），用一个引擎同时证明：gameplay、编辑器工具、引擎与渲染、C++ 基础、架构能力、解决问题的能力。

终态验收（全部 P0 完成即达成）：

1. **2D demo 可完整玩通**：RPG 系统齐（背包 / 任务 / 存档 / 商店 / 对话），数据驱动，打包后在干净机器上运行。
2. **3D demo 可完整玩通**：第三人称探索解谜，能撑起渲染展示（多光源阴影、CSM、IBL、后处理），打包后运行。
3. **性能有数字**：同一条基准命令在 macOS（开发机）与 Windows 独显机上产出前后对比表（帧时间均值 / p95、pass 级 GPU 时间、GPU 显存、进程内存——后两者分开统计）。
3b. **自研 GUI 框架以独立应用展示**：gui profile 构建的 GUIWorkbench 打包可运行，交互场景与像素门禁全绿。
4. **已知半成品收口**：§3 R 组 P0 项全部完成，文档不再声称不存在的能力。
5. **可讲清楚**：每条完成的工作流留一份吸收笔记（§5.4），面试要讲的子系统有设计文档与架构图。

## 1. 已拍板的决策（2026-10-07）

| 议题 | 决定 |
| --- | --- |
| 时间线 | 不设硬截止。全部排出来，按 P0 / P1 / P2 分档；靠 agent 并行推进，用户最后吸收（吸收按 §5.4 随工作流收口时产出，不压到最后）。 |
| 渲染缺口范围 | 全收：测量设施、frames-in-flight 1→2、RDG 编译缓存与 pass 剔除、transient 纹理别名、点光源阴影 GPU 剔除、CSM、后处理补齐、async compute、bindless；另加独立渲染线程、内存与显存统计和优化、压测。 |
| OpenGL | 推迟。源码保留，文档改正（现在不参与编译）。bindless / async compute 等新能力走 RHI 能力开关并保留降级路径，不堵死以后的 GL 或低端后端。 |
| 多线程渲染 | 指**独立渲染线程**：游戏线程与渲染线程流水线化，提交一帧快照交接。 |
| 性能测量平台 | 两边都量：macOS 日常开发，Windows 独显机出正式数字（Windows 由用户执行同一条命令回传结果）。 |
| 3D demo | 先做第三人称探索解谜（A）；进度允许再扩展到轻动作（B：战斗、敌人 AI、动画状态机、受击反馈）。素材用公开资产（按许可证）加自制。 |
| 2D demo | 先还现有待办和遗留债，再做背包 / 任务 / 存档 / 商店等系统，打包成可完整玩通的 demo。 |
| GUI 活跃线 | 纳入总计划，按优先级排（§3 U 组）。 |
| 纯 GUI app 线 | 2026-10-07 用户补充：`ya_profile=gui` + GUIWorkbench 是自研 GUI 框架的独立产品线，单列 GA 组。 |
| 内存口径 | 2026-10-07 用户强调：GPU 显存与进程内存是两个东西，分成 M4 / M8 两项分别统计、分别优化。 |
| 音频 | 先定引擎自己的音频接口，后端接 miniaudio 快速出效果（P1）；自研混音器与空间化在同一接口下替换（P2）。不走 OpenCL（混音是低延迟小数据 CPU 工作）。 |

## 2. 原则

1. **不平行造计划**：已有活跃线覆盖的内容挂到那条线推进，本计划只登记优先级和依赖；新内容才新建子计划目录。
2. **先量后改**：M 组基准设施先落地，之后任何性能相关改动都必须给出同一条基准命令的前后数字，写进对应 progress。
3. **demo 驱动引擎**：demo 需求逼出的引擎改动优先；与 demo 和面试叙事都无关的打磨降档。
4. **能力开关 + 降级路径**：新 GPU 能力（bindless、async compute、descriptor indexing 等）经 RHI capability 查询启用，缺失时走现有路径，并有测试覆盖两条路径。
5. **诚实文档**：代码里没有的能力不写进 AGENTS / skill / README。

## 3. 工作流与优先级

优先级含义：**P0** = 两个 demo 可玩 + 有性能数字 + 半成品收口所必需；**P1** = 显著加强叙事或工程质量；**P2** = 持续演进。

### W — 收口与文档（P0，小项）

| id | 内容 | 优先级 |
| --- | --- | --- |
| W1 | 根 `AGENTS.md`「兼容 OpenGL」等过时声明改正；`render-arch` skill 里已不存在的符号（如 `RenderFrameCoordinator`）改正 | P0 |
| W2 | B2 遗留：`MethodCall::arguments` 吞普通表收窄为 owner 参数；`bindSelf` 旧注释；`ScriptProperty::serializedValue` 未用；属性预览只建不销；`Npc.lua` 的 `world.find("Player")`；`spawnSprite` 默认贴图写死；自动化按名字 `Player` 找实体；`GridMoverTest.lua` 未进 gtest | P0 |
| W3 | `GUIWindowManagerTest.IsolatesPointerFocusCaptureTooltipDpiAndSnapshot` 间歇失败定位 | P0 |
| W4 | profile 构建下 5 个 GUI 测试栈损坏崩溃（`UIFrameSnapshotTest` 4 例、`ComposeClipReplayTest.NestedBuilderClipFlattensToOneClipRun`；debug 全过）。疑似 profile-only 栈溢出 / UB——基准跑在 profile 上，这条线要干净。PB1 复核时发现，归 GUI 线单独排查 | P1 |

### M — 测量、压测与内存（P0 起步）

| id | 内容 | 优先级 |
| --- | --- | --- |
| M1 | pass 级 GPU timestamp（RDG executor 逐 pass 打点，结果按 pass 名汇总） | P0 |
| M2 | 统计窗口：`bench_summary.json` 写窗口均值、p50、p95、p99，而不是最后一帧 | P0 |
| M3 | 基准模式：固定分辨率、关垂直同步、去帧率上限、丢弃预热帧，一条命令跑完出表 | P0 |
| M4 | **GPU 显存**统计：VMA heap budget / usage、按资源类别（贴图、render target、transient、buffer、上传区）汇总，进 summary 与编辑器面板 | P0 |
| M8 | **进程内存**统计：RSS / 峰值（平台计数）、堆分配按子系统归类（分配追踪），进 summary 与编辑器面板。与 M4 是两套口径，不合并；Apple Silicon 统一内存下两者共享物理内存，报告里分别列出并注明 | P0 |
| M5 | 压测场景：多点光源多投影体、大量网格、大 tilemap；固定相机路径，可重复 | P0 |
| M6 | 基准报告脚本：before / after 两份 summary 生成对比表（markdown） | P0 |
| M7 | 显存与进程内存优化（分别以 M4 / M8 数字为依据：transient 别名、上传缓冲复用、贴图压缩 / mip 策略等） | P1 |

### R — 渲染半成品与缺口

| id | 内容 | 优先级 | 归属 |
| --- | --- | --- | --- |
| R1 | frames-in-flight 1→2：只等当前 flight 的 fence、双槽资源真正轮转、延迟删除队列跟随 flight 数 | P0 | `render-view-family` M4 |
| R2 | 点光源阴影 GPU 剔除 + indirect 修稳并默认开启（含历史回归复测） | P0 | 新子计划 |
| R3 | CSM 多级 cascade 修稳并默认开启（稳定性、cascade 过渡、阴影抖动） | P0 | 新子计划（可与 R2 同一条阴影线） |
| R4 | RDG 编译缓存（拓扑不变跳过 compile）与 pass 剔除（无消费者的 pass 不执行） | P1 | `rdg-cache-dx` 或新子计划 |
| R5 | RDG transient 纹理帧内别名复用 | P1 | 同 R4 |
| R6 | 后处理补齐：SSAO 模糊、FXAA（P1）；TAA（需 motion vector 与 jitter，P2） | P1 / P2 | 新子计划 |
| R7 | 独立渲染线程：游戏线程产出帧快照，渲染线程录制与提交；前置 R1 与 `render-view-family` P3 的 `PreparedView` | P1 | 新子计划 |
| R8 | RHI 能力矩阵：capability 查询、降级路径约定（R9 / R10 的前置） | P1 | 新子计划 |
| R9 | bindless / descriptor indexing（能力开关 + 降级） | P2 | 同 R8 线 |
| R10 | async compute（独立 compute 队列，跨队列同步进 RDG；能力开关 + 降级） | P2 | 同 R8 线 |
| R11 | `rdg-cache-dx` P3–P5（Bloom 切片、资源容器按生命周期拆分、Slang 生成 descriptor layout） | P1 | `rdg-cache-dx` |
| R12 | `render-view-family` P2 / P3（命名收敛、`PreparedView` / ViewFamily compiler）；纯 2D View 不跑 3D stage | P1 | `render-view-family`、`rpg-prototype` 步骤 6 |
| R13 | `render-application-boundary` 剩余项 | P2 | `render-application-boundary` |
| R15 | 延迟管线支持聚光灯（`LightPass` 光源数据加方向 / 锥角），点光源上限按延迟路径理顺（现为 `MAX_POINT_LIGHTS=6`，`MAX_POINT_LIGHTS_DEFERRED=32` 未被使用）；G3.9 依赖 | P0 | 新子计划（可并入阴影线） |
| R14 | OpenGL 后端恢复（或其它低端后端），基于 R8 的能力矩阵 | P2（有真实需求再开） | — |

### A — 音频

| id | 内容 | 优先级 |
| --- | --- | --- |
| A1 | 音频接口（设备、声音资源、播放实例、总线音量）+ miniaudio 后端 + 音频组件 + 脚本 API（中立注册层） | P1（两个 demo 的 P0 体验依赖它，实际排在 demo 打包之前） |
| A2 | 自研混音器与空间化（衰减、声像、多普勒、流式解码），输出走 SDL3 audio stream，替换 A1 后端 | P2 |

### G2 — 2D demo（`rpg-prototype` 线）

| id | 内容 | 优先级 |
| --- | --- | --- |
| G2.1 | 现有待办：相机 2D 字段（每单位像素数、像素吸附、整数缩放）+ 相机跟随组件、GUI 文本逐字显示 | P0 |
| G2.2 | 存档 / 读档（引擎级：场景运行态 + 玩法状态的序列化契约） | P0 |
| G2.3 | 背包、任务、商店、对话分支，数据驱动（配置表 + UI 绑定） | P0 |
| G2.4 | 内容：一条完整流程（城镇 → 迷宫 / 解谜 → 收尾事件 → 结局），约 10 分钟 | P0 |
| G2.5 | 打包到干净机器可运行 + 录屏 | P0 |
| G2.6 | 纯 2D View 家族（随 R12） | P1 |
| G2.7 | tilemap 静态实例缓冲（`rpg-prototype` 步骤 5c 的触发条件满足时） | P2 |

### G3 — 3D demo（新子计划）

G3.0 盘点（2026-10-07，读代码核实）：渲染侧（CSM 最多 4 级、IBL、SSAO / Bloom、glTF PBR 材质、KTX2 转码）大体够用；差距在玩法层。物理只有 Static / Dynamic + 固定尺寸 box / sphere（不读缩放、局部坐标），接触监听未注册，无传感器 / 射线 / 角色控制器 / 脚本 API；`GltfImporter` 不读 skin / animation / 节点层级（只有 assimp 格式带骨骼），动画只能单 clip，`MAX_BONE_COUNT=64`；InputMap 只有数字量动作，无鼠标轴 / 摇杆 / 手柄；无跟随相机；无 prefab、世界计时器、存档、音频。

| id | 内容 | 大小 | 优先级 |
| --- | --- | --- | --- |
| G3.0 | 子计划设计：玩法流程、关卡与素材来源 / 许可证、下列引擎项的方案问答 | — | P0 |
| G3.1 | 物理组件重设计：Kinematic；box / sphere / capsule 尺寸参数、convex / mesh（来自模型）；读世界变换与缩放；传感器；接触事件进 ECS | L | P0 |
| G3.2 | 物理脚本 API：`raycast` / `shapecast`、施力 / 速度 / 传送、`onTriggerEnter/Exit` 到实体脚本 | M | P0 |
| G3.3 | 角色控制器（`JPH::CharacterVirtual`）组件与系统，移动 / 跳跃 / 贴地 | M | P0 |
| G3.4 | glTF skin + animation + 节点层级导入（复用 `ImportedSkeletonData`）；评估骨骼上限 | M | P0 |
| G3.5 | 动画按名字播放 + 双 clip 交叉淡入 | M | P0 |
| G3.6 | 第三人称相机（跟随 + 弹簧臂 + 碰撞避让）；InputMap 补鼠标轴 / 轴值 | S/M | P0 |
| G3.7 | 运行时生成模型 / prefab（`world.spawn(path)`）、世界计时器 | S/M | P0 |
| G3.8 | 交互与解谜内容：触发器、可交互物、物理谜题、门 / 机关 | M | P0 |
| G3.9 | 关卡与光照：多光源区域或昼夜，展示 CSM / 点光源阴影 / 聚光灯 / IBL / 后处理；公开资产 + 自制 | M | P0 |
| G3.10 | 编辑器：物理形状参数 inspector 与调试线、模型摆放 | S | P1 |
| G3.11 | 性能叠加层（M 组数据实时显示）+ 打包 + 录屏 | S | P0 |
| G3.12 | 轻动作扩展：动画状态机与 root motion、手柄、命中盒与查询过滤层、世界空间 UI（血条） | L | P2 |

### U — GUI 与编辑器活跃线（初排，开工该线时复核）

| 线 | 优先级 | 理由 |
| --- | --- | --- |
| `ui-behavior-capabilities`（C1–C3 完成） | P0 | 排在 game-ui S4 之前，2D demo 的背包 / 商店 / 对话 UI 依赖 |
| `game-ui-script-framework`（下一步 S4 条目模态与取消路由） | P0 | 2D / 3D demo 的模态界面依赖 |
| **S5 反射 + 脚本层整体规范化**（2026-10-08 用户拍板升级；子计划 `reflection-runtime/`，吸收原 `script-native-registry/`，S5.1 已入库）：开放 MetaValue + TypeOps 每类型行为表放 `reflects-core`（零依赖），五处消费方（序列化 / PropertyAccessor / 拷贝 / 编辑器检查器 / 脚本）的按类型 if 链收进 TypeOps；`Function::invoker` 收发 MetaValue，`ScriptValue` 与 `std::any` 外部 ABI 退场；最终成员视图下沉 reflects；动态注册带版本号与变更通知；脚本 native 剩余 9 个注册点在新 ABI 上迁移 | P1 | C3（Lua 调用热路径）与 G3.2（物理脚本 API，几十个函数）都建在脚本绑定上，必须排在两者之前 |
| `font-framework-convergence` | P1 | 文本逐字显示、CJK 显示质量 |
| `editor-undo-redo` | P1 | 编辑器工具叙事 |
| `gui-framework-editor-readiness`（Phase 8 移除 ImGui 双栈） | P1 | GUI 框架成熟度的核心证明 |
| `gui-invalidation-architecture`、`gui-style-system-convergence`、`gui-anchor-to-slot` | P1 | 剩余项少，收口成本低 |
| `gui-framework-editor-runtime-refactor`、`gui-kernel-ux-parity`、`gui-editor-structure` | P2 | 开放项多，与 demo 弱相关 |
| `source-layout-subtraction`、`editor-ui-grouping`、`display-compose-encoding` | P2 | 结构整理 / 语义尾巴 |

### GA — 纯 GUI app 线（自研 GUI 框架独立产品形态）

`ya_profile=gui` 只构建 GUI 模块 + 最小 host（不带 3D、脚本、物理包）；产品是 `Example/GUIWorkbench`（Feature Gallery，`FWorkbenchSurface` 挂 GUIApp，`Scenarios/*.jsonl` 交互场景，`run_workbench_gpu_parity.py` 像素门禁）与 `Example/GUIFrameworkSmoke`。U 组各 GUI 线的能力在这里以独立应用验收。下表是初排，GA0 盘点后按子计划问答定稿。

GA0 盘点（2026-10-07，`ya-reviewer`）：GUI 框架依赖边界干净（GUI / Render2D / Workbench 源码不 include Scene / ECS / Render3D / Resource / Script / Physics；残留：`ya-rhi` public 带 entt 包、`ya-gui-host` 只能走 Vulkan swapchain）。门禁是零散单点：31 个 `Scenarios/*.jsonl` 没有批量入口；`run_workbench_gpu_parity.py` 只覆盖 Widgets 一页、比的是同帧 GPU 与离屏一致性；仓库里没有提交像素基准图（`Baselines/` 只有 5 份结构化快照，`gui_style_baseline.py` 写死 `build/macosx/arm64/debug`）；`ya.py package` 只认 `.yaproject`，GUI host 按仓库相对路径加载 shader 与字体，没有打包路径；Windows 在发布检查表里是 Blocker。**已核实阻塞**：`Example/Example.xmake.lua` 无条件 include 依赖 `ya-engine` 的 3D 示例（根 `xmake.lua` 注释声称的守卫不存在），gui profile 很可能配置不过；文档最后一次记录 gui profile 跑绿是 2026-08-09。`gui-framework` skill 里的 `make test-gui` 不存在。

gui profile 的构建必须在独立 worktree / 构建目录里做：在共享工作区 `xmake f --ya_profile=gui` 会改掉其他 worker 正在用的配置。

| id | 内容 | 优先级 |
| --- | --- | --- |
| GA0 | 盘点（已完成）；子计划方案问答 | P0 |
| GA1 | gui profile 恢复可配置可构建：3D 示例 include 守卫、`ci.sh gui-shared` 补 headless-host-test 与 `--smoke-actions`；修文档漂移（`make test-gui`、closure-test「唯一 target」注释、「21 场景」）；**附带修 `Example.xmake.lua` 写 `2DRpgPrototype` 而目录是 `2dRpgPrototype` 的大小写路径**（PB1 复核发现：大小写不敏感盘上 `Entry.cpp` 被编两次，全新 checkout 链示例失败） | P0 |
| GA2 | 一条命令门禁：headless 批量跑全部场景断言 + windowed GPU / 离屏一致性；基线脚本路径可移植；结构化 digest 作跨平台主门禁，像素基准按平台分存（MoltenVK 与原生 Vulkan 像素不保证一致） | P0 |
| GA3 | GUI 帧开销与内存数字：windowed CPU / GPU 帧时间、进程内存（M8 口径）、显存（M4 口径） | P0 |
| GA4 | 打包成独立应用：不依赖仓库根的资源根、预编译 shader 或带 Slang 运行时、字体、MoltenVK / SDL 动态库；macOS `.app` 与 Windows 目录布局 | P0 |
| GA5 | Windows 跑绿（MSVC 构建 + 全部场景），需要用户的 Windows 机器 | P1 |
| GA6 | 框架独立性证明：D1 依赖图、对外 API 文档；`ya-rhi` 的 entt 泄漏 | P1 |

### D — 架构文档与吸收

| id | 内容 | 优先级 |
| --- | --- | --- |
| D1 | 模块分层与依赖图：从 xmake target 依赖自动生成，进仓库并在 CI / 脚本里可重生成 | P0 |
| D2 | 设计文档：脚本后端拆分（B2）、GUI 框架（retained / 失效 / 布局契约）、render graph；写的过程中把这三处不合理的地方重构掉 | P0 |
| D3 | 设计文档：渲染线程与帧时间轴（R1 / R7 之后）、音频、存档契约 | P1 |
| D4 | 吸收材料汇总：README、两段 demo 视频、性能报告、面试问答清单 | P0（最后一步） |

## 4. 依赖与波次

```
W1/W2/W3 ─┐
M1..M6 ───┼──► R1 FIF ──► R7 渲染线程
          │      │
          │      └──► R2/R3/R15 阴影与光源 ──► G3.9 关卡光照
          │
          ├──► R4/R5 RDG ──► M7 内存优化
          │
          └──► R8 能力矩阵 ──► R9 bindless / R10 async

U P0 (C2 → S4) ──► G2.3 系统 UI ──► G2.4 内容 ──► G2.5 打包
G2.1/G2.2 ────────┘
S5 (反射 + 脚本规范化) ──► C3 / G3.2
A1 ──► G2.5 / G3.11
G3.0 ──► G3.1 ──► G3.2/G3.3 ──► G3.6 ┐
G3.0 ──► G3.4 ──► G3.5 ─────────────┼──► G3.8 ──► G3.9 ──► G3.11
G3.0 ──► G3.7 ──────────────────────┘
各线收口 ──► D2/D3 ──► D4
```

波次（同一波内可并行；跨波按依赖）：

1. **波 1**：W1–W3；M1–M6、M8；G3.0 子计划设计（只出文档）；GA0 盘点、GA1；U 组 `ui-behavior-capabilities` C2。
2. **波 2**：R1、R2、R3、R15；G2.1、G2.2；`game-ui-script-framework` S4；A1。
3. **波 3**：G2.3、G2.4；**S5（反射 + 脚本规范化，`reflection-runtime/`）→ C3 串行，均先于 G3.2**；G3.1–G3.7（引擎项，可多 worker 并行，按冲突面分配）；R4、R5、R6（P1 部分）；R11。
4. **波 4**：G3.8、G3.9、G3.11、G2.5；R7；R12；D1、D2；U 组 P1。
5. **波 5**（P2）：R9、R10、R6-TAA、R13、M7 余项、A2、G3.12、G2.7、U 组 P2、D3。
6. **收尾**：D4。

## 5. 执行规则

1. **开工即落子计划**：一条工作流开工时，先确定归属（已有线或新子计划目录），新目录要有五件套（plan / progress / todo / feature_matrix.json / session_checklist），并按 `../AGENTS.md` 的门禁问到 95% 把握再动手。本文件 `todo.md` 只记状态与指针。
2. **并行冲突面**：`AppLifecycle.cpp`、`RenderDeviceState*`、`RenderGraphExecutor.cpp`、`ScriptValue.h`、各 target 的 `xmake.lua` 同一时间只允许一个 worker 改；派发时写清每个 worker 拥有的文件。
3. **性能结论必须带数字**：M 组落地后，R / M7 / G2.7 的每个提交在 progress 里贴同一条基准命令的前后表；Windows 数字标注由用户回传。
4. **吸收笔记**：每条工作流收口时写 `absorb.md`（或子计划内同名节）：问题是什么、选了什么方案、放弃了什么、关键代码位置、面试可能被追问的 5 个问题和答案要点。D4 只汇总，不从零写。
5. **提交**：一个可验收目标一个提交，计划文件改动与代码同一提交；只按显式路径 stage；格式 `[module] message`。
6. **派发**：subagent 按 `../../skills/subagent-delegation/SKILL.md` 按线复用，每次派发带 `派发：` 声明。

## 6. 本文件的维护

- 子计划开工、收口、改优先级时，同步更新本目录 `todo.md` 与 `feature_matrix.json`，并在 `progress.md` 记一条。
- 若某项在子计划问答中被推翻或降档，在 §1 追加决策行，不删旧行。
