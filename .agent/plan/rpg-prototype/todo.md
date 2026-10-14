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
- [ ] **UB3 战斗 HUD 布局收敛**（响应 U9：参照成熟 SRPG，2026-10-14 定案）
      ——世界层只留名字+血条，新增确定性伤害预测；屏幕层合并为底部一块战术面板（单位信息+行动按钮）；
      行动序压缩成一行色块；回合信息收敛成顶部一行；面板滑入/滑出。
      方案 C（混合式），不新增 C++ 控件，复用现有 `ActionButton`/`HpTrack`/`showMenu`。
      先决条件：修 `Tools/make_battle_hud.py` 过期问题（与线上 `.yaui` 不一致，重跑会抹掉按钮）。
      可延后：世界锚定菜单（FE 式）、敌人意图外显（ITB 式）。详见 `plan.md` §0.5 与 §7 UB3。

## Wave C — 四子系统（每个都要有真实使用点）

- [x] R1 后处理运行时控制（`render.setLook`）+ 过渡 fade（`FadeOverlay`）——相机 shake/zoom 显式延后
- [x] A1 音频子系统（SDL3 后端：device/clip/voice/总线/PCM 解码；音乐 crossfade 待内容曲目验收）
- [x] X1 粒子子系统（.yaparticle 资产 + 发射器 + 命中/魔法/治疗特效）
- [x] P1 2D 物理 / 触发器（Box2D：刚体/传感器/接触事件；推箱/压力板/移动平台用例随 Wave D C1）

## Wave D — 成长与内容

- [x] G1 成长：等级 / 属性 / 技能 / 职业（技能按等级解锁留 C1）
- [x] G2 装备与背包（物品目录 + 背包/装备面板 + 三槽装备 + 战斗掉落 + 属性即时更新；耐久/强化不做）
- [ ] G3 队伍与同伴
- [x] G4 任务系统（数据模型 + 事件推进 + 奖励结算 + 任务日志 UI）
- [ ] C1 内容：3–4 场战斗 + 3 任务 + Boss + 结局

## Wave E — 收口

- [ ] Z1 存档 / 读档闭环
- [ ] Z2 UI 完善与打磨（战斗 HUD、设置、对话）
- [ ] Z3 收口：矩阵全绿 + 打包 + 通关录屏

## 待用户确认 / 决策门

- [x] 素材下载与入库 —— 已自行下载 Kenney CC0 素材（Tiny Dungeon/Battle、Particle、UI/SFX 音频）
- [x] 单位动画策略 —— 2026-10-14 用户决定：暂时接受静态单位
- [ ] 后处理作用域（管线级先做；是否需要每 View 独立 look 到 R1 再定）
- [x] Box2D 与 tile 棋盘的空间边界约定 —— 已写进 `.agent/skills/2d-gameplay/SKILL.md`
- [ ] 目录名（沿用 `2DRpgPrototype` 还是改名，如 `2DTacticsRpg`）

## 来自 Notes.md 的持续性方向（非 checkpoint）

- [ ] **U1 引擎/编辑器级本地化服务**：游戏侧已有 `Locale.lua`；引擎与编辑器自身切语言是另一条线。
      2026-10-14 定案：见 `localization-final-design.md`——采纳方案 A（可本地化文本属性）+ C-lite（编辑器集成），
      跳过方案 B（FText-like，规模不匹配）。P0 已落地（`eb07437e`），真正空洞是 P2 断链（`editorTr` 0 调用点）。
- [ ] **U2 AI Native**：用 AI 加速内容/开发流程、找到本引擎的生态位（不做 `Yet Another Game Engine`）。
      方向性，持续指引；本计划相关落地 = 内容生成走 `image-gen`（本机无 grok-cli），后续接本地 ComfyUI
- [ ] **U5 风格化 UI 框架**：归 `gui-style-system-convergence`；本计划只登记，战斗面板先用现有控件做可用版

## 历史（v1）

v1 落地内容与遗留见 `todo-v1-landed.md`、`progress-v1-landed.md`。
