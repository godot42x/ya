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

- 素材入库方式（我去下载 Kenney zip，还是用户手动放）。
- 单位静态 vs 动画（Tiny Battle 无内置动画）。
- 后处理作用域（管线级先做；每 View 独立 look 到 R1 再定）。
- 示例目录名（沿用 `2DRpgPrototype` 或改名）。
