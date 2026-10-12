# Progress — RPG Tactics (v2)

v1 进度保留在 `progress-v1-landed.md`。本文件从 2026-10-13 改版起记录。

## 2026-10-13 — 计划改版：从「城镇跑腿」到「2D 战旗 RPG 垂直切片」

用户对 v1 实现不满意（`Example/2DRpgPrototype` + `plan-v1-landed.md`），要求重设计整个游戏主流程，
体现更多 2D 引擎 feature。经四轮问答收口：

- **形态**：RPG，不断做任务打怪的流程；不绑死城镇，内容可全新编排。
- **战斗**：战旗战术（SRPG）——格子地图、多单位、速度出手序、移动 + 行动。
- **成长**：等级 + 属性、装备、技能/职业、队伍/同伴（四项全要）。
- **视觉**：后处理驱动。
- **新增子系统**（四项大件）：后处理运行时控制、音频、粒子、2D 物理/触发器。
- **落点**：走中立脚本注册层 + 新 target，不进 GameRuntime 硬编。
- **规模**：垂直切片（营地 + 遗迹 + 3–4 战 + 3 任务 + 1 Boss）。
- **验收**：能力覆盖优先（`feature_matrix.json` 可审计）。
- **素材**：Kenney CC0 + image-gen；本机无 `grok-cli`；用户后续自建本地 ComfyUI。

### 开工前在代码里核实的事实（2026-10-13）

- **后处理**：pass 全在（bloom / tone-map / 灰度 / 内核 / 噪点），`PostProcessingState` 由配置文件 +
  `RenderPipelineSettings` 驱动；`AppRenderServices::get/setRenderPipelineSettings` 已是运行时入口。
  缺口只是**脚本面与过渡**，不是从零造 pass。（锚点：`Render3D/Common/PostProcessingState.h`、
  `AppRenderServices.h`。）
- **音频**：`grep` 全树无 audio / sound / miniaudio / SDL_Audio —— **完全不存在**。目标 `ya-audio`。
- **粒子**：全树无 particle —— **完全不存在**。目标 `ya-particles`。
- **2D 物理**：`Physics/PhysicsBodyComponent.h` 只有 `Box`/`Sphere` + `_isDynamic`，Jolt 3D，
  无 trigger、无 sensor、无脚本回调。选 Box2D（xmake-repo 有 `box2d-v3.1.1`）。
- **战术层**：全树无回合/寻路/AI/战斗/伤害 —— 全在 Lua 新写。
- **可复用**：tilemap / 精灵合批 / 帧动画 / 像素完美相机 / 场景转移 + Persist / 存档 / 运行时生成 /
  GUI 控件目录 / InputMap 均已落地（v1），直接复用。
- **素材**：Kenney Tiny Dungeon（16px，130 文件，CC0）、Tiny Battle（16px，190 文件，CC0）、
  Particle Pack、UI Pack 均确认存在且 CC0；音频包（RPG Audio / Interface Sounds / Impact Sounds）
  目录存在，具体许可以下载页复核为准。

### 产出

- `plan.md` 重写：目标、边界、决策、游戏设计、能力矩阵、四子系统设计、Wave A–E checkpoints、素材、风险。
- `feature_matrix.json` 重写（v1 版存 `feature_matrix-v1-landed.json`）：9 条复用 + 4 子系统 + 战术/成长/收口。
- `todo.md` / `session_checklist.md` 重写（v1 版各存 `*-v1-landed.*`）。
- v1 的 `plan.md` / `progress.md` / `todo.md` / `feature_matrix.json` / `session_checklist.md`
  改名 `*-v1-landed.*` 保留为历史。

### 状态

- 计划框架完成，**代码未开工**。下一步：Wave A 的 F1（项目壳重编 + 素材入库），
  开工前先过 `todo.md` 的「待用户确认」项（素材下载方式、单位动画策略、目录名）。

### 未完成 / 待确认

- 素材入库方式（我去下载 Kenney zip，还是用户手动放）—— 已自行下载并入 `Content/`（Kenney 页面授权抓取）。
- 单位静态 vs 动画（Tiny Battle 无内置动画；暂用 Tiny Dungeon 静态角色/怪物，够用）。
- 后处理作用域（管线级先做；每 View 独立 look 到 R1 再定）。
- 示例目录名（沿用 `2DRpgPrototype` 或改名）。

## 2026-10-13 — F1：重编壳 + 素材入库

- 目标：`2DRpgPrototype` 以新默认场景启动，显示营地地图；Kenney CC0 素材入库。
- 内容：`Content/Scenes/Camp.scene.json`（28×20，tiny_town 草/树/栅栏）、`Content/Scripts/Elder.lua`
  （发任务，走 `Dialogue`）、`Content/Tilesets/dungeon.yatileset.json`；素材 = Tiny Dungeon / Tiny Battle
  图集 + Kenney Particle Pack 9 张 + UI/SFX 音频 18 个（`Content/Textures|Audio/LICENSE.md` 记来源，全 CC0）。
  生成器 `Tools/make_maps.py`（确定性）。
- 途中发现（两个真 bug，都在我的生成器侧）：
  1. `uvRect` 契约是 `(u0, v0, u1, v1)`，不是 `(origin, size)` —— 第一版 `tile_uv` 返回 `(u0,v0,w,h)`，
     导致每个角色精灵的 UV 窗口宽度为 0，画成一条色带。读 `Sprite2DWorld.slang` 的 `lerp(uvRect.xy, uvRect.zw, ...)` 定案。
  2. Tiny Town 的树是**两个堆叠 tile**（树冠在上、树干在下相邻格），第一版把两者放同一格，树画成断头拱形。
- 验证：`Script/automation/2d-rpg/camp_hub.py`（scene=Camp、Player/Elder 在场、截图）通过；
  `python3 Script/ya.py run ... --exit-after-frame=120` exit 0。截图 `Engine/Saved/Automation/camp-hub/camp_hub.png`。
- 保留 / 偏离：Tiny Battle 是载具不是幻想单位，改主用 Tiny Dungeon 自带的角色（84–95）与怪物（108–131）。
- 提交 `89696f80`。

## 2026-10-13 — T1：棋盘与回合骨架

- 目标：固定战斗地图摆 3 我 + 3 敌；确认键推进回合；行动序条按速度排序；活动单位高亮。
- 内容：`Content/Scenes/BattleRuins1.scene.json`（16×12 石室 + 6 单位 + Battle 控制器）、
  `Content/Scripts/Unit.lua`（属性脚本 + HP/变色/受伤）、`Content/Scripts/Battle.lua`（回合队列）、
  `Content/UI/BattleHUD.yaui.json` + `.lua`（行动序条，生成器 `Tools/make_battle_hud.py`）。
- 设计：Battle **不持有单位列表** —— 它遍历棋盘，用 `map:entityAt(x,y)` 问谁站在这格，再读该单位自己的
  脚本（`team/spd/hp/displayName`）。棋盘即名册，没有第二份列表要同步。
- 途中发现：`ui.get(entry)` 在条目脚本加载完成前返回**根控件句柄**（不是脚本 self），所以 Battle 前几帧
  `hud.setTurnOrder` 不存在；改为每帧重试直到 HUD 就绪（`bHudPending`）。
- 验证：`Script/automation/2d-rpg/battle_turns.py` —— 6 单位、按速度排序（Bat14→Archer12→Mage10→Ghost9→
  Knight8→Slime6）、确认后活动标记移到 Archer；截图确认整块棋盘与 HUD。
  截图 `Engine/Saved/Automation/battle-turns/battle_turns.png`。
- 未完成（T2 起）：移动/可达范围、攻击/伤害、AI、进战/出战拼接。

## 2026-10-13 — T2：移动与可达范围

- 目标：选中单位 → 显示可达格子 → 光标选目标格 → 单位沿路径逐格移动。
- 内容（全在 `Content/Scripts/Battle.lua`）：BFS `reachable()`（受 `isSolid` 与 `entityAt` 阻挡限制）、
  `pathTo()` 回溯路径、可达格用复用的 `ReachHighlight` 精灵池绘制（`world.spawnSprite`，`bYSort` +
  `sortOrder -1`）、光标四向移动、`updateMove` 按格补间（0.14s/格）。
- 途中发现（两个跨边界限制，已写进脚本注释）：
  1. `world.spawnSprite` 的 options 是**类型化的值结构体**，嵌套的 `glm::vec2` 只能传 `Vec2.new(...)`；
     普通 `{1.0, 1.0}` 表不转换（`a Lua table cannot be passed to the engine`）。
  2. 拿到的**别的脚本 self** 不暴露实体原生访问器（`unit:getTransform()` 不存在）；实体访问器挂在 `entity` 上。
     改为读该单位自己缓存的 `unit.transform`（`Unit.lua` 在 onInit 存过）。
- 验证：`Script/automation/2d-rpg/battle_move.py` —— 25 个可达格高亮、光标移到 (6,9)、
  Bat 从 (8,9) 走到 (6,9)；`battle_turns.py` 回归通过。截图
  `Engine/Saved/Automation/battle-move/battle_move.png`（可达菱形清晰可见）。
- 保留 / 偏离：鼠标拾取（screen→cell，N7）暂未做 —— 键盘光标已能选格；鼠标拾取放到需要点选交互的
  checkpoint（或与 N7 一起）再落地，避免为演示而做。

## 2026-10-13 — T3：攻击 / 技能 / 伤害结算

- 目标：行动选攻击/技能/待机；选射程内目标 → 伤害飘字 → HP 条更新 → 死亡移除。
- 内容：
  - `Content/Scripts/Unit.lua`：加 `maxMp/skillName/skillPower/skillCost/skillRange`；确定性伤害
    `damageAgainst(power, target) = max(1, power - def)`（无 RNG，战斗可复现、可精确断言）。
  - `Content/Scripts/Battle.lua`：阶段机 `select → move → menu → target → done`；菜单 `Attack / <技能> / Wait`；
    目标格 = 射程内活着的敌方；进入目标阶段自动吸附最近敌人。
  - `Content/Scripts/BattleFx.lua`（新）：伤害数字（`digits.png` 逐位拼条、上浮+淡出+销毁）、每单位 HP 条
    （1×1 白图按 size 缩放的暗底 + 血量填充，低血变红）。
  - 资产：`Content/Textures/digits.png`（`Tools/make_digits.py` 生成 10 个 8×8 字形）、`white.png`。
  - HUD：`BattleHUD` 加 `Menu1..3 / Hint / Outcome`（`Tools/make_battle_hud.py` 重生成）。
- 途中发现（第三个跨边界坑）：`world.spawnSprite` 的 `size` 又是嵌套 vec2 —— `BattleFx` 第一版写 `size[0]`/`size[1]`，
  Lua 1 基下标使 `size[0]` 为 nil，报 `sol: no matching function call`。统一走 `Vec2.new(size[1], size[2])`。
- 验证：`battle_combat.py`（Archer 普攻 Bat 10→4）、`battle_skill_kill.py`（Mage Firebolt Ghost 16→4，
  扣 def 1）、`battle_turns.py` / `battle_move.py` 回归通过（三项均已适配新阶段机）。
- 未完成：敌方 AI（T4）、胜负结算的完整闭环验收（需要 AI 才能公平打完；`checkBattleOver` 已实现并写
  `Persist.battle.lastOutcome`，留给 T4 一起验）。伤害数字/HP 条为世界空间精灵（未引入世界空间 UI 系统）。

## 2026-10-13 — T4：敌方 AI

- 目标：敌方回合自动进行（接近最近目标 → 射程内攻击 → 否则移动），玩家无需操作。
- 内容：`Content/Scripts/BattleAI.lua`（新，纯 Lua）：`nearestOpponent` 选最近存活敌方（平局按棋盘序，
  确定性）、`approachCell` 在可达集里选最靠近目标的格、`plan` 给出 `{move, attack}`。
  `Battle.lua` 加 `ai` / `aiMove` 阶段：敌方回合不进光标，直接按 plan 走同一条 `pathTo` + `resolveHit`，
  与玩家共用移动与伤害实现。
- 验证：`Script/automation/2d-rpg/battle_ai.py` —— 只把玩家回合按 Wait 跳过，敌方（AI）自行前进并命中
  （Mage 18→12），无玩家输入。截图 `Engine/Saved/Automation/battle-ai/battle_ai.png`。
- 适配回归：敌方回合自动进行后，T1/T2/T3 的自动化不能再假设「谁在第几行行动」或「按 Space 结束敌方回合」；
  三个脚本改为只跳过玩家回合、按单位名等自己的回合。全部五个战斗脚本（turns/move/combat/skill/ai）通过。
- 未完成（M1）：战斗与探索拼接、胜负回传营地的闭环。

## 2026-10-13 — M1：战斗与探索拼接（第一次完整闭环）

- 目标：营地走动 → 进遗迹 → 打一场 → 回营地，状态经 `Persist` 保留。
- 内容：
  - `Camp.scene.json`：`RuinsGate`（`Door.lua`，走上去切到 `BattleRuins1`）+ `SpawnFromRuins` 出生标记
    （战斗结束回到这里）；生成器加 `marker_entity` 与开场就到门口的摆位。
  - `BattleRuins1.scene.json`：Battle 控制器带 `returnScene / returnSpawn / returnActor / returnDelay`。
  - `Battle.lua`：结算时写 `Persist.battle.lastOutcome`，`returnDelay` 秒后用 `world.loadScene` 把队伍送回。
- 验证：`Script/automation/2d-rpg/camp_battle_loop.py` —— 起点 Camp → 踩门 → `BattleRuins1` → 打完
  （本次是 `Defeat...`，因为测试用 Wait 跳过玩家回合、让 AI 打）→ 回到 Camp 的 `SpawnFromRuins`
  （0.5, -8.5）。截图 `Engine/Saved/Automation/camp-battle-loop/back_in_camp.png`。
- 说明 / 偏离：M1 的验收口径是**闭环成立**（Camp→战斗→Camp），不要求测试会打仗；胜负本身由 T2–T4
  的战斗脚本覆盖。测试驱动只按 Wait 结束玩家回合，避开「方向键同时驱动光标与菜单」的自动化噪声。
- 途中发现：场景转移那一帧 automation RPC 会 reset（连接被重置），测试的 `scene_name` / `widget`
  读取做了容错（返回空串/空表）。






## 2026-10-13 — WT1 可行性核实（实现前）

- 世界空间文字可复用世界精灵路径：一个字形 = 一个 `WorldSpriteCandidate`（字体图集 = 1 张纹理，
  alpha 带 coverage，`tint` 上色）。字形 `uvRect` 是 `(offsetU,offsetV,scaleU,scaleV)`，需转成精灵的
  `(u0,v0,u1,v1)`。
- 入口：`FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, size)` 取 face；`atlasTextureFor(character)`
  取图集纹理；`TextureLibrary::get().getNearestSampler()` 做 bitmap 采样（像素游戏够用，世界精灵 shader 无 SDF 分支）。
- **一个真实时序约束**：`RuntimeRenderContext` 在 extract（约 L382）之后才 `flushPendingGlyphs`（L480）。
  字形首次出现的那一帧，`atlasSlot` 还是 ~0u，只能跳过 → **新字延迟一帧出现**；图集重打包时已显示字形
  可能有一帧 UV 抖动。这与引擎对动态文字的既有取舍一致（GUI 靠「request → flush → 重建 snapshot」规避，
  世界快照不重建）。伤害跳字是瞬时的，可接受；长期若要零抖动需给文字候选做图集 revision 失效重建。
- 结论：WT1 按「bitmap 面 + Nearest + 首次延迟一帧」实现；SDF 世界文字（大缩放无锯齿）留后续。

## 2026-10-13 — WT1：世界空间文字

- 目标（响应 Notes.md U3）：伤害跳字/名字/头顶信息用引擎世界文字，脚本一行 `world.spawnText` 即可。
- 引擎改动：
  - `Scene2D/Text2DComponent`（新）：`text / font / fontPixelSize / color / size / pivot / layer / sortOrder / bYSort`，全反射。
  - `Render3D/Common/Text2DExtraction.{h,cpp}`（新）：把一行文字按字形（advance/bearing/换行）展开成
    世界精灵候选（复用 `Sprite2DWorld` pass，不新增 pass）；字形图集 alpha 带 coverage，`tint` 上色。
    字体图集分页，所以每个字形取自己那一页的纹理 + 采样器（采样器按图集的 `SamplerCategory` 选 nearest/linear）。
  - `GameRuntime/Render/RenderFrameExtractor::extractText2D`：解析 `FontManager` face、`requestGlyphs`、转候选。
  - `world.spawnText(name, position, text[, { font, size, color, fontPixelSize }])`（中立层）。
- 关键取舍（已写进头文件注释）：快照是 view-independent 的，所以 `fontPixelSize` 是固定量、不跟随相机缩放；
  字形首次出现晚一帧（图集在 extract 之后 `flushPendingGlyphs`），且**未栅格化的字形跳过而不是画 `?`**
  （`Font::getCharacter` 会替换成 `?`，所以走 `characters.find` 直读）。
- 验证：`Script/automation/2d-rpg/world_text.py` —— 标签实体带正确 `text`；截图
  `Engine/Saved/Automation/world-text/world_text.png` 显示 "Hello 123" 与 "World" 画在世界坐标上。
