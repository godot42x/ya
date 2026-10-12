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

