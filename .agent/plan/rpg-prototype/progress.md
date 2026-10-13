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

## 2026-10-13 — UB1：战斗 HUD 重做（响应 Notes.md U4）

- 目标：行动选择是**屏幕底部横向**按钮条；目标/技能提示可读；每个单位**头顶**显示名字与血条。
- 内容：
  - `Content/UI/BattleHUD.yaui.json`（`Tools/make_battle_hud.py` 重写）：三块——左上状态卡
    （Round/Acting/行动序条 Row1–6）、底部居中行动栏（`Action1..3` + `Hint`）、居中结算横幅 `OutcomeBox/Outcome`。
  - `Content/Scripts/BattleFx.lua`：头顶名字与血条改走 **WT1 世界文字**（`world.spawnText`）+ 1×1 白条精灵；
    伤害跳字**不再用 digits 图集**，改成一个世界文字实体（上浮+淡出+销毁）。数字图集退役。
  - `Content/UI/BattleHUD.lua`：`Action1..3` 菜单、`showOutcome` 连 `OutcomeBox` 一起显隐。
  - `Battle.lua`：行动栏是横向的，菜单导航从上下改为**左右**。
- 途中发现（引擎坐标约定，写进生成器注释）：**canvas 槽的 anchorY 0 = 顶、1 = 底**（`Dialogue` 底部面板
  用 `[0,1]` + 负偏移）；第一版按 UI 直觉用 `[0,1]` 当顶，状态卡落到了屏幕底部。用 `ui.query` 读运行时 rect
  和已知面板（Dialogue/Inventory/QuestSettings）对照才定案。空 `OutcomeBox` 会画一块底板，所以显隐必须
  连盒子一起切。
- 验证：六个战斗脚本（turns/move/combat/skill_kill/ai/camp_battle_loop）全过；截图
  `Engine/Saved/Automation/battle-skill/battle_skill.png` 显示底部行动栏、左上状态卡、每个单位头顶名字+血条
  （低血变红）。
- 偏离：行动栏当前把菜单项做成一排文字按钮；真正的按钮控件（可点/hover）留到风格化 UI（U5）一起。

## 2026-10-13 — R1（一半）：后处理运行时控制 `render.setLook`

- 目标：脚本运行时改后处理的 look（受击闪白/暴击 bloom/倒地灰度/Boss 阶段调色）。
- 现状核实：post pass 全在（bloom/tone-map/灰度/边缘/噪点/反色），`PostProcessingState` 由
  `RenderPipelineSettings` 驱动；`DeferredRenderPipeline::beginSubmission → applyPendingSettings` **每帧**重读，
  所以运行时写设置下一帧生效，且不在录制中途改资源。
- 引擎改动：
  - `GameplayScriptContext.mutatePostProcessing`（`GameplayScriptFunctions.h`）：宿主注入
    「读当前 settings → 让调用方改 postProcessing → 写回」，走 `AppRenderServices::get/setRenderPipelineSettings`；
    无渲染器（headless）是 no-op，脚本在测试里也能跑。
  - `render.setLook{ exposure, bloom, bloomThreshold, bloomStrength, grayscale, kernel, grain, inversion, toneMap, gamma }`：
    read-modify-write，只覆盖写了字段，其余保持当前 look。
- 验证：`Script/automation/2d-rpg/post_look.py` —— 曝光 look 提亮（131.7→158.2），灰度 look 完全去饱和
  （93.4→0.0）；截图 `Engine/Saved/Automation/post-look/look_{neutral,bright,gray}.png`。
- 未完成（R1 后半）：`render.fadeOut/in`（需要一层过渡：往屏幕盖纯色）与相机 `shake`/`zoomTo`。
  fade 需要在 post 或 overlay 加一个基础过渡（下一步）。

## 2026-10-13 — R1（后半）：过渡 fade（`FadeOverlay`）与转场接线

- 目标：进战/切场景有淡入淡出，而不是硬切。
- 内容：
  - `Content/UI/FadeOverlay.yaui.json`（`Tools/make_fade_overlay.py`）：铺满视口的 Border，全场景挂载（zOrder 90）。
  - `Content/UI/FadeOverlay.lua`：`fadeOut/fadeIn/fadeTo/setAlpha/isFading`，在控件 update 时钟上补间；
    mount 时从黑淡入（可见即渐显），所以经 fade 进入的场景自然淡入。
  - `Content/Scripts/Door.lua`：踩门后先 `fadeOut`，在**黑屏时** `world.loadScene`；`fadeSeconds`/`fadeEntry` 可覆写。
- 途中发现：
  - Border 的**脚本字段名是 `fill`**（反射成员是 `_color`），不是 `color`（`color` 是 Text 的）。
  - 整条链上「空面板也会画底板」：`OutcomeBox`、以及 select/menu 之外的阶段若不显式清空行动栏，上一个单位的菜单会残留
    —— `refreshHud` 改为「恰好一个阶段拥有行动栏，其余阶段清空」。
  - **菜单不该提供无法解决的选项**：攻击射程内无目标时，Attack 会一直弹回菜单（玩家被困）；改为射程内无目标就不列 Attack/Skill。
    这也是 `camp_battle_loop` 偶发卡在 Knight（1 射程、够不着）的根因。
- 验证：`Script/automation/2d-rpg/fade_overlay.py`（155.7→0.0→155.7）、`camp_battle_loop.py`（带 fade 的进出闭环）、
  `post_look.py`（曝光/灰度）全过；十个战斗/表现脚本全绿。
- 未完成（R1 收尾，显式延后）：相机 `shake` / `zoomTo` —— 需要在 view producer 里注入 view 矩阵偏移；
  用户目标的「后处理驱动视觉」已由 `setLook` + fade 覆盖，shake/zoom 留到有明确命中反馈需求时再做。

## 2026-10-13 — A1：音频子系统（SDL3 后端）

- 目标：引擎有音频接口；游戏能放 BGM/SFX/UI 音并调总线音量。
- 引擎改动（新 target `ya-audio`，接口中立）：
  - `Audio/AudioSystem.h`：纯接口——设备、clip 目录、voice 句柄、四条总线（master/music/sfx/ui）、
    `setBusVolume`、`update(dt)`、`playMusic/stopMusic`（带秒数的 crossfade）。无 ECS/Scene/Render 依赖，
    以后自研混音器（showcase-roadmap A2）可整体替换，游戏与脚本层不动。
  - `Audio/SdlAudioSystem.cpp`：SDL3 后端。每 voice 一个 `SDL_OpenAudioDeviceStream`，`update` 按需
    喂 PCM；`.ogg` 用 **stb_vorbis** 解码（Kenney 素材就是 ogg），支持 pitch/loop/总线增益。
    无设备（headless）时 `isAvailable()==false`，所有调用 no-op，脚本不必 guard。
  - 接线：`Source.xmake.lua` 引擎 profile 引入 `ya-audio`；GameRuntime 依赖它，启动开设备、
    退出关设备、每帧 `tickLogic` 推进（**不受游戏暂停影响**，点击/暂停音能收尾）。
  - 脚本面（中立层）：`audio.play(path, {bus, volume, pitch, loop})`、`audio.music(path[, seconds])`、
    `audio.stopMusic([seconds])`、`audio.setBus(bus, v)`、`audio.stopAll()`。路径经
    `VirtualFileSystem::translatePath` 解析成真实路径再交给音频层（音频层不依赖 VFS）。
- 途中发现：脚本传的 `Content/Audio/...` 是虚拟路径，`stb_vorbis_decode_filename` 要真实路径 —— 解析放在
  GameRuntime 的脚本函数里（那里知道 mount），不污染框架层。
- 验证：`Script/automation/2d-rpg/audio.py` —— 设备就绪（48000 Hz, 2ch）；`door_open.ogg` 0.92s、
  `click.ogg` 0.10s（单声道 44.1k）解码成功；按确认键触发 `audio.play`。
- 未完成 / 待验：曲子 crossfade 的实机验收（引擎实现了 `playMusic/stopMusic(seconds)`，但还没有内容曲目；
  等拿到/生成一首 BGM 再验）；`AudioSourceComponent`（场景里挂音频源）留到有需求时加（脚本 API 已够用）。
