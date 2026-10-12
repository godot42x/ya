# RPG Tactics：以 2D 战旗 RPG 驱动引擎 2D 能力

2026-10-13 改版。v1（RPG Maker 式俯视小镇跑腿闭合）已落地，工件保留在
`plan-v1-landed.md` / `progress-v1-landed.md` / `todo-v1-landed.md` /
`feature_matrix-v1-landed.json` / `session_checklist-v1-landed.md`，本文件起接管本线。

## 0. 目标与结论

重构 `Example/2DRpgPrototype` 的主流程：从「城镇跑腿闭环」改为**可玩通的 2D 战旗 RPG
垂直切片**（tile 棋盘 + 回合制 + 任务 + 成长养成），并以此驱动引擎补齐四块 2D 子系统。

用户拍板（2026-10-13）：

1. **形态**：RPG，不断做任务、打怪的流程。不绑死城镇，内容可全新编排。
2. **战斗**：战旗战术（SRPG）——格子地图、多单位、按速度出手、移动 + 行动。
3. **成长养成**：等级 + 属性成长、装备、技能/职业、队伍/同伴（四项全要）。
4. **视觉**：后处理驱动（bloom / 色调映射 / 灰度 / 过渡做玩法视觉）。
5. **新增子系统**（四项独立大件）：后处理运行时控制、音频、粒子、2D 物理/触发器。
6. **引擎改动落点**：走中立脚本注册层 + 新 target，不进 GameRuntime 硬编。
7. **规模**：垂直切片——枢纽 + 战斗区 + 地牢，3–4 场战斗，3 个任务，1 个 boss。
8. **验收重点**：能力覆盖优先——每个目标能力有可演示、可指认的使用点（`feature_matrix.json` 可审计）。
9. **素材**：不再依赖 Tiny Town 小镇图；用 Kenney（CC0）或 image-gen 生成。本机 `grok-cli` 未安装，
   生成走 `image-gen` skill（本地 ComfyUI 优先 / Pollinations 兜底）；用户未来会自建本地 ComfyUI。

方法论不变（继承 v1 §0.5）：**每个引擎改动必须写明是被哪个 checkpoint 的哪段游戏需求逼出来的**；
写不出来就不做。但引擎能力只是游戏的子集——游戏内优先用 2D feature，不为演示而演示。

## 1. 现状基线

### 1.1 v1 已落地、本计划直接复用（不重做）

| 能力 | 现状 | 锚点 |
| --- | --- | --- |
| 脚本中立绑定层 | 反射驱动的 `ScriptValue` / `findField` / `findMethod`，Lua + JS 双投影，无 sol2 泄漏 | `Core/Scripting/ScriptBindings.*` |
| Tilemap | `TilemapComponent`（多图层 + zOffset + layerOffset + bYSort）、`.yatileset.json`、编辑器笔刷、`worldToCell/cellToWorld/isSolid/bounds` | `Scene2D/TilemapComponent.h` |
| 精灵与排序 | `Sprite2DComponent`（pivot/uvRect/tint/layer/sortOrder/bYSort/pickId）、画家顺序 + y-sort | `Scene2D/SpriteDrawOrder.h` |
| 帧动画 | `SpriteAnimationComponent` + `.yaanim.json` | `Scene2D/SpriteAnimationComponent.h` |
| 2D 渲染 | 世界精灵实例化合批、纹理表、`Sprite2DFeature`（天空盒后、Bloom 前） | `Render3D/Deferred/Features/Sprite2DFeature.h` |
| 像素完美相机 | `CameraComponent._pixelPerfect` + 整数缩放 + 提取期 texel 吸附 | `CameraComponent.h` `resolveCameraViewFraming` |
| 玩法脚本面 | `world.find/viewSize/viewAspect/loadScene/save/load/saveRestored/spawnSprite/destroyEntity/scriptOf`、`ui.get`、`map:entityAt` | `GameplayScriptFunctions.cpp` |
| 网格行走 | `Engine/Content/Lua/GridMover.lua`（按格移动、朝向、`blocksEntry`、`onPlayerEnter`） | 同上 |
| 场景转移 + 跨场景状态 | `world.loadScene(path, spawnName, actorName)`、`Persist` 常驻表 | `2d-gameplay` skill |
| 存档 | `world.save/load`（场景路径 + `Persist`，脚本 `onSave`） | `AppSceneServices::saveWorld` |
| GUI 框架 | ~35 控件（Button/Text/Panel/TabBar/TreeView/TableGrid/Slider/CheckBox/TextEdit/ScrollViewport/Image/RadioButton/Switch…），`.yaui.json` + 控件 Lua 脚本（`self:find/every/after`、`onInit/onUpdate/onCancel/onShow/onHide`、`onClicked:add`） | `GUI/Widgets/Controls/`、`LuaWidgetHandle.cpp` |
| InputMap | 命名动作、`input:isActionDown/isActionPressed` | `Example/2DRpgPrototype/InputMap.json` |
| 编辑器 2D | 正交 XY 视口、tile 笔刷、Tile Palette、动画集编辑页签 | `EditorTilePaletteTab` 等 |

### 1.2 引擎缺口（本计划要补）

| # | 缺口 | 现状 | 本计划 |
| --- | --- | --- | --- |
| N1 | 后处理运行时控制 | 已有 bloom / tone-map / 灰度 / 边缘 / 噪点的 pass，`PostProcessingState` 由**配置文件**与 `RenderPipelineSettings` 驱动；无脚本 API、无过渡 | Wave C（R1） |
| N2 | 音频 | **完全没有**（无设备、无资源、无组件、无脚本 API） | Wave C（A1） |
| N3 | 粒子 | **完全没有** | Wave C（X1） |
| N4 | 2D 物理 / 触发器 | Jolt 只有 3D box/sphere，无 trigger、无脚本回调、无 2D | Wave C（P1，Box2D） |
| N5 | 战术层能力 | 无回合调度、无寻路/可达范围、无单位/队伍/伤害、无 AI | Wave B / D |
| N6 | 屏幕震动 / 相机缩放 / 淡入淡出 | 无 | Wave C（并入 R1，相机与过渡） |
| N7 | 鼠标拾取玩法化 | `Sprite2DComponent.pickId` 已反射但无人使用 | Wave B（点击选中单位） |
| N8 | 战斗 UI | 只有 Text/Panel/Border；无列表 / 网格 / 滑块交互范式 | Wave B / D（用现有控件搭 HUD 与菜单） |

## 2. 边界

- 不引入 `Node2D` / `Transform2D` / `Camera2D` / 第二棵场景树。单位仍是 `Node3D` + `Transform` + `Sprite2D`。
- 不新增第二套 Scene scheduler / frame loop；渲染改动遵守 `render-arch` skill 与 `P0-contract-matrix.md`（C1–C5）。
- 战术玩法逻辑（回合队列、可达范围、伤害公式、AI）**优先放脚本**；只有「引擎别处也要用、且脚本做不好」的
  才下沉成组件/系统。判据：出现第二个重复实现，或每帧成本不可接受。
- 新子系统不改现有公开语义：`world.*` / `ui.*` 的既有函数不动，新函数经中立层 `registerModuleFunction`。
  **但这不否定重构**：现有系统/框架在架构上不合理、不支持新需求时应及时重构（AGENTS.md 规则 0），
  不叠补丁、不在 hack 上补 hack；重构须与它服务的 checkpoint 同一提交，并说清动了谁的契约。
- 引擎/玩法代码只依赖中立层；sol2 只出现在 Lua 后端 target。
- 不在帧录制中途重建 GPU 资源；粒子/后处理的资源变更延迟到安全时机。
- 素材按许可入库：CC0（Kenney）直接进 `Content/`，生成图附 prompt 与 seed；许可说明写进 `Content/*/LICENSE.md`。
- 只使用 XMake；不引入 CMake。生成文件只读，修生成链不手改 `Generated/*`。

## 3. 已确认决策

| # | 议题 | 结论 |
| --- | --- | --- |
| D1 | 计划位置 | 改写 `.agent/plan/rpg-prototype/`，v1 工件改名 `*-v1-landed.*` 保留 |
| D2 | 世界框架 | 重编：枢纽（村庄/营地）+ 战斗区 + 地牢层，不沿用 Tiny Town 小镇编排 |
| D3 | 战斗 | 战旗战术：格棋盘、速度出手序、移动 + 行动（攻击/技能/道具/待机） |
| D4 | 成长 | 等级 + 属性（HP/攻/防/速）+ 装备槽 + 技能/职业 + 队伍 |
| D5 | 视觉 | 后处理驱动（bloom / 色调映射 / 灰度 / 过渡），配合粒子与相机效果 |
| D6 | 音频后端 | SDL3 音频（`libsdl3` 已在依赖里，少一个依赖）；接口中立，后端可换 |
| D7 | 2D 物理 | 引入 Box2D（xmake-repo `box2d-v3.1.1`）：AABB/圆、static/kinematic/dynamic、sensor、接触事件 |
| D8 | 子系统落点 | 新 target：`ya-postfx`（并入 Render3D 或独立）/`ya-audio`/`ya-particles`/`ya-physics-2d`；脚本面走中立层 |
| D9 | 素材 | Kenney CC0（Tiny Dungeon / Tiny Battle / Particle Pack / UI Pack / RPG Audio）+ image-gen 补缺 |
| D10 | 单位动画 | Tiny Battle 单位是静态 16px；需要动画时用 image-gen + `quantize_walk_sheet.py` 生成，或寻 CC0 动画包 |
| D11 | 战术逻辑位置 | 回合/AI/伤害在 Lua；只有引擎通用的（拾取、可达范围查询若成本超限）下沉 |

## 4. 游戏设计

### 4.1 主流程（垂直切片）

```
序章  边境营地（枢纽地图）
      └─ 长老给任务 1：清理北边遗迹的魔物
进出  营地 ↔ 遗迹入口（场景转移）
战斗1 遗迹·一层（教学：移动 / 普攻 / 待机）
       胜利 → 经验 → 升级 + 首个技能
任务2 回营地：同伴加入（队伍系统）
战斗2 遗迹·二层（机关：推箱 / 压力板 —— Box2D 触发 + 地形）
       胜利 → 装备掉落（装备系统）
战斗3 遗迹·深处（魔法 VFX：粒子 + 暴击 bloom；敌方 AI 提升）
Boss  遗迹·核心（多阶段：血量阶段切换 + 后处理阶段变化 + 音效）
结局  任务完成 → 存档
```

设计要点：每个阶段自然用到一种子系统，但主线首先是「任务 + 打怪」的 RPG 循环，不是能力清单。
玩家始终有：一身属性、一件装备、一个技能、一名同伴、一份任务日志。

### 4.2 系统清单

| 系统 | 内容 | 数据落点 |
| --- | --- | --- |
| 探索 | 网格行走、交互（NPC/告示牌/宝箱/门）、场景转移 | 复用 GridMover + `world.loadScene` |
| 遭遇 | 接触遇敌区 / 交互触发战斗；战斗地图与队伍状态传入 | 新：遭遇触发 + 战斗场景加载 |
| 回合战斗 | 速度决定出手序；回合内 移动（BFS 范围）+ 行动（攻击/技能/道具/待机）；地形影响移动与命中 | 新：`Battle` 脚本模块 + `Unit` 组件/表 |
| 战斗 AI | 敌方：接近最近目标 → 在射程内攻击 → 否则移动；Boss 分阶段 | 新：`BattleAI.lua` |
| 成长 | 经验 → 升级 → 属性成长；技能按等级/职业解锁 | `Persist.party` |
| 装备 | 武器/护甲/饰品槽 → 属性加成；背包 | `Persist.inventory` |
| 技能/职业 | 角色职业决定技能表；MP/冷却 | `Persist.party` |
| 队伍 | 多角色入队、各自属性/技能、出战选取 | `Persist.party` |
| 任务 | 任务状态（active/done）、目标、奖励；战斗结果为推进条件 | `Persist.quests` |
| 存档 | 场景 + `Persist` 全量；脚本 `onSave` | `world.save/load` |

## 5. 引擎能力矩阵（验收基准）

每条能力必须有「可指认的使用点」与「验证方式」，逐步填进 `feature_matrix.json`。
`status` 取值：`planned` / `wip` / `done`。

| 能力 | 使用点（游戏内） | 来源 |
| --- | --- | --- |
| Tilemap 多层 + y-sort | 战斗地图（地面/装饰/遮挡）、营地 | 复用 R1 |
| 精灵合批 + 纹理表 | 战场上多单位 + 特效同屏 | 复用 v1 步骤 5 |
| 帧动画 | 单位待机/受击、宝箱、旗帜 | 复用 R0 |
| 像素完美相机 | 战斗取景、探索跟随 | 复用 §10 |
| 相机缩放 / 屏幕震动 | 攻击命中、暴击、Boss 阶段转换 | 新（R1） |
| 鼠标拾取 | 点击选择单位 / 点击格子移动 | 新（N7） |
| 场景转移 + Persist | 营地 ↔ 遗迹、战斗进出 | 复用 R3 |
| 存档 / 读档 | 通关存档 | 复用 G2.2（首次真正用起来） |
| 运行时生成 / 销毁 | 伤害数字、召唤物、临时障碍 | 复用 `world.spawnSprite/destroyEntity` |
| **后处理运行时** | 战斗进入/退出过渡、受击闪白、倒地灰度、暴击 bloom、Boss 阶段调色 | **新子系统 R1** |
| **音频** | BGM（营地/战斗/Boss）、命中/施法/UI 音、胜利/失败 jingle | **新子系统 A1** |
| **粒子** | 命中火花、魔法弹、治疗光、尘土、火把 | **新子系统 X1** |
| **2D 物理 / 触发器** | 推箱、压力板、尖刺危险区、移动平台、击退 | **新子系统 P1** |
| GUI 控件 | 战斗 HUD（行动菜单/单位面板/行动序条）、背包、装备、技能、任务日志、对话、设置 | 复用 + 扩展 |
| 3D 网格 + 灯光 + 阴影（可选，P2） | 一个 2.5D Boss 竞技场 | 引擎已有（`mix 2.5D`） |

## 6. 新增子系统设计

### R1 — 后处理运行时控制 + 相机效果（`ya-postfx`，或并入 Render3D）

- 现状：`PostProcessingState`（bloom/tone-map/灰度/内核/噪点）已有 pass；`AppRenderServices::setRenderPipelineSettings`
  已是运行时入口（`getRenderPipelineSettings` / `setRenderPipelineSettings`），**缺口是脚本面与过渡**。
- 中立层新函数（登记在 `GameplayScriptFunctions`）：
  `render.setLook{ bloom, bloomStrength, exposure, toneMapCurve, grayscale, kernel, grain }`（部分字段就地改）、
  `render.fadeOut/in(seconds, color)`、`render.shake(intensity, seconds)`、`camera.zoomTo(scale, seconds)`。
- 过渡由一张常驻的「look 时间线」推进（帧内插值），在帧录制前把结果写进 `RenderPipelineSettings`；不在录制中途改。
- 相机效果走 `RuntimeGameViewProducer` / 相机矩阵（震动 = 视野偏移，缩放 = `resolveCameraViewFraming` 的缩放输入）；
  像素完美开关不与震动冲突（震动偏移在吸附之后叠加）。
- 测试：`PostFxRuntimeTest.SetLookReachesPipelineSettings`、`FadeAdvancesAndEnds`、
  `CameraShakeOffsetsViewNotTransform`；像素验收：一次 fade + 一次 bloom 暴击截图。

### A1 — 音频子系统（`ya-audio`，SDL3 后端）

- 接口：设备管理、声音资源（`.yaaudio.json` 或直接 wav/ogg + 元数据）、播放实例、总线音量（master/bgm/sfx/ui）。
- 后端：SDL3 `SDL_AudioStream`（`libsdl3` 已链接；`SDL_OpenAudioDevice` / `SDL_LoadWAV` / 流式 `SDL_PutAudioStreamData`）。
  自研混音器（showcase-roadmap A2）以后替换后端，接口不变。
- 组件：`AudioSourceComponent`（clip、volume、loop、playOnStart、bus）、`AudioListenerComponent`（可选）。
- 脚本（中立层）：`audio.play(clip, {volume, loop, bus, entity?})` 返回句柄、`audio.stop(handle)`、
  `audio.setBusVolume(bus, v)`、`audio.playMusic(clip, {fadeIn})`、`audio.crossfade(clip, seconds)`。
- 玩法驱动：营地 BGM、进战切换战斗 BGM、Boss 音乐、命中/施法/UI 音、胜利 jingle。
- 测试：`AudioBusVolumeIsScoped`、`PlayReturnsAStoppableHandle`、`MissingDeviceDegradesSilently`（无设备不崩）。

### X1 — 粒子子系统（`ya-particles`）

- 资产 `.yaparticle.json`：发射率、生命周期、初速/方向/散布、重力、颜色/大小曲线、混合模式、贴图。
- 组件 `ParticleEmitterComponent`（引用资产 + bPlaying + 覆盖字段）；一键发射走脚本。
- 渲染：CPU 模拟 + 走现有 2D 实例化路径（复用纹理表与 quad 批），不新增独立 pass；
  混合模式沿用精灵的 SrcAlpha；高亮粒子进 Bloom（与 `P0-contract-matrix` 的半透明路径一致）。
- 脚本：`particles.emit(entity | position, asset, {count})`、`emitter:play()/stop()`。
- 玩法驱动：命中火花、魔法弹、治疗光、尘土、火把、Boss 阶段爆发。
- 测试：`ParticleAssetRoundTrips`、`EmitSpawnsCountedParticles`、`ParticlesDieAfterLifetime`。

### P1 — 2D 物理 / 触发器（`ya-physics-2d`，Box2D）

- `add_requires("box2d")`（xmake-repo `box2d-v3.1.1`）；目标只依赖 `ya-foundation-core` / `ya-ecs-core` / `ya-scene-core`。
- 组件：`RigidBody2DComponent`（static/kinematic/dynamic、gravityScale、fixedRotation）、
  `Collider2DComponent`（box/圆、offset、isSensor、friction/restitution、category/mask）。
- 系统 `Physics2DSystem`（固定步长，与 Jolt 的 3D 系统并列，互不干扰）：写回 Transform（x/y，z 不动）。
- 接触事件进 ECS（`OnContact2DBegin/End`），玩法脚本可查询/订阅 `world.contacts2D()` 或事件回调。
- 脚本：`entity:getRigidBody2D()`、`entity:getCollider2D()`、`physics2D.overlapCircle(pos, r, mask)`、
  `physics2D.raycast2D(from, to, mask)`、`body:applyImpulse(v)`。
- 玩法驱动：推箱、压力板（sensor + 上方重量）、尖刺（sensor → 扣血）、移动平台（kinematic）、挥击击退。
- 测试：`BoxBodiesSettleOnAGround`、`SensorFiresBeginAndEnd`、`KinematicMoverPushesADynamicBox`、
  `RaycastHitsTheNearestBox`。

### 中立层与 target 规则（四项共用）

- 新脚本函数一律 `script::registerModuleFunction(module, name, fn)`；类型化的值进出，不落 JSON。
- 新 target 各自 `xmake.lua`，`ya_std_module("<MODULE>_API")`，只依赖 `ya-foundation-core`（+ 必要的 ecs/scene）。
- 新组件用 `YA_REFLECT_BEGIN/FIELD/METHOD` 反射，自动获得脚本可见性（D12 无门禁）；
  跨边界不了的类型静默不导出。

## 7. Checkpoints

每个 checkpoint 一个可验收目标，plan 文件改动与代码同一提交（AGENTS.md 规则 11/13）。
「逼出来的需求」写明引擎改动的游戏来源。

### Wave A — 重编基础

#### F1 — 项目壳重编 + 素材入库
- 游戏验收：`2DRpgPrototype` 以新默认场景启动，显示一张营地地图（tilemap）。旧城镇内容移入
  `Content/Scenes/legacy/` 或删除（保留素材）。
- 内容：Kenney Tiny Dungeon（地图）、Tiny Battle（单位）拷入 `Content/Textures/` 并写 `LICENSE.md`；
  `.yatileset.json`（dungeon）、`.yatileset.json`（营地草/路）。
- 逼出来的引擎改动：无（若 tileset 边距/间距与 Kenney 图集不符，修 `Tileset` 解析）。
- 验收：运行冒烟 exit 0；截图见地图。

### Wave B — 战术核心（先跑通主循环）

#### T1 — 棋盘与回合骨架
- 游戏验收：一个固定战斗地图上摆 3 个我方 + 3 个敌方单位；按确认键推进回合；
  行动序条按速度排序显示；单位格子高亮。
- 内容：`Content/Scenes/BattleRuins1.scene.json`（tilemap + 单位）、`Content/Scripts/Battle.lua`（回合队列）、
  `Content/Scripts/Unit.lua`（HP/攻/防/速/移/射程/阵营）、`Content/UI/BattleHUD.yaui.json` + `.lua`。
- 逼出来的引擎改动：`map:entityAt` 已够用；若要「按格子查任意单位（含无脚本）」则扩展查询面。
- 测试：`TurnOrderFollowsSpeed`、`AdvancingSkipsDefeatedUnits`。
- 验收：手测回合可推进；冒烟 exit 0。

#### T2 — 移动与可达范围
- 游戏验收：选中单位 → 显示可达格子（BFS，受移动力与地形/阻挡限制）→ 点/键选目标格 → 单位沿路径逐格移动动画。
- 逼出来的引擎改动：
  - 格子可达范围查询：脚本侧 BFS（用 `isSolid` + `entityAt`）先做；若每帧成本不可接受或出现第二个实现，
    下沉为 `TilemapComponent::reachableCells(origin, move, blockedMask)`。
  - **鼠标拾取**（N7）：`render.screenToCell(x, y)` 或 `input:mousePosition()` + 相机反投影 → 格子；
    `pickId` 用于选中单位（否则用 `entityAt`）。
- 测试：`ReachableCellsRespectMovementAndBlockers`（Lua 单测）、`ScreenToCellRoundTrips`。
- 验收：手测走位与范围；截图。

#### T3 — 攻击 / 技能 / 伤害结算
- 游戏验收：单位行动选「攻击」，点射程内敌人 → 伤害数字飘出 → HP 条更新 → 死亡移除；
  至少 2 个主动技能（消耗 MP）。
- 逼出来的引擎改动：伤害数字 = `world.spawnSprite` + 逐帧上浮（或 `SpriteAnimation`）；HP 条 = 世界空间 UI 的简化版
  （精灵条，非 yaui）——**不引入世界空间 UI 系统**，用精灵实现。
- 测试：`DamageFormulaInRange`、`SkillSpendsMana`、`DefeatedUnitLeavesTheBoard`。
- 验收：打完一场战斗到胜利结算。

#### T4 — 敌方 AI
- 游戏验收：敌方回合自动：接近最近目标 → 进入射程攻击 → 否则移动；玩家无需操作。
- 逼出来的引擎改动：无（纯 Lua）。
- 测试：`AiClosesDistanceThenAttacks`、`AiDoesNothingWithoutTargets`。
- 验收：一场战斗全自动可分出胜负。

#### M1 — 战斗与探索拼接（垂直切片主循环）
- 游戏验收：营地走动 → 进遗迹（场景转移）→ 接触遇敌区 → 载入战斗地图 → 打完 → 回营地（`Persist` 保留血/经验）。
- 逼出来的引擎改动：战斗进出时的状态交接（队伍/血量存 `Persist`，战斗场景初始化时套回）。
- 测试：`BattleResultTransfersBackToOverworld`。
- 验收：**第一次完整闭环**：营地 → 战斗 → 回营地。

### Wave C — 四子系统（每个都要有真实使用点）

#### R1 — 后处理运行时 + 相机效果
- 游戏验收：进战淡入、受击闪白、暴击 bloom、倒地灰度、Boss 阶段调色、命中屏幕震动。
- 逼出来的引擎改动：见 §6 R1。
- 测试：见 §6 R1；像素验收截图。
- 验收：上述效果在游戏内逐条可指认。

#### A1 — 音频子系统
- 游戏验收：营地/战斗/Boss 三套 BGM 切换（crossfade）、命中/施法/UI 音、胜利 jingle。
- 逼出来的引擎改动：见 §6 A1。
- 测试：见 §6 A1。
- 验收：静音设备下不崩；有声设备手测可听。

#### X1 — 粒子子系统
- 游戏验收：命中火花、魔法弹、治疗光、营地火把/尘土。
- 逼出来的引擎改动：见 §6 X1。
- 测试：见 §6 X1。
- 验收：粒子与精灵同批渲染不破排序；截图。

#### P1 — 2D 物理 / 触发器
- 游戏验收：第二场战斗有推箱 + 压力板（推箱到压力板开启门/清障）；尖刺危险区扣血；一个移动平台。
- 逼出来的引擎改动：见 §6 P1。
- 测试：见 §6 P1。
- 验收：机关战斗可解；截图。

### Wave D — 成长与内容

#### G1 — 成长：等级 / 属性 / 技能 / 职业
- 游戏验收：战斗得经验 → 升级 → 属性成长 → 解锁技能；职业决定技能表。
- 测试：`LevelUpRaisesStats`、`SkillUnlocksAtLevel`。

#### G2 — 装备与背包
- 游戏验收：战斗掉落装备 → 背包界面 → 装备槽（武器/护甲/饰品）→ 属性即时更新。
- UI：`.yaui.json` 背包/装备面板（用 TableGrid / ScrollViewport / Button）。
- 测试：`EquippingRaisesStats`、`UnequipRestoresBase`。

#### G3 — 队伍与同伴
- 游戏验收：任务推进使同伴入队；出战选取；每名角色独立成长。
- 测试：`CompanionJoinsAndFights`。

#### G4 — 任务系统
- 游戏验收：任务日志（进行中/已完成）；战斗结果为推进条件；奖励发放。
- 测试：`QuestAdvancesOnBattleOutcome`、`QuestRewardGrantedOnce`。

#### C1 — 内容：多场战斗 + Boss
- 游戏验收：3–4 场战斗（教学 / 机关 / 魔法 / Boss）+ 3 个任务 + 结局。
- 内容：`BattleRuins2/3/Core.scene.json`、`BattleAI.lua` 分阶段、Boss 阶段脚本。
- 验收：从序章到结局可通关。

### Wave E — 收口

#### Z1 — 存档 / 读档闭环
- 游戏验收：营地存档点存档 → 退出 → 读档回到存档点（含队伍/等级/装备/任务）。
- 测试：`SaveRestoresPartyAndQuests`。

#### Z2 — UI 完善与打磨
- 战斗 HUD（行动菜单、单位面板、行动序条）、设置（音量/语言）、对话完善。
- 验收：所有面板可操作；截图。

#### Z3 — 收口：矩阵打勾 + 打包 + 录屏
- 更新 `feature_matrix.json` 全部 `done`；打包冒烟；录屏一局通关。
- 验收：`python3 Script/ya.py package`；自动化通关脚本全绿；矩阵无 `planned`。

## 8. 素材与许可

| 素材 | 来源 | 用途 | 许可 |
| --- | --- | --- | --- |
| Tiny Dungeon（16px，130 文件） | Kenney | 地牢/战斗地图 tile | CC0 |
| Tiny Battle（16px，190 文件） | Kenney | 单位精灵（静态） | CC0 |
| Tiny Town（已入库） | Kenney | 营地/村庄 tile | CC0 |
| Particle Pack（512px，80 文件） | Kenney | 粒子贴图 | CC0 |
| UI Pack（430 文件） | Kenney | HUD 图标/边框 | CC0 |
| RPG Audio / Interface Sounds / Impact Sounds | Kenney | BGM/音效 | CC0（页面未逐条确认，下载页复核） |
| 单位行走/受击动画 | image-gen（ComfyUI 优先 / Pollinations） | 需要动画单位时 | 自产，记 prompt + seed |

- 下载：`https://kenney.nl/media/pages/assets/<pack>/.../<file>.zip`；入库后写 `Content/Textures/LICENSE.md`、
  `Content/Audio/LICENSE.md`。
- 生成：走 `.agent/skills/image-gen/SKILL.md`；像素图经 `Example/2DRpgPrototype/Tools/quantize_walk_sheet.py`
  量化对齐网格。本机无 `grok-cli`。
- 用户后续会搭本地 ComfyUI；届时生成走本地，不再落外部 API。

## 9. 验证与验收

- 每个 checkpoint：`xmake b <targets>` + `xmake r ya-testing`（相关用例全绿，总数对比上一轮）+ 运行冒烟。
- 冒烟：`python3 Script/ya.py run --project Example/2DRpgPrototype/2DRpgPrototype.yaproject -- --exit-after-frame=120 --log-level=warn`（exit 0）。
- 自动化端到端：`Script/automation/2d-rpg/`（复用 `input.inject_key` + `ui.query`），新增战斗流脚本。
- 视觉验收：提案截图/录屏；后处理/粒子/物理逐条截图。
- 矩阵审计：`feature_matrix.json` 每个 `done` 条目必须有 evidence 路径或测试名。
- 打包：`python3 Script/ya.py package`。

## 10. 风险

- **工程量**：四个新子系统 + 战术层 + 成长四项，是本线最大的一版。缓解：垂直切片优先跑通主循环（Wave B/M1），
  子系统各自有最小使用点，不追求完备。
- **Box2D 与 tile 棋盘两套空间**：物理是连续空间，战旗是离散格子。约定：棋盘判定用 tilemap 查询（权威），
  Box2D 只负责「非格子的可交互物」；两者边界写进 `2d-gameplay` skill。
- **SDL3 音频在 macOS/无设备环境**：静默降级，不崩；测试覆盖无设备路径。
- **后处理全局 vs 每 View**：`PostProcessingState` 目前是管线级。若玩法需要每 View 独立 look，需扩到 View 级——
  到 R1 再定，先做管线级（单机游戏够用）。
- **素材许可**：Kenney 页面未见逐条许可声明时，以 CC0 为准但下载页复核；生成图记录 prompt/seed。
- **Tiny Battle 单位无动画**：静态单位对 SRPG 可接受（待机/受击可用程序化位移 + 粒子补偿）；需要真动画时生成。
- **性能**：多单位 + 粒子 + 物理同屏，走已有实例化路径；若吃紧，按 `r4-measurements.md` 的方法量后再说。

## 11. 与活跃线的关系

- `showcase-roadmap` G2 条目的 2D demo 目标由本计划承接；A1（音频）本计划落地后回填该计划。
- `render-view-family`：纯 2D View 家族（步骤 6）不在本轮；后处理运行时控制不影响该线的 view/frame 语义。
- `shadow-and-light-line`：2.5D Boss 竞技场（可选 P2）依赖 R15b 聚光灯 + 阴影，不在本轮 P0。
- `game-ui-script-framework` / `ui-behavior-capabilities`：战斗 UI 用现有控件与控件脚本语义，不新增第二套。
- `reflection-runtime`：新组件全走反射；中立层 ABI 变化若涉及本计划，先对齐该线。
- v1 遗留（`todo-v1-landed.md`）中与本线冲突的项（pixel-perfect、纯 2D View、tilemap 静态缓冲）按各自线推进。

## 12. 历史

v1（2026-09-29 起）：RPG Maker 式俯视 2D，B1/R0–R4 落地，评审 7 步中 1–3 完成。
工件：`plan-v1-landed.md`、`progress-v1-landed.md`、`todo-v1-landed.md`、
`feature_matrix-v1-landed.json`、`session_checklist-v1-landed.md`、`P0-contract-matrix.md`、
`r4-measurements.md`、`B2-lua-backend-split.md`、`review-2026-09-30.md`。
引擎能力（tilemap/精灵/动画/相机/脚本层/存档/场景转移）由 v1 落地，本计划直接复用。
