# TODO — RPG Tactics (v2)

一眼版进度。详细目标与验收见 `plan.md`；能力覆盖审计见 `feature_matrix.json`。

## 图例

- `[ ]` 未开工 `[~]` 进行中 `[x]` 完成 `[!]` 阻塞

## Wave A — 重编基础

- [x] F1 项目壳重编 + Kenney 素材入库（Tiny Dungeon / Tiny Battle），新默认场景显示营地地图

## Wave B — 战术核心（先跑通主循环）

- [x] T1 棋盘与回合骨架（回合队列按速度、行动序条、格子高亮——高亮随 T2 落地）
- [x] T2 移动与可达范围（BFS 高亮 + 逐格移动）—— 鼠标拾取 screen->cell 延后（键盘光标已够）
- [x] T3 攻击 / 技能 / 伤害结算（伤害数字、HP 条、死亡移除）
- [x] T4 敌方 AI（接近→攻击→移动）—— Boss 分阶段留到内容期（C1）
- [x] M1 战斗与探索拼接（营地→遗迹→遇敌→战斗→回营地）**第一次完整闭环**

## Wave B2 — 表观质量（响应 Notes.md U3/U4，排在 Wave C 前）

- [x] WT1 世界空间文字（`world.spawnText` + `Text2DComponent`；伤害跳字/名字/头顶血条改用引擎世界文字）
- [x] UB1 战斗 HUD 重做（底部横向行动栏、目标/技能选择、头顶血条）

## Wave C — 四子系统（每个都要有真实使用点）

- [x] R1 后处理运行时控制（`render.setLook`）+ 过渡 fade（`FadeOverlay`）——相机 shake/zoom 显式延后
- [x] A1 音频子系统（SDL3 后端：device/clip/voice/总线/PCM 解码；音乐 crossfade 待内容曲目验收）
- [x] X1 粒子子系统（.yaparticle 资产 + 发射器 + 命中/魔法/治疗特效）
- [x] P1 2D 物理 / 触发器（Box2D：刚体/传感器/接触事件；推箱/压力板/移动平台用例随 Wave D C1）

## Wave D — 成长与内容

- [ ] G1 成长：等级 / 属性 / 技能 / 职业
- [ ] G2 装备与背包
- [ ] G3 队伍与同伴
- [ ] G4 任务系统
- [ ] C1 内容：3–4 场战斗 + 3 任务 + Boss + 结局

## Wave E — 收口

- [ ] Z1 存档 / 读档闭环
- [ ] Z2 UI 完善与打磨（战斗 HUD、设置、对话）
- [ ] Z3 收口：矩阵全绿 + 打包 + 通关录屏

## 待用户确认 / 决策门

- [x] 素材下载与入库 —— 已自行下载 Kenney CC0 素材（Tiny Dungeon/Battle、Particle、UI/SFX 音频）
- [ ] 单位动画策略（Tiny Battle 静态是否接受；需要动画才用 image-gen 生成）
- [ ] 后处理作用域（管线级先做；是否需要每 View 独立 look 到 R1 再定）
- [ ] Box2D 与 tile 棋盘的空间边界约定（写进 `2d-gameplay` skill）
- [ ] 目录名（沿用 `2DRpgPrototype` 还是改名，如 `2DTacticsRpg`）

## 来自 Notes.md 的持续性方向（非 checkpoint）

- [ ] **U1 引擎/编辑器级本地化服务**：游戏侧已有 `Locale.lua`；引擎与编辑器自身切语言是另一条线，
      登记给 `game-ui-authoring` / 编辑器线，不在本计划实现
- [ ] **U2 AI Native**：用 AI 加速内容/开发流程、找到本引擎的生态位（不做 `Yet Another Game Engine`）。
      方向性，持续指引；本计划相关落地 = 内容生成走 `image-gen`（本机无 grok-cli），后续接本地 ComfyUI
- [ ] **U5 风格化 UI 框架**：归 `gui-style-system-convergence`；本计划只登记，战斗面板先用现有控件做可用版

## 历史（v1）

v1 落地内容与遗留见 `todo-v1-landed.md`、`progress-v1-landed.md`。
